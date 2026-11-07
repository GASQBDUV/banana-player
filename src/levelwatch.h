#pragma once
#include <functional>
#include <string>
#include <thread>

namespace bp
{
	// The volume and brightness keys' levels. bananad handles the keys and writes what it set to /run/banana/osd
	// ("volume 85", "brightness 40": the file renamed into place), for the menu's level card; it tells only the menu,
	// so the player watches the file (inotify on Linux, else a look every 200 ms). BP_OSD_FILE: another file.
	class LevelWatch
	{
	public:
		// onLevel(what, percent) runs on the watch's own thread
		bool start(std::function<void(const std::string&, int)> onLevel);
		void stop();
		~LevelWatch() { stop(); }

	private:
		void run();
		void read();
		std::function<void(const std::string&, int)> onLevel;
		std::thread thread;
		std::string path, dir, name;
		int stopPipe[2] = {-1, -1};
		long long lastStamp = 0;
	};
}
