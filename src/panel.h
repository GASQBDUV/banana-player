#pragma once
#include <string>

namespace bp
{
	// The bottom panel's backlight, as BananaOS's one-panel programs switch it: bl_power 4 (off) or 0 (on) on the
	// backlight bananad names in /run/banana/backlight-bottom, and /run/banana/panel-off naming it while it is off, so
	// that bananad leaves it dark after a wake or the lid opening. Where there's no such backlight (a computer) only
	// the picture goes black.
	class BottomPanel
	{
	public:
		void init();
		void set(bool on);
		bool on() const { return isOn; }
		// on again whatever happens: a normal quit, and (from a signal handler) a crash
		~BottomPanel() { set(true); }

	private:
		bool isOn = true;
		std::string backlight;	// /sys/class/backlight/<bottom>; empty: none
	};
}
