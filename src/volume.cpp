#include "volume.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>

namespace bp
{
	bool Volume::present() const
	{
		return access(script.c_str(), X_OK) == 0;
	}

	int Volume::get() const
	{
		if (!present()) return -1;
		int v = -1;
		if (FILE* p = popen(("'" + script + "' get 2>/dev/null").c_str(), "r"))
		{
			if (fscanf(p, "%d", &v) != 1) v = -1;
			pclose(p);
		}
		return v >= 0 && v <= 100 ? v : -1;
	}

	void Volume::set(int percent)
	{
		if (!present()) return;
		std::lock_guard<std::mutex> g(lock);
		pending = percent < 0 ? 0 : percent > 100 ? 100 : percent;
		if (!worker.joinable()) worker = std::thread([this] { run(); });
		wake.notify_one();
	}

	// bananad, told that the level changed: it keeps it (alsactl store, two seconds on), as after BananaOS's menu
	static void pokeBananad()
	{
		DIR* d = opendir("/proc");
		if (!d) return;
		while (dirent* e = readdir(d))
		{
			if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
			char path[300], comm[32] = "";
			snprintf(path, sizeof path, "/proc/%s/comm", e->d_name);
			if (FILE* f = fopen(path, "r"))
			{
				if (fgets(comm, sizeof comm, f)) comm[strcspn(comm, "\n")] = 0;
				fclose(f);
			}
			if (!strcmp(comm, "bananad")) kill(atoi(e->d_name), SIGHUP);
		}
		closedir(d);
	}

	void Volume::run()
	{
		std::unique_lock<std::mutex> g(lock);
		for (;;)
		{
			wake.wait(g, [this] { return pending >= 0 || stopping; });
			if (stopping) return;
			int v = pending;
			pending = -1;
			g.unlock();
			std::string cmd = "'" + script + "' set " + std::to_string(v) + " >/dev/null 2>&1";
			if (system(cmd.c_str()) != 0) fprintf(stderr, "volume: set %d failed\n", v);
			pokeBananad();
			g.lock();
		}
	}

	Volume::~Volume()
	{
		{
			std::lock_guard<std::mutex> g(lock);
			stopping = true;
		}
		wake.notify_one();
		if (worker.joinable()) worker.join();
	}
}
