#pragma once
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace bp
{
	// The volume, as BananaOS's own menu sets it: its script (/usr/libexec/banana/volume get | set N) puts the percent
	// the ear hears on whichever card plays now (the speakers' table, the jack's curve, a Bluetooth headset), then
	// bananad gets a SIGHUP and keeps the level across reboots. A set runs on a thread of its own, the latest one only
	// (a finger dragging the bar sends many)
	class Volume
	{
	public:
		std::string script = "/usr/libexec/banana/volume";	// BP_VOLUME_SCRIPT in tests
		bool present() const;
		int get() const;		// 0-100, -1 unknown
		void set(int percent);	// in the background
		~Volume();

	private:
		void run();
		std::thread worker;
		std::mutex lock;
		std::condition_variable wake;
		int pending = -1;
		bool stopping = false;
	};
}
