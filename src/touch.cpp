#include "touch.h"
#include <cerrno>
#include <cstdio>
#include <poll.h>
#include <unistd.h>
#ifdef __linux__
#include <climits>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#endif

namespace bp
{
#ifdef __linux__
	static bool testBit(const unsigned long* bits, int n)
	{
		return (bits[n / (8 * sizeof(long))] >> (n % (8 * sizeof(long)))) & 1;
	}

	bool BottomTouch::start(const std::string& fragment, std::function<void(Pointer)> cb)
	{
		onPointer = std::move(cb);
		DIR* d = opendir("/dev/input");
		if (!d) return false;
		while (dirent* e = readdir(d))
		{
			if (strncmp(e->d_name, "event", 5)) continue;
			// the device's place in sysfs tells the panels' touchscreens apart
			char link[512], real[PATH_MAX];
			snprintf(link, sizeof link, "/sys/class/input/%s/device", e->d_name);
			if (!realpath(link, real) || !strstr(real, fragment.c_str())) continue;
			std::string path = std::string("/dev/input/") + e->d_name;
			int f = open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (f < 0) continue;
			unsigned long abs[ABS_MAX / (8 * sizeof(long)) + 1] = {};
			ioctl(f, EVIOCGBIT(EV_ABS, sizeof abs), abs);
			bool mt = testBit(abs, ABS_MT_POSITION_X) && testBit(abs, ABS_MT_POSITION_Y);
			if (!mt && !(testBit(abs, ABS_X) && testBit(abs, ABS_Y)))
			{
				close(f);
				continue;
			}
			input_absinfo ax = {}, ay = {};
			ioctl(f, EVIOCGABS(mt ? ABS_MT_POSITION_X : ABS_X), &ax);
			ioctl(f, EVIOCGABS(mt ? ABS_MT_POSITION_Y : ABS_Y), &ay);
			minX = ax.minimum; maxX = ax.maximum; minY = ay.minimum; maxY = ay.maximum;
			fd = f;
			break;
		}
		closedir(d);
		if (fd < 0 || pipe(stopPipe) < 0) return false;
		thread = std::thread([this] { run(); });
		return true;
	}

	void BottomTouch::run()
	{
		int x = 0, y = 0, slot = 0;
		bool down = false, wasDown = false, moved = false;
		for (;;)
		{
			pollfd fds[2] = {{stopPipe[0], POLLIN, 0}, {fd, POLLIN, 0}};
			if (poll(fds, 2, -1) < 0)
			{
				if (errno == EINTR) continue;
				return;
			}
			if (fds[0].revents) return;
			input_event ev[64];
			ssize_t r;
			while ((r = read(fd, ev, sizeof ev)) > 0)
				for (size_t i = 0; i < r / sizeof *ev; i++)
				{
					const input_event& e = ev[i];
					if (e.type == EV_ABS && e.code == ABS_MT_SLOT) slot = e.value;
					// a second finger moves nothing: only the first slot's
					else if (e.type == EV_ABS && e.code == ABS_MT_POSITION_X && slot == 0) { x = e.value; moved = true; }
					else if (e.type == EV_ABS && e.code == ABS_MT_POSITION_Y && slot == 0) { y = e.value; moved = true; }
					else if (e.type == EV_ABS && e.code == ABS_X) { x = e.value; moved = true; }
					else if (e.type == EV_ABS && e.code == ABS_Y) { y = e.value; moved = true; }
					else if (e.type == EV_ABS && e.code == ABS_MT_TRACKING_ID && slot == 0) down = e.value >= 0;
					else if (e.type == EV_KEY && e.code == BTN_TOUCH) down = e.value != 0;
					else if (e.type == EV_SYN && e.code == SYN_REPORT)
					{
						Pointer p;
						p.x = maxX > minX ? (x - minX) / (float)(maxX - minX) : 0;
						p.y = maxY > minY ? (y - minY) / (float)(maxY - minY) : 0;
						bool send = true;
						if (down && !wasDown) p.kind = Pointer::Down;
						else if (down && moved) p.kind = Pointer::Move;
						else if (!down && wasDown) p.kind = Pointer::Up;
						else send = false;
						if (send && onPointer) onPointer(p);
						wasDown = down;
						moved = false;
					}
				}
			if (r < 0 && errno != EAGAIN) return;	// the device went away
		}
	}

	void BottomTouch::stop()
	{
		if (!thread.joinable()) return;
		char c = 'q';
		if (write(stopPipe[1], &c, 1) < 0) {}
		thread.join();
		close(stopPipe[0]);
		close(stopPipe[1]);
		close(fd);
		fd = -1;
	}
#else
	bool BottomTouch::start(const std::string&, std::function<void(Pointer)>) { return false; }
	void BottomTouch::run() {}
	void BottomTouch::stop() {}
#endif
}
