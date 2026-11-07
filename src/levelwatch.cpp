#include "levelwatch.h"
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/inotify.h>
#endif

namespace bp
{
	static long long stamp(const std::string& p)
	{
		struct stat st;
		if (stat(p.c_str(), &st) != 0) return 0;
#ifdef __APPLE__
		return (long long)st.st_mtimespec.tv_sec * 1000000000LL + st.st_mtimespec.tv_nsec;
#else
		return (long long)st.st_mtim.tv_sec * 1000000000LL + st.st_mtim.tv_nsec;
#endif
	}

	bool LevelWatch::start(std::function<void(const std::string&, int)> cb)
	{
		onLevel = std::move(cb);
		path = getenv("BP_OSD_FILE") ? getenv("BP_OSD_FILE") : "/run/banana/osd";
		size_t slash = path.rfind('/');
		dir = slash == std::string::npos ? "." : path.substr(0, slash);
		name = path.substr(slash + 1);
		struct stat st;
		if (stat(dir.c_str(), &st) != 0) return false;	// no bananad here
		lastStamp = stamp(path);	// a level from before the player started isn't news
		if (pipe(stopPipe) < 0) return false;
		thread = std::thread([this] { run(); });
		return true;
	}

	void LevelWatch::stop()
	{
		if (!thread.joinable()) return;
		char c = 'q';
		if (write(stopPipe[1], &c, 1) < 0) {}
		thread.join();
		close(stopPipe[0]);
		close(stopPipe[1]);
	}

	void LevelWatch::read()
	{
		long long now = stamp(path);
		if (!now || now == lastStamp) return;
		lastStamp = now;
		std::ifstream f(path);
		std::string what;
		int pct = -1;
		if ((f >> what >> pct) && pct >= 0 && onLevel) onLevel(what, pct);
	}

	void LevelWatch::run()
	{
#ifdef __linux__
		int in = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
		// bananad writes osd.new and renames it over osd: a move into the folder (a plain write closing too)
		if (in >= 0 && inotify_add_watch(in, dir.c_str(), IN_MOVED_TO | IN_CLOSE_WRITE) >= 0)
		{
			for (;;)
			{
				pollfd fds[2] = {{stopPipe[0], POLLIN, 0}, {in, POLLIN, 0}};
				if (poll(fds, 2, -1) < 0 && errno != EINTR) break;
				if (fds[0].revents) break;
				if (fds[1].revents)
				{
					char buf[4096];
					while (::read(in, buf, sizeof buf) > 0) {}
					read();
				}
			}
			close(in);
			return;
		}
		if (in >= 0) close(in);
#endif
		for (;;)
		{
			pollfd fd = {stopPipe[0], POLLIN, 0};
			if (poll(&fd, 1, 200) > 0) break;
			read();
		}
	}
}
