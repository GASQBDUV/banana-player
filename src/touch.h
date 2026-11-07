#pragma once
#include <functional>
#include <string>
#include <thread>

namespace bp
{
	// A finger on the bottom panel: down, moved, up; x and y from 0 to 1 across the panel.
	struct Pointer
	{
		enum Kind { Down, Move, Up } kind;
		float x, y;
	};

	// The bottom touchscreen, read from evdev as banana-ui reads it: SDL can't tell the handheld's two touchscreens
	// apart (they may share a name), so the bottom one is found by its device path ("fe5e0000.i2c" on the RG DS, as in
	// banana-ui; BP_TOUCH overrides it). One finger: the first slot's.
	class BottomTouch
	{
	public:
		// false where there's no such device (a computer): SDL's mouse and touch events stand in
		bool start(const std::string& pathFragment, std::function<void(Pointer)> onPointer);
		void stop();
		~BottomTouch() { stop(); }

	private:
		void run();
		std::function<void(Pointer)> onPointer;
		std::thread thread;
		int fd = -1, stopPipe[2] = {-1, -1};
		int minX = 0, maxX = 0, minY = 0, maxY = 0;
	};
}
