#include "durworker.h"
#include "durprobe.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace bp
{
	void DurationWorker::want(int s, std::vector<std::pair<int, std::string>> rows)
	{
		std::lock_guard<std::mutex> g(m);
		serial = s;
		items.clear();
		for (auto& r : rows) items.push_back({r.first, std::move(r.second)});
		left = (int)items.size();
		lo = hi = 0;
		if (!started && !items.empty() && !stopping)
		{
			started = true;
			thread = std::thread([this] { run(); });
			thread.detach();
		}
		cv.notify_one();
	}

	void DurationWorker::focus(int l, int h)
	{
		std::lock_guard<std::mutex> g(m);
		lo = l;
		hi = h;
	}

	void DurationWorker::setBusy(bool b)
	{
		if (busy.exchange(b) && !b) cv.notify_one();
	}

	std::vector<DurationWorker::Result> DurationWorker::take()
	{
		std::lock_guard<std::mutex> g(m);
		signalled = false;
		std::vector<Result> out;
		out.swap(results);
		return out;
	}

	void DurationWorker::stop()
	{
		std::lock_guard<std::mutex> g(m);
		stopping = true;
		items.clear();
		left = 0;
		cv.notify_one();
	}

	int DurationWorker::pick()
	{
		// on screen first, then the nearest to it (scrolling either way finds them ready)
		int best = -1, bestDist = 0;
		for (int i = 0; i < (int)items.size(); i++)
		{
			if (items[i].done) continue;
			int r = items[i].row, d = r < lo ? lo - r : r >= hi ? r - hi + 1 : 0;
			if (best < 0 || d < bestDist)
			{
				best = i;
				bestDist = d;
				if (!d) break;
			}
		}
		return best;
	}

	void DurationWorker::run()
	{
#ifdef __linux__
		// this thread only (Linux's nice is per thread): playback and the panels go first; its reads too (the
		// best-effort class's lowest level)
		pid_t tid = (pid_t)syscall(SYS_gettid);
		setpriority(PRIO_PROCESS, tid, 15);
		syscall(SYS_ioprio_set, 1 /* IOPRIO_WHO_PROCESS */, tid, (2 << 13) | 7 /* BE, level 7 */);
#endif
		bool timing = getenv("BP_DURATION_TIMING") != nullptr;
		for (;;)
		{
			int s, i;
			std::string path;
			{
				std::unique_lock<std::mutex> g(m);
				cv.wait(g, [&] { return !stopping && left > 0 && !busy.load(); });
				i = pick();
				if (i < 0)
				{
					left = 0;
					continue;
				}
				items[i].done = true;
				left--;
				s = serial;
				path = items[i].path;
				i = items[i].row;
			}
			double ms = 0;
			double secs = probeDuration(path, &ms);
			if (timing && ms > 0) fprintf(stderr, "timing: duration %.1f ms (%.1f s) %s\n", ms, secs, path.c_str());
			bool signal = false;
			{
				std::lock_guard<std::mutex> g(m);
				if (s == serial && !stopping)
				{
					results.push_back({s, i, path, secs});
					signal = !signalled;
					signalled = true;
				}
			}
			if (signal) ready();
			// a pause as long as half the probe (2 ms at least): it never takes more than two thirds of a core
			if (ms > 0) std::this_thread::sleep_for(std::chrono::microseconds((long long)std::max(2000.0, ms * 500)));
		}
	}
}
