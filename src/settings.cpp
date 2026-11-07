#include "settings.h"
#include <sstream>

namespace bp
{
	static const struct { const char* key; bool Settings::*field; } REMEMBER[] = {
		{"remember-position", &Settings::rememberPosition},
		{"remember-audio", &Settings::rememberAudio},
		{"remember-subtitles", &Settings::rememberSubs},
		{"remember-folder", &Settings::rememberFolder},
		{"keep-history", &Settings::keepHistory},
		{"button-hotkeys", &Settings::buttonHotkeys},
		{"show-hidden-folders", &Settings::showHiddenFolders},
	};

	static const char* SHOW_IDS[SHOWS] = {"art", "both", "visualizer", "none"};
	static const char* SHOW_LABELS[SHOWS] = {"Album art", "Art + visualizer", "Visualizer", "None"};
	static const char* VIS_IDS[VISUALIZERS] = {"bars", "waves", "spectrum"};
	static const char* VIS_LABELS[VISUALIZERS] = {"Bars", "Waves", "Spectrum"};
	static const char* LOOP_IDS[LOOPS] = {"off", "all", "one"};
	static const char* LOOP_LABELS[LOOPS] = {"No loop", "Loop all", "Loop one"};

	const char* musicShowLabel(int v) { return SHOW_LABELS[v >= 0 && v < SHOWS ? v : 0]; }
	const char* visualizerLabel(int v) { return VIS_LABELS[v >= 0 && v < VISUALIZERS ? v : 0]; }
	const char* loopLabel(int v) { return LOOP_LABELS[v >= 0 && v < LOOPS ? v : 0]; }

	std::string visualizerGraph(int vis, bool overCover)
	{
		// drawn at the panel's shape (mpv scales it to the panel) and 30 frames a second: light enough for the A55s
		// beside the decoding
		int h = overCover ? 200 : 480;
		std::string size = "s=640x" + std::to_string(h);
		std::string draw;
		// a soft amber, not the menu's bright yellow: it fills much of a dark screen. One colour a channel: the filters
		// give a channel with none its own (white for the second), and stereo is most music
		const std::string ink = "0xC08A22|0xC08A22";
		switch (vis)
		{
		case VIS_WAVES: draw = "showwaves=" + size + ":mode=cline:split_channels=0:rate=30:colors=" + ink; break;
		case VIS_SPECTRUM: draw = "showspectrum=" + size + ":slide=scroll:mode=combined:color=fire:scale=log,fps=30"; break;
		default: draw = "showfreqs=" + size + ":mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:colors=" + ink + ",fps=30"; break;
		}
		if (!overCover) return "[aid1] asplit [ao] [v]; [v] " + draw + ",format=yuv420p [vo]";
		// the art (one picture: overlay repeats it) fitted on a black canvas timed by the visualizer, and the
		// visualizer, its black keyed out, over the lower part
		return "[aid1] asplit [ao] [a]; "
			   "[a] " + draw + ",pad=640:480:0:280,split [t] [b]; "
			   "[t] drawbox=c=black:t=fill [canvas]; "
			   "[vid1] scale=640:480:force_original_aspect_ratio=decrease [cover]; "
			   "[canvas] [cover] overlay=x=(W-w)/2:y=(H-h)/2:eof_action=repeat [base]; "
			   "[b] colorkey=black:0.1 [vis]; "
			   "[base] [vis] overlay,format=yuv420p [vo]";
	}

	std::string blankGraph()
	{
		return "[aid1] asplit [ao] [a]; [a] showwaves=s=32x24:rate=5,drawbox=c=black:t=fill,format=yuv420p [vo]";
	}

	std::string serializeSettings(const Settings& s)
	{
		std::string out = serializeSubStyle(s.style);
		out += std::string("music-show = ") + SHOW_IDS[s.musicShow] + "\n";
		out += std::string("visualizer = ") + VIS_IDS[s.visualizer] + "\n";
		out += std::string("loop = ") + LOOP_IDS[s.loop] + "\n";
		for (auto& r : REMEMBER) out += std::string(r.key) + " = " + (s.*r.field ? "yes" : "no") + "\n";
		return out;
	}

	Settings parseSettings(const std::string& text)
	{
		Settings s;
		s.style = parseSubStyle(text);	// it leaves out the keys below
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			size_t eq = line.find('=');
			if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
			auto trim = [](std::string v) {
				size_t a = v.find_first_not_of(" \t\r"), b = v.find_last_not_of(" \t\r");
				return a == std::string::npos ? std::string() : v.substr(a, b - a + 1);
			};
			std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
			for (auto& r : REMEMBER)
				if (key == r.key && (value == "yes" || value == "no")) s.*r.field = value == "yes";
			for (int v = 0; v < SHOWS; v++)
				if (key == "music-show" && value == SHOW_IDS[v]) s.musicShow = v;
			for (int v = 0; v < VISUALIZERS; v++)
				if (key == "visualizer" && value == VIS_IDS[v]) s.visualizer = v;
			for (int v = 0; v < LOOPS; v++)
				if (key == "loop" && value == LOOP_IDS[v]) s.loop = v;
			// the one setting before 0.1.1, kept: auto, cover-bars, bars, waves, spectrum, cover
			if (key == "audio-display")
			{
				if (value == "cover-bars") { s.musicShow = SHOW_BOTH; s.visualizer = VIS_BARS; }
				else if (value == "bars" || value == "waves" || value == "spectrum")
				{
					s.musicShow = SHOW_VIS;
					s.visualizer = value == "bars" ? VIS_BARS : value == "waves" ? VIS_WAVES : VIS_SPECTRUM;
				}
				else s.musicShow = SHOW_ART;
			}
		}
		return s;
	}
}
