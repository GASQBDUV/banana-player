#include "battery.h"
#include <algorithm>
#include <cstdlib>
#include <dirent.h>
#include <fstream>

namespace bp
{
	static std::string line(const std::string& path)
	{
		std::ifstream f(path);
		std::string s;
		std::getline(f, s);
		while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
		return s;
	}

	BatteryState readBattery(const std::string& base)
	{
		BatteryState b;
		bool charger = false;
		DIR* d = opendir(base.c_str());
		if (!d) return b;
		while (dirent* e = readdir(d))
		{
			if (e->d_name[0] == '.') continue;
			std::string p = base + "/" + e->d_name;
			if (line(p + "/type") != "Battery")
			{
				if (line(p + "/online") == "1") charger = true;
				continue;
			}
			if (b.percent >= 0) continue;
			std::string cap = line(p + "/capacity");
			if (cap.empty()) continue;
			b.percent = std::min(100, std::max(0, atoi(cap.c_str())));
			std::string st = line(p + "/status");
			b.charging = st == "Charging" || st == "Full";
		}
		closedir(d);
		if (charger && b.percent >= 0) b.charging = true;
		return b;
	}
}
