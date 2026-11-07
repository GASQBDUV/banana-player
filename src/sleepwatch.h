#pragma once
#include <functional>
#include <thread>

namespace bp
{
	// Pauses before the handheld sleeps and keeps it paused after. bananad sends the player's process group SIGSTOP
	// when the lid closes (or power is pressed) and SIGCONT when it opens: on its own mpv would play on from there.
	//   - the lid closing (SW_LID, read from evdev as bananad does): pause at once. This is also the whole of it when
	//     lid sleep is off and closing the lid only turns the backlights off.
	//   - SIGCONT, after any sleep: pause, in case bananad stopped us before the lid's event was read.
	// Opening the lid does nothing: play is the player's to press.
	class SleepWatch
	{
	public:
		// onSleep runs on the watch's own thread
		bool start(std::function<void()> onSleep);
		void stop();
		~SleepWatch() { stop(); }

	private:
		void run();
		std::function<void()> onSleep;
		std::thread thread;
		int stopPipe[2] = {-1, -1};
		int lidFd = -1;
	};
}
