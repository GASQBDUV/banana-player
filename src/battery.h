#pragma once
#include <string>

namespace bp
{
	// The battery as BananaOS's menu reads it: the first power supply of type "Battery" for the percent, charging when
	// its status says so or any other supply (USB, mains) is online (the rk817's gauge says "Discharging" while it
	// charges). percent < 0: no battery here (a computer).
	struct BatteryState
	{
		int percent = -1;
		bool charging = false;
	};

	// base: /sys/class/power_supply (BP_POWER_SUPPLY for tests)
	BatteryState readBattery(const std::string& base);
}
