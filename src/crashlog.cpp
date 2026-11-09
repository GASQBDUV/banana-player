#include "crashlog.h"

#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dlfcn.h>
#include <exception>
#include <execinfo.h>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif
#if defined(__APPLE__)
#include <sys/ucontext.h>
#else
#include <ucontext.h>
#endif

namespace bp {
namespace crashlog {

std::string trimmed(const std::string& text, size_t keep)
{
	if (text.size() <= keep) return text;
	size_t len = strlen(MARK);
	for (size_t p = text.find(MARK, text.size() - keep); p != std::string::npos; p = text.find(MARK, p + len))
		if (p == 0 || text[p - 1] == '\n') return text.substr(p);
	return "";
}

bool trimFile(const std::string& path, size_t limit, size_t keep)
{
	struct stat st;
	if (stat(path.c_str(), &st) != 0 || (size_t)st.st_size <= limit) return false;
	std::string text;
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return false;
		text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	}
	std::string tmp = path + ".tmp";
	{
		std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
		out << trimmed(text, keep);
		if (!out.flush())
		{
			out.close();
			unlink(tmp.c_str());
			return false;
		}
	}
	if (rename(tmp.c_str(), path.c_str()) != 0)
	{
		unlink(tmp.c_str());
		return false;
	}
	return true;
}

// the handler's writing: into a fixed buffer, never past its end
namespace {
struct Buf
{
	char* p;
	size_t cap, len = 0;
	Buf(char* b, size_t n) : p(b), cap(n) {}
	void add(const char* s, size_t n)
	{
		if (n > cap - len) n = cap - len;
		memcpy(p + len, s, n);
		len += n;
	}
	void add(const char* s) { add(s, strlen(s)); }
	void num(unsigned long long v, int width = 0)
	{
		char t[24];
		int i = 0;
		do t[i++] = char('0' + v % 10), v /= 10; while (v);
		while (i < width) t[i++] = '0';
		while (i) add(&t[--i], 1);
	}
	void snum(long long v)
	{
		if (v < 0) add("-"), v = -v;
		num((unsigned long long)v);
	}
	void hex(unsigned long long v)
	{
		char t[18];
		int i = 0;
		do t[i++] = "0123456789abcdef"[v & 15], v >>= 4; while (v);
		add("0x");
		while (i) add(&t[--i], 1);
	}
};
}

size_t formatTime(char* out, size_t n, long long epoch, long offsetSec)
{
	long long t = epoch + offsetSec;
	long long days = t / 86400, secs = t % 86400;
	if (secs < 0) secs += 86400, days--;
	// days to a date (Howard Hinnant's civil_from_days)
	long long z = days + 719468;
	long long era = (z >= 0 ? z : z - 146096) / 146097;
	long long doe = z - era * 146097;
	long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	long long mp = (5 * doy + 2) / 153;
	long long d = doy - (153 * mp + 2) / 5 + 1;
	long long m = mp < 10 ? mp + 3 : mp - 9;
	long long y = yoe + era * 400 + (m <= 2);
	Buf b(out, n);
	b.num((unsigned long long)y, 4), b.add("-"), b.num(m, 2), b.add("-"), b.num(d, 2), b.add(" ");
	b.num(secs / 3600, 2), b.add(":"), b.num(secs / 60 % 60, 2), b.add(":"), b.num(secs % 60, 2);
	long off = offsetSec < 0 ? -offsetSec : offsetSec;
	b.add(offsetSec < 0 ? " -" : " +"), b.num(off / 3600, 2), b.num(off / 60 % 60, 2);
	return b.len;
}

namespace {
// all set by install(): the handler only reads them
char path[1024];
char header[256];	// "version 0.5.1 (abc1234), binary base 0x…"
long utcOffset = 0;
struct timespec started;
int symPipe[2] = {-1, -1};	// backtrace_symbols_fd's lines come back through it, to be cut to FRAME_LINE_MAX
char context[2][512];
std::atomic<int> contextAt{0};
char terminateWhat[512];	// an uncaught exception's message, set before its abort
std::atomic<bool> busy{false};
char entry[ENTRY_MAX];
char stack[64 * 1024];	// the handler's own: a stack overflow still gets its record
const int SIGNALS[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};
#ifdef __APPLE__
const bool MAC = true;
#else
const bool MAC = false;
#endif

const char* signalName(int sig)
{
	switch (sig)
	{
	case SIGSEGV: return "SIGSEGV";
	case SIGBUS: return "SIGBUS";
	case SIGILL: return "SIGILL";
	case SIGFPE: return "SIGFPE";
	case SIGABRT: return "SIGABRT";
	}
	return "signal";
}

const char* codeName(int sig, int code)
{
	if (code == SI_USER) return "SI_USER";
#ifdef SI_TKILL
	if (code == SI_TKILL) return "SI_TKILL";
#endif
	switch (sig)
	{
	case SIGSEGV:
		if (code == SEGV_MAPERR) return "SEGV_MAPERR";
		if (code == SEGV_ACCERR) return "SEGV_ACCERR";
		break;
	case SIGBUS:
		if (code == BUS_ADRALN) return "BUS_ADRALN";
		if (code == BUS_ADRERR) return "BUS_ADRERR";
		if (code == BUS_OBJERR) return "BUS_OBJERR";
		break;
	case SIGILL:
		if (code == ILL_ILLOPC) return "ILL_ILLOPC";
		if (code == ILL_ILLOPN) return "ILL_ILLOPN";
		if (code == ILL_PRVOPC) return "ILL_PRVOPC";
		break;
	case SIGFPE:
		if (code == FPE_INTDIV) return "FPE_INTDIV";
		if (code == FPE_FLTDIV) return "FPE_FLTDIV";
		break;
	}
	return "";
}

unsigned long long faultPc(void* ucv)
{
	if (!ucv) return 0;
	ucontext_t* uc = (ucontext_t*)ucv;
	(void)uc;
#if defined(__linux__) && defined(__aarch64__)
	return uc->uc_mcontext.pc;
#elif defined(__linux__) && defined(__x86_64__)
	return uc->uc_mcontext.gregs[REG_RIP];
#elif defined(__APPLE__) && defined(__aarch64__)
	return uc->uc_mcontext ? uc->uc_mcontext->__ss.__pc : 0;
#elif defined(__APPLE__) && defined(__x86_64__)
	return uc->uc_mcontext ? uc->uc_mcontext->__ss.__rip : 0;
#else
	return 0;
#endif
}

void say(int fd, const char* s, size_t n)
{
	while (n)
	{
		ssize_t w = write(fd, s, n);
		if (w < 0 && errno == EINTR) continue;
		if (w <= 0) return;
		s += w, n -= (size_t)w;
	}
}

void onSignal(int sig, siginfo_t* si, void* uc)
{
	// a second thread crashing meanwhile waits: the first one's record, then its end, ends the process
	if (busy.exchange(true))
		for (;;) pause();
	int saved = errno;
	Buf b(entry, sizeof entry);
	struct timespec now = {}, mono = {};
	clock_gettime(CLOCK_REALTIME, &now);
	clock_gettime(CLOCK_MONOTONIC, &mono);
	char when[48];
	b.add(MARK), b.add(when, formatTime(when, sizeof when, now.tv_sec, utcOffset)), b.add(" ===\n");
	b.add(header), b.add("\n");
	b.add("signal: "), b.add(signalName(sig)), b.add(" ("), b.snum(sig), b.add("), code "), b.snum(si ? si->si_code : 0);
	if (si && *codeName(sig, si->si_code)) b.add(" "), b.add(codeName(sig, si->si_code));
	b.add(", address "), b.hex(si ? (unsigned long long)(uintptr_t)si->si_addr : 0);
	if (unsigned long long pc = faultPc(uc)) b.add(", pc "), b.hex(pc);
	b.add("\n");
	b.add("uptime: "), b.num((unsigned long long)(mono.tv_sec - started.tv_sec)), b.add(" s\n");
#ifdef __linux__
	{
		char name[17] = {};
		prctl(PR_GET_NAME, (unsigned long)name, 0, 0, 0);
		b.add("thread: "), b.add(name), b.add(" (tid "), b.num((unsigned long long)syscall(SYS_gettid));
		b.add((long)syscall(SYS_gettid) == (long)getpid() ? ", the main thread)\n" : ")\n");
	}
#endif
	b.add("playing: ");
	const char* ctx = context[contextAt.load()];
	b.add(*ctx ? ctx : "(nothing)"), b.add("\n");
	if (sig == SIGABRT && *terminateWhat) b.add("uncaught: "), b.add(terminateWhat), b.add("\n");
	b.add("backtrace:\n");
	void* frames[MAX_FRAMES];
	int n = backtrace(frames, MAX_FRAMES);
	for (int i = 0; i < n; i++)
	{
		b.add("  ");
		// one frame at a time through the pipe, each cut to its most (the pipe can't fill: one line is short)
		size_t at = b.len, room = FRAME_LINE_MAX;
		if (symPipe[1] >= 0)
		{
			backtrace_symbols_fd(&frames[i], 1, symPipe[1]);
			char line[512];
			ssize_t r;
			bool first = true;
			while ((r = read(symPipe[0], line, sizeof line)) > 0)
			{
				size_t k = (size_t)r, s = 0;
				// macOS numbers each call's frames: always 0 here
				if (first && MAC)
					while (s < k && (line[s] == '0' || line[s] == ' ')) s++;
				first = false;
				k -= s;
				if (k > room) k = room;
				b.add(line + s, k);
				room -= k;
			}
			while (b.len > at && (b.p[b.len - 1] == '\n' || b.p[b.len - 1] == ' ')) b.len--;
		}
		if (b.len == at) b.hex((unsigned long long)(uintptr_t)frames[i]);
		b.add("\n");
	}
	if (n == 0) b.add("  (none)\n");
	b.add("\n");
	// the last bytes kept for the entry's end, even when it was cut
	if (b.len == b.cap) memcpy(b.p + b.cap - 5, "\n…\n", 5);

	// into crash.log: never past CAP (an entry cut short rather than the cap passed)
	bool logged = false;
	if (*path)
	{
		int fd = open(path, O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC, 0644);
		if (fd >= 0)
		{
			struct stat st;
			if (fstat(fd, &st) == 0 && (size_t)st.st_size < CAP)
			{
				size_t room = CAP - (size_t)st.st_size;
				say(fd, entry, b.len < room ? b.len : room);
				logged = true;
			}
			close(fd);
		}
	}
	// and a line in log.txt (stderr), which shows where to look
	char line[1200];
	Buf l(line, sizeof line);
	l.add("crash: "), l.add(signalName(sig));
	if (si && *codeName(sig, si->si_code)) l.add(" "), l.add(codeName(sig, si->si_code));
	l.add(" at "), l.hex(si ? (unsigned long long)(uintptr_t)si->si_addr : 0);
	if (logged) l.add(", see "), l.add(path);
	else l.add(", not in crash.log (full or none)");
	l.add("\n");
	say(2, line, l.len);

	// the default's end: the same exit status and core as without this
	signal(sig, SIG_DFL);
	errno = saved;
	raise(sig);
}

void onTerminate()
{
	// an uncaught exception: its message for the record, then abort as the default does
	if (std::exception_ptr e = std::current_exception())
	{
		try { std::rethrow_exception(e); }
		catch (const std::exception& x) { snprintf(terminateWhat, sizeof terminateWhat, "%s", x.what()); }
		catch (...) { snprintf(terminateWhat, sizeof terminateWhat, "an exception not from std::exception"); }
	}
	else snprintf(terminateWhat, sizeof terminateWhat, "std::terminate without an exception");
	fprintf(stderr, "crash: terminate: %s\n", terminateWhat);
	abort();
}
}

void install(const std::string& stateDir, const char* version, const char* commit)
{
	clock_gettime(CLOCK_MONOTONIC, &started);
	{
		time_t t = time(nullptr);
		struct tm tm;
		if (localtime_r(&t, &tm)) utcOffset = tm.tm_gmtoff;
	}
	if (!stateDir.empty() && stateDir.size() + 16 < sizeof path)
	{
		snprintf(path, sizeof path, "%s/crash.log", stateDir.c_str());
		// room for a whole entry next time
		trimFile(path, CAP - ENTRY_MAX, KEEP);
	}
	// where the binary is loaded: a frame's address minus this is what addr2line takes (DEVELOPMENT.md)
	unsigned long long base = 0;
	Dl_info info;
	if (dladdr((void*)&install, &info) && info.dli_fbase) base = (unsigned long long)(uintptr_t)info.dli_fbase;
	snprintf(header, sizeof header, "version %s%s%s%s, binary base 0x%llx", version, *commit ? " (" : "",
		commit, *commit ? ")" : "", base);
	// backtrace()'s first call loads libgcc, which a crashed process can't do safely
	void* warm[2];
	backtrace(warm, 2);
	if (pipe(symPipe) == 0)
		for (int fd : symPipe)
		{
			fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
			fcntl(fd, F_SETFD, FD_CLOEXEC);
		}
	else symPipe[0] = symPipe[1] = -1;

	// the main thread's own stack for the handler (other threads keep theirs: an overflow there isn't recorded)
	stack_t ss = {};
	ss.ss_sp = stack;
	ss.ss_size = sizeof stack;
	sigaltstack(&ss, nullptr);
	struct sigaction sa = {};
	sa.sa_sigaction = onSignal;
	sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
	sigemptyset(&sa.sa_mask);
	for (int s : SIGNALS) sigaction(s, &sa, nullptr);
	std::set_terminate(onTerminate);
}

void setContext(const char* text)
{
	int next = 1 - contextAt.load();
	snprintf(context[next], sizeof context[next], "%s", text ? text : "");
	contextAt.store(next);
}

static int recurse(volatile int* p, int depth)
{
	volatile char pad[4096];
	pad[0] = (char)depth;
	if (depth < 0) return 0;	// never: the stack runs out first
	return recurse(p, depth + 1) + pad[0];
}

void testCrash(const char* how)
{
	std::string h = how ? how : "";
	fprintf(stderr, "crash test: %s\n", h.c_str());
	if (h == "abort") abort();
	if (h == "throw") throw std::runtime_error("BP_CRASH_TEST");
	if (h == "overflow") recurse(nullptr, 0);
	volatile int* p = nullptr;
	*p = 1;
}

}
}
