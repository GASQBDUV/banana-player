#include "panel.h"
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>

namespace bp
{
	static const char* MARKER = "/run/banana/panel-off";

	// what the crash handler writes: set before the backlight goes off, plain buffers for async-signal-safe calls
	static char blPowerPath[300];
	static volatile sig_atomic_t panelOff = 0;

	static void restoreNow()
	{
		if (!panelOff) return;
		int fd = open(blPowerPath, O_WRONLY);
		if (fd >= 0)
		{
			if (write(fd, "0", 1) < 0) {}
			close(fd);
		}
		unlink(MARKER);
		panelOff = 0;
	}

	static void onCrash(int sig)
	{
		restoreNow();
		signal(sig, SIG_DFL);
		raise(sig);
	}

	void BottomPanel::init()
	{
		std::ifstream f("/run/banana/backlight-bottom");
		std::string path;
		std::getline(f, path);
		if (path.rfind("/sys/class/backlight/", 0) != 0 || path.find("..") != std::string::npos) return;
		std::string power = path + "/bl_power";
		if (access(power.c_str(), W_OK) != 0 || power.size() >= sizeof blPowerPath) return;
		backlight = path;
		strcpy(blPowerPath, power.c_str());
		for (int sig : {SIGSEGV, SIGABRT, SIGBUS, SIGILL, SIGFPE}) signal(sig, onCrash);
	}

	void BottomPanel::set(bool on)
	{
		if (on == isOn) return;
		isOn = on;
		if (backlight.empty()) return;
		if (on)
		{
			restoreNow();
			return;
		}
		// the marker first: bananad keeps the panel dark from the moment it can see it
		std::ofstream(MARKER) << backlight << "\n";
		panelOff = 1;
		std::ofstream(blPowerPath) << "4";
	}
}
