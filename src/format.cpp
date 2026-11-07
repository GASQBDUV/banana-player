#include "format.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bp
{
	std::string formatTime(double seconds)
	{
		if (!(seconds >= 0) || std::isinf(seconds)) return "--:--";
		long t = (long)seconds;
		char buf[32];
		if (t >= 3600) snprintf(buf, sizeof buf, "%ld:%02ld:%02ld", t / 3600, t / 60 % 60, t % 60);
		else snprintf(buf, sizeof buf, "%ld:%02ld", t / 60, t % 60);
		return buf;
	}

	static const double SPEEDS[] = {0.5, 1.0, 1.5, 2.0, 2.5};
	static const int NSPEEDS = sizeof SPEEDS / sizeof *SPEEDS;

	double stepSpeed(double current, int dir, bool wrap)
	{
		if (dir > 0)
		{
			for (double s : SPEEDS)
				if (s > current + 1e-6) return s;
			return wrap ? SPEEDS[0] : SPEEDS[NSPEEDS - 1];
		}
		for (int i = NSPEEDS - 1; i >= 0; i--)
			if (SPEEDS[i] < current - 1e-6) return SPEEDS[i];
		return wrap ? SPEEDS[NSPEEDS - 1] : SPEEDS[0];
	}

	std::string speedLabel(double speed)
	{
		char buf[16];
		snprintf(buf, sizeof buf, "%gx", std::round(speed * 100) / 100);
		return buf;
	}

	std::string formatBytes(long long bytes)
	{
		static const char* units[] = {"B", "KB", "MB", "GB", "TB"};
		double v = (double)bytes;
		int u = 0;
		while (v >= 1024 && u < 4) { v /= 1024; u++; }
		char buf[32];
		snprintf(buf, sizeof buf, u == 0 || v >= 100 ? "%.0f %s" : "%.1f %s", v, units[u]);
		return buf;
	}

	std::string formatBitrate(double bps)
	{
		char buf[32];
		if (bps >= 1e6) snprintf(buf, sizeof buf, "%.1f Mb/s", bps / 1e6);
		else snprintf(buf, sizeof buf, "%.0f kb/s", bps / 1e3);
		return buf;
	}

	double timeLeft(double position, double duration, double speed)
	{
		if (!(duration > 0) || !(position >= 0)) return -1;
		return std::max(0.0, duration - position) / (speed > 0 ? speed : 1);
	}
}
