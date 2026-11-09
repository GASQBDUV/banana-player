#include "settings.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <vector>

namespace bp
{
	static const struct { const char* key; bool Settings::*field; } REMEMBER[] = {
		{"remember-position", &Settings::rememberPosition},
		{"remember-audio", &Settings::rememberAudio},
		{"remember-subtitles", &Settings::rememberSubs},
		{"remember-folder", &Settings::rememberFolder},
		{"keep-history", &Settings::keepHistory},
		{"keep-network-history", &Settings::keepNetHistory},
		{"button-hotkeys", &Settings::buttonHotkeys},
		{"show-hidden-folders", &Settings::showHiddenFolders},
		{"battery-percent", &Settings::batteryPercent},
		{"keep-photo-zoom", &Settings::keepPhotoZoom},
		{"check-updates", &Settings::checkUpdates},
		{"same-brightness", &Settings::sameBrightness},
	};

	static const char* SHOW_IDS[SHOWS] = {"art", "both", "visualizer", "none"};
	static const char* SHOW_LABELS[SHOWS] = {"Album art", "Art + visualizer", "Visualizer", "None"};
	static const char* VIS_IDS[VISUALIZERS] = {"bars", "waves", "spectrum", "circle", "timeline"};
	static const char* VIS_LABELS[VISUALIZERS] = {"Bars", "Waves", "Spectrum", "Circle", "Timeline"};
	static const char* LOOP_IDS[LOOPS] = {"off", "all", "one"};
	static const char* CLOCK_IDS[CLOCKS] = {"24h", "12h", "off"};
	static const char* SCREEN_OFF_IDS[SCREEN_OFFS] = {"never", "15s", "30s", "1m", "2m", "5m"};

	int screenOffSeconds(int v)
	{
		static const int SECS[SCREEN_OFFS] = {0, 15, 30, 60, 120, 300};
		return v >= 0 && v < SCREEN_OFFS ? SECS[v] : 0;
	}

	const char* screenOffLabel(int v)
	{
		static const char* L[SCREEN_OFFS] = {"Never", "15 seconds", "30 seconds", "1 minute", "2 minutes", "5 minutes"};
		return v >= 0 && v < SCREEN_OFFS ? L[v] : L[0];
	}
	static const char* LOOP_LABELS[LOOPS] = {"No loop", "Loop all", "Loop one"};

	const char* musicShowLabel(int v) { return SHOW_LABELS[v >= 0 && v < SHOWS ? v : 0]; }
	const char* visualizerLabel(int v) { return VIS_LABELS[v >= 0 && v < VISUALIZERS ? v : 0]; }
	const char* loopLabel(int v) { return LOOP_LABELS[v >= 0 && v < LOOPS ? v : 0]; }

	// Circle: the spectrum's bars (BARS_W x BARS_H, low notes left, growing up) and beside them PALETTE columns, the
	// ring's glow fading out to black; each pixel of the square picture is one of theirs (remap), so a frame costs a
	// lookup a pixel. The bars run from the top down both sides, mirrored, SEGMENTS a side
	static const int BARS_W = 256, BARS_H = 128, PALETTE = 16, SEGMENTS = 40;
	static const double BAR_FROM = 84, BAR_TO = 200, RING = 76, CENTRE_Y = 274;
	static const char* CIRCLE_MAPS[2] = {"circle2-x.pgm", "circle2-y.pgm"};

	bool writeCircleMaps(const std::string& dir)
	{
		const int w = CIRCLE_BOX, h = CIRCLE_BOX;
		size_t bytes = (size_t)w * h * 2;
		std::string header = "P5\n" + std::to_string(w) + " " + std::to_string(h) + "\n65535\n";
		auto ready = [&](const std::string& path) {
			struct stat st;
			return stat(path.c_str(), &st) == 0 && (size_t)st.st_size == header.size() + bytes;
		};
		if (ready(dir + "/" + CIRCLE_MAPS[0]) && ready(dir + "/" + CIRCLE_MAPS[1])) return true;
		std::vector<unsigned char> xs(bytes), ys(bytes);
		for (int y = 0; y < h; y++)
			for (int x = 0; x < w; x++)
			{
				double dx = x + CIRCLE_X - CIRCLE_W / 2.0 + 0.5, dy = y + CIRCLE_Y - CENTRE_Y + 0.5, r = std::hypot(dx, dy);
				double seg = std::fabs(std::atan2(dx, -dy)) / M_PI * SEGMENTS;	// 0 at the top, SEGMENTS at the bottom
				int mx, my;
				if (r >= BAR_FROM && r <= BAR_TO && seg - std::floor(seg) < 0.6)
				{
					// a bar: its frequency by the angle, its height by the distance out
					mx = (int)((std::floor(seg) + 0.5) / SEGMENTS * BARS_W);
					my = (int)std::lround((BARS_H - 1) * (1 - (r - BAR_FROM) / (BAR_TO - BAR_FROM)));
				}
				else
				{
					// the ring and its glow, else black (the palette's last column)
					mx = BARS_W + std::min(PALETTE - 1, (int)std::lround(std::fabs(r - RING) * 2));
					my = 0;
				}
				size_t i = ((size_t)y * w + x) * 2;
				xs[i] = (unsigned char)(mx >> 8);
				xs[i + 1] = (unsigned char)mx;
				ys[i] = (unsigned char)(my >> 8);
				ys[i + 1] = (unsigned char)my;
			}
		mkdir(dir.c_str(), 0755);
		for (int k = 0; k < 2; k++)
		{
			std::string path = dir + "/" + CIRCLE_MAPS[k], tmp = path + ".tmp";
			std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
			out << header;
			const std::vector<unsigned char>& v = k ? ys : xs;
			out.write((const char*)v.data(), (std::streamsize)v.size());
			if (!out.flush()) return false;
			out.close();
			if (rename(tmp.c_str(), path.c_str()) != 0) return false;
		}
		return true;
	}

	bool lavfiSafePath(const std::string& dir)
	{
		return !dir.empty() && dir.find_first_of(":='\\") == std::string::npos;
	}

	// a path as a filter's argument: quoted (spaces, commas, brackets, ';'); one with ':', '=', a quote or a
	// backslash can't be named at all
	static bool lavfiPath(const std::string& p, std::string& out)
	{
		if (!lavfiSafePath(p)) return false;
		out = "'" + p + "'";
		return true;
	}

	static std::string circleGraph(bool overCover, const std::string& dir)
	{
		std::string xm, ym;
		lavfiPath(dir + "/" + CIRCLE_MAPS[0], xm);
		lavfiPath(dir + "/" + CIRCLE_MAPS[1], ym);
		std::string bars = std::to_string(BARS_W) + "x" + std::to_string(BARS_H);
		// cyan (left: the low notes, at the top) to blue-purple to pink (the high, at the bottom), multiplied into
		// the white bars; the ring a pale blue, fading over the palette's columns
		// The still inputs (the gradient, the palette, the tables) are pictures that end, never loops: blend, hstack
		// and remap go on with an ended input's last frame, and end when the bars (the sound) do. A loop never ends,
		// so neither did the graph: past the song's end its sources poured out frames as fast as they could, mpv's
		// clock stopped (the song never ended) and its memory grew by the gigabyte. One frame each (r=1): nullsrc's
		// 25 a second over d=1 made the first second 55 frames, each one remapped
		std::string g =
			"[aid1] asplit [ao] [a]; "
			"[a] aformat=sample_rates=44100|48000:channel_layouts=mono,showfreqs=s=" + bars + ":mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=30:colors=white|white,format=gbrp [bars]; "
			"nullsrc=s=" + bars + ":r=1:d=1,format=gbrp,"
			"geq=r='59+196*clip(2*X/W-1,0,1)':g='232-140*clip(2*X/W,0,1)-13*clip(2*X/W-1,0,1)':b='224+31*clip(2*X/W,0,1)-39*clip(2*X/W-1,0,1)' [grad]; "
			"[bars] [grad] blend=all_mode=multiply [cbars]; "
			"nullsrc=s=" + std::to_string(PALETTE) + "x" + std::to_string(BARS_H) + ":r=1:d=1,format=gbrp,"
			"geq=r='207*exp(-pow(X/4,2))':g='232*exp(-pow(X/4,2))':b='255*exp(-pow(X/4,2))' [pal]; "
			"[cbars] [pal] hstack [src]; "
			"movie=" + xm + " [xm]; movie=" + ym + " [ym]; ";
		std::string pad = ",pad=" + std::to_string(CIRCLE_W) + ":" + std::to_string(CIRCLE_H) + ":" + std::to_string(CIRCLE_X) + ":" + std::to_string(CIRCLE_Y) + ":black";
		if (!overCover) return g + "[src] [xm] [ym] remap" + pad + " [vo]";
		// the art inside the ring
		return g + "[src] [xm] [ym] remap" + pad + " [ring]; "
			"[vid1] scale=96:96:force_original_aspect_ratio=decrease,format=gbrp [cover]; "
			"[ring] [cover] overlay=x=(W-w)/2:y=" + std::to_string((int)CENTRE_Y) + "-h/2:eof_action=repeat:format=gbrp [vo]";
	}

	// Timeline: each frame one 8-pixel bar's strip (a 6-pixel bar and a gap) for the last 30th of a second, and the
	// last 80 together, the newest on the right (tile's overlap: each frame shifts the picture one bar). A bar is the
	// sound's level in dB (showvolume's RMS, -50 dB at the foot, 0 at the top), green, with the bass's level (below
	// 150 Hz, 20 dB down, so it sits under the rest) coral over it. showvolume's log scale spans ~95 dB: the bar is
	// drawn longer than the picture and only its loud end kept, which spreads 50 dB over the height.
	// The bars are drawn lying down (rows) and the picture turned upright at the end: tile copies an overlapped
	// picture rect by rect, a row of pixels at a time, so 80 upright columns were 80 x 480 tiny copies a frame
	// (3.6 ms on the A55s), 80 rows a few hundred long ones, and transpose costs less than the difference. Only the
	// strip is converted (yuv420p alone, 480x8; over the art it stays RGBA, clear round its bars, alpha 0 or 255, so
	// it's premultiplied as it is, and the tile's padding clear too). The grid is in the strip, so it starts on the
	// right with the bars
	static std::string timelineDraw(int h, bool grid)
	{
		int len = h * 20 * 100 / (21 * 50);
		std::string H = std::to_string(h);
		std::string vol = "showvolume=r=30:b=0:w=" + std::to_string(len) + ":h=6:f=0:t=0:v=0:o=h:m=r:ds=log:c=";
		// showvolume's colours are 0xAABBGGRR: coral #F0806E, green #9EDB98
		std::string g = "[a] aformat=sample_rates=44100|48000:channel_layouts=mono,asplit [tla] [tlb]; "
						"[tlb] lowpass=f=150,volume=-20dB," + vol + "0xff6e80f0 [tlc]; "
						"[tla] " + vol + "0xff98db9e [tlg]; "
						"[tlg] [tlc] overlay=format=rgb,crop=" + H + ":6:" + std::to_string(len - h) + ":0,pad=" + H + ":8:0:0:black@0";
		// a faint line every 96 pixels from the top (white at 10%: #19 on black, a shade lighter over a bar), drawn in
		// the strip (the strip's x is the height from the foot): a still grid lightened under the bars (blend, with
		// its frame sync) cost 1 ms a frame on the A55s, these next to nothing
		if (grid)
		{
			g += ",format=yuv420p";
			for (int y = 96; y < h; y += 96) g += ",drawbox=x=" + std::to_string(h - 1 - y) + ":y=0:w=1:h=8:c=white@0.1:t=fill";
		}
		return g + ",tile=1x80:overlap=79:init_padding=79" + (grid ? "" : ":color=black@0") + ",transpose=cclock";
	}

	std::string visualizerGraph(int vis, bool overCover, const std::string& mapsDir)
	{
		std::string quoted;
		if (vis == VIS_CIRCLE && !mapsDir.empty() && lavfiPath(mapsDir, quoted)) return circleGraph(overCover, mapsDir);
		if (vis == VIS_CIRCLE) vis = VIS_BARS;
		// drawn at the panel's shape (mpv scales it to the panel) and 30 frames a second: light enough for the A55s
		// beside the decoding
		int h = overCover ? 200 : 480;
		std::string size = "s=640x" + std::to_string(h);
		std::string draw;
		// a soft amber, not the menu's bright yellow: it fills much of a dark screen. One colour a channel: the filters
		// give a channel with none its own (white for the second), and stereo is most music
		const std::string ink = "0xC08A22|0xC08A22";
		// The sound as the visualizers see it (the speakers' branch, [ao], keeps the file's own): 44.1 or 48 kHz, so a
		// 96/192 kHz file costs no more than a CD (and its spectrum isn't mostly empty ultrasound), and Bars in mono
		// (its two channels are drawn in one colour anyway, and every bar is painted pixel by pixel: half the cost)
		const std::string in = "aformat=sample_rates=44100|48000";
		switch (vis)
		{
		case VIS_WAVES: draw = in + ",showwaves=" + size + ":mode=cline:split_channels=0:rate=30:colors=" + ink; break;
		// its own fps: a frame made only every 30th of a second (an fps filter after it took one per column, each a
		// copy of the whole picture, and dropped most)
		case VIS_SPECTRUM: draw = in + ",showspectrum=" + size + ":slide=scroll:mode=combined:color=fire:scale=log:fps=30"; break;
		default: draw = in + ":channel_layouts=mono,showfreqs=" + size + ":mode=bar:cmode=combined:ascale=log:fscale=log:win_size=1024:rate=30:colors=" + ink; break;
		}
		if (vis == VIS_TIMELINE && !overCover) return "[aid1] asplit [ao] [a]; " + timelineDraw(h, true) + " [vo]";
		// near the format the filter draws in, which mpv shows as it is: converting each frame to yuv420p cost 6.5-7 ms
		// a frame on the A55s, more than drawing Waves or Spectrum. RGBA's alpha dropped (rgb0, a byte shuffle: 0.5 ms):
		// mpv would lay a clear background over a checkerboard; Spectrum's YUV as it comes
		if (!overCover) return "[aid1] asplit [ao] [v]; [v] " + draw + (vis == VIS_SPECTRUM ? "" : ",format=rgb0") + " [vo]";
		// the art fitted once, as one frame, and cut in two; only the 640x200 strip under it is drawn each frame (the
		// visualizer over its slice of the art), in the visualizer's own format so no frame is converted.
		// Redrawing the whole picture cost 3.5-4.4 ms a frame on a Mac and over the 33 ms budget on the A55s, on the
		// thread that feeds the sound. The art's slice is overlay's main input: its one frame is held, and a frame is
		// made for each of the visualizer's, till the sound ends (its timebase fine, or the frames' times would be
		// rounded to the picture's 25ths). Bars' and Waves' background is already clear, and their colours already
		// scaled by their alpha (Waves' edges fade), hence premultiplied; Spectrum's is black, so it's drawn with alpha
		// (its loudness, 4x: only the near-black clear, as colorkey did, without its 5 ms a frame).
		// vstack repeats the top's ended frame, as Circle's hstack does
		bool yuv = vis == VIS_SPECTRUM;
		std::string strip = vis == VIS_TIMELINE ? timelineDraw(h, false)
						  : yuv					? "[a] " + draw + ":opacity=4,format=yuva444p"
												: "[a] " + draw;
		return "[aid1] asplit [ao] [a]; "
			   "[vid1] scale=640:480:force_original_aspect_ratio=decrease,pad=640:480:-1:-1:black,setsar=1,format=rgba" +
			   std::string(yuv ? ",scale=out_range=full,format=yuv444p" : "") + ",settb=AVTB,split [art1] [art2]; "
			   "[art1] crop=640:280:0:0 [top]; "
			   "[art2] crop=640:200:0:280 [under]; " +
			   strip + " [s]; "
			   "[under] [s] overlay=format=" + (yuv ? "yuv444" : "rgb:alpha=premultiplied") + " [strip]; "
			   "[top] [strip] vstack [vo]";
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
		out += std::string("clock = ") + CLOCK_IDS[s.clock] + "\n";
		out += std::string("screen-off = ") + SCREEN_OFF_IDS[s.screenOffAfter] + "\n";
		out += std::string("pan-area = ") + (s.panRight ? "right" : "left") + "\n";
		out += std::string("pan-drag = ") + (s.dragBox ? "box" : "photo") + "\n";
		out += std::string("eq = ") + (s.eqOn ? "on" : "off") + "\n";
		out += "eq-preset = " + s.eqPreset + "\n";
		out += "eq-origin = " + s.eqOrigin + "\n";
		out += "eq-gains = " + formatEqGains(s.eqGains) + "\n";
		out += std::string("sound-effect = ") + soundFxKey(s.soundFx) + "\n";
		out += std::string("web-dashboard = ") + (s.webDashboard ? "on" : "off") + "\n";
		if (s.updateRemindAfter > 0) out += "update-remind-after = " + std::to_string(s.updateRemindAfter) + "\n";
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
			for (int v = 0; v < CLOCKS; v++)
				if (key == "clock" && value == CLOCK_IDS[v]) s.clock = v;
			for (int v = 0; v < SCREEN_OFFS; v++)
				if (key == "screen-off" && value == SCREEN_OFF_IDS[v]) s.screenOffAfter = v;
			if (key == "pan-area" && (value == "left" || value == "right")) s.panRight = value == "right";
			if (key == "pan-drag" && (value == "photo" || value == "box")) s.dragBox = value == "box";
			if (key == "eq" && (value == "on" || value == "off")) s.eqOn = value == "on";
			if (key == "eq-preset") s.eqPreset = value;
			if (key == "eq-origin") s.eqOrigin = value;
			if (key == "eq-gains") parseEqGains(value, s.eqGains);
			if (key == "sound-effect") s.soundFx = soundFxFromKey(value);
			if (key == "web-dashboard" && (value == "on" || value == "off")) s.webDashboard = value == "on";
			if (key == "update-remind-after") s.updateRemindAfter = atoll(value.c_str());
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
