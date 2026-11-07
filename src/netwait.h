// What the network backends share for waiting: a clock, and the cancel() of one call from another thread.
#pragma once
#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace bp
{
	namespace netwait
	{
		using Clock = std::chrono::steady_clock;

		inline int64_t nowMs() { return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch()).count(); }

		// cancel() from another thread, for a wait on this thread: a flag, and a pipe that wakes the poll. It only
		// reaches the call in progress: a cancel with no call under way is dropped (the next call isn't the one meant)
		class Canceller
		{
		public:
			Canceller()
			{
				if (pipe(fds) != 0) fds[0] = fds[1] = -1;
				for (int fd : fds)
					if (fd >= 0) { fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK); fcntl(fd, F_SETFD, FD_CLOEXEC); }
			}
			~Canceller() { for (int fd : fds) if (fd >= 0) close(fd); }
			Canceller(const Canceller&) = delete;
			Canceller& operator=(const Canceller&) = delete;

			// a call's start: an older cancel is forgotten
			void begin()
			{
				flag = false;
				drain();
				busy = true;
			}
			void end() { busy = false; }
			void cancel()
			{
				if (!busy) return;
				flag = true;
				if (fds[1] >= 0) { char c = 'c'; (void)!write(fds[1], &c, 1); }
			}
			bool cancelled() const { return flag; }
			int fd() const { return fds[0]; }
			void drain()
			{
				char buf[64];
				while (fds[0] >= 0 && read(fds[0], buf, sizeof buf) > 0) {}
			}
			// a pause (the backoff before another try); false when cancelled meanwhile
			bool sleep(int ms)
			{
				pollfd p = { fds[0], POLLIN, 0 };
				int64_t until = nowMs() + ms;
				while (!flag)
				{
					int64_t left = until - nowMs();
					if (left <= 0) return true;
					poll(&p, fds[0] >= 0 ? 1 : 0, (int)std::min<int64_t>(left, 100));
				}
				return false;
			}

		private:
			int fds[2];
			std::atomic<bool> flag{ false }, busy{ false };
		};

		// a call's guard: begin() till the call returns
		struct Busy
		{
			Canceller& c;
			explicit Busy(Canceller& x) : c(x) { c.begin(); }
			~Busy() { c.end(); }
		};
	}
}
