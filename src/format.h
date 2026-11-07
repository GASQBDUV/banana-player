#pragma once
#include <string>

namespace bp
{
	// 75 -> "1:15", 3725 -> "1:02:05"; a negative or unknown time -> "--:--"
	std::string formatTime(double seconds);

	// the speeds offered: 0.5x 1x 1.5x 2x 2.5x. stepSpeed goes to the next one up (dir 1) or down (-1) from any speed,
	// stopping at the ends, or going round with wrap
	double stepSpeed(double current, int dir, bool wrap = false);
	std::string speedLabel(double speed);	// "1.5x"

	// what's left of it in real time at that speed: 600 s left at 2x -> 300; unknown (-1) when either is
	double timeLeft(double position, double duration, double speed);

	// 1536 -> "1.5 KB", 734003200 -> "700 MB" (1024s)
	std::string formatBytes(long long bytes);
	// bits a second: 128000 -> "128 kb/s", 4500000 -> "4.5 Mb/s"
	std::string formatBitrate(double bps);
}
