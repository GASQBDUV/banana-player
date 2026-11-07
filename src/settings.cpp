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

	static const char* AUDIO_IDS[AUDIO_DISPLAYS] = {"auto", "cover-bars", "bars", "waves", "spectrum", "cover"};
	static const char* AUDIO_LABELS[AUDIO_DISPLAYS] = {"Auto", "Cover + bars", "Bars", "Waves", "Spectrum", "Cover only"};

	const char* audioDisplayLabel(int d)
	{
		return d >= 0 && d < AUDIO_DISPLAYS ? AUDIO_LABELS[d] : AUDIO_LABELS[0];
	}

	std::string audioVisualizerGraph(int d)
	{
		// drawn at 640x480 (the panel's shape; mpv scales it to the panel) and 30 frames a second: light enough for
		// the RK3568's cores beside the decoding. BananaOS's yellow on black.
		std::string draw;
		switch (d)
		{
		case AUDIO_COVER_BARS:
			// the cover (one picture: overlay repeats it) fitted on a black canvas timed by the bars, and the bars,
			// their black keyed out, over its lower part
			return "[aid1] asplit [ao] [a]; "
				   "[a] showfreqs=s=640x200:mode=bar:ascale=log:fscale=log:win_size=1024:colors=0xF0B429,fps=30,"
				   "pad=640:480:0:280,split [t] [b]; "
				   "[t] drawbox=c=black:t=fill [canvas]; "
				   "[vid1] scale=640:480:force_original_aspect_ratio=decrease [cover]; "
				   "[canvas] [cover] overlay=x=(W-w)/2:y=(H-h)/2:eof_action=repeat [base]; "
				   "[b] colorkey=black:0.1 [bars]; "
				   "[base] [bars] overlay,format=yuv420p [vo]";
		case AUDIO_AUTO: case AUDIO_BARS:
			draw = "showfreqs=s=640x480:mode=bar:ascale=log:fscale=log:win_size=1024:colors=0xF0B429,fps=30";
			break;
		case AUDIO_WAVES: draw = "showwaves=s=640x480:mode=cline:rate=30:colors=0xF0B429"; break;
		case AUDIO_SPECTRUM: draw = "showspectrum=s=640x480:slide=scroll:mode=combined:color=fire:scale=log,fps=30"; break;
		default: return "";
		}
		return "[aid1] asplit [ao] [v]; [v] " + draw + ",format=yuv420p [vo]";
	}

	std::string serializeSettings(const Settings& s)
	{
		std::string out = serializeSubStyle(s.style);
		out += std::string("audio-display = ") + AUDIO_IDS[s.audioDisplay] + "\n";
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
			for (int d = 0; d < AUDIO_DISPLAYS; d++)
				if (key == "audio-display" && value == AUDIO_IDS[d]) s.audioDisplay = d;
		}
		return s;
	}
}
