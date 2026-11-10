// The bottom panel's file rows' durations, probed on a thread of their own (libavformat reads each file's header: a
// few ms a file, seconds on a broken one). Low priority: a lowered nice and I/O class on Linux, a pause after each
// probe, none at all while a file opens (setBusy). The rows on screen first, then outward from them; a new folder
// replaces the list (a probe already running finishes, its answer dropped by serial).
//
// Made on the heap and never destroyed (like the Updater): the thread is detached and may be inside libavformat at
// exit. Answers go into DurationCache::shared() and to take(); ready() (on the worker's thread) says there are some,
// once until take() is called.
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bp
{
	class DurationWorker
	{
	public:
		struct Result
		{
			int serial = 0, row = 0;
			std::string path;
			double secs = -1;	// -1: none to show
		};
		explicit DurationWorker(std::function<void()> ready) : ready(std::move(ready)) {}
		// the folder's rows to probe (row index, local path); replaces what was asked before. Empty: nothing more
		void want(int serial, std::vector<std::pair<int, std::string>> rows);
		// the rows on screen [lo, hi): probed first
		void focus(int lo, int hi);
		// a file opening: no probe starts till it's open
		void setBusy(bool busy);
		// main thread: the answers since the last take
		std::vector<Result> take();
		// at quit: no more probes (the thread waits for ever)
		void stop();

	private:
		struct Item
		{
			int row;
			std::string path;
			bool done = false;
		};
		void run();
		int pick();	// under lock: the next item's index, -1 none
		std::function<void()> ready;
		std::thread thread;
		std::mutex m;
		std::condition_variable cv;
		int serial = 0, lo = 0, hi = 0, left = 0;
		std::vector<Item> items;
		std::vector<Result> results;
		bool signalled = false, stopping = false, started = false;
		std::atomic<bool> busy{false};
	};
}
