#include "sleepwatch.h"
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <algorithm>
#include <string>
#include <ctime>
#include <unistd.h>
#ifdef __linux__
#include <cstring>
#include <dirent.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#endif

namespace bp
{
	// written by the SIGCONT handler (a write is async-signal-safe), read by the watch's thread
	static int contPipe[2] = {-1, -1};

	static void onCont(int)
	{
		int saved = errno;
		char c = 'c';
		if (write(contPipe[1], &c, 1) < 0) {}
		errno = saved;
	}

	static void cloexecNonblock(int fd)
	{
		fcntl(fd, F_SETFD, FD_CLOEXEC);
		fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
	}

#ifdef __linux__
	// the input device with a lid switch: the hall sensor's gpio-keys on the RG DS
	static int openLid()
	{
		DIR* d = opendir("/dev/input");
		if (!d) return -1;
		int found = -1;
		while (dirent* e = readdir(d))
		{
			if (strncmp(e->d_name, "event", 5)) continue;
			std::string path = std::string("/dev/input/") + e->d_name;
			int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (fd < 0) continue;
			unsigned long sw[SW_MAX / (8 * sizeof(long)) + 1] = {};
			ioctl(fd, EVIOCGBIT(EV_SW, sizeof sw), sw);
			if ((sw[SW_LID / (8 * sizeof(long))] >> (SW_LID % (8 * sizeof(long)))) & 1)
			{
				found = fd;
				break;
			}
			close(fd);
		}
		closedir(d);
		return found;
	}
#endif

	bool SleepWatch::start(std::function<void(bool)> cb, std::function<void()> awake)
	{
		onSleep = std::move(cb);
		onAwake = std::move(awake);
		if (pipe(stopPipe) < 0) return false;
		if (contPipe[0] < 0)
		{
			if (pipe(contPipe) < 0) return false;
			cloexecNonblock(contPipe[0]);
			cloexecNonblock(contPipe[1]);
		}
		struct sigaction sa = {};
		sa.sa_handler = onCont;
		sa.sa_flags = SA_RESTART;
		sigemptyset(&sa.sa_mask);
		sigaction(SIGCONT, &sa, nullptr);
#ifdef __linux__
		lidFd = openLid();
		if (lidFd < 0) fprintf(stderr, "no lid switch found: only SIGCONT pauses after a sleep\n");
#endif
		thread = std::thread([this] { run(); });
		return true;
	}

	void SleepWatch::stop()
	{
		if (!thread.joinable()) return;
		char c = 'q';
		if (write(stopPipe[1], &c, 1) < 0) {}
		thread.join();
		signal(SIGCONT, SIG_DFL);
		close(stopPipe[0]);
		close(stopPipe[1]);
		if (lidFd >= 0) close(lidFd);
		lidFd = -1;
	}

	static long long monoMs()
	{
		timespec t;
		clock_gettime(CLOCK_MONOTONIC, &t);
		return (long long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
	}

	void SleepWatch::run()
	{
		// the lid closed and no SIGCONT since: when it's taken as never stopped (monoMs; 0 none)
		long long awakeAt = 0;
		for (;;)
		{
			pollfd fds[3] = {{stopPipe[0], POLLIN, 0}, {contPipe[0], POLLIN, 0}, {lidFd, POLLIN, 0}};
			int timeout = awakeAt ? (int)std::max(0LL, awakeAt - monoMs()) : -1;
			int n = poll(fds, lidFd >= 0 ? 3 : 2, timeout);
			if (n < 0)
			{
				if (errno == EINTR) continue;	// SIGCONT itself lands here too; its byte is in the pipe
				return;
			}
			if (n > 0 && fds[0].revents) return;
			bool lid = false, cont = false;
			if (n > 0 && fds[1].revents)
			{
				char buf[16];
				while (read(contPipe[0], buf, sizeof buf) > 0) {}
				cont = true;
			}
#ifdef __linux__
			if (n > 0 && lidFd >= 0 && fds[2].revents)
			{
				input_event ev[16];
				ssize_t r;
				while ((r = read(lidFd, ev, sizeof ev)) > 0)
					for (size_t i = 0; i < r / sizeof *ev; i++)
						if (ev[i].type == EV_SW && ev[i].code == SW_LID && ev[i].value == 1) lid = true;
				if (r < 0 && errno != EAGAIN)
				{
					close(lidFd);	// the device went away: SIGCONT still covers the sleeps
					lidFd = -1;
				}
			}
#endif
			if ((lid || cont) && onSleep) onSleep(lid && !cont);
			if (cont)
			{
				// back from a stop (a timer of the lid's with it: it was a sleep)
				awakeAt = 0;
				if (onAwake) onAwake();
			}
			else if (lid) awakeAt = monoMs() + 1500;
			else if (awakeAt && monoMs() >= awakeAt)
			{
				// never stopped: closing the lid only darkened the panels (lid sleep off)
				awakeAt = 0;
				if (onAwake) onAwake();
			}
		}
	}
}
