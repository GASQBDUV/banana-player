#include "ui.h"
#include "format.h"
#include "playlist.h"
#include "netpath.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	// BananaOS's colours
	static const SDL_Color BG{0x17, 0x16, 0x1C, 255}, CREAM{0xF1, 0xED, 0xE2, 255}, YELLOW{0xF0, 0xB4, 0x29, 255},
		TEAL{0x23, 0x43, 0x46, 255}, DIM{0x8A, 0x86, 0x7C, 255}, RED{0xE0, 0x5A, 0x4F, 255}, SCREEN{0x3A, 0x3A, 0x44, 255},
		PRESSED{0x3A, 0x6E, 0x72, 255},
		// a panel's body (dialogs, empty states), and secondary text on teal (DIM there is too faint)
		SURFACE{0x21, 0x1F, 0x28, 255}, DIM_TEAL{0xB4, 0xAF, 0xA3, 255};
	static const int SIZES[3] = {18, 24, 32};
	static const Uint32 QUIT_WINDOW_MS = 2000, SCRUB_MS = 100;
	static const double BACK_S = 5, FORWARD_S = 10;

	constexpr Ui::Rect Ui::BAR;

	static float sx = 1, sy = 1;	// logical 640x480 -> the panel's pixels

	// ---- glyphs: small pictures as rows of cells ('#' drawn), every icon on one grid ----

#define GLYPH(name, ...) static const char* const name##_ROWS[] = {__VA_ARGS__}; \
	static const Ui::Glyph name{name##_ROWS, (int)(sizeof name##_ROWS / sizeof *name##_ROWS)};
	GLYPH(G_CLOCK, "...#####...", ".#########.", ".####.####.", "#####.#####", "#####.#####", "#####...###", "###########",
		".#########.", ".#########.", "...#####...")
	GLYPH(G_CARD, "...#######", "..##.#.#.#", ".###.#.#.#", ".#########", ".#########", ".#########", ".#########",
		".#########", ".#########", ".#########")
	GLYPH(G_FOLDER, "#####......", "###########", "###########", "###########", "###########", "###########",
		"###########", "###########", "###########")
	GLYPH(G_FILM, "###########", "#.#.#.#.#.#", "###########", "###########", "###########", "###########",
		"###########", "#.#.#.#.#.#", "###########")
	GLYPH(G_NOTE, "......####", "......####", "......#..#", "......#...", "......#...", "......#...", "...####...",
		"..#####...", "..#####...", "...###....")
	GLYPH(G_PICTURE, "###########", "#.........#", "#......##.#", "#......##.#", "#....#....#", "#...###...#",
		"#..#####..#", "#.#######.#", "#.........#", "###########")
	GLYPH(G_NETWORK, "...#####...", "...#####...", "...#####...", ".....#.....", ".#########.", ".#.......#.",
		"###.....###", "###.....###", "###.....###")
	GLYPH(G_PLAY, "##......", "####....", "######..", "########", "########", "######..", "####....", "##......")
	GLYPH(G_PAUSE, "###..###", "###..###", "###..###", "###..###", "###..###", "###..###", "###..###", "###..###")
	GLYPH(G_PREV, "##.....##", "##....###", "##..#####", "#########", "#########", "##..#####", "##....###", "##.....##")
	GLYPH(G_NEXT, "##.....##", "###....##", "#####..##", "#########", "#########", "#####..##", "###....##", "##.....##")
	GLYPH(G_GEAR, "...###...", ".#.###.#.", "#########", ".###.###.", "###...###", ".###.###.", "#########",
		".#.###.#.", "...###...")
	GLYPH(G_INFO, "..#####..", ".###.###.", "#########", "###..####", "####.####", "####.####", "####.####",
		".##...##.", "..#####..")
	GLYPH(G_LOCK, "..#####..", ".##...##.", ".#.....#.", ".#.....#.", "#########", "#########", "####.####",
		"####.####", "#########", "#########")
	GLYPH(G_MOON, "...####..", ".####....", ".###.....", "###......", "###......", "###......", ".###....#",
		".#######.", "...####..")
	GLYPH(G_SPEAKER, "....#....", "...##..#.", "####.#..#", "####..#.#", "####..#.#", "####.#..#", "...##..#.",
		"....#....")
	GLYPH(G_CC, "###########", "###########", "#.......###", "###########", "###########", "##.....##.#",
		"###########", "###########")
	GLYPH(G_POWER, "....#....", ".#..#..#.", "#...#...#", "#...#...#", "#.......#", "#.......#", ".#.....#.",
		"..#####..")
	GLYPH(G_CROSS, "##.....##", "###...###", ".###.###.", "..#####..", "...###...", "..#####..", ".###.###.",
		"###...###", "##.....##")
	GLYPH(G_SUN, "....#....", ".#.....#.", "...###...", "..#####..", "#.#####.#", "..#####..", "...###...",
		".#.....#.", "....#....")
	GLYPH(G_CHECK, "........#", ".......##", "#.....##.", "##...##..", ".##.##...", "..###....", "...#.....")
	GLYPH(G_ALERT, ".....#.....", "....###....", "...##.##...", "...##.##...", "..###.###..", "..###.###..",
		".#########.", ".####.####.", "###########")
	GLYPH(G_BOOKMARK, "#######", "#######", "#######", "#######", "#######", "#######", "#######", "###.###",
		"##...##", "#.....#")
	GLYPH(G_DOWNLOAD, "...###...", "...###...", "...###...", "#########", ".#######.", "..#####..", "...###...",
		"....#....", ".........", "#########")
	GLYPH(G_RADIO, "..###..", ".#...#.", "#.....#", "#.....#", "#.....#", ".#...#.", "..###..")
	GLYPH(G_RADIO_ON, "..###..", ".#...#.", "#.###.#", "#.###.#", "#.###.#", ".#...#.", "..###..")
	GLYPH(G_DOWN, "##.....##", ".##...##.", "..##.##..", "...###...", "....#....")
	GLYPH(G_UP, "....#....", "...###...", "..##.##..", ".##...##.", "##.....##")
	GLYPH(G_RIGHT, "##...", ".##..", "..##.", "...##", "..##.", ".##..", "##...")
	GLYPH(G_LEFT, "...##", "..##.", ".##..", "##...", ".##..", "..##.", "...##")
	GLYPH(G_MINUS, "#########", "#########", "#########")
	GLYPH(G_PLUS, "...###...", "...###...", "...###...", "#########", "#########", "#########", "...###...",
		"...###...", "...###...")
	GLYPH(G_SEARCH, "..####...", ".##..##..", "##....##.", "##....##.", ".##..##..", "..######.", "......###",
		".......##")
	GLYPH(G_REFRESH, "...#####.#", ".##.....##", "#.....####", "#.........", "#........#", "#........#", ".##....##.",
		"...####...")
	GLYPH(G_EYE, "...#####...", ".##.....##.", "##..###..##", "#..#####..#", "##..###..##", ".##.....##.",
		"...#####...")
	GLYPH(G_PENCIL, "......###", ".....####", "....####.", "...####..", "..####...", ".####....", "####.....",
		"###......", "##.......")
	GLYPH(G_TRASH, "...###...", "#########", ".........", ".#######.", ".#.#.#.#.", ".#.#.#.#.", ".#.#.#.#.",
		".#.#.#.#.", ".#######.")
	GLYPH(G_PLUG, "..#...#..", "..#...#..", "#########", "#########", ".#######.", "..#####..", "...###...",
		"....#....", "....#....")
	GLYPH(G_BACK, "....##", "...##.", "..##..", ".##...", "##....", ".##...", "..##..", "...##.", "....##")
GLYPH(G_CHAPTERS, "###.#######", "###.#######", "...........", "###.#####..", "###.#####..", "...........",
		"###.######.", "###.######.")
	// music: the equalizer (Sound), Loop, and the Display popup's (title only, art + visualizer, the styles)
	GLYPH(G_EQ, ".#...#...#.", ".#..###..#.", ".#..###..#.", "###..#...#.", "###..#...#.", ".#...#..###", ".#...#..###",
		".#...#...#.", ".#...#...#.")
	GLYPH(G_LOOP, ".......#...", ".########..", "#......#...", "#.........#", "#.........#", "...#......#", "..########.",
		"...#.......")
	GLYPH(G_TITLE_ONLY, "#########", "#########", ".........", ".#######.", ".........", "..#####..")
	GLYPH(G_ARTVIZ, "###########", "#.........#", "#......##.#", "#......##.#", "#.........#", "#.....#...#", "#.#...#.#.#",
		"#.#.#.#.#.#", "#.#.#.#.#.#", "###########")
	GLYPH(G_VIS_BARS, ".........##", "...##....##", "...##.##.##", "...##.##.##", "##.##.##.##", "##.##.##.##", "##.##.##.##",
		"##.##.##.##")
	GLYPH(G_VIS_WAVES, "..##.......", ".#..#......", "#....#.....", ".....#....#", "......#..#.", ".......##..")
	GLYPH(G_VIS_SPECTRUM, "...#.......", "..###...#..", "..####.###.", ".#########.", ".##########", "###########")
	GLYPH(G_VIS_CIRCLE, ".....#.....", ".#...#...#.", "..#.....#..", "....###....", "...#...#...", "##.#...#.##", "...#...#...",
		"....###....", "..#.....#..", ".#...#...#.", ".....#.....")
	// playlists: one (lines and a note), a select box empty and ticked, a row's grip, Move, Shuffle
	GLYPH(G_PLAYLIST, "#######....", "...........", "#######.###", "........###", "#######.#..", "........#..", "####....#..",
		".....####..", "....#####..", ".....###...")
	GLYPH(G_BOX, "#########", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", "#.......#", "#########")
	GLYPH(G_BOX_ON, "#########", "#########", "#######.#", "######..#", "#.###..##", "#..#..###", "##...####", "###.#####",
		"#########")
	GLYPH(G_GRIP, "#########", ".........", "#########", ".........", "#########")
	GLYPH(G_MOVE, "...#...", "..###..", ".#####.", "...#...", "...#...", "...#...", ".#####.", "..###..", "...#...")
	GLYPH(G_SHUFFLE, ".........#.", "##.....####", "..#...#..#.", "...#.#.....", "....#......", "...#.#.....", "..#...#..#.",
		"##.....####", ".........#.")
#undef GLYPH

	// the two rows of buttons: what each does is the button it stands for
	static const struct { Button button; const char* hint; } GRID[2][5] = {
		{{Button::L, "L"}, {Button::Left, "LEFT"}, {Button::A, "A"}, {Button::Right, "RIGHT"}, {Button::R, "R"}},
		{{Button::X, "X"}, {Button::Y, "Y"}, {Button::Select, "SELECT"}, {Button::Up, "UP/DOWN"}, {Button::Start, "START"}},
	};

	// the settings' on/off rows (TOGGLE_ROW + their index)
	static const char* REMEMBER_LABELS[12] = {"Remember position", "Remember audio track", "Remember subtitles",
		"Remember last folder", "Keep play history", "Button hotkeys", "Show hidden folders", "Battery percentage",
		"Keep zoom between photos", "Check automatically", "Same for both screens", "Network files in Recent"};
	static bool Settings::*const REMEMBER[12] = {&Settings::rememberPosition, &Settings::rememberAudio, &Settings::rememberSubs,
		&Settings::rememberFolder, &Settings::keepHistory, &Settings::buttonHotkeys, &Settings::showHiddenFolders,
		&Settings::batteryPercent, &Settings::keepPhotoZoom, &Settings::checkUpdates, &Settings::sameBrightness,
		&Settings::keepNetHistory};
	static const struct { const char* name; const char* about; } GROUP_NAMES[] = {
		{"Subtitles", "Font, size, colour"},
		{"Music", "Display, visualizer, sound"},
		{"Photos", "Zoom, pan area, drag"},
		{"Remember", "Position, tracks"},
		{"Files", "Folders, play history"},
		{"Controls & screen", "Brightness, buttons"},
		{"Updates", "New versions"},
	};
	static const char* RECENT = "\x01recent";
	static const char* NETROOT = "\x01net";	// the Network tab's first page: the saved connections
	static const char* FORMDIR = "\x01form";	// a connection's form (Add connection, Edit)
	static const char* FINDNET = "\x01find";	// the servers found on the network
	static const char* PLROOT = "\x01pls";	// the Playlists tab's first page: the saved playlists
	static const char* PLDIR = "\x01pl:";	// a playlist's items: PLDIR + the playlist's file
	static bool isPlDir(const std::string& d) { return d.rfind(PLDIR, 0) == 0; }
	static std::string plDirFor(const std::string& file) { return PLDIR + file; }
	static std::string plFileOf(const std::string& d) { return isPlDir(d) ? d.substr(strlen(PLDIR)) : ""; }
	// a page of the browser's own, not a folder on a card (never listed or stat'ed). Compared whole: FORMDIR and
	// FINDNET don't start with \x01 (the escape takes their "f" as a hex digit)
	static bool pseudoDir(const std::string& d)
	{
		return d == RECENT || d == NETROOT || d == FORMDIR || d == FINDNET || d == PLROOT || isPlDir(d);
	}
	// the browser's rows' ids: 1 a folder, 2 a file, 3 Clear play history, 4 a connection, 6 Try again, -1 a message;
	// a playlist (Playlists' first page), a playlist's item
	enum { PL_ROW = 7, PL_ITEM = 8 };
	// the form's rows
	enum { FORM_TYPE = 20, FORM_NAME, FORM_HOST, FORM_PORT, FORM_SHARE, FORM_FOLDER, FORM_SIGNIN, FORM_USER, FORM_PASS, FORM_FTPS, FORM_TEST, FORM_SAVE };
	// the found servers' rows
	enum { R_FOUND = 50, R_SEARCH, R_TYPE_ADDRESS };
	// a note drawn dim (a placeholder: what an empty field comes out as)
	static const char DIM_NOTE = '\x02';
	// a file's note: where it was left and its size ("at 0:42 · 13.9 MB")
	static std::string fileNote(const std::string& at, long long size)
	{
		std::string sz = size >= 0 ? formatBytes(size) : "";
		return at.empty() ? sz : sz.empty() ? at : at + " \xC2\xB7 " + sz;
	}
	static long long fileSize(const std::string& path)
	{
		struct stat st;
		return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? (long long)st.st_size : -1;
	}
	static const char* SHOW_SHORT[SHOWS] = {"Art", "Both", "Viz", "None"};	// MusicShow on a button
	static const char* LOOP_SHORT[LOOPS] = {"Off", "All", "One"};		// Loop on a button (beside its mark)
	static const Ui::Glyph* const VIS_GLYPHS[VISUALIZERS] = {&G_VIS_BARS, &G_VIS_WAVES, &G_VIS_SPECTRUM, &G_VIS_CIRCLE};
	// a show mode's mark: the visualizer ones take the style's
	static const Ui::Glyph* showGlyph(int show, int vis)
	{
		return show == SHOW_ART ? &G_PICTURE : show == SHOW_NONE ? &G_TITLE_ONLY : VIS_GLYPHS[vis >= 0 && vis < VISUALIZERS ? vis : 0];
	}
	// the effects' short names (the song's line, Settings' value), and what each is (the Effects tab)
	static const char* FX_SHORT[SOUNDFX_COUNT] = {"None", "Room", "Hall", "Church", "Cave", "Wide", "Phones"};
	static const char* FX_ABOUT[SOUNDFX_COUNT] = {"Just the music", "A small, lively room", "A big hall, a long warm tail",
		"Stone walls, a very long tail", "Rock walls and echoes", "Wider than the speakers", "Less tiring in headphones"};
	static const char* CUSTOM_NAME = "Custom";	// an edited curve, not saved (eqPreset "")
	// the keyboard's field for a preset's name (apart from the form's and the found servers' ids)
	static const int EQ_NAME = 60, PL_NAME = 61;
	static const char* const LQ = "\xE2\x80\x9C", *const RQ = "\xE2\x80\x9D";	// “ ”
	static std::string quoted(const std::string& s) { return LQ + s + RQ; }
	// "Rock", "Custom", "Rock · Hall", "EQ off · Hall", "Off": the sound as one line (Settings' value)
	static std::string soundSummary(const Settings& s)
	{
		std::string preset = s.eqPreset.empty() ? CUSTOM_NAME : s.eqPreset;
		bool fx = s.soundFx != SoundFx::None;
		if (!s.eqOn) return fx ? std::string("EQ off \xC2\xB7 ") + FX_SHORT[(int)s.soundFx] : "Off";
		return fx ? preset + " \xC2\xB7 " + FX_SHORT[(int)s.soundFx] : preset;
	}

	static std::string baseName(const std::string& p)
	{
		size_t slash = p.rfind('/');
		return slash == std::string::npos ? p : p.substr(slash + 1);
	}

	static bool exists(const std::string& p)
	{
		struct stat st;
		return stat(p.c_str(), &st) == 0;
	}

	bool Ui::init(SDL_Window* window, const std::string& dir)
	{
		fontsDir = dir;
		panel.init();
		// tests: a fake /sys/class/backlight and /data/banana/backlight
		if (const char* d = getenv("BP_BACKLIGHT_DIR")) backlight.sysDir = d;
		if (const char* f = getenv("BP_BACKLIGHT_STORE")) backlight.store = f;
		if (const char* v = getenv("BP_VOLUME_SCRIPT")) volume.script = v;
		ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
		if (!ren) ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
		if (!ren) return false;
		int ow, oh;
		SDL_GetRendererOutputSize(ren, &ow, &oh);
		sx = ow / (float)W;
		sy = oh / (float)H;
		// the text is drawn at the panel's own size rather than scaled up: Pixelify Sans stays sharp
		std::string path = fontsDir + "/PixelifySans-Regular.ttf";
		for (int i = 0; i < 3; i++)
		{
			fonts[i] = TTF_OpenFont(path.c_str(), (int)std::lround(SIZES[i] * sy));
			if (!fonts[i])
			{
				SDL_Log("font %s: %s", path.c_str(), TTF_GetError());
				return false;
			}
		}
		for (int i = 0; i < 3; i++)
		{
			int minx, maxx, miny, maxy, adv;
			capHeight[i] = TTF_GlyphMetrics(fonts[i], 'H', &minx, &maxx, &miny, &maxy, &adv) == 0 ? maxy : TTF_FontAscent(fonts[i]) * 3 / 4;
		}
		static const char* FALLBACKS[] = {"fusion-pixel-12px-proportional-zh_hans.ttf", "NotoSansThai.ttf",
			"NotoSansArabic.ttf", "NotoSansHebrew.ttf", "NotoSansDevanagari.ttf"};
		for (const char* name : FALLBACKS)
		{
			std::array<TTF_Font*, 3> f = {};
			for (int i = 0; i < 3; i++)
			{
				// Fusion Pixel is a 12-pixel font: at whole multiples of 12 its pixels stay square
				int px = (int)std::lround(SIZES[i] * sy);
				if (name == FALLBACKS[0]) px = std::max(12, (int)std::lround(SIZES[i] * sy / 12.0) * 12);
				f[i] = TTF_OpenFont((fontsDir + "/" + name).c_str(), px);
			}
			if (f[0] && f[1] && f[2]) fallbacks.push_back(f);
			else for (auto* x : f) if (x) TTF_CloseFont(x);
		}
		return true;
	}

	// Pixelify Sans's "fi" and "fl" ligatures come out as another letter ("file" read "Ale"): its runs are cut between
	// the two, each piece shaped on its own
	static std::vector<std::pair<TTF_Font*, std::string>> unligated(std::vector<std::pair<TTF_Font*, std::string>> in, TTF_Font* pixel)
	{
		std::vector<std::pair<TTF_Font*, std::string>> out;
		for (auto& r : in)
		{
			if (r.first != pixel) { out.push_back(r); continue; }
			size_t from = 0;
			for (size_t i = 0; i + 1 < r.second.size(); i++)
				if (r.second[i] == 'f' && (r.second[i + 1] == 'i' || r.second[i + 1] == 'l'))
				{
					out.push_back({pixel, r.second.substr(from, i + 1 - from)});
					from = i + 1;
				}
			out.push_back({pixel, r.second.substr(from)});
		}
		return out;
	}

	std::vector<std::pair<TTF_Font*, std::string>> Ui::runs(const std::string& s, int size) const
	{
		return unligated(shapeRuns(s, size), fonts[size]);
	}

	std::vector<std::pair<TTF_Font*, std::string>> Ui::shapeRuns(const std::string& s, int size) const
	{
		// the characters, each with its bytes
		std::vector<std::pair<Uint32, std::string>> chars;
		for (size_t i = 0; i < s.size();)
		{
			unsigned char c = s[i];
			Uint32 cp = c;
			int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
			if (n > 1)
			{
				cp = c & (0x7F >> n);
				for (int k = 1; k < n && i + k < s.size(); k++) cp = (cp << 6) | (s[i + k] & 0x3F);
			}
			chars.push_back({cp, s.substr(i, n)});
			i += n;
		}
		auto hasAll = [&](TTF_Font* f) {
			for (auto& c : chars)
				if (c.first >= 0x20 && !TTF_GlyphIsProvided32(f, c.first)) return false;
			return true;
		};
		if (hasAll(fonts[size])) return {{fonts[size], s}};
		for (auto& f : fallbacks)
			if (hasAll(f[size])) return {{f[size], s}};
		// no one font has them all: Pixelify Sans where it can, else the first fallback that has the character
		std::vector<std::pair<TTF_Font*, std::string>> out;
		for (auto& c : chars)
		{
			TTF_Font* f = fonts[size];
			if (c.first >= 0x20 && !TTF_GlyphIsProvided32(f, c.first))
				for (auto& fb : fallbacks)
					if (TTF_GlyphIsProvided32(fb[size], c.first)) { f = fb[size]; break; }
			if (!out.empty() && out.back().first == f) out.back().second += c.second;
			else out.push_back({f, c.second});
		}
		return out;
	}

	int Ui::textWidth(const std::string& s, int size) const
	{
		int total = 0;
		for (auto& r : runs(s, size))
		{
			int w = 0;
			TTF_SizeUTF8(r.first, r.second.c_str(), &w, nullptr);
			total += w;
		}
		return total;
	}

	Ui::~Ui()
	{
		if (photoTex) SDL_DestroyTexture(photoTex);
		for (auto& c : cache) SDL_DestroyTexture(c.second);
		for (auto* f : fonts)
			if (f) TTF_CloseFont(f);
		for (auto& f : fallbacks)
			for (auto* x : f) TTF_CloseFont(x);
		if (ren) SDL_DestroyRenderer(ren);
	}

	std::vector<Ui::Row> Ui::rows(const Player& p) const
	{
		if (page == Page::Browse) return browseRows;
		std::vector<Row> out;
		if (page == Page::Chapters)
		{
			// a chapter a row (its index the id), the one playing as selected
			int cur = p.currentChapter();
			for (size_t i = 0; i < p.chapters.size(); i++) out.push_back({(int64_t)i, chapterLabel(p.chapters[i], (int)i + 1), (int)i == cur});
			return out;
		}
		TrackType type = trackType();
		auto list = p.tracksOf(type);
		if (type == TrackType::Sub)
			out.push_back({0, "Off", p.selected(TrackType::Sub) == nullptr, list.empty() ? "No subtitles in this file" : ""});
		for (size_t i = 0; i < list.size(); i++) out.push_back({list[i].id, trackLabel(list[i], (int)i + 1), list[i].selected});
		if (type == TrackType::Sub) out.push_back({STYLE_ROW, "Subtitle style...", false});
		if (out.empty()) out.push_back({-1, "No audio in this file", false});
		return out;
	}

	void Ui::openMenu(Page pg, const Player& p)
	{
		page = pg;
		cursor = scroll = 0;
		if (pg == Page::Settings) { setGroup = -1; return; }
		auto r = rows(p);
		for (size_t i = 0; i < r.size(); i++)
			if (r[i].selected) cursor = (int)i;
		scroll = std::max(0, cursor - ROWS_SHOWN + 1);
	}

	void Ui::openTracks(int tab, const Player& p)
	{
		tracksTab = tab;
		openMenu(Page::Tracks, p);
	}

	void Ui::openChapters(const Player& p)
	{
		page = Page::Chapters;
		// on the one playing, three rows above it showing where the list allows (what came before it, a little)
		int n = (int)p.chapters.size();
		cursor = std::clamp(p.currentChapter(), 0, std::max(0, n - 1));
		scroll = std::clamp(cursor - 3, 0, std::max(0, n - ROWS_SHOWN));
	}

	void Ui::choose(int i, Player& p)
	{
		auto r = rows(p);
		if (i < 0 || i >= (int)r.size()) return;
		if (page == Page::Browse)
		{
			// no connections yet: A adds one (as the page's button does); no playlists, New playlist
			if (emptyAction()) { doEmptyAction(p); return; }
			if (r[i].id < 0) return;
			// selecting: a row ticked (a folder still opens)
			if (selMode != SEL_NONE && selChoose(i, p)) return;
			if (r[i].id == PL_ROW)
			{
				// a playlist: its items (and the one kept for the next launch, as a folder gone into is)
				std::string file = browsePaths[i];
				enterFolder(plDirFor(file), p);
				p.setLastFolder(file);
				return;
			}
			if (r[i].id == PL_ITEM)
			{
				// an item that isn't there: what can be done about it (its menu, why under the title)
				if (plMissingAt(i)) { openBrowseMenu(i, MENU_Y); return; }
				plPlay(plCur, i, false, p);
				return;
			}
			if (browseDir == FORMDIR) { formChoose(r[i].id, p); return; }
			if (browseDir == FINDNET)
			{
				if (r[i].id == R_FOUND && i < (int)found.size()) { FoundHost h = found[i]; openForm(nullptr, &h, p); }
				else if (r[i].id == R_SEARCH) enterFolder(FINDNET, p);
				else if (r[i].id == R_TYPE_ADDRESS) openForm(nullptr, nullptr, p);
				return;
			}
			if (r[i].id == 4 || r[i].id == 6)
			{
				// a connection (its start folder), or Try again
				std::string path = browsePaths[i];
				NetPath np;
				if (r[i].id == 4 && netStore && parseNetPath(path, np)) netStore->touch(np.server);
				enterFolder(path, p);
				return;
			}
			if (r[i].id == 3)
			{
				if (clearHistory(p)) enterFolder("", p);	// nothing left to list: back to the first page
				else enterFolder(RECENT, p);			// the row says "again to clear"
				return;
			}
			std::string path = browsePaths[i];	// a copy: entering a folder rebuilds the list
			// the folder last gone into, or a video opened from, is the one kept (Remember last folder): backing out
			// to the first page to quit leaves it as it was
			if (r[i].id == 1)
			{
				enterFolder(path, p);
				if (path != RECENT) p.setLastFolder(path);
			}
			else
			{
				if (browseDir != RECENT) p.setLastFolder(browseDir);
				// a network file whose folder wasn't listed (Recent): listed first, a moment, for the files that play
				// after it, its subtitles and its cover (netListed opens it)
				NetPath np;
				const NetServer* sv = isNetPath(path) && parseNetPath(path, np) && netStore ? netStore->find(np.server) : nullptr;
				if (sv && netWorker && netNames.find(netParent(path)) == netNames.end())
				{
					netOpenPending = path;
					netWorker->list(++netSerial, *sv, netStore->password(*sv), netParent(path));
				}
				else openRequest = path;
			}
			return;
		}
		if (r[i].id == STYLE_ROW)
		{
			openMainMenu(p, TAB_SETTINGS);
			openGroup(GROUP_SUBS);
			styleFromSubs = true;
			return;
		}
		if (page == Page::Chapters)
		{
			// from there: playing (a paused one too), the card says where, back to the player
			int c = (int)r[i].id;
			p.seekChapter(c);
			p.unpause();
			p.showChapterCard(c);
			page = Page::Main;
			return;
		}
		if (r[i].id < 0) return;
		TrackType type = trackType();
		p.selectTrack(type, r[i].id);
		p.osd(std::string(type == TrackType::Audio ? "Audio: " : "Subtitles: ") + r[i].label);
		page = Page::Main;
	}

	void Ui::startBrowser(Player& p, const std::string& folder)
	{
		roots = browseRoots();
		page = Page::Browse;
		if (!folder.empty())
		{
			enterFolder(folder, p);
			return;
		}
		const std::string& last = p.resumeStore().lastFolder;
		struct stat st;
		// a playlist kept: the Playlists tab, in it (one deleted since: the tab's list)
		if (plStore && p.settings.rememberFolder && last.rfind(plStore->dir() + "/", 0) == 0)
		{
			browseTab = TAB_PLAYLISTS;
			enterFolder(stat(last.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? plDirFor(last) : std::string(PLROOT), p);
			return;
		}
		bool there = p.settings.rememberFolder && !last.empty() && stat(last.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
			(p.settings.showHiddenFolders || (!isHiddenFolder(last) && !insideHiddenFolder(last)));
		enterFolder(there ? last : "", p);
	}

	void Ui::showBrowser(Player& p, const std::string& lastPlayed, const PlaySource& from, int index)
	{
		page = Page::Browse;
		touchLocked = false;	// the lock is the video's
		dropTouch();
		if (from.kind == SourceKind::Playlist && plStore)
		{
			// the playlist it played from, the cursor on the item (by the order it played in; else by its path)
			if (browseTab != TAB_PLAYLISTS) tabDir[browseTab] = browseDir;
			browseTab = TAB_PLAYLISTS;
			if (!exists(from.id)) { enterFolder(PLROOT, p); return; }
			enterFolder(plDirFor(from.id), p);
			int item = from.id == plOrderId && index >= 0 && index < (int)plOrder.size() ? plOrder[index] : -1;
			if (item < 0 || item >= (int)browsePaths.size() || browsePaths[item] != lastPlayed)
			{
				item = -1;
				for (size_t i = 0; i < browsePaths.size() && item < 0; i++)
					if (browseRows[i].id == PL_ITEM && browsePaths[i] == lastPlayed) item = (int)i;
			}
			if (item >= 0) cursor = item;
			fitList();
			return;
		}
		// from a playlist's tab to a file of a card's (Recent): its folder is the Local tab's
		if (browseTab == TAB_PLAYLISTS && !isNetPath(lastPlayed) && !lastPlayed.empty())
		{
			tabDir[browseTab] = browseDir;
			browseTab = TAB_LOCAL;
		}
		if (isNetPath(lastPlayed))
		{
			// its network folder, listed again (the cursor goes to it once the listing comes)
			if (browseTab != TAB_NETWORK) tabDir[browseTab] = browseDir;
			browseTab = TAB_NETWORK;
			netSelect = lastPlayed;
			enterFolder(netParent(lastPlayed), p);
			return;
		}
		// listed again either way: positions changed, a file may have come or gone
		enterFolder(lastPlayed.empty() ? browseDir : parentFolder(lastPlayed), p);
		for (size_t i = 0; i < browsePaths.size(); i++)
			if (browsePaths[i] == lastPlayed) cursor = (int)i;
		fitList();
	}

	void Ui::dropTouch()
	{
		// a finger still down on what's gone presses nothing on what comes (as it lifts); none down: nothing to drop
		if (panTouch || listTouch || dragging || pressedBtn >= 0) swallowTouch = true;
		panTouch = panMoved = listTouch = dragging = false;
		pressedBtn = -1;
		flickAt = tapAt = holdAt = 0;
	}

	void Ui::fitList()
	{
		int n = (int)browseRows.size();
		cursor = std::clamp(cursor, 0, std::max(0, n - 1));
		// the cursor in the window, and no empty rows below the list's end while there are rows above
		scroll = std::clamp(scroll, std::max(0, cursor - ROWS_SHOWN + 1), std::min(cursor, std::max(0, n - ROWS_SHOWN)));
	}

	// the rows' ids: 1 a folder to go into, 2 a video to open, -1 nothing (a message)
	void Ui::enterFolder(std::string dir, Player& p)
	{
		// the playlist's edits written before another page reads it (or the list counts it)
		plFlush(p);
		placeIn[browseDir] = {cursor, scroll};
		netOpenPending.clear();	// gone elsewhere first: not opened
		if (browseDir == FINDNET && dir != FINDNET && discovery) discovery->stop();
		browseDir = dir;
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& path, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back(path);
		};
		auto at = [&](const std::string& path) {
			const ResumeEntry* e = p.resumeStore().find(path);
			// a song's (kept by an earlier version) left out: songs start from their start
			return e && e->position >= 0 && !isAudioFile(path) ? "at " + formatTime(e->position) : std::string();
		};
		netLoading = false;
		// what Recent lists: a file still there (on a card taken out: not), or a network one whose connection is
		// still saved (whether the file is there is only known by asking its server: when it's opened)
		auto inRecent = [&](const std::string& path) {
			if (isNetPath(path))
			{
				NetPath np;
				return parseNetPath(path, np) && netStore && netStore->find(np.server);
			}
			return exists(path) && (p.settings.showHiddenFolders || !insideHiddenFolder(path));
		};
		plMissing.clear();
		plMissingSerial++;
		if (dir == PLROOT)
		{
			// the saved playlists, by name
			if (!plStore) add(-1, "Playlists aren't available", "", "");
			else
				for (auto& pl : plStore->list())
					add(PL_ROW, pl.name, pl.file, std::to_string(pl.count) + (pl.count == 1 ? " item" : " items"));
			if (browseRows.empty() && plStore) add(-1, "No playlists yet", "", "");
		}
		else if (isPlDir(dir))
		{
			// a playlist's items, in its order (read again each time: another page may have changed it)
			plCur = Playlist();
			int skipped = 0;
			plLoaded = plStore && plStore->load(plFileOf(dir), plCur, skipped);
			if (!plLoaded) plCur.file = plFileOf(dir);
			plRows(p);
		}
		else if (dir == FORMDIR) formRows();
		else if (dir == FINDNET)
		{
			// a search from scratch each time it's opened (Search again too)
			found.clear();
			if (discovery && !discovery->running()) discovery->start(onFound, 4);
			findWasRunning = discovery && discovery->running();
			findRows();
		}
		else if (dir == NETROOT)
		{
			// the saved connections, the most recently used first
			std::vector<NetServer> list = netStore ? netStore->servers() : std::vector<NetServer>();
			std::stable_sort(list.begin(), list.end(), [](const NetServer& a, const NetServer& b) { return a.lastUsed > b.lastUsed; });
			for (auto& sv : list)
			{
				std::string start = "/" + sv.share + (sv.share.empty() || sv.folder.empty() ? "" : "/" + sv.folder);
				add(4, serverName(sv), makeNetPath(sv.id, start), std::string(netTypeLabel(sv.type)) + "  " + sv.host);
			}
			if (browseRows.empty()) add(-1, "No connections yet", "", "");
		}
		else if (isNetPath(dir))
		{
			// asked of the worker; "Loading..." till its answer (netListed)
			NetPath np;
			parseNetPath(dir, np);
			const NetServer* sv = netStore ? netStore->find(np.server) : nullptr;
			if (!sv) add(-1, "This connection is gone", "", "");
			else if (!netWorker) add(-1, "No network support in this build", "", "");
			else
			{
				netLoading = true;
				add(-1, "Loading...", "", "");
				netWorker->list(++netSerial, *sv, netStore->password(*sv), dir);
			}
		}
		else if (dir.empty())
		{
			int recent = 0;
			for (auto& path : p.history().items())
				if (inRecent(path)) recent++;
			if (p.settings.keepHistory && recent) add(1, "Recent", RECENT, std::to_string(recent) + (recent == 1 ? " item" : " items"));
			for (auto& r : roots) add(1, r.label, r.path, r.path);
		}
		else if (dir == RECENT && !p.settings.keepHistory) add(-1, "Play history is off (Settings)", "", "");
		else if (dir == RECENT)
		{
			// the history (a file taken away or on a card taken out left out), and a way to clear it
			for (auto& path : p.history().items())
				if (inRecent(path)) add(2, baseName(path), path, fileNote(at(path), isNetPath(path) ? -1 : fileSize(path)));
			if (!browseRows.empty()) add(3, "Clear play history", "", clearArmed ? "Press again to clear" : "Press twice");
		}
		else
		{
			bool ok;
			for (auto& e : listFolder(dir, ok, p.settings.showHiddenFolders))
				add(e.dir ? 1 : 2, e.name, e.path, e.dir ? (isHiddenFolder(e.path) ? "hidden" : "") : fileNote(at(e.path), fileSize(e.path)));
			if (!ok) add(-1, "This folder can't be opened", "", "");
		}
		if (browseRows.empty() && !isPlDir(dir)) add(-1, "Nothing to play here", "", "");
		auto it = placeIn.find(dir);
		cursor = it == placeIn.end() ? 0 : std::min(it->second.first, (int)browseRows.size() - 1);
		scroll = it == placeIn.end() ? 0 : it->second.second;
		// Recent opens at its top: the most recent first, and a place in a list that changed since means little
		if (dir == RECENT) cursor = scroll = 0;
		fitList();
		browseVersion++;
	}

	void Ui::netListed(NetListing* l, Player& p)
	{
		std::unique_ptr<NetListing> own(l);
		// a playlist item's folder (tick asked): the player's, before anything of the browser's
		if (auto side = sidePending.find(l->serial); !l->test && side != sidePending.end())
		{
			sidePending.erase(side);
			p.netFolderListed(l->dir, l->names, l->result.ok());
			return;
		}
		// a folder to add to a playlist, read first: added now (an older one, cancelled, is nothing)
		if (!l->test && l->serial > (1 << 29) && l->serial < (1 << 30))
		{
			if (l->serial != addPending.serial) return;
			AddPending a = addPending;
			addPending = {};
			if (!l->result.ok())
			{
				NetPath np;
				const NetServer* sv = netStore && parseNetPath(l->dir, np) ? netStore->find(np.server) : nullptr;
				// (not found: the folder, most likely; the server answered its parent's listing)
				p.osd(l->result.error == NetError::NotFound ? "Can't find " + quoted(folderName(l->dir))
					: netErrorText(l->result.error, sv ? sv->host : "the server"));
				return;
			}
			if (netNames.size() > 32) netNames.clear();
			netNames[l->dir] = l->names;
			int photos = 0;
			auto add = folderEntries(l->dir, l->names, photos);
			addTo(a.dest, add, photos, folderName(l->dir), a.back, p);
			return;
		}
		if (l->test)
		{
			// Test connection's answer (the form's row, a connection's menu); an older test's is nothing
			if (l->serial != testSerial) return;
			testText = l->result.ok() ? "It works" : netErrorText(l->result.error, "The server");
			ctxStatus = testText;
			if (browseDir == FORMDIR) formRows();
			browseVersion++;
			return;
		}
		// the folder of a file to open, listed first: opened now, with what the folder has (alone if it failed)
		if (!l->test && !netOpenPending.empty() && l->serial == netSerial && l->dir == netParent(netOpenPending))
		{
			if (l->result.ok()) netNames[l->dir] = l->names;
			openRequest = netOpenPending;
			netOpenPending.clear();
			return;
		}
		if (l->test || l->serial != netSerial || l->dir != browseDir || page != Page::Browse) return;
		netLoading = false;
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& path, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back(path);
		};
		NetPath np;
		parseNetPath(l->dir, np);
		const NetServer* sv = netStore ? netStore->find(np.server) : nullptr;
		if (!l->result.ok())
		{
			add(-1, netErrorText(l->result.error, sv ? sv->host : "the server"), "", "");
			add(6, "Try again", l->dir, "");
		}
		else
		{
			// a few folders' worth (Recent, Back to one): opening a file outside them plays it alone
			if (netNames.size() > 32) netNames.clear();
			netNames[l->dir] = l->names;
		}
		if (l->result.ok())
			for (auto& e : l->entries)
			{
				std::string path = netJoin(l->dir, e.name);
				const ResumeEntry* r = p.resumeStore().find(path);
				std::string note = !e.dir && r && r->position >= 0 && !isAudioFile(e.name) ? "at " + formatTime(r->position) : "";
				add(e.dir ? 1 : 2, e.name, path, e.dir ? note : fileNote(note, e.size));
			}
		if (browseRows.empty()) add(-1, "Nothing to play here", "", "");
		auto it = placeIn.find(l->dir);
		cursor = it == placeIn.end() ? 0 : it->second.first;
		scroll = it == placeIn.end() ? 0 : it->second.second;
		if (!netSelect.empty())
			for (size_t i = 0; i < browsePaths.size(); i++)
				if (browsePaths[i] == netSelect) cursor = (int)i;
		netSelect.clear();
		fitList();
		browseVersion++;
	}

	bool Ui::emptyAction() const
	{
		// (an empty playlist's: Add files)
		bool emptyPl = isPlDir(browseDir) && plLoaded && plStore && plCur.entries.empty();
		return page == Page::Browse && !inMenu && selMode == SEL_NONE &&
			((browseDir == NETROOT) || (browseDir == PLROOT && plStore) || emptyPl) && browseRows.size() == 1 && browseRows[0].id < 0;
	}

	void Ui::doEmptyAction(Player& p)
	{
		if (browseDir == PLROOT) plNameOpen(false, "", "");
		else if (isPlDir(browseDir))
		{
			// Add files: the cards (where the Local tab was), the files ticked there added to this one
			selStart(SEL_ADD, plCur.file);
			switchTab(TAB_LOCAL, p);
		}
		else openForm(nullptr, nullptr, p);
	}

	bool Ui::atFirstPage() const
	{
		return browseDir.empty() || browseDir == NETROOT || browseDir == PLROOT;
	}

	void Ui::switchTab(int tab, Player& p)
	{
		if (tab == browseTab) return;
		tabDir[browseTab] = browseDir;
		browseTab = tab;
		std::string dir = tabDir[tab];
		if (tab == TAB_NETWORK && dir.empty()) dir = NETROOT;
		if (tab == TAB_PLAYLISTS && dir.empty()) dir = PLROOT;
		enterFolder(dir, p);
	}

	void Ui::openMainMenu(Player& p, int tab)
	{
		if (!inMenu) menuReturnDir = browseDir;
		inMenu = true;
		showTab(tab, p);
	}

	void Ui::showTab(int tab, Player& p)
	{
		menuTab = tab;
		styleFromSubs = false;
		cursor = scroll = 0;
		switch (tab)
		{
		case TAB_RECENT:
			if (roots.empty()) roots = browseRoots();	// started with a file: the browser never opened
			page = Page::Browse;
			enterFolder(RECENT, p);
			// the playing one is first: the cursor on the one after, what's likely wanted next
			if (p.playing() && browsePaths.size() > 1 && browsePaths[0] == p.currentPath()) cursor = 1;
			break;
		case TAB_SETTINGS: page = Page::Settings; setGroup = -1; break;
		case TAB_ABOUT: openAbout(p); break;
		default: page = Page::Exit; break;
		}
	}

	void Ui::closeMenu(Player& p)
	{
		inMenu = false;
		if (p.playing()) { page = Page::Main; return; }
		// the browser, back in its folder (and on its row)
		page = Page::Browse;
		enterFolder(menuReturnDir, p);
	}

	bool Ui::folderRow(int row) const
	{
		// a folder of the cards' (not the first page's places, nor Recent)
		return !browseDir.empty() && !pseudoDir(browseDir) && !isNetPath(browseDir) && row >= 0 && row < (int)browseRows.size() && browseRows[row].id == 1;
	}

	std::vector<std::string> Ui::browseLabels() const
	{
		std::vector<std::string> out;
		for (size_t i = 0; i < browseRows.size(); i++)
		{
			std::string l = std::string((int)i >= scroll && (int)i < scroll + ROWS_SHOWN ? "shown  " : "hidden ") +
				(i == (size_t)cursor ? "> " : "  ") + browseRows[i].label;
			// a playlist's and its items' notes; an item that's missing is "[Gone] name", with none (checked here for
			// all of them)
			if (browseRows[i].id == PL_ROW || browseRows[i].id == PL_ITEM)
			{
				if (plMissingAt((int)i))
				{
					size_t at = l.size() - browseRows[i].label.size();
					l.insert(at, "[Gone] ");
					l += "  |";
				}
				else l += "  | " + browseRows[i].note;
			}
			// selecting: a ticked row (Edit's item, Add's file)
			if (selTicked((int)i)) l.insert(9, "[x] ");	// after "shown  > "
			out.push_back(l);
		}
		return out;
	}

	bool Ui::holdRow(int row) const
	{
		if (folderRow(row)) return true;
		if (row < 0 || row >= (int)browseRows.size()) return false;
		int64_t id = browseRows[row].id;
		if (id == PL_ROW || id == PL_ITEM) return true;
		// a file: selecting (it ticked), to add to a playlist; a server's folder: its menu (Add to playlist...)
		if (!plStore) return false;
		return (id == 2 && !isPlDir(browseDir) && !isPhotoFile(browsePaths[row])) || (id == 1 && isNetPath(browseDir));
	}

	void Ui::openBrowseMenu(int row, MenuHow how)
	{
		if (browseDir == FORMDIR) return;
		bool onRow = row >= 0 && row < (int)browseRows.size();
		if (browseDir == PLROOT || isPlDir(browseDir))
		{
			// Playlists: the tab's menu (X), a playlist's (Y or a hold on it, or X in it), an item's (Y or a hold)
			std::vector<int> items;
			std::string title, note, file;
			int item = -1;
			auto count = [](size_t n) { return std::to_string(n) + (n == 1 ? " item" : " items"); };
			if (browseDir == PLROOT && how == MENU_X)
			{
				if (!plStore) return;
				items = {CTX_PL_NEW, CTX_CANCEL};
				title = "Your playlists";
			}
			else if (browseDir == PLROOT)
			{
				if (!onRow || browseRows[row].id != PL_ROW) return;
				file = browsePaths[row];
				title = browseRows[row].label;
				note = browseRows[row].note;
				items = {CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_PL_RENAME, CTX_PL_DELETE, CTX_CANCEL};
			}
			else if (how == MENU_X)
			{
				if (!plLoaded) return;
				file = plCur.file;
				title = plCur.name;
				note = count(plCur.entries.size());
				// eight with items (the box steps its rows closer to fit, the count left to the page), four without
				if (!plCur.entries.empty())
					items = {CTX_PL_PLAY, CTX_PL_SHUFFLE, CTX_PL_ADD_FILES, CTX_PL_ADD_FOLDER, CTX_PL_SELECT, CTX_PL_DEDUP};
				else items = {CTX_PL_ADD_FILES, CTX_PL_ADD_FOLDER};
				items.push_back(CTX_PL_RENAME);
				items.push_back(CTX_PL_DELETE);
			}
			else
			{
				if (!onRow || browseRows[row].id != PL_ITEM) return;
				file = plCur.file;
				item = row;
				title = browseRows[row].label;
				int miss = plMissingAt(row);
				if (miss)
				{
					note = miss == 1 ? "Not found. Is the card in?" : "Its connection was deleted";
					items = {CTX_PL_REMOVE, CTX_CANCEL};
				}
				else items = {CTX_PL_HERE, CTX_PL_MOVE, CTX_PL_REMOVE, CTX_PL_SHOW, CTX_CANCEL};
			}
			ctxOpen = true;
			ctxCursor = 0;
			ctxItems = items;
			ctxServer.clear();
			ctxStatus.clear();
			ctxArmed = false;
			ctxPath.clear();
			ctxTitle = title;
			ctxNote = note;
			ctxPlFile = file;
			ctxPlItem = item;
			return;
		}
		ctxTitle.clear();
		ctxNote.clear();
		ctxPlFile.clear();
		ctxPlItem = -1;
		ctxFolder.clear();
		bool folder = folderRow(row);
		bool server = browseDir == NETROOT && row >= 0 && row < (int)browseRows.size() && browseRows[row].id == 4;
		bool netFolder = isNetPath(browseDir) && onRow && browseRows[row].id == 1;
		// X on the cards' pages and a server's folders: Select files (to add to a playlist)
		bool selectFiles = plStore && how == MENU_X && browseDir != NETROOT && browseDir != FINDNET && browseTab != TAB_PLAYLISTS;
		std::vector<int> items;
		std::string id;
		if (server && how != MENU_X)
		{
			// the connection's own: Edit, Test, Delete
			NetPath np;
			if (!parseNetPath(browsePaths[row], np)) return;
			id = np.server;
			items = {CTX_EDIT, CTX_TEST, CTX_DELETE, CTX_CANCEL};
		}
		else if (browseDir == NETROOT && how == MENU_X) items = {CTX_ADD, CTX_FIND, CTX_REFRESH, CTX_CANCEL};
		else if (folder) items = {CTX_HIDE, CTX_REFRESH, CTX_CANCEL};
		else if (netFolder && how != MENU_X && plStore)
		{
			// a server's folder: its videos and songs added to a playlist
			items = {CTX_ADD_TO, CTX_CANCEL};
			ctxTitle = browseRows[row].label;
		}
		else if (how == MENU_X) items = {CTX_REFRESH, CTX_CANCEL};
		else return;	// Y, or a hold, on what has no menu of its own
		// a card's folder (Y, a hold): Add to playlist... first
		if (folder && how != MENU_X && plStore) items.insert(items.begin(), CTX_ADD_TO);
		if (folder || netFolder) ctxFolder = browsePaths[row];
		if (selectFiles) items.insert(items.begin(), CTX_SELECT_FILES);
		ctxOpen = true;
		ctxCursor = 0;
		ctxItems = items;
		ctxServer = id;
		ctxStatus.clear();
		ctxArmed = false;
		ctxPath = folder && id.empty() ? browsePaths[row] : "";
	}

	void Ui::chooseBrowseMenu(int item, Player& p)
	{
		int what = item >= 0 && item < (int)ctxItems.size() ? ctxItems[item] : CTX_CANCEL;
		if (what == CTX_PL_DELETE)
		{
			// twice (the files themselves stay where they are: the menu says so)
			if (!ctxArmed) { ctxArmed = true; return; }
			ctxOpen = false;
			plDelete(ctxPlFile, p);
			return;
		}
		if (what >= CTX_PL_NEW)
		{
			ctxOpen = false;
			std::string file = ctxPlFile;
			int at = ctxPlItem;
			// the playlist the menu is for: the one open, or one of the list's (read now)
			Playlist pl;
			int skipped = 0;
			bool open = isPlDir(browseDir) && plLoaded && file == plCur.file;
			if (open) pl = plCur;
			else if (!file.empty() && (!plStore || !plStore->load(file, pl, skipped))) { p.osd("Couldn't read the playlist"); return; }
			switch (what)
			{
			case CTX_PL_NEW: plNameOpen(false, "", ""); break;
			case CTX_PL_RENAME: plNameOpen(true, file, pl.name); break;
			case CTX_PL_PLAY: case CTX_PL_SHUFFLE:
				if (pl.entries.empty()) p.osd(quoted(pl.name) + " is empty");
				else plPlay(pl, 0, what == CTX_PL_SHUFFLE, p);
				break;
			case CTX_PL_HERE: if (open && at >= 0 && at < (int)pl.entries.size()) plPlay(pl, at, false, p); break;
			case CTX_PL_REMOVE:
				if (open && at >= 0 && at < (int)plCur.entries.size())
				{
					auto remap = removeEntries(plCur.entries, {at});
					plSaved(p, remap);
				}
				break;
			case CTX_PL_DEDUP:
				if (open)
				{
					int removed = 0;
					auto remap = removeDuplicates(plCur.entries, removed);
					if (!removed) { p.osd("No duplicates"); break; }
					plSaved(p, remap);
					p.osd("Removed " + std::to_string(removed) + (removed == 1 ? " duplicate" : " duplicates"));
				}
				break;
			case CTX_PL_SHOW:
				if (open && at >= 0 && at < (int)pl.entries.size())
				{
					// its folder, in the Local or the Network tab, the cursor on it
					std::string path = pl.entries[at].path;
					tabDir[browseTab] = browseDir;
					if (isNetPath(path))
					{
						browseTab = TAB_NETWORK;
						netSelect = path;
						enterFolder(netParent(path), p);
						break;
					}
					browseTab = TAB_LOCAL;
					enterFolder(parentFolder(path), p);
					for (size_t i = 0; i < browsePaths.size(); i++)
						if (browsePaths[i] == path) cursor = (int)i;
					fitList();
				}
				break;
			case CTX_PL_ADD_FILES: case CTX_PL_ADD_FOLDER:
				// to the cards (where the Local tab was), ticking files or picking a folder for this one
				if (open)
				{
					selStart(what == CTX_PL_ADD_FILES ? SEL_ADD : SEL_PICK, file);
					switchTab(TAB_LOCAL, p);
				}
				break;
			case CTX_PL_SELECT: if (open) selStart(SEL_EDIT, ""); break;
			case CTX_PL_MOVE:
				// the one item, moved at once (and back to the list as it was, after)
				if (open && at >= 0 && at < (int)plCur.entries.size())
				{
					selStart(SEL_EDIT, "");
					plSel = {at};
					moveFromMenu = true;
					moveStart(p);
				}
				break;
			case CTX_SELECT_FILES: selStart(SEL_ADD, ""); break;
			case CTX_ADD_TO: if (!ctxFolder.empty()) openPicker(ctxFolder, p); break;
			default: break;
			}
			return;
		}
		const NetServer* sv = netStore && !ctxServer.empty() ? netStore->find(ctxServer) : nullptr;
		if (what == CTX_TEST && sv)
		{
			// the menu stays up, the answer under its title
			startTest(*sv, netStore->password(*sv));
			ctxStatus = "Testing...";
			return;
		}
		if (what == CTX_DELETE && sv)
		{
			// twice: a saved password isn't typed again in a moment
			if (!ctxArmed) { ctxArmed = true; return; }
			std::string id = sv->id;
			netStore->remove(id);
			if (netWorker) netWorker->forget(id);
			ctxOpen = false;
			enterFolder(NETROOT, p);
			return;
		}
		ctxOpen = false;
		if (what == CTX_EDIT && sv) { openForm(sv, nullptr, p); return; }
		if (what == CTX_ADD) { openForm(nullptr, nullptr, p); return; }
		if (what == CTX_FIND) { enterFolder(FINDNET, p); return; }
		if (what == CTX_REFRESH)
		{
			// read again where it stands, the cursor kept (on the same file, if it's still there)
			std::string at = cursor >= 0 && cursor < (int)browsePaths.size() ? browsePaths[cursor] : "";
			int c = cursor, s = scroll;
			if (isNetPath(browseDir)) netSelect = at;	// (the listing comes later: netListed puts it there)
			enterFolder(browseDir, p);
			cursor = c;
			scroll = s;
			for (size_t i = 0; i < browsePaths.size(); i++)
				if (!at.empty() && browsePaths[i] == at) cursor = (int)i;
			fitList();
			return;
		}
		if (what != CTX_HIDE) return;	// Cancel
		std::string marker = ctxPath + "/.hidden";
		bool ok;
		if (isHiddenFolder(ctxPath)) ok = unlink(marker.c_str()) == 0;
		else
		{
			FILE* f = fopen(marker.c_str(), "w");
			ok = f && fclose(f) == 0;
		}
		if (!ok) p.osd("Couldn't change the folder (is the card read-only?)");
		int c = cursor, s = scroll;
		enterFolder(browseDir, p);	// again: it's gone from the list, or marked
		cursor = c;
		scroll = s;
		fitList();
	}

	void Ui::browseUp(Player& p)
	{
		if (atFirstPage()) return;
		if (isPlDir(browseDir))
		{
			// back to the playlists, on this one
			std::string file = plFileOf(browseDir);
			enterFolder(PLROOT, p);
			for (size_t i = 0; i < browsePaths.size(); i++)
				if (browsePaths[i] == file) cursor = (int)i;
			fitList();
			return;
		}
		// the form, the found servers: back to the connections (a form left unsaved)
		if (browseDir == FORMDIR || browseDir == FINDNET)
		{
			enterFolder(NETROOT, p);
			return;
		}
		if (isNetPath(browseDir))
		{
			// up a folder; from a server's top, its list of connections
			NetPath np;
			parseNetPath(browseDir, np);
			enterFolder(np.path == "/" ? std::string(NETROOT) : netParent(browseDir), p);
			return;
		}
		bool atRoot = browseDir == RECENT;
		for (auto& r : roots)
			if (r.path == browseDir) atRoot = true;
		enterFolder(atRoot ? "" : parentFolder(browseDir), p);
	}

	bool Ui::clearHistory(Player& p)
	{
		if (clearArmed && SDL_GetTicks() - clearArmed < QUIT_WINDOW_MS)
		{
			clearArmed = 0;
			p.clearHistory();
			p.osd("Play history cleared");
			return true;
		}
		clearArmed = SDL_GetTicks();
		return false;
	}

	std::vector<int> Ui::settingRows() const
	{
		const int T = TOGGLE_ROW;
		switch (setGroup)
		{
		case GROUP_SUBS:
		{
			std::vector<int> out;
			for (int k = 0; k < STYLE_KEYS; k++) out.push_back(STYLE_ROW0 + k);
			return out;
		}
		case GROUP_MUSIC: return {MUSIC_ROW, VIS_ROW, SOUND_ROW};
		case GROUP_PHOTOS: return {T + 8, PAN_ROW, DRAG_ROW};
		case GROUP_REMEMBER: return {T + 0, T + 1, T + 2};
		case GROUP_FILES: return {T + 3, T + 6, T + 4, T + 11, CLEAR_ROW};
		case GROUP_CONTROLS:
		{
			// the brightness rows where there are backlights (the handheld)
			std::vector<int> out;
			// one row for both panels (Same for both screens), or one each
			bool both = backlight.present(Backlight::TOP) && backlight.present(Backlight::BOTTOM);
			if (backlight.present(Backlight::TOP)) out.push_back(TOP_LIGHT_ROW);
			if (backlight.present(Backlight::BOTTOM) && !(both && sameBrightness)) out.push_back(BOTTOM_LIGHT_ROW);
			if (both) out.push_back(T + 10);
			out.push_back(T + 5);
			out.push_back(T + 7);
			return out;
		}
		case GROUP_UPDATES:
		{
			// a Store install (or a computer's build): the status alone
			if (!updater || updater->state() == Updater::State::Off) return {CHECK_ROW};
			Updater::State st = updater->state();
			bool install = st == Updater::State::Available || st == Updater::State::Downloading || st == Updater::State::Ready ||
				(st == Updater::State::Failed && !updater->available().empty());
			if (install) return {T + 9, CHECK_ROW, INSTALL_ROW};
			return {T + 9, CHECK_ROW};
		}
		default: return {};
		}
	}

	std::string Ui::settingHelp(int row) const
	{
		const int T = TOGGLE_ROW;
		if (row >= STYLE_ROW0 && row < STYLE_ROW0 + STYLE_KEYS) return "How plain subtitles (SRT, VTT...) look. Styled ones (ASS) keep theirs.";
		if (row == MUSIC_ROW) return "What the top screen shows while a song plays (SELECT while playing).";
		if (row == VIS_ROW) return "How the music is drawn: Bars, Waves, Spectrum or Circle (SELECT while playing).";
		if (row == SOUND_ROW) return "Equalizer and effects for music (not videos). A opens it";
		if (row == PAN_ROW) return "Which side the pan area (the small photo) is on.";
		if (row == DRAG_ROW) return "The box: a drag moves the frame over the small photo. The photo: a drag moves the photo, as on a phone.";
		if (row == CLEAR_ROW) return "Empties Recent. Press A twice. The places you stopped at are kept.";
		if (row == TOP_LIGHT_ROW || row == BOTTOM_LIGHT_ROW) return "The backlight, 10% to 100%: the same levels as BananaOS's own settings.";
		if (row == CHECK_ROW) return "Looks for a new version now.";
		if (row == INSTALL_ROW) return "Downloads the new version, then Restart now.";
		switch (row - T)
		{
		case 0: return "A video opens where you stopped. Songs always start at the beginning.";
		case 1: return "Keeps each file's audio track. A new file starts in the language you chose last.";
		case 2: return "Keeps each file's subtitles. A new file starts in the language you chose last.";
		case 3: return "The file list opens where you last were.";
		case 4: return "Keeps Recent. Turning it off also deletes the history.";
		case 5: return "Off: the buttons can't pause, skip or close while something plays (touch still can).";
		case 6: return "Shows the folders you've hidden (X on a folder), so you can unhide them.";
		case 7: return "Shows the battery's percent beside its icon.";
		case 8: return "The next photo opens at the same zoom and place. Off: each opens whole.";
		case 9: return "Looks for a new version a few seconds after the start (Wi-Fi needed).";
		case 10: return "One brightness for both screens. Off: each its own.";
		case 11: return "Files played from a network folder go in Recent too. Off: they're left out, and the ones there now go.";
		default: return "";
		}
	}

	void Ui::stepGroup(int dir)
	{
		setGroup = (setGroup + dir + GROUPS) % GROUPS;
		cursor = scroll = 0;
		styleFromSubs = false;
	}

	void Ui::openGroup(int g)
	{
		page = Page::Settings;
		setGroup = g;
		cursor = scroll = 0;
	}

	static int rowsIndexOf(const std::vector<int>& rows, int row)
	{
		for (size_t i = 0; i < rows.size(); i++)
			if (rows[i] == row) return (int)i;
		return -1;
	}

	std::vector<int> Ui::quickRows() const
	{
		std::vector<int> r;
		if (volume.present()) r.push_back(Q_VOLUME);
		bool top = backlight.present(Backlight::TOP), bottom = backlight.present(Backlight::BOTTOM);
		if (top || bottom) r.push_back(Q_BRIGHT);
		if (top && bottom && !sameBrightness) r.push_back(Q_BOTTOM);
		if (top && bottom) r.push_back(Q_SAME);
		r.push_back(Q_QUIT);
		return r;
	}

	void Ui::toggleQuick(Player& p)
	{
		quickOpen = !quickOpen;
		if (!quickOpen) return;
		if (!panel.on()) panel.set(true);	// the bottom panel back, to see them
		sameBrightness = p.settings.sameBrightness;
		volumePct = volume.get();
		quickCursor = 0;
		quickDrag = false;
		pressedBtn = -1;
	}

	void Ui::quickAdjust(int row, int dir, Player& p)
	{
		if (row == Q_VOLUME && volumePct >= 0)
		{
			// bananad's keys' step
			volumePct = std::clamp((volumePct + (dir > 0 ? 5 : -1)) / 5 * 5, 0, 100);	// to the next five either way
			volume.set(volumePct);
		}
		else if (row == Q_BRIGHT || row == Q_BOTTOM)
		{
			Backlight::Panel panelOf = row == Q_BOTTOM ? Backlight::BOTTOM : Backlight::TOP;
			int pm = backlight.permille(panelOf);
			if (pm >= 0) setBrightness(panelOf, stepBrightness(pm, dir), p);
		}
	}

	void Ui::quickSetAt(int row, int x, Player& p)
	{
		Rect bar = quickBar(row);
		double f = std::clamp((x - bar.x) / (double)bar.w, 0.0, 1.0);
		if (rowsIndexOf(quickRows(), Q_VOLUME) == row && volumePct >= 0)
		{
			int v = (int)std::lround(f * 20) * 5;
			if (v != volumePct)
			{
				volumePct = v;
				volume.set(v);
			}
			return;
		}
		int which = quickRows()[row];
		Backlight::Panel panelOf = which == Q_BOTTOM ? Backlight::BOTTOM : Backlight::TOP;
		int pm = std::clamp((int)std::lround(f * 10) * 100, 100, 1000);
		if (pm != backlight.permille(panelOf)) setBrightness(panelOf, pm, p);
	}

	void Ui::quickChoose(int row, Player& p)
	{
		if (row == Q_SAME)
		{
			Settings s = p.settings;
			s.sameBrightness = !s.sameBrightness;
			p.setSettings(s);
			sameBrightness = s.sameBrightness;
			// one level for both: the bottom to the top's
			int pm = backlight.permille(Backlight::TOP);
			if (sameBrightness && pm >= 100) backlight.set(Backlight::BOTTOM, pm);
		}
		else if (row == Q_QUIT) quitRequested = true;
	}

	bool Ui::quickKey(Button b, Player& p)
	{
		auto rows = quickRows();
		int n = (int)rows.size();
		quickCursor = std::clamp(quickCursor, 0, n - 1);
		int row = rows[quickCursor];
		switch (b)
		{
		case Button::Up: quickCursor = (quickCursor + n - 1) % n; break;
		case Button::Down: quickCursor = (quickCursor + 1) % n; break;
		case Button::Left: case Button::Right: quickAdjust(row, b == Button::Right ? 1 : -1, p); break;
		case Button::A: quickChoose(row, p); break;
		case Button::B: case Button::Start: quickOpen = false; break;
		default: break;
		}
		return false;
	}

	bool Ui::quickPointer(const Pointer& pt, int x, int y, Player& p)
	{
		auto rows = quickRows();
		auto barRow = [&](int i) { return rows[i] == Q_VOLUME || rows[i] == Q_BRIGHT || rows[i] == Q_BOTTOM; };
		if (pt.kind == Pointer::Down)
		{
			pressedBtn = -1;
			// outside: closed (the finger lifting presses nothing)
			if (!quickPanel().hit(x, y))
			{
				quickOpen = false;
				swallowTouch = true;
				return false;
			}
			for (int i = 0; i < (int)rows.size(); i++)
			{
				if (!quickRowRect(i).hit(x, y)) continue;
				quickCursor = i;
				if (barRow(i) && quickMinus(i).hit(x, y)) pressedBtn = 300 + i * 4;
				else if (barRow(i) && quickPlus(i).hit(x, y)) pressedBtn = 301 + i * 4;
				else if (barRow(i) && Rect{quickBar(i).x - 6, quickRowRect(i).y, quickBar(i).w + 12, 54}.hit(x, y))
				{
					// the bar, as the progress bar: at once, and while the finger drags
					quickDrag = true;
					quickDragRow = i;
					quickSetAt(i, x, p);
				}
				else if (!barRow(i)) pressedBtn = 302 + i * 4;
			}
			return false;
		}
		if (quickDrag)
		{
			quickSetAt(quickDragRow, x, p);
			if (pt.kind == Pointer::Up) quickDrag = false;
			return false;
		}
		if (pt.kind == Pointer::Up && pressedBtn >= 300)
		{
			int i = (pressedBtn - 300) / 4, what = (pressedBtn - 300) % 4;
			pressedBtn = -1;
			if (i >= (int)rows.size() || !quickRowRect(i).hit(x, y)) return false;
			if (what == 0) quickAdjust(rows[i], -1, p);
			else if (what == 1) quickAdjust(rows[i], 1, p);
			else quickChoose(rows[i], p);
		}
		return false;
	}

	void Ui::drawQuick(const Player& p)
	{
		Rect pr = quickPanel();
		drawDialog(pr, "");
		drawTextMid("Quick settings", 1, YELLOW, pr.x + 24, pr.y + 28);
		auto rows = quickRows();
		for (int i = 0; i < (int)rows.size(); i++)
		{
			Rect r = quickRowRect(i);
			bool at = i == quickCursor;
			int row = rows[i], mid = r.y + r.h / 2;
			if (row == Q_QUIT)
			{
				fill(r, pressedBtn == 302 + i * 4 ? PRESSED : RED);
				int tw = (int)std::lround(textWidth("Quit Banana Player", 1) / sx), gw = glyphW(G_POWER), x = r.x + r.w / 2 - (tw + gw + 12) / 2;
				drawGlyphMid(G_POWER, x + gw / 2, mid, CREAM);
				drawTextMid("Quit Banana Player", 1, CREAM, x + gw + 12, mid);
				if (at)
				{
					fill({r.x, r.y, r.w, 3}, YELLOW);
					fill({r.x, r.y + r.h - 3, r.w, 3}, YELLOW);
					fill({r.x, r.y, 3, r.h}, YELLOW);
					fill({r.x + r.w - 3, r.y, 3, r.h}, YELLOW);
				}
				continue;
			}
			if (at) drawFocus(r);
			if (row == Q_SAME)
			{
				// a tick box
				Rect box{r.x + 16, mid - 13, 26, 26};
				fill(box, at ? YELLOW : CREAM);
				fill({box.x + 2, box.y + 2, box.w - 4, box.h - 4}, at ? TEAL : SURFACE);
				if (p.settings.sameBrightness) drawGlyphMid(G_CHECK, box.x + box.w / 2, box.y + box.h / 2, YELLOW);
				drawTextMid("Same brightness for both screens", 1, at ? YELLOW : CREAM, r.x + 56, mid, r.w - 66);
				continue;
			}
			int pct;
			std::string label;
			if (row == Q_VOLUME)
			{
				pct = volumePct;
				label = "Volume";
			}
			else
			{
				int pm = backlight.permille(row == Q_BOTTOM ? Backlight::BOTTOM : Backlight::TOP);
				pct = pm < 0 ? -1 : (pm + 5) / 10;
				label = row == Q_BOTTOM ? "Bottom" : p.settings.sameBrightness ? "Brightness" : "Top";
			}
			SDL_Color ink = at ? YELLOW : CREAM;
			drawGlyphMid(row == Q_VOLUME ? G_SPEAKER : G_SUN, r.x + 25, mid, ink);
			drawTextMid(label, 1, ink, r.x + 46, mid, 146);
			drawTextMid(pct < 0 ? "-" : std::to_string(pct) + "%", 0, at ? DIM_TEAL : DIM, quickMinus(i).x - 6, mid, 0, 1);
			for (int k = 0; k < 2; k++)
			{
				Rect bt = k ? quickPlus(i) : quickMinus(i);
				fill(bt, pressedBtn == 300 + i * 4 + k ? PRESSED : at ? BG : TEAL);
				drawGlyphMid(k ? G_PLUS : G_MINUS, bt.x + bt.w / 2, bt.y + bt.h / 2, CREAM);
			}
			Rect bar = quickBar(i);
			fill(bar, at ? BG : TEAL);
			if (pct > 0) fill({bar.x, bar.y, bar.w * std::min(pct, 100) / 100, bar.h}, YELLOW);
		}
		if (p.settings.buttonHotkeys) drawHints({{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"F/B", "Close"}});
		else drawFooterText("Tap outside to close", DIM);
	}

	void Ui::setBrightness(Backlight::Panel panel, int pm, Player& p)
	{
		// Same for both screens: the other panel to the same level
		if (p.settings.sameBrightness && backlight.present(Backlight::TOP) && backlight.present(Backlight::BOTTOM))
		{
			backlight.set(Backlight::TOP, pm);
			backlight.set(Backlight::BOTTOM, pm);
		}
		else backlight.set(panel, pm);
	}

	void Ui::changeSetting(int row, int dir, Player& p)
	{
		if (row == CLEAR_ROW) { clearHistory(p); return; }
		if (row == CHECK_ROW) { if (updater) updater->check(); return; }
		if (row == TOP_LIGHT_ROW || row == BOTTOM_LIGHT_ROW)
		{
			Backlight::Panel panel = row == TOP_LIGHT_ROW ? Backlight::TOP : Backlight::BOTTOM;
			int pm = backlight.permille(panel);
			if (pm >= 0) setBrightness(panel, stepBrightness(pm, dir), p);
			return;
		}
		if (row == INSTALL_ROW)
		{
			if (!updater) return;
			if (updater->state() == Updater::State::Ready) restartRequested = true;
			else updater->download();
			return;
		}
		Settings s = p.settings;
		if (row - STYLE_ROW0 < STYLE_KEYS) stepStyle(s.style, (StyleKey)(row - STYLE_ROW0), dir);
		else if (row == MUSIC_ROW) s.musicShow = ((s.musicShow + dir) % SHOWS + SHOWS) % SHOWS;
		else if (row == VIS_ROW) s.visualizer = ((s.visualizer + dir) % VISUALIZERS + VISUALIZERS) % VISUALIZERS;
		else if (row == PAN_ROW) s.panRight = !s.panRight;
		else if (row == DRAG_ROW) s.dragBox = !s.dragBox;
		else s.*REMEMBER[row - TOGGLE_ROW] = !(s.*REMEMBER[row - TOGGLE_ROW]);
		p.setSettings(s);
	}

	bool Ui::repeats(Button b, const Player& p) const
	{
		// a photo: on to the next, and zooming, while held
		if (page == Page::Main && !inMenu && p.playingPhoto())
			return b == Button::Left || b == Button::Right || b == Button::L || b == Button::R || b == Button::Up || b == Button::Down;
		if (b == Button::L2 || b == Button::R2) return false;
		if (displayShown(p)) return false;	// a held key would spin the chips round
		if (b == Button::Left || b == Button::Right) return page != Page::Tracks && page != Page::Chapters;
		if (b == Button::L || b == Button::R) return false;	// (the browser's tabs)
		return page != Page::Main && (b == Button::Up || b == Button::Down);
	}

	// a touch button: what its button does, whether the buttons themselves are on or not
	bool Ui::pressAnyway(Button b, Player& p)
	{
		bool keep = p.settings.buttonHotkeys;
		p.settings.buttonHotkeys = true;
		bool r = press(b, p);
		p.settings.buttonHotkeys = keep;
		return r;
	}

	bool Ui::musicKey(Button b, Player& p)
	{
		if (!p.playingAudio()) return false;
		// Y: a song with chapters, its chapters (as a video's); one without, nothing (its slot is Sound's, by touch)
		if (b == Button::Y) return p.chapters.empty();
		if (b == Button::Select)
		{
			displayOpen = true;
			displayRow = 0;
			return true;
		}
		if (b != Button::X) return false;
		Settings s = p.settings;
		s.loop = (s.loop + 1) % LOOPS;
		p.setSettings(s);
		p.osd(loopLabel(s.loop));
		return true;
	}

	bool Ui::press(Button b, Player& p)
	{
		sameBrightness = p.settings.sameBrightness;
		if (quickOpen) return quickKey(b, p);
		if (kbOpen)
		{
			kbPress(b);
			if (eqNameSubmit) eqNameDone(p);
			if (plNameSubmit) plNameDone(p);
			return false;
		}
		displayOpen = displayOpen && displayShown(p);	// (the song gone under it: closed)
		if (displayOpen) return displayKey(b, p);
		// Button hotkeys off: the buttons do nothing over a playing video (touch does it all); the pages opened from
		// there by touch still take them
		if (page == Page::Main && !p.settings.buttonHotkeys) return false;
		// L2/R2: the chapter before or after, over what plays and its two pages (the Chapters page's mark follows).
		// Not in START's menu, not over a photo (its keys are its own), and nothing with Button hotkeys off: they
		// have no touch button to stand for
		if (b == Button::L2 || b == Button::R2)
		{
			if (p.settings.buttonHotkeys && !inMenu && p.playing() && !p.playingPhoto() && p.error.empty() &&
				(page == Page::Main || page == Page::Tracks || page == Page::Chapters))
				p.stepChapter(b == Button::L2 ? -1 : 1);
			return false;
		}
		// the buttons play on with the panel dark; the ones that open a page bring it back
		if (!panel.on() && (b == Button::X || b == Button::Y || b == Button::Start)) panel.set(true);
		if (b != Button::B) quitArmed = 0;
		if (inMenu)
		{
			if (b == Button::L || b == Button::R) { showTab((menuTab + (b == Button::R ? 1 : TABS - 1)) % TABS, p); return false; }
			// B in the subtitle style opened from the Subtitles list: back to that list
			if (b == Button::B && page == Page::Settings && setGroup == GROUP_SUBS && styleFromSubs)
			{
				styleFromSubs = false;
				inMenu = false;
				openTracks(TRACKS_SUBS, p);
				cursor = (int)rows(p).size() - 1;	// on Subtitle style, where it was
				scroll = std::max(0, cursor - ROWS_SHOWN + 1);
				return false;
			}
			// B in a settings group: back to the groups
			if (b == Button::B && page == Page::Settings && setGroup >= 0)
			{
				cursor = setGroup;
				setGroup = -1;
				return false;
			}
			if (b == Button::Start || b == Button::B) { closeMenu(p); return false; }
			if (page == Page::Exit)
			{
				if (b == Button::A) quitRequested = true;
				return false;
			}
			// the tab's page takes the rest (Recent's list, the settings' rows, About's scrolling)
		}
		if (page == Page::Sound) return soundKey(b, p);
		if (page == Page::Settings)
		{
			auto rows = settingRows();
			int n = setGroup < 0 ? GROUPS : (int)rows.size();
			cursor = std::min(cursor, n - 1);	// a group's rows can change (the updater's)
			// in a group: Y (the west button) to the one before, X to the one after (L/R are the menu's tabs)
			if (setGroup >= 0 && (b == Button::X || b == Button::Y))
			{
				stepGroup(b == Button::X ? 1 : -1);
				return false;
			}
			// the groups: tiles two to a row, the cursor moved as on a grid
			if (setGroup < 0 && (b == Button::Up || b == Button::Down || b == Button::Left || b == Button::Right))
			{
				if (b == Button::Up && cursor >= 2) cursor -= 2;
				else if (b == Button::Down) cursor = cursor + 2 < n ? cursor + 2 : cursor / 2 < (n - 1) / 2 ? n - 1 : cursor;
				else if (b == Button::Left && cursor % 2 == 1) cursor--;
				else if (b == Button::Right && cursor % 2 == 0 && cursor + 1 < n) cursor++;
				return false;
			}
			switch (b)
			{
			case Button::Up: cursor = (cursor + n - 1) % n; break;
			case Button::Down: cursor = (cursor + 1) % n; break;
			case Button::Left: if (setGroup >= 0 && rows[cursor] != SOUND_ROW) changeSetting(rows[cursor], -1, p); break;
			case Button::Right: case Button::A:
				if (setGroup < 0) openGroup(cursor);
				else if (rows[cursor] == SOUND_ROW) { openSound(p, true); return false; }
				else changeSetting(rows[cursor], 1, p);
				break;
			case Button::B:
				if (setGroup >= 0) { cursor = setGroup; setGroup = -1; }
				else page = home(p);
				break;
			case Button::Start: page = home(p); break;
			default: break;
			}
			if (cursor < scroll) scroll = cursor;
			if (cursor >= scroll + SET_SHOWN) scroll = cursor - SET_SHOWN + 1;
			return false;
		}
		if (page == Page::Info || page == Page::About)
		{
			int maxScroll = std::max(0, (int)infoLines.size() - INFO_SHOWN);
			switch (b)
			{
			case Button::Up: scroll = std::max(0, scroll - 1); break;
			case Button::Down: scroll = std::min(maxScroll, scroll + 1); break;
			case Button::Left: scroll = std::max(0, scroll - INFO_SHOWN); break;
			case Button::Right: scroll = std::min(maxScroll, scroll + INFO_SHOWN); break;
			case Button::B: case Button::A:
				// About as the menu's tab: nothing to go back to (B closes the menu)
				if (page == Page::About && inMenu) break;
				if (page == Page::About) { page = Page::Settings; setGroup = -1; cursor = scroll = 0; }
				else page = home(p);
				break;
			default: break;
			}
			return false;
		}
		if (popupShown(p))
		{
			int n = (int)popupItems().size();
			popupCursor = std::min(popupCursor, n - 1);
			switch (b)
			{
			case Button::Up: popupCursor = (popupCursor + n - 1) % n; break;
			case Button::Down: popupCursor = (popupCursor + 1) % n; break;
			case Button::A: choosePopup(popupCursor, p); break;
			case Button::B: choosePopup(n > 1 ? 1 : 0, p); break;	// Later (or Hide, OK)
			default: break;
			}
			return false;
		}
		if (page == Page::Browse && ctxOpen)
		{
			switch (b)
			{
			case Button::Up: ctxCursor = (ctxCursor + (int)ctxItems.size() - 1) % (int)ctxItems.size(); ctxArmed = false; break;
			case Button::Down: ctxCursor = (ctxCursor + 1) % (int)ctxItems.size(); ctxArmed = false; break;
			case Button::A: chooseBrowseMenu(ctxCursor, p); break;
			case Button::B: case Button::X: case Button::Y: ctxOpen = false; break;
			default: break;
			}
			return false;
		}
		// a network folder read before it's added: B cancels, nothing else
		if (page == Page::Browse && addPending.serial)
		{
			if (b == Button::B) cancelAdd(p);
			return false;
		}
		if (page == Page::Browse && pickerOpen)
		{
			pickerKey(b, p);
			return false;
		}
		if (page == Page::Browse && !inMenu && selMode != SEL_NONE && selKey(b, p)) return false;
		if (page == Page::Browse && !inMenu && b == Button::X) { openBrowseMenu(cursor, MENU_X); return false; }
		if (page == Page::Browse && !inMenu && b == Button::Y) { openBrowseMenu(cursor, MENU_Y); return false; }
		// the form: LEFT/RIGHT step a choice (Type, Sign in, FTPS); L/R don't leave it for the other tab
		if (page == Page::Browse && !inMenu && browseDir == FORMDIR)
		{
			if (b == Button::L || b == Button::R) return false;
			if ((b == Button::Left || b == Button::Right) && cursor >= 0 && cursor < (int)browseRows.size())
			{
				formChange(browseRows[cursor].id, b == Button::Left ? -1 : 1);
				return false;
			}
		}
		if (page == Page::Browse && !inMenu && browseDir == FINDNET && (b == Button::L || b == Button::R)) return false;
		if (page == Page::Browse && !inMenu && (b == Button::L || b == Button::R))
		{
			// a tab each way, no wrap
			int t = browseTab + (b == Button::R ? 1 : -1);
			if (t >= 0 && t < BROWSE_TABS) switchTab(t, p);
			return false;
		}
		if (page == Page::Browse && b == Button::B)
		{
			// opened over a video: back to it from the first page
			if (p.playing() && atFirstPage()) { page = Page::Main; return false; }
			if (!atFirstPage()) { browseUp(p); return false; }
			// the first page: B twice leaves
			if (quitArmed && SDL_GetTicks() - quitArmed < QUIT_WINDOW_MS) return true;
			quitArmed = SDL_GetTicks();
			return false;
		}
		if (page == Page::Browse && b == Button::Start) { openMainMenu(p); return false; }
		if (page == Page::Tracks)
		{
			// the tabs: Y the left one (Audio), X the right one (Subtitles); L/R as well, as the browser's tabs go
			if (b == Button::Y || b == Button::L) { if (tracksTab != TRACKS_AUDIO) openTracks(TRACKS_AUDIO, p); return false; }
			if (b == Button::X || b == Button::R) { if (tracksTab != TRACKS_SUBS) openTracks(TRACKS_SUBS, p); return false; }
		}
		if (page == Page::Chapters && p.chapters.empty())
		{
			// (the list can also empty under it: the next file has none)
			if (b == Button::B) page = home(p);
			return false;
		}
		if (page != Page::Main)
		{
			int n = (int)rows(p).size();
			cursor = std::min(cursor, n - 1);
			switch (b)
			{
			case Button::Up: cursor = (cursor + n - 1) % n; break;
			case Button::Down: cursor = (cursor + 1) % n; break;
			// a page at a time (the browser): LEFT/RIGHT, and L/R as BananaOS's lists jump
			case Button::Left: cursor = std::max(0, cursor - ROWS_SHOWN); break;
			case Button::Right: cursor = std::min(n - 1, cursor + ROWS_SHOWN); break;
			case Button::A: choose(cursor, p); break;
			case Button::B: page = home(p); break;
			default: break;
			}
			if (cursor < scroll) scroll = cursor;
			if (cursor >= scroll + ROWS_SHOWN) scroll = cursor - ROWS_SHOWN + 1;
			return false;
		}
		if (p.playingPhoto() && photoKey(b, p)) return false;
		// a file that didn't open: nothing to pause, choose or speed up (Prev, Next, the menu and leaving still go)
		if (!p.error.empty() && (b == Button::A || b == Button::X || b == Button::Y || b == Button::Select || b == Button::Up || b == Button::Down))
			return false;
		switch (b)
		{
		case Button::A: p.togglePause(); break;
		case Button::B:
			if (quitArmed && SDL_GetTicks() - quitArmed < QUIT_WINDOW_MS) return true;
			quitArmed = SDL_GetTicks();
			// (Button hotkeys off: it came from the touch button, and B does nothing here)
			p.osd(std::string(p.settings.buttonHotkeys ? "Press B" : "Tap") + " again to " + (browserMode ? "close" : "quit"),
				QUIT_WINDOW_MS);
			break;
		case Button::X: if (!musicKey(b, p)) openTracks(tracksTab, p); break;
		// (a file with no chapters, or none known yet: the page says so)
		case Button::Y: if (!musicKey(b, p)) openChapters(p); break;
		case Button::Start: openMainMenu(p); break;
		case Button::Select:
			if (musicKey(b, p)) break;
			p.setAspect(nextAspect(p.aspect));
			p.osd(std::string("Aspect: ") + aspectName(p.aspect));
			break;
		case Button::Left: p.seekRelative(-BACK_S); break;
		case Button::Right: p.seekRelative(FORWARD_S); break;
		case Button::L: if (!p.prev()) p.osd("This is the first one"); break;
		case Button::R: if (!p.next()) p.osd("This is the last one"); break;
		case Button::Up: case Button::Down:
		{
			double s = stepSpeed(p.speed, b == Button::Up ? 1 : -1);
			p.setSpeed(s);
			p.osd("Speed " + speedLabel(s));
			break;
		}
		default: break;
		}
		return false;
	}

	bool Ui::photoKey(Button b, Player& p)
	{
		flickAt = 0;
		const Player::PhotoView& v = p.photo();
		switch (b)
		{
		case Button::A:
		{
			// Fit and 2x, as a double-tap: zoomed in, out; else in on the middle
			bool zoomed = v.zoom > 1.001;
			photoZoomTo(zoomed ? 1 : 2, v.panX, v.panY, p);
			return true;
		}
		case Button::Left: case Button::L: if (!p.prev()) p.osd("This is the first one"); return true;
		case Button::Right: case Button::R: if (!p.next()) p.osd("This is the last one"); return true;
		case Button::Up: case Button::Down: photoZoomTo(stepZoom(v.zoom, b == Button::Up ? 1 : -1), v.panX, v.panY, p); return true;
		case Button::L3: p.rotatePhoto(); p.osd("Turned " + std::to_string(p.photo().rotate) + "°", 800); return true;
		case Button::Select: p.togglePhotoFill(); p.osd(p.photo().fill ? "Fill" : "Fit", 800); return true;
		case Button::X: case Button::Y: return true;	// nothing for a photo
		default: return false;	// B and START: as over a video
		}
	}

	void Ui::photoZoomTo(double zoom, double panX, double panY, Player& p)
	{
		p.setPhotoView(zoom, panX, panY);
		p.osd("Zoom " + std::to_string((int)std::lround(p.photo().zoom * 100)) + "%", 800);
	}

	Ui::Rect Ui::thumbRect(const Player& p) const
	{
		Rect pan = panRect(p);
		double a = p.photoWidth() > 0 ? p.photoWidth() / (double)p.photoHeight() : 4.0 / 3;
		int w = pan.w, h = (int)std::lround(pan.w / a);
		if (h > pan.h)
		{
			h = pan.h;
			w = (int)std::lround(pan.h * a);
		}
		return {pan.x + (pan.w - w) / 2, pan.y + (pan.h - h) / 2, w, h};
	}

	bool Ui::photoPointer(const Pointer& pt, int x, int y, Player& p)
	{
		Uint32 now = SDL_GetTicks();
		PhotoScale sc = p.photoScaleNow();
		const Player::PhotoView& v = p.photo();
		int dx = x - panDownX, dy = y - panDownY;
		if (pt.kind == Pointer::Move)
		{
			if (std::abs(dx) > 8 || std::abs(dy) > 8) panMoved = true;
			if (!panMoved) return false;
			// Drag moves the photo: it goes with the finger, the pan area standing for the top panel. The box: it
			// goes with the finger over the small photo (its centre is minus the pan, in shares of the photo as
			// drawn here), the top panel showing what's in it
			Rect tr = thumbRect(p);
			if (p.settings.dragBox) p.setPhotoView(v.zoom, panStartX - dx / (double)tr.w, panStartY - dy / (double)tr.h);
			else p.setPhotoView(v.zoom, panStartX + dx / (double)PAN_W / sc.x, panStartY + dy / (double)PAN_H / sc.y);
			// its speed, smoothed over the last moves, for a flick
			Uint32 dt = std::max<Uint32>(1, now - lastAt);
			flickVX = 0.6 * (x - lastX) / (double)dt + 0.4 * flickVX;
			flickVY = 0.6 * (y - lastY) / (double)dt + 0.4 * flickVY;
			lastX = x;
			lastY = y;
			lastAt = now;
			return false;
		}
		if (pt.kind != Pointer::Up) return false;
		panTouch = false;
		if (panMoved)
		{
			// let go while moving: it glides on
			if (now - lastAt < 80 && std::hypot(flickVX, flickVY) > 0.3) flickAt = now;
			return false;
		}
		Rect tr = thumbRect(p);
		double u = std::clamp((x - tr.x) / (double)tr.w, 0.0, 1.0) - 0.5, w = std::clamp((y - tr.y) / (double)tr.h, 0.0, 1.0) - 0.5;
		if (tapAt && now - tapAt < 350 && std::abs(x - tapX) < 40 && std::abs(y - tapY) < 40)
		{
			// a double-tap: in to 2x on that spot, or (zoomed) back out
			tapAt = 0;
			bool zoomed = v.zoom > 1.001;
			photoZoomTo(zoomed ? 1 : 2, zoomed ? 0 : -u, zoomed ? 0 : -w, p);
			return false;
		}
		tapAt = now;
		tapX = x;
		tapY = y;
		// a tap on the thumbnail, zoomed in: the view there
		if ((sc.x > 1.001 || sc.y > 1.001) && tr.hit(x, y)) p.setPhotoView(v.zoom, -u, -w);
		return false;
	}

	void Ui::tick(Player& p)
	{
		// a playlist's edits, written once they've stopped for a second
		if (plDirty && SDL_GetTicks() - plDirtyAt >= 1000) plFlush(p);
		// a dragged row near the list's top or bottom: the list scrolls under it, a row each 150 ms
		if (plDrag.on && page == Page::Browse && isPlDir(browseDir) && SDL_GetTicks() - plDrag.scrollAt >= 150)
		{
			int n = (int)browseRows.size();
			int was = scroll;
			if (plDrag.y < ROW_Y + 24) scroll = std::max(0, scroll - 1);
			else if (plDrag.y > ROW_Y + ROWS_SHOWN * ROW_H - 24) scroll = std::min(std::max(0, n - ROWS_SHOWN), scroll + 1);
			if (scroll != was) plDrag.scrollAt = SDL_GetTicks();
		}
		// a playlist's network item: its folder listed for its subtitles and cover (once a play; netListed answers)
		if (std::string dir = p.netFolderWanted(); !dir.empty() && !sideAsked.count(dir))
		{
			sideAsked.insert(dir);
			NetPath np;
			const NetServer* sv = netStore && parseNetPath(dir, np) ? netStore->find(np.server) : nullptr;
			if (sv && netWorker)
			{
				sidePending[++sideSerial] = dir;
				netWorker->list(sideSerial, *sv, netStore->password(*sv), dir);
			}
			else p.netFolderListed(dir, {}, false);
		}
		if (!flickAt) return;
		if (!p.playingPhoto() || page != Page::Main || panTouch)
		{
			flickAt = 0;
			return;
		}
		Uint32 now = SDL_GetTicks(), dt = now - flickAt;
		if (!dt) return;
		flickAt = now;
		PhotoScale sc = p.photoScaleNow();
		const Player::PhotoView& v = p.photo();
		double x0 = v.panX, y0 = v.panY;
		Rect tr = thumbRect(p);
		if (p.settings.dragBox) p.setPhotoView(v.zoom, x0 - flickVX * dt / tr.w, y0 - flickVY * dt / tr.h);
		else p.setPhotoView(v.zoom, x0 + flickVX * dt / PAN_W / sc.x, y0 + flickVY * dt / PAN_H / sc.y);
		// slowing, and stopped at an edge
		double decay = std::exp(-(double)dt / 300);
		flickVX = p.photo().panX == x0 ? 0 : flickVX * decay;
		flickVY = p.photo().panY == y0 ? 0 : flickVY * decay;
		if (std::hypot(flickVX, flickVY) < 0.02) flickAt = 0;
	}

	Ui::Rect Ui::buttonRect(int id) const
	{
		if (id == LINE1_BTN) return {24, LINE1_Y - 12, 592, 24};
		if (id == LINE2_BTN) return {24, LINE2_Y - 12, 592, 24};
		if (id == CHAPTER_BTN) return {24, CHAPTER_Y - 14, 592, 24};
		// Sound's
		if (id == SOUND_TAB0 || id == SOUND_TAB1) return soundTabRect(id - SOUND_TAB0);
		if (id == EQ_SWITCH) return {16, 70, 136, 52};
		if (id == EQ_PREV) return {160, 70, 56, 52};
		if (id == EQ_NEXT) return {568, 70, 56, 52};
		if (id == EQ_NAME_ZONE) return {216, 70, 352, 52};
		if (id == EQ_RESET || id == EQ_SAVE || id == EQ_OPTIONS) return {16 + (id - EQ_RESET) * 206, 400, 196, 32};
		if (id >= FX_ROW && id < FX_ROW + SOUNDFX_COUNT) return {16, ROW_Y + (id - FX_ROW) * ROW_H, 608, ROW_H - 4};
		// the select mode's bar: three buttons (Edit, Add), two (Pick, Move)
		if (id == SEL_ALL) return {24, 428, 184, 44};
		if (id == SEL_MOVE) return {228, 428, 184, 44};
		if (id == SEL_ACT) return selMode == SEL_PICK ? Rect{328, 428, 288, 44} : Rect{432, 428, 184, 44};
		if (id == SEL_CANCEL) return selMode == SEL_PICK ? Rect{24, 428, 288, 44} : Rect{228, 428, 184, 44};
		if (id == MOVE_CANCEL) return {24, 428, 288, 44};
		if (id == MOVE_DONE) return {328, 428, 288, 44};
		if (id == BUSY_CANCEL) return {152, 224, 336, 46};
		if (id >= PHOTO_BTN) return {0, 0, 0, 0};	// photoBtn's: they move with the pan area
		if (id == QUIT_BTN) return {468, 428, 148, 44};
		if (id == EMPTY_BTN) return {200, 300, 240, 52};
		if (id == GROUP_PREV) return {16, 58, 172, 30};
		if (id == GROUP_NEXT) return {452, 58, 172, 30};
		if (id == SCREEN_BTN) return {320, 428, 140, 44};
		if (id == INFO_BTN) return {172, 428, 140, 44};
		if (id == LOCK_BTN) return {24, 428, 140, 44};
		if (id == EXIT_BTN) return {170, 200, 300, 80};
		if (id == BACK_ICON) return {8, 8, 48, 48};
		return {24 + id % 5 * BTN_STEP, id / 5 ? ROW2_Y : ROW1_Y, BTN_W, BTN_H};
	}

	bool Ui::act(int id, Player& p)
	{
		// "<" over what plays closes it at once (the place kept); B and Close ask twice, a pocket's press being one
		if (id == BACK_ICON && page == Page::Main && p.playing()) return true;
		// elsewhere (a page over what plays, START's menu): B, back a step
		if (id == BACK_ICON && (inMenu || (page != Page::Browse && page != Page::Main))) return pressAnyway(Button::B, p);
		if (id == QUIT_BTN || id == BACK_ICON) return pressAnyway(Button::B, p);
		if (id == EMPTY_BTN) { doEmptyAction(p); return false; }
		// the select mode's bar: its keys' (Cancel leaves from any folder)
		if (id == SEL_ALL) { selKey(Button::Select, p); return false; }
		if (id == SEL_MOVE) { selKey(Button::Y, p); return false; }
		if (id == SEL_ACT) { selKey(Button::X, p); return false; }
		if (id == SEL_CANCEL) { selBack(p, true); return false; }
		if (id == MOVE_CANCEL || id == MOVE_DONE) { moveEnd(id == MOVE_DONE, p); return false; }
		if (id == BUSY_CANCEL) { cancelAdd(p); return false; }
		// a song's line under the buttons, and its Sound button (Y's place, a song with no chapters): the Sound page
		if (page == Page::Main && p.playingAudio() && (id == LINE1_BTN || (id == 6 && p.chapters.empty() && p.error.empty())))
		{
			openSound(p, false);
			return false;
		}
		if (id == LINE1_BTN || id == LINE2_BTN) { openTracks(id == LINE1_BTN ? TRACKS_AUDIO : TRACKS_SUBS, p); return false; }
		if (id == CHAPTER_BTN) { openChapters(p); return false; }
		if (id == GROUP_PREV || id == GROUP_NEXT) { stepGroup(id == GROUP_NEXT ? 1 : -1); return false; }
		if (id == SCREEN_BTN) { panel.set(false); return false; }
		if (id == EXIT_BTN) { quitRequested = true; return false; }
		if (id == LOCK_BTN)
		{
			touchLocked = true;
			p.osd("Touch locked: hold the lock 1 s to unlock");
			return false;
		}
		if (id >= PHOTO_BTN && id < PHOTO_BTN + PHOTO_BTNS)
		{
			static const Button PHOTO_KEYS[PHOTO_BTNS] = {Button::L, Button::R, Button::Down, Button::Up, Button::L3,
				Button::Select, Button::None, Button::Start};
			Button b = PHOTO_KEYS[id - PHOTO_BTN];
			if (b != Button::None) return pressAnyway(b, p);
			id = INFO_BTN;
		}
		if (id == INFO_BTN)
		{
			page = Page::Info;
			cursor = scroll = 0;
			infoAt = 0;	// read at once
			return false;
		}
		Button b = GRID[id / 5][id % 5].button;
		if (b != Button::Up) return pressAnyway(b, p);
		if (!p.error.empty()) return false;
		// a tap on the speed goes round its choices
		double s = stepSpeed(p.speed, 1, true);
		p.setSpeed(s);
		p.osd("Speed " + speedLabel(s));
		return false;
	}

	bool Ui::pointer(const Pointer& pt, Player& p)
	{
		int x = (int)(pt.x * W), y = (int)(pt.y * H);
		if (kbOpen && panel.on())
		{
			kbPointer(pt, x, y);
			if (eqNameSubmit) eqNameDone(p);
			if (plNameSubmit) plNameDone(p);
			return false;
		}
		// dark: a touch anywhere brings the panel back, and does nothing else (the finger can't see what's there)
		if (!panel.on())
		{
			if (pt.kind == Pointer::Down)
			{
				panel.set(true);
				swallowTouch = true;
			}
			return false;
		}
		if (swallowTouch)
		{
			if (pt.kind == Pointer::Up) swallowTouch = false;
			return false;
		}
		auto barFraction = [&] { return std::clamp((x - BAR.x) / (double)BAR.w, 0.0, 1.0); };

		if (quickOpen) return quickPointer(pt, x, y, p);
		displayOpen = displayOpen && displayShown(p);
		if (displayOpen) return displayPointer(pt, x, y, p);
		if (page == Page::Sound) return soundPointer(pt, x, y, p);
		// locked, over a video: Screen off goes through; a hold on the lock (draw() times it) releases it; nothing else
		if (touchLocked && p.playing() && !p.playingPhoto() && page == Page::Main && pressedBtn != SCREEN_BTN)
		{
			if (pt.kind == Pointer::Down && buttonRect(LOCK_BTN).hit(x, y)) lockHoldAt = SDL_GetTicks();
			else if (pt.kind == Pointer::Down && buttonRect(SCREEN_BTN).hit(x, y)) pressedBtn = SCREEN_BTN;
			else if (pt.kind == Pointer::Up) lockHoldAt = 0;	// let go too soon: still locked
			else if (pt.kind == Pointer::Down && SDL_GetTicks() - lockNoticeAt > 3000)
			{
				lockNoticeAt = SDL_GetTicks();
				p.osd("Touch locked: hold the lock 1 s to unlock");
			}
			return false;
		}

		if (popupShown(p))
		{
			// the updater's pop-up: a row tapped (as the finger lifts); elsewhere, nothing (it's a question)
			int n = (int)popupItems().size();
			if (pt.kind == Pointer::Down)
			{
				pressedBtn = -1;
				for (int i = 0; i < n; i++)
					if (popupRow(i).hit(x, y)) pressedBtn = 200 + i;
			}
			else if (pt.kind == Pointer::Up && pressedBtn >= 200)
			{
				int item = pressedBtn - 200;
				pressedBtn = -1;
				if (item < n && popupRow(item).hit(x, y)) choosePopup(item, p);
			}
			return false;
		}
		if (ctxOpen)
		{
			// the folder's menu: a row tapped (as the finger lifts), anywhere else closes it
			if (pt.kind == Pointer::Down)
			{
				pressedBtn = -1;
				for (int i = 0; i < (int)ctxItems.size(); i++)
					if (ctxRow(i).hit(x, y)) pressedBtn = 100 + i;
				if (pressedBtn < 0) ctxOpen = false;
			}
			else if (pt.kind == Pointer::Up && pressedBtn >= 100)
			{
				int item = pressedBtn - 100;
				pressedBtn = -1;
				if (item != ctxCursor) ctxArmed = false;
				ctxCursor = item;
				if (ctxRow(item).hit(x, y)) chooseBrowseMenu(item, p);
			}
			return false;
		}
		// a network folder read before it's added: its dialog's Cancel, nothing else
		if (page == Page::Browse && addPending.serial)
		{
			if (pt.kind == Pointer::Down) pressedBtn = buttonRect(BUSY_CANCEL).hit(x, y) ? BUSY_CANCEL : -1;
			else if (pt.kind == Pointer::Up && pressedBtn == BUSY_CANCEL)
			{
				pressedBtn = -1;
				if (buttonRect(BUSY_CANCEL).hit(x, y)) cancelAdd(p);
			}
			return false;
		}
		if (page == Page::Browse && pickerOpen) return pickerPointer(pt, x, y, p);
		// a playlist's row on the finger by its grip: the landing line follows, lifted it's dropped there
		if (plDrag.on)
		{
			plDrag.y = std::clamp(y, 0, H - 1);
			if (pt.kind == Pointer::Up) dragEnd(p);
			return false;
		}
		if (dragging)
		{
			dragFraction = barFraction();
			if (pt.kind == Pointer::Up)
			{
				// the drag's last place, to the frame
				dragging = false;
				p.seekFraction(dragFraction);
			}
			else if (SDL_GetTicks() - scrubAt >= SCRUB_MS && std::fabs(dragFraction - scrubbed) > 0.0005)
			{
				// the picture follows the finger: keyframes only, and no more often than the decoder keeps up with
				scrubAt = SDL_GetTicks();
				scrubbed = dragFraction;
				p.scrub(dragFraction);
			}
			return false;
		}
		if (panTouch) return photoPointer(pt, x, y, p);
		if (listTouch)
		{
			// a list (the tracks, the browser, the settings): a drag scrolls it, a tap acts as the finger lifts
			bool set = page == Page::Settings, info = page == Page::Info || page == Page::About;
			int n = set ? (setGroup < 0 ? GROUPS : (int)settingRows().size()) : info ? (int)infoLines.size() : (int)rows(p).size();
			int rowH = set ? (setGroup < 0 ? 84 : SET_H) : info ? INFO_H : ROW_H, shown = set ? SET_SHOWN : info ? INFO_SHOWN : ROWS_SHOWN;
			if (pt.kind == Pointer::Move && (listScrolled || std::abs(y - touchStartY) > 12))
			{
				listScrolled = true;
				holdAt = 0;
				int rowsMoved = (int)std::lround((touchStartY - y) / (double)rowH);
				scroll = std::clamp(scrollStart + rowsMoved, 0, std::max(0, n - shown));
				cursor = std::clamp(cursor, scroll, scroll + shown - 1);
			}
			else if (pt.kind == Pointer::Up)
			{
				listTouch = false;
				holdAt = 0;
				if (listScrolled || info) return false;	// the info page only scrolls
				// a setting: the left of its value (its arrow) steps back, the rest of the row on
				if (set && setGroup < 0) openGroup(cursor);
				else if (set && settingRows()[cursor] == SOUND_ROW) openSound(p, true);	// a page, not a value: either side opens it
				else if (set) changeSetting(settingRows()[cursor], touchX >= SET_ARROW_X - 12 && touchX < 490 ? -1 : 1, p);
				else choose(cursor, p);
			}
			return false;
		}
		if (pressedBtn >= 0)
		{
			if (pt.kind != Pointer::Up) return false;
			int id = pressedBtn;
			pressedBtn = -1;
			// let go off the button: nothing (a finger that slid away changed its mind)
			Rect r = id >= PHOTO_BTN && id < PHOTO_BTN + PHOTO_BTNS ? photoBtn(id - PHOTO_BTN, p) : buttonRect(id);
			return r.hit(x, y) && act(id, p);
		}
		if (pt.kind != Pointer::Down) return false;
		if (inMenu && y < 60)
		{
			// the tab bar: a tab, or "<" (it closes the menu as the finger lifts)
			if (buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
			for (int t = 0; t < TABS; t++)
				if (tabRect(t).hit(x, y)) showTab(t, p);
			return false;
		}
		if (page == Page::Exit)
		{
			if (buttonRect(EXIT_BTN).hit(x, y)) pressedBtn = EXIT_BTN;
			return false;
		}

		bool quitTap = (page == Page::Main && buttonRect(QUIT_BTN).hit(x, y)) ||
			(backIconShown(p) && buttonRect(BACK_ICON).hit(x, y));
		if (!quitTap) quitArmed = 0;
		// "<": B (up a folder; twice, out of what plays or, from the first page, out of the player)
		if (backIconShown(p) && buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
		if (emptyAction() && buttonRect(EMPTY_BTN).hit(x, y)) { pressedBtn = EMPTY_BTN; return false; }
		if (page == Page::Info || page == Page::About)
		{
			if (y < INFO_Y) return false;
			listTouch = true;
			listScrolled = false;
			touchStartY = y;
			scrollStart = scroll;
			return false;
		}
		if (page == Page::Settings && setGroup >= 0)
			for (int id : {GROUP_PREV, GROUP_NEXT})
				if (buttonRect(id).hit(x, y)) { pressedBtn = id; return false; }
		if (page == Page::Settings)
		{
			int top = SET_Y, h = SET_H;
			int n = setGroup < 0 ? GROUPS : (int)settingRows().size();
			int row = scroll + (y - top) / h;
			if (setGroup < 0)
			{
				// a tile
				row = -1;
				for (int g = 0; g < GROUPS; g++)
					if (groupRect(g).hit(x, y)) row = g;
				if (row < 0) return false;
			}
			else if (y < top || y >= top + SET_SHOWN * h || row >= n) return false;
			cursor = row;
			listTouch = true;
			listScrolled = false;
			touchStartY = y;
			touchX = x;
			scrollStart = scroll;
			return false;
		}
		if (page == Page::Browse && !inMenu && selMode != SEL_NONE)
		{
			// selecting: no tabs (the header's "<" was taken above); the bar's buttons, those that can act now
			if (y < 60) return false;
			if (y >= 424)
			{
				int n = selCount(), ticked = 0, files = selFolderFiles(ticked);
				std::vector<std::pair<int, bool>> ids;
				if (moving) ids = {{MOVE_CANCEL, true}, {MOVE_DONE, true}};
				else if (selMode == SEL_EDIT) ids = {{SEL_ALL, !plCur.entries.empty()}, {SEL_MOVE, n > 0}, {SEL_ACT, n > 0}};
				else if (selMode == SEL_ADD) ids = {{SEL_ALL, files > 0}, {SEL_CANCEL, true}, {SEL_ACT, n > 0}};
				else ids = {{SEL_CANCEL, true}, {SEL_ACT, pickable()}};
				for (auto& id : ids)
					if (id.second && buttonRect(id.first).hit(x, y)) pressedBtn = id.first;
				return false;
			}
			if (moving) return false;
			// Edit: a finger on a row's grip drags it at once
			if (selMode == SEL_EDIT && x >= 576 && y >= ROW_Y && y < ROW_Y + ROWS_SHOWN * ROW_H)
			{
				int i = scroll + (y - ROW_Y) / ROW_H;
				if (selTickable(i)) { dragBegin(i, y, p); return false; }
			}
		}
		if (page == Page::Browse && !inMenu && y < 60)
		{
			for (int t = 0; t < BROWSE_TABS; t++)
				if (browseTabRect(t).hit(x, y)) switchTab(t, p);
			return false;
		}
		if (page == Page::Tracks && y < 60)
		{
			// a tab switches as the finger lands (as the browser's do)
			for (int t = 0; t < 2; t++)
				if (tracksTabRect(t).hit(x, y) && t != tracksTab) openTracks(t, p);
			return false;
		}
		if (page != Page::Main)
		{
			if (y < ROW_Y || y >= ROW_Y + ROWS_SHOWN * ROW_H) return false;
			int i = scroll + (y - ROW_Y) / ROW_H;
			if (i >= (int)rows(p).size()) return false;
			cursor = i;
			listTouch = true;
			listScrolled = false;
			touchStartY = y;
			scrollStart = scroll;
			holdAt = page == Page::Browse && !inMenu && selMode == SEL_NONE && holdRow(i) ? SDL_GetTicks() : 0;
			return false;
		}
		if (p.playingPhoto())
		{
			if (panRect(p).hit(x, y))
			{
				panTouch = true;
				panMoved = false;
				flickAt = 0;
				flickVX = flickVY = 0;
				panDownX = lastX = x;
				panDownY = lastY = y;
				lastAt = SDL_GetTicks();
				panStartX = p.photo().panX;
				panStartY = p.photo().panY;
				return false;
			}
			for (int i = 0; i < PHOTO_BTNS; i++)
				if (photoBtn(i, p).hit(x, y)) pressedBtn = PHOTO_BTN + i;
			if (buttonRect(QUIT_BTN).hit(x, y)) pressedBtn = QUIT_BTN;
			return false;
		}
		// the chapter line first: the bar's room around it reaches up over its lower part
		if (chapterLineShown(p) && buttonRect(CHAPTER_BTN).hit(x, y)) { pressedBtn = CHAPTER_BTN; return false; }
		// a video's track lines: each opens its tab
		if (!p.playingAudio() && p.error.empty())
			for (int id : {LINE1_BTN, LINE2_BTN})
				if (buttonRect(id).hit(x, y)) { pressedBtn = id; return false; }
		// a song's: its first line opens Sound
		if (p.playingAudio() && p.error.empty() && buttonRect(LINE1_BTN).hit(x, y)) { pressedBtn = LINE1_BTN; return false; }
		// the bar is easier to hit with some room around it
		if (Rect{BAR.x - 8, BAR.y - 20, BAR.w + 16, BAR.h + 40}.hit(x, y) && p.duration > 0)
		{
			// the bar acts as the finger lands (not as it lifts, as the buttons do): the picture goes there at once
			// and follows the finger while it holds and drags
			dragging = true;
			dragFraction = scrubbed = barFraction();
			scrubAt = SDL_GetTicks();
			p.scrub(dragFraction);
			return false;
		}
		for (int id : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, QUIT_BTN, SCREEN_BTN, INFO_BTN, LOCK_BTN})
			if (buttonRect(id).hit(x, y)) pressedBtn = id;
		return false;
	}

	std::string Ui::signature(const Player& p) const
	{
		const Track* a = p.selected(TrackType::Audio);
		const Track* s = p.selected(TrackType::Sub);
		char buf[256];
		snprintf(buf, sizeof buf, "%d %d %d %d %ld %ld %d %d %lld %lld %zu %d %d %d %.2f %d %d %d %d|", (int)page, cursor, scroll,
			p.paused + setGroup * 2 + 2, (long)std::floor(p.position), (long)p.duration, (int)p.aspect, p.loaded, a ? (long long)a->id : 0LL,
			s ? (long long)s->id : 0LL, p.tracks.size(), dragging, (int)(dragFraction * 1000), quitArmed != 0, p.speed,
			p.index(), p.count(), pressedBtn + touchLocked * 100 + (lockHoldAt != 0) * 1000, listTouch * 1000 + browseVersion + (clearArmed != 0) * 100000 + (int)p.history().items().size() * 1000000);
		std::string photo = "|tab" + std::to_string(browseTab) + (netLoading ? "L" : "");
		if (touchLocked && lockHoldAt) photo += "|hold" + std::to_string((SDL_GetTicks() - lockHoldAt) / 100);
		if (p.playingPhoto())
		{
			const Player::PhotoView& v = p.photo();
			char pb[160];
			snprintf(pb, sizeof pb, "|photo %.4f %.4f %.4f %d %d %d %d", v.zoom, v.panX, v.panY, v.rotate, v.fill, p.thumbId, p.photoWidth());
			photo = pb;
		}
		// the backlights' levels while their rows show (the keys change them too)
		std::string upd = quickOpen ? "|quick" + std::to_string(quickCursor) + " " + std::to_string(volumePct) + " " + std::to_string(pressedBtn) +
			" " + std::to_string(backlight.permille(Backlight::TOP)) + "," + std::to_string(backlight.permille(Backlight::BOTTOM)) : "";
		upd += page == Page::Settings && setGroup == GROUP_CONTROLS
			? "|bl" + std::to_string(backlight.permille(Backlight::TOP)) + "," + std::to_string(backlight.permille(Backlight::BOTTOM)) : "";
		if (updater)
			upd += "|upd" + std::to_string((int)updater->state()) + " " + std::to_string(updater->progress()) + " " + updater->available() +
				" " + std::to_string(popupShown(p)) + std::to_string(popupCursor) + std::to_string(pressedBtn);
		// the chapters: the list, the one playing (the Chapters page's progress goes by the position, above), the tab
		photo += "|ch" + std::to_string(p.chapterSerial) + " " + std::to_string(p.chapters.size()) + " " +
			std::to_string(p.currentChapter()) + " t" + std::to_string(tracksTab);
		// Sound: its tab, the band, the effect under the cursor, its dialogs and the presets' list; the Display popup
		if (page == Page::Sound || displayOpen)
		{
			char sb[160];
			snprintf(sb, sizeof sb, "|snd%d %d %d %d %d %d %d %d %d %d %d", soundTab, fxCursor, eqBand, eqDialog, eqDlgCursor, eqListScroll,
				displayShown(p) ? displayRow : -1, p.eqPresetSerial(), p.playingAudio(), stashOn, eqTouch);
			photo += sb;
		}
		// the dots of "Loading..." go round
		if (p.loading() || p.thumbPending()) photo += "|loading" + std::to_string(SDL_GetTicks() / 400 % 4);
		// Playlists: the store, items found missing, a menu's title and line, a name refused
		photo += "|pl" + std::to_string(plStore ? plStore->serial() : -1) + " " + std::to_string(plMissingSerial) + ctxTitle + "|" + ctxNote + "|" + plNameNote;
		// selecting: the mode and where it adds, the ticks, Move's block, a dragged row's landing (the finger in steps
		// of 8), the picker, the arms, a folder being read
		if (selMode != SEL_NONE || pickerOpen || addPending.serial)
		{
			char sb[200];
			snprintf(sb, sizeof sb, "|sel%d %d %d %d %d,%d %d %d,%d %d %d %d %d %d %d", selMode, selCount(), selSerial, moving,
				plSel.empty() ? -1 : *plSel.begin(), (int)plSel.size(), plDrag.on, plDrag.on ? dragSlot() : -1, plDrag.y / 8, pickerOpen,
				pickerCursor, pickerScroll, removeArmed, dropArmed != 0, addPending.serial);
			photo += sb + selDest;
		}
		// Main: what it plays from, and whether the file is a server's (a playlist's item says where it comes from)
		if (p.playing() && page == Page::Main)
		{
			const PlaySource& src = p.source();
			photo += "|src" + std::to_string((int)src.kind) + (src.shuffled ? "s" : "") + (isNetPath(p.currentPath()) ? "n" : "") +
				(p.netWaiting() ? "w" : "") + src.title + "|" + src.id;
		}
		return buf + photo + p.title + "|" + p.error + "|" + serializeSettings(p.settings) + "|bat" + std::to_string(battery.percent) + (battery.charging ? "c" : "") + (inMenu ? "|menu" + std::to_string(menuTab) : "") + (ctxOpen ? "|ctx" + std::to_string(ctxCursor) + std::to_string(pressedBtn) + ctxStatus + (ctxArmed ? "!" : "") : "") + upd
			+ (kbOpen ? "|kb" + eqNameNote + std::to_string(kbCursor) + "," + std::to_string(kbPressed) + "," + std::to_string((int)kbShift) + (kbSymbols ? "s" : "") + std::to_string(kbEdit.caret) + kbEdit.text + (kbCaretShown() ? "|" : "") : "");
	}

	SDL_Texture* Ui::text(const std::string& s, int size, SDL_Color c)
	{
		char key[32];
		snprintf(key, sizeof key, "%d %02x%02x%02x|", size, c.r, c.g, c.b);
		std::string k = key + s;
		auto it = cache.find(k);
		if (it != cache.end()) return it->second;
		if (cache.size() > 256)
		{
			for (auto& t : cache) SDL_DestroyTexture(t.second);
			cache.clear();
		}
		// each piece in its font, side by side on one baseline
		std::vector<SDL_Surface*> parts;
		int w = 0, ascent = 0, below = 0;
		auto pieces = runs(s.empty() ? " " : s, size);
		std::vector<TTF_Font*> partFonts;
		for (auto& r : pieces)
		{
			SDL_Surface* p = TTF_RenderUTF8_Blended(r.first, r.second.c_str(), c);
			if (!p) continue;
			partFonts.push_back(r.first);
			parts.push_back(p);
			w += p->w;
			ascent = std::max(ascent, TTF_FontAscent(r.first));
			below = std::max(below, p->h - TTF_FontAscent(r.first));
		}
		if (parts.empty()) return nullptr;
		SDL_Surface* surf = parts.size() == 1 ? parts[0] : SDL_CreateRGBSurfaceWithFormat(0, w, ascent + below, 32, SDL_PIXELFORMAT_ARGB8888);
		if (parts.size() > 1)
		{
			int x = 0;
			for (size_t i = 0; i < parts.size(); i++)
			{
				SDL_Surface* p = parts[i];
				SDL_SetSurfaceBlendMode(p, SDL_BLENDMODE_NONE);
				SDL_Rect at{x, ascent - TTF_FontAscent(partFonts[i]), p->w, p->h};
				SDL_BlitSurface(p, nullptr, surf, &at);
				x += p->w;
				SDL_FreeSurface(p);
			}
		}
		if (!surf) return nullptr;
		SDL_Texture* t = SDL_CreateTextureFromSurface(ren, surf);
		SDL_FreeSurface(surf);
		cache[k] = t;
		return t;
	}

	// align: 0 left of x, 1 right of x, 2 centred on x; text wider than maxW (logical) is cut with "..."
	void Ui::drawText(const std::string& s, int size, SDL_Color c, int x, int y, int maxW, int align)
	{
		drawTextAt(s, size, c, x, y, maxW, align, false);
	}

	void Ui::drawTextMid(const std::string& s, int size, SDL_Color c, int x, int cy, int maxW, int align)
	{
		drawTextAt(s, size, c, x, cy, maxW, align, true);
	}

	void Ui::drawTextAt(const std::string& s, int size, SDL_Color c, int x, int y, int maxW, int align, bool middle)
	{
		std::string str = s;
		if (maxW > 0)
		{
			int w = 0, limit = (int)(maxW * sx);
			w = textWidth(str, size);
			bool cut = false;
			while (w > limit && !str.empty())
			{
				// drop one UTF-8 character
				do str.pop_back(); while (!str.empty() && ((unsigned char)str.back() & 0xC0) == 0x80);
				cut = true;
				w = textWidth(str + "...", size);
			}
			if (cut) str += "...";
		}
		SDL_Texture* t = text(str, size, c);
		if (!t) return;
		int w, h;
		SDL_QueryTexture(t, nullptr, nullptr, &w, &h);
		int px = (int)std::lround(x * sx), py = (int)std::lround(y * sy);
		// middle: the capitals' middle on y (the same for every line in the font, descenders or not)
		if (middle)
		{
			// in another font (Arabic, Thai, CJK...): its box's middle (their capitals aren't Pixelify's)
			bool own = true;
			for (auto& r : runs(str, size)) own = own && r.first == fonts[size];
			py -= own ? TTF_FontAscent(fonts[size]) - capHeight[size] / 2 : h / 2;
		}
		if (align == 1) px -= w;
		else if (align == 2) px -= w / 2;
		SDL_Rect dst{px, py, w, h};
		SDL_RenderCopy(ren, t, nullptr, &dst);
	}

	void Ui::fill(Rect r, SDL_Color c)
	{
		SDL_SetRenderDrawBlendMode(ren, c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
		SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
		// by its edges on the panel's pixels: rects side by side meet, and the same width comes out the same everywhere
		int x0 = (int)std::lround(r.x * sx), y0 = (int)std::lround(r.y * sy);
		int x1 = (int)std::lround((r.x + r.w) * sx), y1 = (int)std::lround((r.y + r.h) * sy);
		if (r.w > 0 && x1 <= x0) x1 = x0 + 1;
		if (r.h > 0 && y1 <= y0) y1 = y0 + 1;
		SDL_Rect d{x0, y0, x1 - x0, y1 - y0};
		SDL_RenderFillRect(ren, &d);
	}

	void Ui::draw(const Player& p, bool force)
	{
		sameBrightness = p.settings.sameBrightness;
		if (quitArmed && SDL_GetTicks() - quitArmed >= QUIT_WINDOW_MS) quitArmed = 0;
		// before the signature: "Again to clear" goes back to how it was when the second tap would no longer clear
		if (clearArmed && SDL_GetTicks() - clearArmed >= QUIT_WINDOW_MS) clearArmed = 0;
		if (!batteryAt || SDL_GetTicks() - batteryAt >= 10000) readPower();
		// the lock held long enough: released (the finger still down presses nothing)
		if (touchLocked && lockHoldAt && SDL_GetTicks() - lockHoldAt >= UNLOCK_HOLD_MS)
		{
			touchLocked = false;
			lockHoldAt = 0;
			swallowTouch = true;	// the button shows "Lock" again
		}
		if (!touchLocked) lockHoldAt = 0;
		if (!panel.on())
		{
			// black once (a computer has no backlight to switch off), then nothing drawn till it is on again
			if (drawn != "off")
			{
				SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
				SDL_RenderClear(ren);
				SDL_RenderPresent(ren);
				drawn = "off";
			}
			return;
		}
		if (page == Page::Info && (infoAt == 0 || SDL_GetTicks() - infoAt >= 1000))
		{
			readInfo(p);
			scroll = std::min(scroll, std::max(0, (int)infoLines.size() - INFO_SHOWN));
		}
		// the search for servers over: its rows say so (Search again)
		if (browseDir == FINDNET && page == Page::Browse && discovery && findWasRunning && !discovery->running())
		{
			findWasRunning = false;
			findRows();
		}
		if (dropArmed && SDL_GetTicks() - dropArmed >= QUIT_WINDOW_MS) dropArmed = 0;
		// a finger held on a folder: its menu (the lift that follows chooses nothing). On a playlist's item, or a file:
		// selecting, that one ticked
		if (holdAt && listTouch && !listScrolled && SDL_GetTicks() - holdAt >= 600)
		{
			holdAt = 0;
			listTouch = false;
			swallowTouch = true;
			bool item = isPlDir(browseDir) && cursor < (int)browseRows.size() && browseRows[cursor].id == PL_ITEM;
			bool file = !isPlDir(browseDir) && cursor < (int)browseRows.size() && browseRows[cursor].id == 2;
			if ((item || file) && plStore)
			{
				selStart(item ? SEL_EDIT : SEL_ADD, "");
				swallowTouch = true;
				if (item) plSel.insert(cursor);
				else
				{
					picked.insert(browsePaths[cursor]);
					pickOrder.push_back(browsePaths[cursor]);
				}
			}
			else openBrowseMenu(cursor, MENU_HOLD);
		}
		plCheckShown();
		std::string sig = signature(p);
		if (page == Page::Info || page == Page::About)
			for (auto& l : infoLines) sig += l.label + "=" + l.text + ";";
		if (!force && sig == drawn) return;
		drawn = sig;

		renderPage(p);
		SDL_RenderPresent(ren);
	}

	void Ui::renderPage(const Player& p)
	{
		SDL_SetRenderDrawColor(ren, BG.r, BG.g, BG.b, 255);
		SDL_RenderClear(ren);
		if (page == Page::Info || page == Page::About) drawInfo(p);
		else if (page == Page::Exit) drawExit(p);
		else if (page == Page::Settings) drawSettings(p);
		else if (page == Page::Chapters) drawChapters(p);
		else if (page == Page::Sound) drawSound(p);
		else if (page != Page::Main) drawTracks(p);
		else drawMain(p);
		if (displayShown(p)) drawDisplay(p);
		if (inMenu) drawTabs();
		if (ctxOpen && page == Page::Browse) drawBrowseMenu();
		if (pickerOpen && page == Page::Browse) drawPicker();
		if (addPending.serial && page == Page::Browse) drawBusy();
		if (popupShown(p)) drawUpdatePopup();
		if (quickOpen) drawQuick(p);
		if (kbOpen) drawKeyboard();
	}

	void Ui::readPower()
	{
		const char* base = getenv("BP_POWER_SUPPLY");
		battery = readBattery(base ? base : "/sys/class/power_supply");
		batteryAt = SDL_GetTicks();
	}

	void Ui::drawBattery(int right, int y, const Player& p)
	{
		if (battery.percent < 0) return;
		// the body (an outline, filled to the charge) and its nub; the percent before it, the bolt before that
		const int bw = 30, bh = 16;
		int x = right - bw - 3;
		SDL_Color ink = battery.charging ? YELLOW : battery.percent <= 15 ? RED : CREAM;
		fill({x, y, bw, bh}, CREAM);
		fill({x + 2, y + 2, bw - 4, bh - 4}, BG);
		int level = (bw - 6) * battery.percent / 100;
		if (level > 0) fill({x + 3, y + 3, level, bh - 6}, ink);
		fill({x + bw, y + 5, 3, bh - 10}, CREAM);
		int left = x;
		if (p.settings.batteryPercent)
		{
			std::string pct = std::to_string(battery.percent) + "%";
			drawText(pct, 0, battery.percent <= 15 && !battery.charging ? RED : CREAM, x - 6, y - 2, 0, 1);
			left = x - 6 - (int)(textWidth(pct, 0) / sx) - 4;
		}
		if (battery.charging)
		{
			// a lightning bolt, 5x7 cells of 2
			static const char* BOLT[7] = {"...##", "..##.", ".##..", "#####", "..##.", ".##..", "##..."};
			int bx = left - 14, by = y + 1;
			for (int r = 0; r < 7; r++)
				for (int c = 0; c < 5; c++)
					if (BOLT[r][c] == '#') fill({bx + c * 2, by + r * 2, 2, 2}, YELLOW);
		}
	}

	std::string Ui::updateStatus() const
	{
		if (!updater || updater->state() == Updater::State::Off)
			return updater && updater->fromStore() ? "From the Store" : std::string("This is ") + BP_VERSION;
		switch (updater->state())
		{
		case Updater::State::Checking: return "Checking...";
		case Updater::State::UpToDate: return "Up to date (" + updater->current() + ")";
		case Updater::State::Available: case Updater::State::Downloading: return updater->available() + " is out";
		case Updater::State::Ready: return updater->available() + " ready";
		case Updater::State::Failed: return "Didn't work";
		default: return "This is " + updater->current();
		}
	}

	bool Ui::popupShown(const Player& p) const
	{
		if (!updater || popupClosed || page != Page::Browse || inMenu || ctxOpen) return false;
		Updater::State st = updater->state();
		// a release found: unless "Not until tomorrow" holds; after "Update now", what became of it
		if (st == Updater::State::Available) return time(nullptr) >= (time_t)p.settings.updateRemindAfter;
		return popupFollow && (st == Updater::State::Downloading || st == Updater::State::Ready || st == Updater::State::Failed);
	}

	std::vector<const char*> Ui::popupItems() const
	{
		switch (updater->state())
		{
		case Updater::State::Available: return {"Update now", "Later", "Not until tomorrow"};
		case Updater::State::Downloading: return {"Hide"};
		case Updater::State::Ready: return {"Restart now", "Later"};
		default: return {"OK"};
		}
	}

	void Ui::choosePopup(int item, Player& p)
	{
		Updater::State st = updater->state();
		if (st == Updater::State::Available && item == 0)
		{
			updater->download();
			popupFollow = true;
			popupCursor = 0;
			return;
		}
		if (st == Updater::State::Available && item == 2)
		{
			Settings s = p.settings;
			s.updateRemindAfter = (long long)nextMidnight(time(nullptr));
			p.setSettings(s);
		}
		if (st == Updater::State::Ready && item == 0)
		{
			restartRequested = true;
			return;
		}
		// Later, Hide, OK, and Not until tomorrow: closed for this time (a download goes on; a ready update is put in
		// place at the next start)
		popupClosed = true;
		popupCursor = 0;
	}

	void Ui::drawUpdatePopup()
	{
		auto items = popupItems();
		int top = 150, bottom = popupRow((int)items.size() - 1).y + 60;
		drawDialog({112, top, 416, bottom - top}, "");
		std::string title, sub;
		switch (updater->state())
		{
		case Updater::State::Available:
			title = "Banana Player " + updater->available() + " is out";
			sub = "You have " + updater->current();
			break;
		case Updater::State::Downloading:
		{
			int pct = updater->progress();
			title = "Downloading " + updater->available() + "...";
			sub = pct >= 0 ? std::to_string(pct) + "%" : "Starting";
			break;
		}
		case Updater::State::Ready:
			title = updater->available() + " is ready";
			sub = "Restart now, or it's in place next time";
			break;
		default:
			title = "The update didn't work";
			sub = updater->message();
			break;
		}
		drawTextMid(title, 1, YELLOW, W / 2, top + 30, 392, 2);
		drawTextMid(sub, 0, CREAM, W / 2, top + 62, 392, 2);
		if (updater->state() == Updater::State::Downloading)
		{
			// the bar
			int pct = std::max(0, updater->progress());
			fill({156, top + 84, 328, 8}, TEAL);
			fill({156, top + 84, 328 * pct / 100, 8}, YELLOW);
		}
		for (int i = 0; i < (int)items.size(); i++)
		{
			Rect r = popupRow(i);
			bool at = i == popupCursor;
			if (pressedBtn == 200 + i) fill(r, PRESSED);
			else if (at) drawFocus(r);
			drawTextMid(items[i], 1, at ? YELLOW : CREAM, W / 2, r.y + r.h / 2, r.w - 10, 2);
		}
		drawHints({{"A", "Choose"}, {"B", items.size() > 1 ? "Later" : "Close"}});
	}

	void Ui::drawBrowseMenu()
	{
		Rect box = ctxBox();
		// its title: the connection, the folder Hide is for, else the place it's over
		const NetServer* sv = netStore && !ctxServer.empty() ? netStore->find(ctxServer) : nullptr;
		std::string name = !ctxTitle.empty() ? ctxTitle : sv ? serverName(*sv) : !ctxPath.empty() ? baseName(ctxPath)
			: browseDir.empty() ? std::string("Your cards") : browseDir == NETROOT ? std::string("Your connections")
			: browseDir == RECENT ? std::string("Recent")
			: isNetPath(browseDir) && netBaseName(browseDir).empty() ? std::string("Shares")
			: isNetPath(browseDir) ? netBaseName(browseDir) : baseName(browseDir);
		drawDialog(box, name);
		// a playlist's Delete, pressed once: what it doesn't do
		bool plArmed = ctxArmed && ctxCursor < (int)ctxItems.size() && ctxItems[ctxCursor] == CTX_PL_DELETE;
		if (ctxExtra() && (!ctxNote.empty() || plArmed))
			drawTextMid(plArmed ? "Your files stay where they are" : ctxNote, 0, plArmed ? YELLOW : DIM, W / 2, box.y + 60, 368, 2);
		else if (ctxExtra())
			drawTextMid(ctxStatus.empty() ? std::string(sv ? std::string(netTypeLabel(sv->type)) + "  " + sv->host : "") : ctxStatus, 0,
				ctxStatus == "It works" ? YELLOW : DIM, W / 2, box.y + 60, 368, 2);
		for (int i = 0; i < (int)ctxItems.size(); i++)
		{
			int it = ctxItems[i];
			bool del = it == CTX_DELETE || it == CTX_PL_DELETE;
			bool armed = del && ctxArmed && i == ctxCursor;
			const char* label = it == CTX_HIDE ? (isHiddenFolder(ctxPath) ? "Unhide folder" : "Hide folder")
				: it == CTX_REFRESH ? "Refresh" : it == CTX_ADD ? "Add connection" : it == CTX_FIND ? "Find on the network"
				: it == CTX_EDIT ? "Edit connection" : it == CTX_TEST ? "Test connection"
				: armed ? "Press again to delete" : it == CTX_DELETE ? "Delete connection" : it == CTX_PL_DELETE ? "Delete playlist"
				: it == CTX_PL_NEW ? "New playlist\xE2\x80\xA6" : it == CTX_PL_PLAY ? (isPlDir(browseDir) ? "Play all" : "Play")
				: it == CTX_PL_SHUFFLE ? "Shuffle play" : it == CTX_PL_RENAME ? "Rename\xE2\x80\xA6" : it == CTX_PL_DEDUP ? "Remove duplicates"
				: it == CTX_PL_HERE ? "Play from here" : it == CTX_PL_REMOVE ? "Remove from playlist" : it == CTX_PL_SHOW ? "Show in folder"
				: it == CTX_PL_ADD_FILES ? "Add files\xE2\x80\xA6" : it == CTX_PL_ADD_FOLDER ? "Add folder\xE2\x80\xA6"
				: it == CTX_PL_SELECT ? "Select" : it == CTX_PL_MOVE ? "Move" : it == CTX_SELECT_FILES ? "Select files"
				: it == CTX_ADD_TO ? "Add to playlist\xE2\x80\xA6" : "Cancel";
			const Glyph* g = it == CTX_HIDE ? &G_EYE : it == CTX_REFRESH ? &G_REFRESH : it == CTX_ADD ? &G_PLUS : it == CTX_FIND ? &G_SEARCH
				: it == CTX_EDIT ? &G_PENCIL : it == CTX_TEST ? &G_PLUG : del ? &G_TRASH
				: it == CTX_PL_NEW ? &G_PLUS : it == CTX_PL_PLAY || it == CTX_PL_HERE ? &G_PLAY : it == CTX_PL_SHUFFLE ? &G_SHUFFLE
				: it == CTX_PL_RENAME ? &G_PENCIL : it == CTX_PL_DEDUP || it == CTX_PL_REMOVE ? &G_MINUS : it == CTX_PL_SHOW ? &G_FOLDER
				: it == CTX_PL_ADD_FILES ? &G_PLUS : it == CTX_PL_ADD_FOLDER ? &G_FOLDER : it == CTX_PL_SELECT || it == CTX_SELECT_FILES ? &G_BOX_ON
				: it == CTX_PL_MOVE ? &G_MOVE : it == CTX_ADD_TO ? &G_PLAYLIST : &G_CROSS;
			drawDialogRow(ctxRow(i), g, label, i == ctxCursor, pressedBtn == 100 + i, del ? (armed ? YELLOW : RED) : it == CTX_CANCEL ? DIM : CREAM);
		}
		// a menu too long for its line (a playlist's): Delete's word on the footer instead
		if (plArmed && !ctxExtra()) drawFooterText("Your files stay where they are", YELLOW);
		else drawHints({{"A", "Choose"}, {"B", "Close"}});
	}

	SDL_Surface* Ui::capture(const Player& p)
	{
		// drawn afresh and read back before it's shown (after a present, the frame's contents are gone)
		renderPage(p);
		int w, h;
		SDL_GetRendererOutputSize(ren, &w, &h);
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
		if (s && SDL_RenderReadPixels(ren, nullptr, SDL_PIXELFORMAT_ABGR8888, s->pixels, s->pitch) != 0)
		{
			SDL_FreeSurface(s);
			s = nullptr;
		}
		SDL_RenderPresent(ren);
		drawn.clear();
		return s;
	}


	void Ui::drawExit(const Player&)
	{
		drawTextMid("Quit Banana Player?", 2, CREAM, W / 2, 132, 0, 2);
		drawTextMid("Your place in what you were playing is kept.", 0, DIM, W / 2, 170, 0, 2);
		Rect b = buttonRect(EXIT_BTN);
		fill(b, pressedBtn == EXIT_BTN ? PRESSED : RED);
		// it's the page's one choice: always the one A takes
		fill({b.x - 4, b.y - 4, b.w + 8, 2}, YELLOW);
		fill({b.x - 4, b.y + b.h + 2, b.w + 8, 2}, YELLOW);
		fill({b.x - 4, b.y - 4, 2, b.h + 8}, YELLOW);
		fill({b.x + b.w + 2, b.y - 4, 2, b.h + 8}, YELLOW);
		int tw = (int)std::lround(textWidth("Quit Banana Player", 1) / sx), gw = glyphW(G_POWER);
		int x = b.x + b.w / 2 - (tw + gw + 12) / 2;
		drawGlyphMid(G_POWER, x + gw / 2, b.y + b.h / 2, CREAM);
		drawTextMid("Quit Banana Player", 1, CREAM, x + gw + 12, b.y + b.h / 2);
		drawHints({{"A", "Quit"}, {"B", "Close"}, {"L/R", "Tabs"}});
	}

	// ---- the pieces every page is made of ----

	void Ui::drawBackIcon()
	{
		Rect b = buttonRect(BACK_ICON);
		// armed (the browser's first page: once more quits) in yellow
		bool armed = quitArmed && page == Page::Browse && !inMenu;
		fill(b, pressedBtn == BACK_ICON ? PRESSED : armed ? YELLOW : TEAL);
		drawGlyphMid(G_BACK, b.x + b.w / 2 - 1, b.y + b.h / 2, armed ? BG : CREAM, 3);
	}

	void Ui::drawHeader(const std::string& title, const Player& p)
	{
		drawBackIcon();
		int room = 616 - 72 - (battery.percent >= 0 ? BATTERY_W : 0);
		drawTextMid(title, 2, CREAM, 72, 32, room);
		drawBattery(616, 24, p);
	}

	void Ui::drawTab(Rect r, const char* name, bool active)
	{
		fill(r, active ? YELLOW : TEAL);
		drawTextMid(name, 1, active ? BG : CREAM, r.x + r.w / 2, r.y + r.h / 2, r.w - 8, 2);
	}

	void Ui::drawHints(const std::vector<Hint>& hints)
	{
		// the button's name in yellow (as on the buttons), what it does in grey; centred along the bottom
		auto w = [&](const std::string& t) { return t.empty() ? 0 : (int)std::lround(textWidth(t, 0) / sx); };
		int gap = 24, total = 0;
		for (auto& h : hints) total += w(h.key) + (h.key.empty() ? 0 : 7) + w(h.verb);
		if (total + gap * ((int)hints.size() - 1) > 612) gap = 14;
		total += gap * ((int)hints.size() - 1);
		int x = W / 2 - total / 2;
		for (auto& h : hints)
		{
			if (!h.key.empty())
			{
				drawTextMid(h.key, 0, YELLOW, x, FOOT_Y);
				x += w(h.key) + 7;
			}
			drawTextMid(h.verb, 0, DIM, x, FOOT_Y);
			x += w(h.verb) + gap;
		}
	}

	void Ui::drawFooterText(const std::string& text, SDL_Color c)
	{
		drawTextMid(text, 0, c, W / 2, FOOT_Y, 612, 2);
	}

	void Ui::drawFocus(Rect r)
	{
		// the cursor: the row lit, a yellow edge on its left
		fill(r, TEAL);
		fill({r.x, r.y, 4, r.h}, YELLOW);
	}

	void Ui::drawScrollBar(int top, int height, int first, int shown, int total)
	{
		// along the right edge: where the window is in the list (nothing when it all shows)
		if (total <= shown) return;
		fill({630, top, 4, height}, SURFACE);
		int h = std::max(16, height * shown / total);
		int y = top + (height - h) * first / std::max(1, total - shown);
		fill({630, y, 4, h}, DIM);
	}

	void Ui::drawEmpty(const Glyph& g, const std::string& title, const std::string& sub, const char* action)
	{
		// in the middle of the page: a big mark, what it is, what to do
		int cy = action ? 196 : 216;
		drawGlyphMid(g, W / 2, cy - 36, DIM, 4);
		drawTextMid(title, 1, CREAM, W / 2, cy + 16, 560, 2);
		if (!sub.empty()) drawTextMid(sub, 0, DIM, W / 2, cy + 46, 560, 2);
		if (action)
		{
			Rect b = buttonRect(EMPTY_BTN);
			bool at = cursor == 0;
			fill(b, pressedBtn == EMPTY_BTN ? PRESSED : TEAL);
			if (at) { fill({b.x, b.y, b.w, 2}, YELLOW); fill({b.x, b.y + b.h - 2, b.w, 2}, YELLOW); fill({b.x, b.y, 2, b.h}, YELLOW); fill({b.x + b.w - 2, b.y, 2, b.h}, YELLOW); }
			int tw = (int)std::lround(textWidth(action, 1) / sx), gw = glyphW(G_PLUS);
			int x = b.x + b.w / 2 - (tw + gw + 10) / 2;
			drawGlyphMid(G_PLUS, x + gw / 2, b.y + b.h / 2, at ? YELLOW : CREAM);
			drawTextMid(action, 1, at ? YELLOW : CREAM, x + gw + 10, b.y + b.h / 2);
		}
	}

	std::string Ui::elideLeft(const std::string& s, int size, int maxW) const
	{
		// the end kept (a path's last folders): "...Movies/Season 1"
		int limit = (int)(maxW * sx);
		if (textWidth(s, size) <= limit) return s;
		std::string t = s;
		while (!t.empty() && textWidth("..." + t, size) > limit)
		{
			size_t n = 1;
			while (n < t.size() && ((unsigned char)t[n] & 0xC0) == 0x80) n++;
			t.erase(0, n);
		}
		return "..." + t;
	}

	void Ui::drawDialog(Rect box, const std::string& title)
	{
		// over the page, dimmed: a panel, its title on top; the footer is the dialog's (its hints drawn after)
		fill({0, 0, W, H}, {0, 0, 0, 170});
		fill({0, 436, W, 44}, {0x08, 0x07, 0x09, 255});	// the page's hints gone (as dark as the rest): the dialog's go there
		fill(box, TEAL);
		fill({box.x + 2, box.y + 2, box.w - 4, box.h - 4}, SURFACE);
		if (!title.empty()) drawTextMid(title, 1, YELLOW, box.x + box.w / 2, box.y + 30, box.w - 40, 2);
	}

	void Ui::drawDialogRow(Rect r, const Glyph* g, const std::string& label, bool at, bool pressed, SDL_Color ink)
	{
		if (pressed) fill(r, PRESSED);
		else if (at) drawFocus(r);
		SDL_Color c = at && ink.r == CREAM.r && ink.g == CREAM.g ? YELLOW : ink;
		int x = r.x + 20;
		if (g)
		{
			drawGlyphMid(*g, x + 11, r.y + r.h / 2, c);
			x += 36;
		}
		drawTextMid(label, 1, c, x, r.y + r.h / 2, r.x + r.w - x - 12);
	}

	void Ui::drawTabs()
	{
		static const char* names[TABS] = {"Recent", "Settings", "About", "Quit"};
		fill({0, 0, W, 62}, BG);	// over whatever the page drew there
		drawBackIcon();
		for (int t = 0; t < TABS; t++) drawTab(tabRect(t), names[t], t == menuTab);
	}

	Ui::RowIcon Ui::rowIcon(int i) const
	{
		if (i < 0 || i >= (int)browseRows.size() || i >= (int)browsePaths.size()) return ICON_NONE;
		const std::string& path = browsePaths[i];
		// the form's actions (its ids are its own: they meet the found servers')
		if (browseDir == FORMDIR) return browseRows[i].id == FORM_TEST ? ICON_TEST : browseRows[i].id == FORM_SAVE ? ICON_SAVE : ICON_NONE;
		if (browseDir == FINDNET)
			return browseRows[i].id == R_FOUND ? ICON_NETWORK : browseRows[i].id == R_SEARCH ? ICON_REFRESH
				: browseRows[i].id == R_TYPE_ADDRESS ? ICON_TYPE : ICON_NONE;
		switch (browseRows[i].id)
		{
		case PL_ROW: return ICON_PLAYLIST;
		case 4: return ICON_NETWORK;
		case 6: return ICON_REFRESH;
		case 1:
			if (path == RECENT) return ICON_RECENT;
			return browseDir.empty() ? ICON_PLACE : ICON_FOLDER;	// the first page's: the places (this card...)
		case 2: return isPhotoFile(path) ? ICON_PHOTO : isAudioFile(path) ? ICON_SONG : ICON_VIDEO;
		default: return ICON_NONE;	// messages, Clear, Try again
		}
	}


	int Ui::glyphW(const Glyph& g, int cell) const
	{
		int w = 0;
		for (int r = 0; r < g.h; r++) w = std::max(w, (int)strlen(g.rows[r]));
		return w * cell;
	}

	void Ui::drawGlyph(const Glyph& g, int x, int y, SDL_Color c, int cell)
	{
		// on the panel's own pixels: every cell the same size (at 1.6x a cell of 2 is 3 pixels, never 3 and 4 in turn)
		int cs = std::max(1, (int)std::lround(cell * sx)), cy = std::max(1, (int)std::lround(cell * sy));
		int px = (int)std::lround(x * sx), py = (int)std::lround(y * sy);
		// the picture centred where its logical box would be (its pixel size can be a little smaller)
		int gw = glyphW(g, 1);
		px += (int)std::lround((gw * cell * sx - gw * cs) / 2);
		py += (int)std::lround((g.h * cell * sy - g.h * cy) / 2);
		SDL_SetRenderDrawBlendMode(ren, c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
		SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
		for (int r = 0; r < g.h; r++)
		{
			const char* row = g.rows[r];
			// runs of cells as one rect
			for (int i = 0; row[i];)
			{
				if (row[i] != '#') { i++; continue; }
				int j = i;
				while (row[j] == '#') j++;
				SDL_Rect d{px + i * cs, py + r * cy, (j - i) * cs, cy};
				SDL_RenderFillRect(ren, &d);
				i = j;
			}
		}
	}

	void Ui::drawGlyphMid(const Glyph& g, int cx, int cy, SDL_Color c, int cell)
	{
		drawGlyph(g, cx - glyphW(g, cell) / 2, cy - g.h * cell / 2, c, cell);
	}

	void Ui::drawRowIcon(RowIcon icon, int x, int y, SDL_Color c)
	{
		// each in a 22 x 20 box (logical units)
		const Glyph* g = icon == ICON_RECENT ? &G_CLOCK : icon == ICON_PLACE ? &G_CARD : icon == ICON_FOLDER ? &G_FOLDER
			: icon == ICON_VIDEO ? &G_FILM : icon == ICON_SONG ? &G_NOTE : icon == ICON_PHOTO ? &G_PICTURE
			: icon == ICON_NETWORK ? &G_NETWORK : icon == ICON_REFRESH ? &G_REFRESH : icon == ICON_TYPE ? &G_PENCIL
			: icon == ICON_TEST ? &G_PLUG : icon == ICON_SAVE ? &G_CHECK : icon == ICON_PLAYLIST ? &G_PLAYLIST : nullptr;
		if (g) drawGlyphMid(*g, x + 11, y + 10, c);
	}


	std::string Ui::browseWhere() const
	{
		// where it is: "Videos / Show / Season 1" (from the first page's place it's in), "MY-PC / Media / Movies"
		std::string where = browseDir.empty() ? "Your cards" : browseDir == RECENT ? "Recent"
			: browseDir == NETROOT ? "Your connections" : browseDir == FORMDIR ? (formId.empty() ? "Add connection" : "Edit connection")
			: browseDir == FINDNET ? "On your network" : browseDir == PLROOT ? "Your playlists"
			: isPlDir(browseDir) ? "Playlists/" + plCur.name : browseDir;
		if (isNetPath(browseDir) || (!browseDir.empty() && !pseudoDir(browseDir))) return whereOf(browseDir);
		// the folders apart: "share / Movies"
		std::string out;
		for (char c : where) out += c == '/' ? std::string(" / ") : std::string(1, c);
		return out;
	}

	void Ui::drawTracks(const Player& p)
	{
		bool browse = page == Page::Browse;
		bool sel = browse && !inMenu && selMode != SEL_NONE;
		if (sel) drawSelHeader(p);
		else if (browse && !inMenu)
		{
			drawBackIcon();
			// the tabs, as START's menu has them
			static const char* TABS_NAMES[BROWSE_TABS] = {"Local", "Playlists", "Network"};
			for (int t = 0; t < BROWSE_TABS; t++) drawTab(browseTabRect(t), TABS_NAMES[t], t == browseTab);
			drawBattery(616, 24, p);
			// where it is, its end kept; the folder it's in brighter. A playlist: how many items, on the right
			int whereW = 592;
			if (isPlDir(browseDir) && plLoaded)
			{
				size_t n = plCur.entries.size();
				std::string count = std::to_string(n) + (n == 1 ? " item" : " items");
				drawTextMid(count, 0, DIM, 616, SUB_Y, 0, 1);
				whereW -= (int)std::lround(textWidth(count, 0) / sx) + 24;
			}
			drawWhere(24, whereW);
		}
		else if (!browse) drawTracksHeader(p);
		auto r = rows(p);
		bool form = browse && browseDir == FORMDIR;
		bool noAudio = !browse && trackType() == TrackType::Audio && r.size() == 1 && r[0].id < 0;
		// a message alone (nothing here, loading, no connections): in the middle of the page, not as a row to choose
		if (browse && !form && r.size() == 1 && r[0].id < 0)
		{
			const std::string& m = r[0].label;
			bool net = browseDir == NETROOT || isNetPath(browseDir);
			bool pl = browseDir == PLROOT || isPlDir(browseDir);
			const Glyph& g = net ? G_NETWORK : browseDir == RECENT ? G_CLOCK : G_FOLDER;
			std::string sub = m == "No connections yet" ? "A computer or NAS that shares its files"
				: m == "Nothing to play here" ? "No videos, music or photos in this folder"
				: m == "This folder can't be opened" ? "Is the card still in?"
				: m == "Play history is off (Settings)" ? "Turn it on in Settings, Files" : "";
			std::string title = m == "Play history is off (Settings)" ? "Play history is off" : m;
			if (pl)
				drawEmpty(G_PLAYLIST, title, m == "No playlists yet" ? "Gather videos and songs to play in your order"
					: m == "Playlists aren't available" ? "There's no folder to keep them in"
					: m == "This playlist can't be opened" ? "Was it deleted?" : "Add videos and songs from your cards or the network",
					!emptyAction() ? nullptr : isPlDir(browseDir) ? "Add files" : "New playlist");
			else drawEmpty(g, title, sub, emptyAction() ? "Add connection" : nullptr);
		}
		else if (noAudio) drawEmpty(G_SPEAKER, "No audio in this file", "This video has no sound track", nullptr);
		else
			for (int i = scroll; i < (int)r.size() && i < scroll + ROWS_SHOWN; i++)
			{
				int y = ROW_Y + (i - scroll) * ROW_H, mid = y + (ROW_H - 4) / 2;
				bool at = i == cursor;
				bool style = r[i].id == STYLE_ROW;
				// the subtitle style: apart from the tracks, a line above it
				if (style || (form && r[i].id == FORM_TEST)) fill({32, y - 3, 576, 1}, {0x3A, 0x38, 0x44, 255});
				// selecting: a ticked row on a lighter ground; Move's block lit, yellow either side
				bool block = sel && ((moving && plSel.count(i)) || (plDrag.on && dragSel.count(i)));
				if (block)
				{
					fill({16, y, 608, ROW_H - 4}, TEAL);
					fill({16, y, 4, ROW_H - 4}, YELLOW);
					fill({620, y, 4, ROW_H - 4}, YELLOW);
				}
				else if (at && !(sel && plDrag.on)) drawFocus({16, y, 608, ROW_H - 4});
				else if (sel && selTicked(i)) fill({16, y, 608, ROW_H - 4}, SURFACE);
				if (browse && r[i].id == PL_ITEM)
				{
					drawPlRow(i, mid, at || block);
					continue;
				}
				int noteW = 0;
				// Recent's Clear row says how it stands now (the list was made before the first press expired)
				std::string note = browse && r[i].id == 3 ? (clearArmed ? "Press again to clear" : "Press twice") : r[i].note;
				bool placeholder = !note.empty() && note[0] == DIM_NOTE;
				if (placeholder) note.erase(0, 1);
				SDL_Color soft = at ? DIM_TEAL : DIM;
				if (!note.empty())
				{
					// a resume time and a size, a count, a root's path: right-aligned and short; a form's value, longer
					int w = form ? 380 : 260;
					SDL_Color c = clearArmed && r[i].id == 3 ? YELLOW : form && !placeholder ? (r[i].id == FORM_TEST && note == "It works" ? YELLOW : CREAM) : soft;
					// a path keeps its end
					std::string shown = !form && note[0] == '/' ? elideLeft(note, 0, w) : note;
					drawTextMid(shown, form ? 1 : 0, c, 608, mid, w, 1);
					noteW = form ? std::min(w, (int)std::lround(textWidth(shown, 1) / sx)) + 20 : std::min(w, (int)std::lround(textWidth(shown, 0) / sx)) + 16;
				}
				int textX = 32;
				// adding: a file's box (none for a photo: it can't go in, dimmed; nor a folder), the marks after it
				bool boxes = sel && selMode == SEL_ADD;
				bool inert = sel && r[i].id == 2 && !selTickable(i);
				if (boxes && selTickable(i)) drawGlyphMid(selTicked(i) ? G_BOX_ON : G_BOX, 40, mid, selTicked(i) ? YELLOW : DIM);
				if (browse)
				{
					// what the row is, as a mark before its name
					RowIcon icon = rowIcon(i);
					if (icon != ICON_NONE)
					{
						drawRowIcon(icon, boxes ? 60 : 32, mid - 10, inert ? DIM : at ? YELLOW : CREAM);
						textX = boxes ? 94 : 66;
					}
				}
				else if (style)
				{
					drawGlyphMid(G_GEAR, 43, mid, at ? YELLOW : CREAM);
					drawGlyphMid(G_RIGHT, 604, mid, at ? YELLOW : DIM);
					textX = 66;
				}
				else if (r[i].id >= 0)
				{
					// the track in use: a filled ring (the cursor is only where it is)
					drawGlyphMid(r[i].selected ? G_RADIO_ON : G_RADIO, 41, mid, r[i].selected ? YELLOW : at ? CREAM : DIM);
					textX = 66;
				}
				SDL_Color ink = (r[i].id < 0 && !style) || inert ? soft : at ? YELLOW : CREAM;
				drawTextMid(r[i].label, 1, ink, textX, mid, 600 - textX - noteW);
			}
		// a dragged row: where it lands, a line in the gap (none while it would stay where it is)
		if (sel && plDrag.on && !dragSel.empty() && dragTarget() != *dragSel.begin())
		{
			int slot = dragSlot();
			if (slot >= scroll && slot <= scroll + ROWS_SHOWN) fill({16, ROW_Y + (slot - scroll) * ROW_H - 3, 608, 2}, YELLOW);
		}
		int n = (int)r.size();
		drawScrollBar(ROW_Y, ROWS_SHOWN * ROW_H - 4, scroll, ROWS_SHOWN, n);
		if (!browse)
		{
			// the other tab's key, as on its tab; on Subtitle style A opens it
			bool audio = trackType() == TrackType::Audio;
			const char* verb = cursor < n && r[cursor].id == STYLE_ROW ? "Style" : "Choose";
			if (noAudio) drawHints({{"X", "Subtitles"}, {"B", "Back"}});
			else drawHints({{"A", verb}, {audio ? "X" : "Y", audio ? "Subtitles" : "Audio"}, {"B", "Back"}});
			return;
		}
		if (sel) { drawSelBar(); return; }
		if (quitArmed) { drawFooterText("Press B again to quit", YELLOW); return; }
		const char* other = browseTab == TAB_PLAYLISTS ? "Tabs" : "Playlists";
		const char* tabKey = browseTab == TAB_LOCAL ? "R" : browseTab == TAB_PLAYLISTS ? "L/R" : "L";
		bool plEmpty = r.size() == 1 && r[0].id < 0;
		if (inMenu) drawHints({{"A", "Play"}, {"B", "Close"}, {"L/R", "Tabs"}});
		else if (form) drawHints({{"A", "Change"}, {"LEFT/RIGHT", "Choose"}, {"B", "Cancel"}});
		else if (browseDir == FINDNET) drawHints({{"A", "Add this one"}, {"B", "Back"}});
		else if (emptyAction() && browseDir == PLROOT) drawHints({{"A", "New playlist"}, {tabKey, other}, {"START", "Menu"}});
		else if (emptyAction() && isPlDir(browseDir)) drawHints({{"A", "Add files"}, {"B", "Back"}, {"X", "Options"}, {tabKey, other}});
		else if (emptyAction()) drawHints({{"A", "Add connection"}, {"X", "Options"}, {tabKey, other}, {"START", "Menu"}});
		else if (browseDir == PLROOT && plEmpty) drawHints({{tabKey, other}, {"START", "Menu"}, {"B\xC3\x97" "2", "Quit"}});
		else if (browseDir == PLROOT)
			drawHints({{"A", "Open"}, {"X", "Options"}, {"Y", "Playlist"}, {tabKey, other}, {"START", "Menu"}, {"B\xC3\x97" "2", "Quit"}});
		else if (isPlDir(browseDir) && plEmpty) drawHints({{"B", "Back"}, {"X", "Options"}, {tabKey, other}});
		else if (isPlDir(browseDir))
			drawHints({{"A", "Play"}, {"B", "Back"}, {"X", "Options"}, {"Y", "Item"}, {tabKey, other}, {"START", "Menu"}});
		else if (browseDir == NETROOT) drawHints({{"A", "Open"}, {"X", "Options"}, {"Y", "Connection"}, {tabKey, other}, {"START", "Menu"}});
		else if (r.size() == 1 && r[0].id < 0 && !form)
			drawHints({{"B", "Back"}, {"X", "Options"}, {tabKey, other}, {"START", "Menu"}});
		else if (browseDir.empty()) drawHints({{"A", "Open"}, {"X", "Options"}, {tabKey, other}, {"START", "Menu"}, {"B\xC3\x97" "2", "Quit"}});
		else drawHints({{"A", browseDir == RECENT ? "Play" : "Open"}, {"B", "Back"}, {"X", "Options"}, {tabKey, other}, {"START", "Menu"}});
	}

	void Ui::drawPlRow(int i, int mid, bool at)
	{
		const std::string& path = browsePaths[i];
		int miss = plMissingAt(i);
		SDL_Color soft = at ? DIM_TEAL : DIM;
		// selecting: its box where its number was, its grip on the right (Move's block: its arrows, yellow)
		bool edit = selMode == SEL_EDIT && !inMenu;
		bool block = edit && ((moving && plSel.count(i)) || (plDrag.on && dragSel.count(i)));
		if (edit) drawGlyphMid(plSel.count(i) ? G_BOX_ON : G_BOX, 40, mid, plSel.count(i) ? YELLOW : soft);
		else drawTextMid(std::to_string(i + 1), 0, soft, 56, mid, 0, 1);
		if (edit) drawGlyphMid(block ? G_MOVE : G_GRIP, 600, mid, block ? YELLOW : soft);
		int right = edit ? 572 : 608;
		// its number in the playlist, its mark (an alert when it isn't there), its name, where it is
		const Glyph& g = miss ? G_ALERT : isAudioFile(path) ? G_NOTE : G_FILM;
		drawGlyphMid(g, 64 + 11, mid, miss ? DIM : at ? YELLOW : CREAM);
		// a missing one says so in its name, "[Gone]" (why: its menu's line), not on the right
		std::string note = miss ? "" : browseRows[i].note;
		int noteW = 0;
		if (!note.empty())
		{
			drawTextMid(note, 0, miss ? RED : soft, right, mid, 220, 1);
			int w = std::min(220, (int)std::lround(textWidth(note, 0) / sx));
			noteW = w + 16;
			// a network item: the server's name, marked as one
			if (!miss && isNetPath(path))
			{
				drawGlyphMid(G_NETWORK, right - w - 19, mid, DIM);
				noteW += 30;
			}
		}
		int x = 96;
		if (miss)
		{
			static const std::string GONE = "[Gone] ";
			drawTextMid(GONE, 1, RED, x, mid);
			x += (int)std::lround(textWidth(GONE, 1) / sx);
		}
		drawTextMid(browseRows[i].label, 1, miss ? soft : at ? YELLOW : CREAM, x, mid, std::max(0, right - 8 - x - noteW));
	}

	void Ui::drawTracksHeader(const Player& p)
	{
		drawBackIcon();
		// the tabs: the open one as START's menu draws its tab; the other with its key on its outer side (Y on the
		// left one, X on the right one, as the buttons sit), its name centred in the rest
		static const char* NAMES[2] = {"Audio", "Subtitles"};
		for (int t = 0; t < 2; t++)
		{
			Rect r = tracksTabRect(t);
			if (t == tracksTab) { drawTab(r, NAMES[t], true); continue; }
			fill(r, TEAL);
			const char* key = t == TRACKS_AUDIO ? "Y" : "X";
			int kw = (int)std::lround(textWidth(key, 0) / sx), mid = r.y + r.h / 2;
			int left = r.x, right = r.x + r.w;
			if (t == TRACKS_AUDIO)
			{
				drawTextMid(key, 0, YELLOW, r.x + 14, mid);
				left = r.x + 14 + kw;
			}
			else
			{
				drawTextMid(key, 0, YELLOW, r.x + r.w - 14, mid, 0, 1);
				right = r.x + r.w - 14 - kw;
			}
			drawTextMid(NAMES[t], 1, CREAM, (left + right) / 2, mid, right - left - 8, 2);
		}
		drawBattery(616, 24, p);
	}

	void Ui::drawChapters(const Player& p)
	{
		drawHeader("Chapters", p);
		int n = (int)p.chapters.size();
		if (n == 0)
		{
			drawEmpty(G_CHAPTERS, "No chapters in this file", "When a file has chapters, L2 and R2 jump between them", nullptr);
			drawHints({{"B", "Back"}});
			return;
		}
		int cur = p.currentChapter(), at = std::min(cursor, n - 1);
		for (int i = scroll; i < n && i < scroll + ROWS_SHOWN; i++)
		{
			int y = ROW_Y + (i - scroll) * ROW_H, mid = y + 22;
			bool focus = i == at, now = i == cur;
			if (focus) drawFocus({16, y, 608, ROW_H - 4});
			SDL_Color soft = focus ? DIM_TEAL : DIM;
			// the one playing: its mark, as the status line has it
			if (now) drawGlyphMid(p.paused ? G_PAUSE : G_PLAY, 40, mid, YELLOW);
			drawTextMid(std::to_string(i + 1), 0, now ? YELLOW : soft, 90, mid, 0, 1);
			const Chapter& c = p.chapters[i];
			std::string start = formatTime(c.time);
			drawTextMid(start, 0, soft, 608, mid, 0, 1);
			int timeW = (int)std::lround(textWidth(start, 0) / sx);
			int maxW = 608 - 16 - timeW - 104;
			if (chapterUntitled(c.title)) drawTextMid(chapterLabel(c, i + 1), 1, soft, 104, mid, maxW);
			else drawTextMid(c.title, 1, focus ? YELLOW : CREAM, 104, mid, maxW);
			if (now)
			{
				// how far into it: up to the next one's start, or the file's end
				double end = i + 1 < n ? p.chapters[i + 1].time : p.duration;
				double f = end > c.time && p.position >= 0 ? std::clamp((p.position - c.time) / (end - c.time), 0.0, 1.0) : 0;
				fill({104, y + 37, 504, 3}, focus ? BG : SCREEN);
				fill({104, y + 37, (int)(504 * f), 3}, YELLOW);
			}
		}
		drawScrollBar(ROW_Y, ROWS_SHOWN * ROW_H - 4, scroll, ROWS_SHOWN, n);
		drawHints({{"A", "Play from here"}, {"L2/R2", "Prev/Next"}, {"B", "Back"}});
	}

	void Ui::readInfo(const Player& p)
	{
		setInfoLines(p.mediaInfo());
	}

	void Ui::openAbout(const Player& p)
	{
		SDL_version sdl;
		SDL_GetVersion(&sdl);
		char sdlVersion[32];
		snprintf(sdlVersion, sizeof sdlVersion, "%d.%d.%d", sdl.major, sdl.minor, sdl.patch);
		std::string mpv = p.property("mpv-version");
		if (mpv.rfind("mpv ", 0) == 0) mpv = mpv.substr(4);
		setInfoLines({
			{"Banana Player", "", true},
			{"Version", BP_VERSION},
			{"Made by", "tonywei92"},
			{"Source", "https://github.com/tonywei92/banana-player"},
			{"License", "MIT: copies and forks keep the copyright notice and the licence (LICENSE)"},
			{"Plays", "Video and music on BananaOS for the Anbernic RG DS and RG DS Plus"},
			{"Built with", "", true},
			{"mpv", mpv},
			{"FFmpeg", p.property("ffmpeg-version")},
			{"SDL", sdlVersion},
			{"Fonts", "", true},
			{"Pixelify Sans", "SIL Open Font License 1.1"},
			{"Inter", "SIL Open Font License 1.1"},
			{"Fusion Pixel", "SIL Open Font License 1.1"},
			{"Thanks", "", true},
			{"BananaOS", "https://github.com/JorreFog/BananaLinux"},
		});
		page = Page::About;
		scroll = 0;
	}

	void Ui::setInfoLines(const std::vector<Player::InfoRow>& rows)
	{
		infoLines.clear();
		int maxW = (int)((W - INFO_VALUE_X - 24) * sx);
		for (auto& r : rows)
		{
			if (r.header) { infoLines.push_back({r.label, "", true}); continue; }
			// a long value (a folder, a codec's full name) over a few lines: broken after a '/' or a space where it can
			std::string rest = r.value, label = r.label;
			for (int line = 0; !rest.empty() && line < 4; line++)
			{
				size_t fit = rest.size();
				int w = 0;
				TTF_SizeUTF8(fonts[0], rest.c_str(), &w, nullptr);
				if (w > maxW)
				{
					// the longest start that fits, whole UTF-8 characters
					size_t lo = 1;
					for (size_t i = 1; i <= rest.size(); i++)
					{
						if (i < rest.size() && ((unsigned char)rest[i] & 0xC0) == 0x80) continue;
						TTF_SizeUTF8(fonts[0], rest.substr(0, i).c_str(), &w, nullptr);
						if (w > maxW) break;
						lo = i;
					}
					fit = lo;
					size_t brk = rest.find_last_of("/ ", fit - 1);
					if (brk != std::string::npos && brk > fit / 2) fit = brk + 1;
				}
				infoLines.push_back({label, rest.substr(0, fit), false});
				rest = rest.substr(fit);
				label.clear();
			}
		}
		infoAt = SDL_GetTicks();
	}

	void Ui::drawInfo(const Player& p)
	{
		if (!inMenu) drawHeader("Info", p);
		for (int i = scroll; i < (int)infoLines.size() && i < scroll + INFO_SHOWN; i++)
		{
			int y = INFO_Y + (i - scroll) * INFO_H, mid = y + INFO_H / 2;
			const InfoLine& l = infoLines[i];
			if (l.header) drawTextMid(l.label, 1, YELLOW, 24, mid);
			else
			{
				drawTextMid(l.label, 0, DIM, 32, mid, INFO_VALUE_X - 40);
				drawTextMid(l.text, 0, CREAM, INFO_VALUE_X, mid);
			}
		}
		drawScrollBar(INFO_Y, INFO_SHOWN * INFO_H, scroll, INFO_SHOWN, (int)infoLines.size());
		if (inMenu) drawHints({{"UP/DOWN", "Scroll"}, {"B", "Close"}, {"L/R", "Tabs"}});
		else drawHints({{"UP/DOWN", "Scroll"}, {"A/B", "Back"}});
	}

	void Ui::drawSettings(const Player& p)
	{
		if (setGroup < 0)
		{
			// the groups as tiles, two to a row: a mark, a name, what's in it
			static const Glyph* MARKS[GROUPS] = {&G_CC, &G_NOTE, &G_PICTURE, &G_BOOKMARK, &G_FOLDER, &G_SUN, &G_DOWNLOAD};
			for (int g = 0; g < GROUPS; g++)
			{
				Rect t = groupRect(g);
				bool at = g == cursor;
				fill(t, pressedBtn == 400 + g ? PRESSED : at ? TEAL : SURFACE);
				if (at)
				{
					fill({t.x, t.y, t.w, 2}, YELLOW);
					fill({t.x, t.y + t.h - 2, t.w, 2}, YELLOW);
					fill({t.x, t.y, 2, t.h}, YELLOW);
					fill({t.x + t.w - 2, t.y, 2, t.h}, YELLOW);
				}
				drawGlyphMid(*MARKS[g], t.x + 34, t.y + t.h / 2, at ? YELLOW : CREAM, 3);
				drawTextMid(GROUP_NAMES[g].name, 1, at ? YELLOW : CREAM, t.x + 66, t.y + 27, t.w - 76);
				drawTextMid(GROUP_NAMES[g].about, 0, at ? DIM_TEAL : DIM, t.x + 66, t.y + 52, t.w - 76);
			}
			drawHints({{"D-PAD", "Move"}, {"A", "Open"}, {"B", "Close"}, {"L/R", "Tabs"}});
			return;
		}
		// the group in the middle, the ones before and after it either side: a tap (or X / Y) away
		static const char* SHORT[GROUPS] = {"Subtitles", "Music", "Photos", "Remember", "Files", "Controls", "Updates"};
		drawTextMid(GROUP_NAMES[setGroup].name, 1, YELLOW, W / 2, 73, 248, 2);
		for (int k = 0; k < 2; k++)
		{
			Rect c = buttonRect(k ? GROUP_NEXT : GROUP_PREV);
			int g = (setGroup + (k ? 1 : GROUPS - 1)) % GROUPS;
			fill(c, pressedBtn == (k ? GROUP_NEXT : GROUP_PREV) ? PRESSED : SURFACE);
			const char* key = k ? "X" : "Y";	// Y is on the left of the buttons, as its chip is
			int kw = (int)std::lround(textWidth(key, 0) / sx);
			if (k)
			{
				drawTextMid(key, 0, YELLOW, c.x + c.w - 10 - kw, c.y + c.h / 2);
				drawGlyphMid(G_RIGHT, c.x + c.w - 24 - kw, c.y + c.h / 2, CREAM);
				drawTextMid(SHORT[g], 0, CREAM, c.x + 12, c.y + c.h / 2, c.w - 44 - kw);
			}
			else
			{
				drawTextMid(key, 0, YELLOW, c.x + 10, c.y + c.h / 2);
				drawGlyphMid(G_LEFT, c.x + 24 + kw, c.y + c.h / 2, CREAM);
				drawTextMid(SHORT[g], 0, CREAM, c.x + c.w - 12, c.y + c.h / 2, c.w - 44 - kw, 1);
			}
		}
		auto rows = settingRows();
		for (int i = scroll; i < (int)rows.size() && i < scroll + SET_SHOWN; i++)
		{
			int row = rows[i];
			int y = SET_Y + (i - scroll) * SET_H;
			bool at = i == cursor;
			int mid = y + (SET_H - 4) / 2;
			if (at) drawFocus({16, y, 608, SET_H - 4});
			std::string label, value;
			int k = row - STYLE_ROW0;	// a style row's key
			if (row == CHECK_ROW || row == INSTALL_ROW)
			{
				// actions with a state: no arrows
				std::string label = "Check for updates", value = updateStatus();
				if (row == INSTALL_ROW)
				{
					label = "Update to " + updater->available();
					int pct = updater->progress();
					value = updater->state() == Updater::State::Ready ? "Restart now"
						: updater->state() == Updater::State::Downloading ? (pct >= 0 ? std::to_string(pct) + "%" : "Downloading")
						: updater->state() == Updater::State::Failed ? "Try again" : "Download";
				}
				drawTextMid(label, 1, at ? YELLOW : CREAM, 32, mid, 320);
				drawTextMid(value, 1, at ? YELLOW : DIM_TEAL, 608, mid, 260, 1);
				continue;
			}
			if (row == SOUND_ROW)
			{
				// a page of its own: what it's set to, and a chevron (no arrows: nothing to step)
				drawTextMid("Sound", 1, at ? YELLOW : CREAM, 32, mid, 320);
				drawTextMid(soundSummary(p.settings), 1, at ? YELLOW : p.settings.eqOn || p.settings.soundFx != SoundFx::None ? CREAM : DIM,
					584, mid, 240, 1);
				drawGlyphMid(G_RIGHT, 604, mid, at ? YELLOW : DIM);
				continue;
			}
			if (row == CLEAR_ROW)
			{
				// an action, not a value: no arrows
				size_t n = p.history().items().size();
				drawTextMid("Clear play history", 1, at ? YELLOW : CREAM, 32, mid, 320);
				drawTextMid(clearArmed ? "Press again to clear" : n ? std::to_string(n) + (n == 1 ? " file" : " files") : "Empty", 1,
					clearArmed ? YELLOW : at ? DIM_TEAL : DIM, 608, mid, 0, 1);
				continue;
			}
			if (k >= 0 && k < STYLE_KEYS)
			{
				label = subStyleSettings()[k].label;
				value = styleValueLabel(p.settings.style, (StyleKey)k);
			}
			else if (row == MUSIC_ROW)
			{
				label = "Music display";
				value = musicShowLabel(p.settings.musicShow);
			}
			else if (row == VIS_ROW)
			{
				label = "Visualizer";
				value = visualizerLabel(p.settings.visualizer);
			}
			else if (row == TOP_LIGHT_ROW || row == BOTTOM_LIGHT_ROW)
			{
				label = row == BOTTOM_LIGHT_ROW ? "Bottom brightness" : sameBrightness ? "Brightness" : "Top brightness";
				int pm = backlight.permille(row == TOP_LIGHT_ROW ? Backlight::TOP : Backlight::BOTTOM);
				value = pm < 0 ? "-" : std::to_string((pm + 5) / 10) + "%";
			}
			else if (row == PAN_ROW)
			{
				label = "Pan area";
				value = p.settings.panRight ? "Right" : "Left";
			}
			else if (row == DRAG_ROW)
			{
				label = "Drag moves";
				value = p.settings.dragBox ? "The box" : "The photo";
			}
			else
			{
				label = REMEMBER_LABELS[row - TOGGLE_ROW];
				value = p.settings.*REMEMBER[row - TOGGLE_ROW] ? "On" : "Off";
			}
			drawTextMid(label, 1, at ? YELLOW : CREAM, 32, mid, 330);
			// arrows on every row: they are where a finger steps back and on
			drawTextMid("<", 1, at ? YELLOW : DIM, SET_ARROW_X, mid);
			drawTextMid(value, 1, at ? YELLOW : CREAM, 490, mid, 196, 2);
			drawTextMid(">", 1, at ? YELLOW : DIM, 608, mid, 0, 1);
		}
		// what the row under the cursor does, in a line or two above the footer
		std::string help = cursor >= 0 && cursor < (int)rows.size() ? settingHelp(rows[cursor]) : "";
		SDL_Color helpInk = DIM;
		if (setGroup == GROUP_SUBS)
		{
			Rect box{24, SET_Y + (int)rows.size() * SET_H + 8, 592, 56};
			drawPreview(p.settings.style, box);
			const Track* s = p.selected(TrackType::Sub);
			if (s && (s->codec == "ass" || s->codec == "ssa"))
			{
				help = "This video's subtitles are styled (ASS): they keep their own look";
				helpInk = YELLOW;
			}
		}
		if (setGroup == GROUP_UPDATES && updater && cursor < (int)rows.size() && (rows[cursor] == CHECK_ROW || rows[cursor] == INSTALL_ROW))
		{
			help = updater->fromStore() ? "Installed from the BananaOS Store: the Store updates it"
				: updater->state() == Updater::State::Off ? "Updates are for the handheld's Ports install"
				: updater->state() == Updater::State::Failed ? updater->message()
				: updater->state() == Updater::State::Ready ? "Restart now, or it's put in place at the next start"
				: "Downloads from GitHub, checked before anything changes";
			helpInk = updater->state() == Updater::State::Failed ? YELLOW : DIM;
		}
		auto lines = wrapText(help, 0, 592, 2);
		for (size_t i = 0; i < lines.size(); i++) drawTextMid(lines[i], 0, helpInk, 24, HELP_Y + (int)i * 22, 592);
		bool action = cursor < (int)rows.size() && (rows[cursor] == CHECK_ROW || rows[cursor] == INSTALL_ROW || rows[cursor] == CLEAR_ROW ||
			rows[cursor] == SOUND_ROW);
		if (action)
			drawHints({{"A", rows[cursor] == CLEAR_ROW ? "Clear" : rows[cursor] == CHECK_ROW ? "Check" : rows[cursor] == SOUND_ROW ? "Open" : "Update"},
				{"UP/DOWN", "Choose"}, {"Y/X", "Group"}, {"B", "Back"}});
		else drawHints({{"UP/DOWN", "Choose"}, {"LEFT/RIGHT", "Change"}, {"Y/X", "Group"}, {"B", "Back"}});
	}

	// a line in the chosen font, colour and border over a stand-in for the picture, at the size it has on the top
	// panel (mpv's sizes are in pixels of a 720-line picture)
	void Ui::drawPreview(const SubStyle& st, Rect box)
	{
		fill(box, SCREEN);
		SubStyleLook look = subStyleLook(st);
		double toPanel = H / 720.0 * sy;
		std::string path = fontsDir + "/" + look.fontFile;
		TTF_Font* f = TTF_OpenFont(path.c_str(), std::max(8, (int)std::lround(look.size * toPanel)));
		if (!f) return;
		const char* sample = "Subtitles look like this";
		SDL_Color fg{(Uint8)(look.color >> 16), (Uint8)(look.color >> 8), (Uint8)look.color, 255};
		SDL_Color bc{(Uint8)(look.borderColor >> 16), (Uint8)(look.borderColor >> 8), (Uint8)look.borderColor, 255};
		int outline = look.border > 0 ? std::max(1, (int)std::lround(look.border * toPanel)) : 0;
		SDL_Surface* text = TTF_RenderUTF8_Blended(f, sample, fg);
		SDL_Surface* edge = nullptr;
		if (outline)
		{
			TTF_SetFontOutline(f, outline);
			edge = TTF_RenderUTF8_Blended(f, sample, bc);
		}
		int cx = (int)((box.x + box.w / 2) * sx), cy = (int)((box.y + box.h / 2) * sy);
		SDL_Rect clip{(int)(box.x * sx), (int)(box.y * sy), (int)(box.w * sx), (int)(box.h * sy)};
		SDL_RenderSetClipRect(ren, &clip);
		for (SDL_Surface* surf : {edge, text})
		{
			if (!surf) continue;
			SDL_Texture* t = SDL_CreateTextureFromSurface(ren, surf);
			SDL_Rect dst{cx - surf->w / 2, cy - surf->h / 2, surf->w, surf->h};
			SDL_RenderCopy(ren, t, nullptr, &dst);
			SDL_DestroyTexture(t);
			SDL_FreeSurface(surf);
		}
		SDL_RenderSetClipRect(ren, nullptr);
		TTF_CloseFont(f);
	}

	void Ui::drawPhoto(const Player& p)
	{
		std::string name = p.currentPath().substr(p.currentPath().rfind('/') + 1);
		drawHeader(!p.title.empty() ? p.title : !p.error.empty() ? name : "Opening...", p);
		Rect pan = panRect(p);
		fill(pan, SCREEN);
		PhotoScale sc = p.photoScaleNow();
		const Player::PhotoView& v = p.photo();
		if (!p.error.empty())
		{
			drawText("This photo can't be shown:", 0, RED, pan.x + 12, pan.y + 12, pan.w - 24);
			drawText(p.error, 0, RED, pan.x + 12, pan.y + 40, pan.w - 24);
		}
		else if (p.thumbW > 0)
		{
			if (photoTexId != p.thumbId)
			{
				if (photoTex) SDL_DestroyTexture(photoTex);
				photoTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STATIC, p.thumbW, p.thumbH);
				if (photoTex) SDL_UpdateTexture(photoTex, nullptr, p.thumb.data(), p.thumbW * 4);
				photoTexId = p.thumbId;
			}
			// the thumbnail as the photo is shown: turned about its middle, in the box it fills turned
			Rect tr = thumbRect(p);
			int turn = p.thumbTurn();
			bool across = turn % 180 == 90;
			float cx = (tr.x + tr.w / 2.f) * sx, cy = (tr.y + tr.h / 2.f) * sy;
			float w = (across ? tr.h * sy : tr.w * sx), h = (across ? tr.w * sx : tr.h * sy);
			SDL_Rect dst{(int)std::lround(cx - w / 2), (int)std::lround(cy - h / 2), (int)std::lround(w), (int)std::lround(h)};
			if (photoTex) SDL_RenderCopyEx(ren, photoTex, nullptr, &dst, turn, nullptr, SDL_FLIP_NONE);
			// the part on the top panel, boxed (all of it shown: no box)
			if (sc.x > 1.001 || sc.y > 1.001)
			{
				double hx = std::min(0.5, 0.5 / sc.x), hy = std::min(0.5, 0.5 / sc.y);
				int bx = tr.x + (int)std::lround(tr.w * (0.5 - v.panX - hx)), by = tr.y + (int)std::lround(tr.h * (0.5 - v.panY - hy));
				int bw = std::max(4, (int)std::lround(tr.w * 2 * hx)), bh = std::max(4, (int)std::lround(tr.h * 2 * hy));
				fill({bx, by, bw, 2}, YELLOW);
				fill({bx, by + bh - 2, bw, 2}, YELLOW);
				fill({bx, by, 2, bh}, YELLOW);
				fill({bx + bw - 2, by, 2, bh}, YELLOW);
			}
		}
		else if (p.thumbPending()) drawText("Loading" + std::string(SDL_GetTicks() / 400 % 4, '.'), 1, DIM, pan.x + pan.w / 2, pan.y + pan.h / 2 - 12, 0, 2);
		else drawText("No preview", 1, DIM, pan.x + pan.w / 2, pan.y + pan.h / 2 - 12, 0, 2);

		// under the pan area: the zoom and the size; how to use it
		std::string zoom = "Zoom " + std::to_string((int)std::lround(v.zoom * 100)) + "%";
		if (p.photoWidth() > 0) zoom += "   " + std::to_string(p.photoWidth()) + " x " + std::to_string(p.photoHeight());
		drawText(zoom, 0, CREAM, pan.x, pan.y + pan.h + 10, pan.w);
		drawText(maxPan(sc.x) > 0 || maxPan(sc.y) > 0 ? (p.settings.dragBox ? "Drag the box · double-tap: zoom out" : "Drag to move · double-tap: zoom out")
			: "Double-tap: zoom in",
			0, DIM, pan.x, pan.y + pan.h + 36, pan.w);

		bool hints = p.settings.buttonHotkeys;
		struct { const char* label; const char* hint; bool off; } btns[PHOTO_BTNS] = {
			{"Prev", "L", p.index() == 0},
			{"Next", "R", p.index() + 1 >= p.count()},
			{"Zoom -", "DOWN", v.zoom <= 1.001},
			{"Zoom +", "UP", v.zoom >= MAX_ZOOM - 0.001},
			{"Rotate", "L3", false},
			{v.fill ? "Fill" : "Fit", "SELECT", false},
			{"Info", "", false},
			{"Menu", "START", false},
		};
		for (int i = 0; i < PHOTO_BTNS; i++)
		{
			Rect b = photoBtn(i, p);
			fill(b, pressedBtn == PHOTO_BTN + i ? PRESSED : TEAL);
			bool hint = hints && *btns[i].hint;
			drawText(btns[i].label, 1, btns[i].off ? DIM : CREAM, b.x + b.w / 2, b.y + (hint ? 5 : 15), b.w - 6, 2);
			if (hint) drawText(btns[i].hint, 0, btns[i].off ? DIM : YELLOW, b.x + b.w / 2, b.y + 33, b.w - 6, 2);
		}
		if (p.count() > 1)
		{
			Rect last = photoBtn(PHOTO_BTNS - 1, p);
			drawText(std::to_string(p.index() + 1) + " / " + std::to_string(p.count()), 0, DIM, last.x + last.w, last.y + last.h + 12, 0, 1);
		}

		drawLeave(p);
		// along the bottom, beside it: what A does here, or what a second B would do
		const char* leave = browserMode ? "close" : "quit";
		if (quitArmed) drawTextMid(std::string(hints ? "Press B again to " : "Tap again to ") + leave, 0, YELLOW, 24, 450, 430);
		else if (hints) drawTextMid("A", 0, YELLOW, 24, 450), drawTextMid("Zoom to 2x, or the whole photo", 0, DIM, 24 + (int)std::lround(textWidth("A", 0) / sx) + 7, 450, 400);
	}

	std::vector<std::string> Ui::wrapText(const std::string& text, int size, int maxW, int maxLines) const
	{
		// at spaces where it can, the last line cut with "..." when there's more
		std::vector<std::string> lines;
		std::string rest = text;
		int limit = (int)(maxW * sx);
		while (!rest.empty() && (int)lines.size() < maxLines)
		{
			if (textWidth(rest, size) <= limit || (int)lines.size() == maxLines - 1) { lines.push_back(rest); break; }
			size_t fit = 0;
			for (size_t i = 1; i <= rest.size(); i++)
			{
				if (i < rest.size() && ((unsigned char)rest[i] & 0xC0) == 0x80) continue;
				if (textWidth(rest.substr(0, i), size) > limit) break;
				fit = i;
			}
			if (fit == 0) fit = 1;
			size_t brk = rest.find_last_of(' ', fit);
			if (brk != std::string::npos && brk > fit / 2) fit = brk + 1;
			std::string line = rest.substr(0, fit);
			while (!line.empty() && line.back() == ' ') line.pop_back();
			lines.push_back(line);
			rest = rest.substr(fit);
		}
		return lines;
	}

	void Ui::drawButton(Rect b, const Glyph* g, const std::string& label, const char* key, bool off, int size)
	{
		// a label (a glyph before it), the button that does the same under it in yellow (Button hotkeys on)
		bool hint = key && *key;
		int mid = hint ? b.y + b.h * 2 / 5 : b.y + b.h / 2;
		SDL_Color ink = off ? DIM : CREAM;
		int tw = label.empty() ? 0 : std::min(b.w - 12, (int)std::lround(textWidth(label, size) / sx));
		int gw = g ? glyphW(*g) : 0, gap = g && tw ? 8 : 0;
		int x = b.x + b.w / 2 - (gw + gap + tw) / 2;
		if (g) drawGlyphMid(*g, x + gw / 2, mid, ink);
		if (tw) drawTextMid(label, size, ink, x + gw + gap, mid, b.w - 12 - gw - gap);
		if (hint) drawTextMid(key, 0, off ? DIM : YELLOW, b.x + b.w / 2, b.y + b.h * 4 / 5 - 2, b.w - 6, 2);
	}

	bool Ui::chapterLineShown(const Player& p) const
	{
		// before the first chapter starts there's none to name (and nothing to tap)
		return p.playing() && !p.playingPhoto() && !p.loading() && p.error.empty() && !p.chapters.empty() &&
			p.currentChapter() >= 0 && p.currentChapter() < (int)p.chapters.size();
	}

	void Ui::drawMain(const Player& p)
	{
		if (p.playingPhoto()) { drawPhoto(p); return; }
		std::string name = p.currentPath().substr(p.currentPath().rfind('/') + 1);
		bool err = !p.error.empty();
		drawHeader(!p.title.empty() ? p.title : err ? name : "Opening...", p);
		if (roots.empty()) roots = browseRoots();	// (started with a file: the browser never read them)
		drawSource(p);
		const char* leave = browserMode ? "close" : "quit";
		bool hotkeys = p.settings.buttonHotkeys;
		// the status line: how it plays (or what went wrong), and on its right the count or what a press does now
		std::string right = p.count() > 1 ? std::to_string(p.index() + 1) + " / " + std::to_string(p.count()) : "";
		SDL_Color rightInk = DIM;
		if (quitArmed) { right = std::string(hotkeys ? "Press B again to " : "Tap again to ") + leave; rightInk = YELLOW; }
		else if (touchLocked) { right = "Hold the lock 1 s to unlock"; rightInk = YELLOW; }
		if (err)
		{
			drawGlyphMid(G_ALERT, 35, STATUS_Y, RED);
			drawTextMid("Can't play this file", 1, RED, 56, STATUS_Y, 300);
			auto lines = wrapText(p.error, 0, 592, 2);
			for (size_t i = 0; i < lines.size(); i++) drawTextMid(lines[i], 0, CREAM, 24, STATUS_Y + 32 + (int)i * 24, 592);
		}
		else
		{
			std::string status = p.loading() ? "Loading" + std::string(SDL_GetTicks() / 400 % 4, '.') : p.paused ? "Paused" : "Playing";
			if (p.source().shuffled) status += " \xC2\xB7 Shuffle";
			if (std::fabs(p.speed - 1) > 1e-6) status += " \xC2\xB7 " + speedLabel(p.speed);
			if (!hotkeys) status += " \xC2\xB7 buttons off";
			int x = 24;
			if (!p.loading())
			{
				drawGlyphMid(p.paused ? G_PAUSE : G_PLAY, 32, STATUS_Y, YELLOW);
				x = 52;
			}
			drawTextMid(status, 1, YELLOW, x, STATUS_Y, 330);

			// the chapter playing, between the status line and the bar: its title (the Y button has its number), or
			// "Chapter 3 of 12" dim when it has none. A tap opens the list
			if (chapterLineShown(p))
			{
				int c = p.currentChapter(), n = (int)p.chapters.size();
				if (pressedBtn == CHAPTER_BTN) fill(buttonRect(CHAPTER_BTN), PRESSED);
				drawGlyphMid(G_CHAPTERS, 35, CHAPTER_Y, DIM);
				if (chapterUntitled(p.chapters[c].title))
					drawTextMid("Chapter " + std::to_string(c + 1) + " of " + std::to_string(n), 0, DIM, 52, CHAPTER_Y, 540);
				else drawTextMid(p.chapters[c].title, 0, CREAM, 52, CHAPTER_Y, 540);
				drawGlyphMid(G_RIGHT, 604, CHAPTER_Y, DIM);
			}

			double frac = dragging ? dragFraction : p.duration > 0 && p.position >= 0 ? std::clamp(p.position / p.duration, 0.0, 1.0) : 0;
			fill(BAR, TEAL);
			fill({BAR.x, BAR.y, (int)(BAR.w * frac), BAR.h}, YELLOW);
			// where each chapter starts: a notch through the bar (none too close to the last, none at all past 100:
			// they'd be a comb)
			if (p.duration > 0 && p.chapters.size() <= 100)
			{
				int last = -1000;
				for (auto& c : p.chapters)
				{
					if (c.time <= 0) continue;
					int x = BAR.x + (int)(BAR.w * std::min(1.0, c.time / p.duration));
					if (x - last < 6) continue;
					fill({x - 1, BAR.y, 2, BAR.h}, BG);
					last = x;
				}
			}
			fill({BAR.x + (int)(BAR.w * frac) - 4, BAR.y - 6, 8, BAR.h + 12}, CREAM);
			drawText(formatTime(dragging ? dragFraction * p.duration : p.position), 1, CREAM, BAR.x, BAR.y + 32);
			drawText(formatTime(p.duration), 1, DIM, BAR.x + BAR.w, BAR.y + 32, 0, 1);
			// how long is left, at the speed it plays (half at 2x)
			double left = timeLeft(dragging ? dragFraction * p.duration : p.position, p.duration, p.speed);
			// dragging the bar of a file with chapters: the chapter under the finger instead ("3 · The Dragon")
			int under = -1;
			if (dragging && p.duration > 0)
				for (int i = 0; i < (int)p.chapters.size(); i++)
					if (p.chapters[i].time <= dragFraction * p.duration) under = i;
			if (under >= 0)
				drawText(std::to_string(under + 1) + " \xC2\xB7 " + chapterLabel(p.chapters[under], under + 1), 0, CREAM,
					BAR.x + BAR.w / 2, BAR.y + 36, 300, 2);
			else if (left >= 0) drawText(formatTime(left) + " left", 0, DIM, BAR.x + BAR.w / 2, BAR.y + 36, 0, 2);
		}
		if (!right.empty()) drawTextMid(right, 0, rightInk, 616, STATUS_Y, 250, 1);
		// a file with chapters, the buttons on: what L2/R2 do, left of the count (a message in yellow takes the place)
		if (!err && !p.chapters.empty() && hotkeys && !quitArmed && !touchLocked)
		{
			int x = 616;
			if (!right.empty())
			{
				x -= (int)std::lround(textWidth(right, 0) / sx) + 12;
				fill({x, STATUS_Y - 8, 1, 16}, SCREEN);
				x -= 12;
			}
			int vw = (int)std::lround(textWidth(" Chapters", 0) / sx);
			drawTextMid(" Chapters", 0, DIM, x, STATUS_Y, 0, 1);
			drawTextMid("L2/R2", 0, YELLOW, x - vw, STATUS_Y, 0, 1);
		}

		for (int r = 0; r < 2; r++)
			for (int c = 0; c < 5; c++)
			{
				Rect b{24 + c * BTN_STEP, r ? ROW2_Y : ROW1_Y, BTN_W, BTN_H};
				Button which = GRID[r][c].button;
				std::string label;
				const Glyph* g = nullptr;
				bool off = false;
				switch (which)
				{
				case Button::L: label = "Prev"; g = &G_PREV; off = p.index() == 0; break;
				case Button::Left: label = "-5s"; break;
				case Button::A: label = p.paused || err ? "Play" : "Pause"; g = p.paused || err ? &G_PLAY : &G_PAUSE; break;
				case Button::Right: label = "+10s"; break;
				case Button::R: label = "Next"; g = &G_NEXT; off = p.index() + 1 >= p.count(); break;
				// music: loop, Sound (or chapters), the Display popup; a video: audio, subtitles, picture size
				case Button::X:
					if (p.playingAudio()) { label = LOOP_SHORT[std::clamp(p.settings.loop, 0, LOOPS - 1)]; g = &G_LOOP; }
					break;
				case Button::Select:
					if (!p.playingAudio()) { label = aspectName(p.aspect); break; }
					label = SHOW_SHORT[std::clamp(p.settings.musicShow, 0, SHOWS - 1)];
					g = showGlyph(p.settings.musicShow, p.settings.visualizer);
					break;
				case Button::Up: label = speedLabel(p.speed); break;
				default: label = "Menu"; break;
				}
				// a file that didn't open: only what leads elsewhere (the next file, the menu)
				if (err && which != Button::L && which != Button::R && which != Button::Start) off = true;
				fill(b, pressedBtn == r * 5 + c && !off ? PRESSED : TEAL);
				const char* key = hotkeys ? GRID[r][c].hint : nullptr;
				if (which == Button::X && !p.playingAudio())
				{
					// a video's Audio & Subtitles: their two marks, no words
					SDL_Color ink = off ? DIM : CREAM;
					drawGlyphMid(G_SPEAKER, b.x + 40, b.y + 24, ink);
					drawGlyphMid(G_CC, b.x + 66, b.y + 24, ink);
					if (key) drawTextMid(key, 0, off ? DIM : YELLOW, b.x + b.w / 2, b.y + b.h * 4 / 5 - 2, b.w - 6, 2);
				}
				else if (which == Button::Y && (!p.playingAudio() || !p.chapters.empty()))
				{
					// Chapters: the one playing of how many ("3/12"; "3" when that's too wide, "–/12" before the first).
					// None (or not known yet) dims it, though it still opens the page that says so
					int n = (int)p.chapters.size(), c = p.currentChapter();
					std::string num;
					if (n > 0)
					{
						num = (c >= 0 ? std::to_string(c + 1) : std::string("\xE2\x80\x93")) + "/" + std::to_string(n);
						if (textWidth(num, 1) / sx > 66 && c >= 0) num = std::to_string(c + 1);
					}
					drawButton(b, &G_CHAPTERS, num, key, off || n == 0, 1);
				}
				else if (which == Button::Y)
				{
					// a song with no chapters: Sound, by touch only (no key: Y does nothing here). "On" while the
					// equalizer or an effect is; both off, all of it dim
					bool on = (p.settings.eqOn || p.settings.soundFx != SoundFx::None) && !off;
					drawButton({b.x, b.y, b.w, b.h * 4 / 5}, &G_EQ, on ? "On" : "Off", nullptr, !on, 1);
					drawTextMid("Sound", 0, on ? DIM_TEAL : DIM, b.x + b.w / 2, b.y + b.h * 4 / 5 - 2, b.w - 6, 2);
				}
				// the button it stands for, while the buttons work here (Button hotkeys); else the name alone, centred
				else drawButton(b, g, label, key, off, 1);
			}

		const Track* a = p.selected(TrackType::Audio);
		const Track* s = p.selected(TrackType::Sub);
		auto label = [&](const Track* t, TrackType type) {
			if (!t) return std::string("Off");
			auto list = p.tracksOf(type);
			int n = 1;
			for (size_t i = 0; i < list.size(); i++)
				if (list[i].id == t->id) n = (int)i + 1;
			return trackLabel(*t, n);
		};
		// what's chosen, each after its mark: the sound, then the subtitles (a song: its artist and album)
		// a video's lines open their tab of Audio & Subtitles: a chevron says so
		bool video = !p.playingAudio();
		auto lineEnd = [&](int id, int y) {
			if (pressedBtn == id) fill(buttonRect(id), PRESSED);
			drawGlyphMid(G_RIGHT, 604, y, DIM);
		};
		if (!err)
		{
			lineEnd(LINE1_BTN, LINE1_Y);
			int trackW = 540;
			if (!video)
			{
				// a song's line opens Sound: on its right what it's set to ("Rock", "Rock · Hall", "EQ off · Hall",
				// "EQ off"), the effect drawn first and the preset cut to what's left of 180
				const Settings& st = p.settings;
				bool fx = st.soundFx != SoundFx::None;
				auto w = [&](const std::string& t, int maxW) { return std::min(maxW, (int)std::lround(textWidth(t, 0) / sx)); };
				int x = 592;
				if (!st.eqOn && !fx)
				{
					drawTextMid("EQ off", 0, DIM, x, LINE1_Y, 0, 1);
					x -= w("EQ off", 180);
				}
				else
				{
					if (fx)
					{
						std::string tail = std::string(st.eqOn ? " \xC2\xB7 " : "") + FX_SHORT[(int)st.soundFx];
						drawTextMid(tail, 0, CREAM, x, LINE1_Y, 0, 1);
						x -= w(tail, 180);
					}
					if (!st.eqOn)
					{
						drawTextMid("EQ off \xC2\xB7 ", 0, DIM, x, LINE1_Y, 0, 1);
						x -= w("EQ off \xC2\xB7 ", 180);
					}
					else
					{
						std::string preset = st.eqPreset.empty() ? CUSTOM_NAME : st.eqPreset;
						int room = std::max(40, 180 - (592 - x));
						drawTextMid(preset, 0, CREAM, x, LINE1_Y, room, 1);
						x -= w(preset, room);
					}
				}
				drawGlyphMid(G_EQ, x - 8 - glyphW(G_EQ) / 2, LINE1_Y, DIM);
				trackW = x - 8 - glyphW(G_EQ) - 12 - 52;
			}
			drawGlyphMid(G_SPEAKER, 33, LINE1_Y, DIM);
			drawTextMid(label(a, TrackType::Audio), 0, CREAM, 52, LINE1_Y, trackW);
		}
		if (err) {}
		else if (p.playingAudio())
		{
			std::string by = p.artist();
			if (!p.album().empty()) by += (by.empty() ? "" : " \xC2\xB7 ") + p.album();
			if (!by.empty())
			{
				drawGlyphMid(G_NOTE, 33, LINE2_Y, DIM);
				drawTextMid(by, 0, CREAM, 52, LINE2_Y, 564);
			}
		}
		else
		{
			lineEnd(LINE2_BTN, LINE2_Y);
			drawGlyphMid(G_CC, 33, LINE2_Y, DIM);
			drawTextMid(label(s, TrackType::Sub), 0, CREAM, 52, LINE2_Y, 540);
		}

		// the row along the bottom: Lock, Info, Screen off, and leaving
		Rect lk = buttonRect(LOCK_BTN), inf = buttonRect(INFO_BTN), so = buttonRect(SCREEN_BTN), q = buttonRect(QUIT_BTN);
		bool holding = touchLocked && lockHoldAt;
		fill(lk, holding ? PRESSED : touchLocked ? YELLOW : pressedBtn == LOCK_BTN ? PRESSED : TEAL);
		if (holding)
		{
			// the hold, filling as it goes
			double f = std::min(1.0, (SDL_GetTicks() - lockHoldAt) / (double)UNLOCK_HOLD_MS);
			fill({lk.x, lk.y + lk.h - 4, (int)(lk.w * f), 4}, YELLOW);
		}
		SDL_Color lkInk = touchLocked && !holding ? BG : CREAM;
		{
			const char* t = touchLocked ? (holding ? "Hold..." : "Locked") : "Lock";
			int tw = (int)std::lround(textWidth(t, 0) / sx), gw = glyphW(G_LOCK), x = lk.x + lk.w / 2 - (gw + 8 + tw) / 2;
			drawGlyphMid(G_LOCK, x + gw / 2, lk.y + lk.h / 2, lkInk);
			drawTextMid(t, 0, lkInk, x + gw + 8, lk.y + lk.h / 2);
		}
		fill(inf, pressedBtn == INFO_BTN ? PRESSED : TEAL);
		drawButton(inf, &G_INFO, "Info", nullptr, false, 0);
		fill(so, pressedBtn == SCREEN_BTN ? PRESSED : TEAL);
		drawButton(so, &G_MOON, "Screen off", nullptr, false, 0);
		drawLeave(p);
		if (touchLocked)
		{
			// locked: everything but the lock and Screen off dimmed (they're all that a finger does now)
			SDL_Color veil{0x17, 0x16, 0x1C, 170};
			fill({0, 64, W, lk.y - 64}, veil);
			fill(inf, veil);
			fill(q, veil);
		}
	}

	void Ui::drawLeave(const Player& p)
	{
		Rect q = buttonRect(QUIT_BTN);
		bool hotkeys = p.settings.buttonHotkeys;
		fill(q, pressedBtn == QUIT_BTN ? PRESSED : TEAL);
		{
			// leaving: B twice (a press in a pocket isn't one), or this twice
			std::string t = quitArmed ? "Again?" : browserMode ? "Close" : "Quit";
			const Glyph& g = browserMode ? G_CROSS : G_POWER;
			std::string key = hotkeys && !quitArmed ? "B\xC3\x97" "2" : "";
			int tw = (int)std::lround(textWidth(t, 0) / sx), gw = glyphW(g), kw = key.empty() ? 0 : (int)std::lround(textWidth(key, 0) / sx) + 10;
			int x = q.x + q.w / 2 - (gw + 8 + tw + kw) / 2;
			SDL_Color ink = quitArmed ? YELLOW : CREAM;
			drawGlyphMid(g, x + gw / 2, q.y + q.h / 2, ink);
			drawTextMid(t, 0, ink, x + gw + 8, q.y + q.h / 2);
			if (!key.empty()) drawTextMid(key, 0, YELLOW, x + gw + 8 + tw + 10, q.y + q.h / 2);
		}
	}

	// ---- a connection's form ----

	static const char* fieldName(int f)
	{
		switch (f)
		{
		case FORM_NAME: return "Name";
		case FORM_HOST: return "Server";
		case FORM_PORT: return "Port";
		case FORM_SHARE: return "Share";
		case FORM_FOLDER: return "Folder";
		case FORM_USER: return "User";
		case FORM_PASS: return "Password";
		case EQ_NAME: return "Preset name";
		case PL_NAME: return "Playlist name";
		default: return "";
		}
	}

	static std::string trimmed(const std::string& s)
	{
		size_t a = s.find_first_not_of(" \t"), b = s.find_last_not_of(" \t");
		return a == std::string::npos ? "" : s.substr(a, b - a + 1);
	}

	void Ui::openForm(const NetServer* existing, const FoundHost* host, Player& p)
	{
		form = existing ? *existing : NetServer();
		formId = existing ? existing->id : "";
		if (!existing)
		{
			form.type = NetType::Smb;
			form.port = defaultPort(form.type);
			form.guest = false;	// most shares (a Windows PC's) want an account
		}
		// one found on the network: by its name (an address that changes with DHCP would lose it); asked for at
		// each connection (resolveHost)
		if (host) form.host = !host->name.empty() ? host->name : host->ip;
		formPassword.clear();
		formPasswordTyped = false;
		formNote.clear();
		testText.clear();
		testSerial++;	// an answer still coming is for another form
		enterFolder(FORMDIR, p);
		// on the first thing to fill in
		int want = host ? FORM_USER : existing ? FORM_NAME : FORM_HOST;
		for (size_t i = 0; i < browseRows.size(); i++)
			if (browseRows[i].id == want) cursor = (int)i;
		fitList();
	}

	NetServer Ui::formServer() const
	{
		NetServer s = form;
		s.host = trimmed(s.host);
		if (s.port <= 0) s.port = defaultPort(s.type);
		return s;
	}

	void Ui::formRows()
	{
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back("");
		};
		auto value = [](const std::string& v, const std::string& placeholder) { return v.empty() ? std::string(1, DIM_NOTE) + placeholder : v; };
		bool ftp = form.type == NetType::Ftp;
		NetServer s = formServer();
		add(FORM_TYPE, "Type", ftp ? "< FTP >" : "< SMB (Windows, NAS) >");
		add(FORM_NAME, "Name", value(form.name, automaticServerName(s)));
		add(FORM_HOST, "Server", value(form.host, "Address or name"));
		add(FORM_PORT, "Port", form.port > 0 && form.port != defaultPort(form.type) ? std::to_string(form.port) : std::string(1, DIM_NOTE) + std::to_string(defaultPort(form.type)));
		if (ftp) add(FORM_FOLDER, "Folder", value(form.folder, "/"));
		else add(FORM_SHARE, "Share", value(form.share, "Any (choose after)"));
		add(FORM_SIGNIN, "Sign in", form.guest ? (ftp ? "< Anonymous >" : "< As a guest >") : "< With an account >");
		if (!form.guest)
		{
			add(FORM_USER, "User", value(form.user, "User name"));
			std::string pw = formPasswordTyped ? (formPassword.empty() ? std::string(1, DIM_NOTE) + "None" : std::string(std::min<size_t>(formPassword.size(), 12), '*'))
				: !formId.empty() && !form.secret.empty() ? std::string(1, DIM_NOTE) + "Saved" : std::string(1, DIM_NOTE) + "None";
			add(FORM_PASS, "Password", pw);
		}
		if (ftp) add(FORM_FTPS, "Encryption (FTPS)", form.ftps ? "< On >" : "< Off >");
		add(FORM_TEST, "Test connection", testText);
		add(FORM_SAVE, "Save", formNote);
		fitList();
		browseVersion++;
	}

	void Ui::formChange(int64_t id, int)
	{
		if (id == FORM_TYPE)
		{
			// the port follows the type, unless one was typed
			bool usual = form.port <= 0 || form.port == defaultPort(form.type);
			form.type = form.type == NetType::Smb ? NetType::Ftp : NetType::Smb;
			if (usual) form.port = defaultPort(form.type);
		}
		else if (id == FORM_SIGNIN) form.guest = !form.guest;
		else if (id == FORM_FTPS) form.ftps = !form.ftps;
		else return;
		testText.clear();
		formRows();
	}

	void Ui::formChoose(int64_t id, Player& p)
	{
		switch (id)
		{
		case FORM_TYPE: case FORM_SIGNIN: case FORM_FTPS: formChange(id, 1); break;
		case FORM_NAME: openKeyboard(FORM_NAME, form.name); break;
		case FORM_HOST: openKeyboard(FORM_HOST, form.host); break;
		case FORM_PORT: openKeyboard(FORM_PORT, form.port > 0 && form.port != defaultPort(form.type) ? std::to_string(form.port) : ""); break;
		case FORM_SHARE: openKeyboard(FORM_SHARE, form.share); break;
		case FORM_FOLDER: openKeyboard(FORM_FOLDER, form.folder); break;
		case FORM_USER: openKeyboard(FORM_USER, form.user); break;
		case FORM_PASS: openKeyboard(FORM_PASS, formPasswordTyped ? formPassword : ""); break;
		case FORM_TEST:
		{
			NetServer s = formServer();
			if (s.host.empty()) { testText = "Type the server first"; formRows(); break; }
			const NetServer* saved = netStore && !formId.empty() ? netStore->find(formId) : nullptr;
			startTest(s, formPasswordTyped ? formPassword : saved ? netStore->password(*saved) : "");
			formRows();
			break;
		}
		case FORM_SAVE: formSave(p); break;
		default: break;
		}
	}

	void Ui::formSave(Player& p)
	{
		NetServer s = formServer();
		if (s.host.empty())
		{
			formNote = "Type the server first";
			formRows();
			for (size_t i = 0; i < browseRows.size(); i++)
				if (browseRows[i].id == FORM_HOST) cursor = (int)i;
			fitList();
			return;
		}
		if (!netStore) return;
		s.lastUsed = (long long)time(nullptr);	// the top of the list: the one just made
		std::string id = netStore->put(s, formPassword, !formId.empty() && !formPasswordTyped);
		if (netWorker) netWorker->forget(id);	// its kept connection was for what it was
		enterFolder(NETROOT, p);
		for (size_t i = 0; i < browsePaths.size(); i++)
		{
			NetPath np;
			if (parseNetPath(browsePaths[i], np) && np.server == id) cursor = (int)i;
		}
		fitList();
	}

	void Ui::startTest(const NetServer& s, const std::string& password)
	{
		if (!netWorker) { testText = "No network support in this build"; return; }
		testText = "Testing...";
		netWorker->test(++testSerial, s, password);
	}

	// ---- the servers found on the network ----

	void Ui::hostFound(const FoundHost& h, Player&)
	{
		// one per address (a better name comes later for some)
		bool known = false;
		for (auto& f : found)
			if (f.ip == h.ip) { f = h; known = true; }
		if (!known) found.push_back(h);
		if (browseDir == FINDNET && page == Page::Browse) findRows();
	}

	void Ui::findRows()
	{
		// the cursor stays on its host as others come in above it
		std::string at = cursor >= 0 && cursor < (int)found.size() ? found[cursor].ip : "";
		std::sort(found.begin(), found.end(), [](const FoundHost& a, const FoundHost& b) {
			return naturalLess(a.name.empty() ? a.ip : a.name, b.name.empty() ? b.ip : b.name);
		});
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back("");
		};
		for (size_t i = 0; i < found.size(); i++)
		{
			add(R_FOUND, found[i].name.empty() ? found[i].ip : found[i].name, found[i].ip);
			if (found[i].ip == at) cursor = (int)i;
		}
		bool running = discovery && discovery->running();
		if (running) add(-1, "Searching...", "");
		else
		{
			if (found.empty()) add(-1, discovery ? "No servers found" : "No network support in this build", "");
			add(R_SEARCH, "Search again", "");
		}
		add(R_TYPE_ADDRESS, "Type the address instead", "");
		fitList();
		browseVersion++;
	}

	// ---- the on-screen keyboard ----

	std::vector<KeyDef> Ui::kbKeys() const
	{
		return keyboardLayout(kbSymbols, kbShift, 8, 192, 624, 240);
	}

	void Ui::openKeyboard(int field, const std::string& text)
	{
		kbActiveAt = SDL_GetTicks();
		kbOpen = true;
		kbField = field;
		kbEdit.set(text);
		kbSymbols = false;
		kbShift = ShiftState::Off;
		kbCursor = keyboardHome(kbKeys());
		kbPressed = -1;
		pressedBtn = -1;
	}

	void Ui::kbClose(bool keep)
	{
		if (kbField == EQ_NAME)
		{
			// Done: checked by the player first (eqNameDone), which closes it or says what's wrong
			if (keep) { eqNameSubmit = true; return; }
			eqNameNote.clear();
		}
		if (kbField == PL_NAME)
		{
			// the same for a playlist's name (plNameDone); cancelled, a new one for Add to... adds nothing
			if (keep) { plNameSubmit = true; return; }
			plNameNote.clear();
			plNameThenAdd = false;
		}
		kbOpen = false;
		kbPressed = -1;
		if (!keep) { browseVersion++; return; }
		std::string t = kbEdit.text;
		switch (kbField)
		{
		case FORM_NAME: form.name = trimmed(t); break;
		case FORM_HOST: form.host = trimmed(t); break;
		case FORM_PORT: form.port = atoi(t.c_str()); if (form.port <= 0 || form.port > 65535) form.port = defaultPort(form.type); break;
		case FORM_SHARE:
			// "Media", "/Media/", "\\Media": the share's name (a path after it is the start folder)
			for (char& c : t) if (c == '\\') c = '/';
			t = trimmed(t);
			while (!t.empty() && t[0] == '/') t.erase(0, 1);
			while (!t.empty() && t.back() == '/') t.pop_back();
			if (size_t slash = t.find('/'); slash != std::string::npos) { form.folder = t.substr(slash + 1); t = t.substr(0, slash); }
			form.share = t;
			break;
		case FORM_FOLDER: form.folder = trimmed(t); break;
		case FORM_USER: form.user = trimmed(t); break;
		case FORM_PASS: formPassword = t; formPasswordTyped = true; break;	// (spaces kept: they can be a password's)
		default: break;
		}
		formNote.clear();
		testText.clear();
		formRows();
	}

	bool Ui::kbCaretShown() const
	{
		// on for 530 ms, off for 530, from the last key (a caret that's mid-blink as you type looks lost)
		return (SDL_GetTicks() - kbActiveAt) % 1060 < 530;
	}

	void Ui::kbKey(int i)
	{
		auto keys = kbKeys();
		if (i < 0 || i >= (int)keys.size()) return;
		const KeyDef& k = keys[i];
		switch (k.action)
		{
		case KeyAction::Text:
			// a port: digits only
			if (kbField == FORM_PORT && !(k.text.size() == 1 && isdigit((unsigned char)k.text[0]))) break;
			kbEdit.insert(k.text);
			if (kbShift == ShiftState::Once) kbShift = ShiftState::Off;
			break;
		case KeyAction::Shift:
			kbShift = kbShift == ShiftState::Off ? ShiftState::Once : kbShift == ShiftState::Once ? ShiftState::Locked : ShiftState::Off;
			break;
		case KeyAction::Back: kbEdit.backspace(); break;
		case KeyAction::Space: if (kbField != FORM_PORT) kbEdit.insert(" "); break;
		case KeyAction::Page:
			kbSymbols = !kbSymbols;
			kbCursor = std::min(kbCursor, (int)kbKeys().size() - 1);
			break;
		case KeyAction::Done: kbClose(true); break;
		case KeyAction::Left: kbEdit.left(); break;
		case KeyAction::Right: kbEdit.right(); break;
		}
	}

	void Ui::kbPress(Button b)
	{
		kbActiveAt = SDL_GetTicks();
		auto keys = kbKeys();
		switch (b)
		{
		case Button::Up: kbCursor = keyboardMove(keys, kbCursor, 0, -1); break;
		case Button::Down: kbCursor = keyboardMove(keys, kbCursor, 0, 1); break;
		case Button::Left: kbCursor = keyboardMove(keys, kbCursor, -1, 0); break;
		case Button::Right: kbCursor = keyboardMove(keys, kbCursor, 1, 0); break;
		case Button::A: kbKey(kbCursor); break;
		// B deletes; with nothing left to delete, it closes (what was there before is kept)
		case Button::B:
			if (kbEdit.text.empty()) kbClose(false);
			else kbEdit.backspace();
			break;
		case Button::X: if (kbField != FORM_PORT) kbEdit.insert(" "); break;
		case Button::Y: kbShift = kbShift == ShiftState::Off ? ShiftState::Once : ShiftState::Off; break;
		case Button::L: kbEdit.left(); break;
		case Button::R: kbEdit.right(); break;
		case Button::Select:
			kbSymbols = !kbSymbols;
			kbCursor = std::min(kbCursor, (int)kbKeys().size() - 1);
			break;
		case Button::Start: kbClose(true); break;
		default: break;
		}
	}

	void Ui::kbPointer(const Pointer& pt, int x, int y)
	{
		if (pt.kind == Pointer::Down) kbActiveAt = SDL_GetTicks();
		auto keys = kbKeys();
		Rect back = buttonRect(BACK_ICON);
		if (pt.kind == Pointer::Down)
		{
			kbPressed = back.hit(x, y) ? -2 : keyboardHit(keys, x, y);
			if (kbPressed >= 0) kbCursor = kbPressed;
		}
		else if (pt.kind == Pointer::Up)
		{
			int was = kbPressed;
			kbPressed = -1;
			// "<": what was typed is let go, the field as it was
			if (was == -2 && back.hit(x, y)) kbClose(false);
			else if (was >= 0 && keyboardHit(keys, x, y) == was) kbKey(was);
		}
	}

	void Ui::drawKeyboard()
	{
		fill({0, 0, W, H}, BG);
		Rect back = buttonRect(BACK_ICON);
		fill(back, kbPressed == -2 ? PRESSED : TEAL);
		drawGlyphMid(G_BACK, back.x + back.w / 2 - 1, back.y + back.h / 2, CREAM, 3);
		drawTextMid(fieldName(kbField), 2, CREAM, 72, 32, 540);
		// the field: the text and its caret, its end in view when it's longer than the box
		fill({16, 92, 608, 64}, YELLOW);
		fill({18, 94, 604, 60}, SURFACE);
		// the caret is a bar of its own between the two halves (a '|' in the text would move it as it blinks), with
		// room kept for it whether it shows or not
		std::string before = kbEdit.text.substr(0, kbEdit.caret), after = kbEdit.text.substr(kbEdit.caret);
		auto logical = [&](const std::string& t) { return t.empty() ? 0 : (int)std::lround(textWidth(t, 1) / sx); };
		while (logical(before) + 6 + logical(after) > 580 && !before.empty())
		{
			size_t n = 1;
			while (n < before.size() && ((unsigned char)before[n] & 0xC0) == 0x80) n++;
			before.erase(0, n);
		}
		int caretX = 32 + logical(before) + 1;
		if (!before.empty()) drawTextMid(before, 1, CREAM, 32, 124, 580);
		if (kbCaretShown()) fill({caretX, 108, 3, 32}, YELLOW);
		if (!after.empty()) drawTextMid(after, 1, CREAM, caretX + 5, 124, std::max(0, 580 - (caretX + 5 - 32)));
		if (kbField == EQ_NAME && !eqNameNote.empty()) drawTextMid(eqNameNote, 0, YELLOW, 24, 176, 592);
		if (kbField == PL_NAME && !plNameNote.empty()) drawTextMid(plNameNote, 0, YELLOW, 24, 176, 592);
		drawHints({{"A", "Type"}, {"B", "Delete"}, {"X", "Space"}, {"Y", "Shift"}, {"L/R", "Move"}, {"START", "Done"}});
		auto keys = kbKeys();
		for (int i = 0; i < (int)keys.size(); i++)
		{
			const KeyDef& k = keys[i];
			Rect r{k.x + 3, k.y + 3, k.w - 6, k.h - 6};
			bool at = i == kbCursor, on = k.action == KeyAction::Shift && kbShift != ShiftState::Off;
			bool special = k.action != KeyAction::Text;
			fill(r, kbPressed == i ? PRESSED : at ? YELLOW : special ? SDL_Color{0x1E, 0x33, 0x37, 255} : TEAL);
			drawTextMid(k.label, 1, at ? BG : on ? YELLOW : CREAM, r.x + r.w / 2, r.y + r.h / 2, r.w - 4, 2);
		}
	}

	// ---- playlists ----

	void Ui::setStateDir(const std::string& stateDir)
	{
		if (stateDir.empty()) plStore.reset();
		else plStore = std::make_unique<PlaylistStore>(stateDir + "/playlists");
	}

	std::vector<std::string> Ui::plNames() const
	{
		std::vector<std::string> out;
		if (plStore)
			for (auto& i : plStore->list()) out.push_back(i.name);
		return out;
	}

	void Ui::plRows(Player& p)
	{
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& path, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back(path);
		};
		if (!plLoaded) { add(-1, "This playlist can't be opened", "", ""); return; }
		if (plCur.entries.empty()) { add(-1, quoted(plCur.name) + " is empty", "", ""); return; }
		// no stat of every item here (a long list on a slow card): whether it's there is found as its row shows
		for (auto& e : plCur.entries)
		{
			std::string note;
			if (isNetPath(e.path))
			{
				NetPath np;
				const NetServer* sv = netStore && parseNetPath(e.path, np) ? netStore->find(np.server) : nullptr;
				if (sv) note = serverName(*sv);
			}
			else
			{
				// where it was left (as the folders show it), then the folder it's in
				const ResumeEntry* r = p.resumeStore().find(e.path);
				std::string at = r && r->position >= 0 && !isAudioFile(e.path) ? "at " + formatTime(r->position) : "";
				std::string folder = baseName(parentFolder(e.path));
				note = at.empty() ? folder : folder.empty() ? at : at + " \xC2\xB7 " + folder;
			}
			add(PL_ITEM, e.title.empty() ? entryTitle(e.path) : e.title, e.path, note);
		}
	}

	int Ui::plMissingAt(int row) const
	{
		if (!isPlDir(browseDir) || row < 0 || row >= (int)browseRows.size() || browseRows[row].id != PL_ITEM) return 0;
		if (plMissing.size() != browseRows.size()) plMissing.assign(browseRows.size(), -1);
		int8_t& m = plMissing[row];
		if (m < 0)
		{
			const std::string& path = browsePaths[row];
			if (isNetPath(path))
			{
				// (whether the file is on the server is only known by asking it: as it opens)
				NetPath np;
				m = parseNetPath(path, np) && netStore && netStore->find(np.server) ? 0 : 2;
			}
			else m = access(path.c_str(), F_OK) == 0 ? 0 : 1;
			plMissingSerial++;
		}
		return m;
	}

	void Ui::plCheckShown()
	{
		if (page != Page::Browse || !isPlDir(browseDir)) return;
		for (int i = scroll; i < scroll + ROWS_SHOWN && i < (int)browseRows.size(); i++) plMissingAt(i);
	}

	void Ui::plPlay(const Playlist& pl, int index, bool shuffle, Player& p)
	{
		int n = (int)pl.entries.size();
		if (n == 0 || index < 0 || index >= n) return;
		plFlush(p);
		std::vector<int> order(n);
		for (int i = 0; i < n; i++) order[i] = i;
		if (shuffle)
		{
			// a one-off order (Loop all plays it again as it is)
			std::random_device rd;
			order = shuffleOrder(n, -1, ((uint64_t)rd() << 32) ^ rd() ^ SDL_GetTicks());
			index = 0;
		}
		playRequest = {};
		for (int i : order) playRequest.paths.push_back(pl.entries[i].path);
		playRequest.index = index;
		playRequest.source.kind = SourceKind::Playlist;
		playRequest.source.title = pl.name;
		playRequest.source.id = pl.file;
		playRequest.source.shuffled = shuffle;
		plOrder = order;
		plOrderId = pl.file;
		sideAsked.clear();
		p.setLastFolder(pl.file);
	}

	void Ui::plSaved(Player& p, const std::vector<int>& remap)
	{
		// written a second after the edits stop (a run of moves is one write): tick() or plFlush
		plDirty = true;
		plDirtyAt = SDL_GetTicks();
		plTellPlayer(plCur, remap, p);
		int c = remapIndex(cursor, remap);
		plRefresh(p);
		cursor = std::max(0, c);
		fitList();
	}

	void Ui::plFlush(Player& p)
	{
		if (!plDirty) return;
		plDirty = false;
		if (!plStore || !plStore->save(plCur)) p.osd("Couldn't save the playlist");
	}

	void Ui::plTellPlayer(const Playlist& pl, const std::vector<int>& remap, Player& p)
	{
		// the playlist that plays: its new items, in the order they play (shuffled: that order, less the ones gone,
		// and the ones added after)
		if (!p.playing() || p.source().kind != SourceKind::Playlist || p.source().id != pl.file) return;
		std::vector<int> order = plOrderId == pl.file ? plOrder : std::vector<int>();
		if (order.empty())
			for (int i = 0; i < (int)remap.size(); i++) order.push_back(i);
		if ((int)order.size() != p.count()) return;
		std::vector<int> now;
		std::vector<char> seen(pl.entries.size(), 0);
		int current = 0;
		for (int k = 0; k < (int)order.size(); k++)
		{
			int r = order[k] >= 0 && order[k] < (int)remap.size() ? remap[order[k]] : -1;
			if (r < 0 || r >= (int)pl.entries.size()) continue;
			if (k < p.index()) current++;
			now.push_back(r);
			seen[r] = 1;
		}
		for (int i = 0; i < (int)pl.entries.size(); i++)
			if (!seen[i]) now.push_back(i);
		std::vector<std::string> paths;
		for (int i : now) paths.push_back(pl.entries[i].path);
		plOrder = now;
		plOrderId = pl.file;
		p.updatePlaylist(paths, current);
	}

	void Ui::plRefresh(Player& p)
	{
		int c = cursor, s = scroll;
		plRows(p);
		plMissing.clear();
		plMissingSerial++;
		cursor = c;
		scroll = s;
		fitList();
		browseVersion++;
	}

	void Ui::plDelete(const std::string& file, Player& p)
	{
		if (!plStore) return;
		// its edits not written yet: never (written after, they'd bring it back)
		if (plDirty && plCur.file == file) plDirty = false;
		else plFlush(p);
		std::string name = baseName(file);
		for (auto& i : plStore->list())
			if (i.file == file) name = i.name;
		if (!plStore->remove(file)) { p.osd("Couldn't delete the playlist"); return; }
		p.osd("Deleted " + quoted(name));
		if (p.resumeStore().lastFolder == file) p.setLastFolder("");
		int c = cursor, s = scroll;
		bool inList = browseDir == PLROOT;
		enterFolder(PLROOT, p);
		if (inList)
		{
			cursor = c;
			scroll = s;
			fitList();
		}
	}

	void Ui::plNameOpen(bool rename, const std::string& file, const std::string& name)
	{
		if (!plStore) return;
		plRenaming = rename;
		plRenameFile = file;
		plNameNote.clear();
		plNameSubmit = false;
		std::string text = name;
		if (!rename)
		{
			// "New playlist" (or the folder's name), else the first number free
			auto names = plNames();
			std::string base = name.empty() ? std::string("New playlist") : name;
			// room for " 12" (cut between characters)
			if (base.size() > PL_NAME_MAX_BYTES - 4)
			{
				size_t cut = PL_NAME_MAX_BYTES - 4;
				while (cut > 0 && ((unsigned char)base[cut] & 0xC0) == 0x80) cut--;
				base = trimmed(base.substr(0, cut));
			}
			text = base;
			for (int n = 2; n < 1000 && checkPlaylistName(text, names) == PlNameError::Taken; n++) text = base + " " + std::to_string(n);
		}
		openKeyboard(PL_NAME, text);
	}

	void Ui::plNameDone(Player& p)
	{
		plNameSubmit = false;
		if (!plStore) return;
		// (a rename reads the file: the edits waiting go first)
		plFlush(p);
		std::string name = trimmed(kbEdit.text), file;
		auto names = plNames();
		PlNameError e = plRenaming ? plStore->rename(plRenameFile, name, file) : plStore->create(name, file);
		if (e == PlNameError::None)
		{
			plNameNote.clear();
			kbOpen = false;
			kbPressed = -1;
			if (!plRenaming && plNameThenAdd)
			{
				// Add to... > New playlist...: what was to be added goes in it (and the browser stays where it is)
				plNameThenAdd = false;
				pickerFolder = plNameAddFolder;
				pickerAdd(file, p);
				return;
			}
			if (!plRenaming)
			{
				// in it at once: what's next is adding to it
				enterFolder(plDirFor(file), p);
				p.setLastFolder(file);
				return;
			}
			p.osd("Renamed to " + quoted(name));
			std::string old = plRenameFile;
			if (p.playing() && p.source().kind == SourceKind::Playlist && p.source().id == old) p.renameSource(old, name, file);
			if (plOrderId == old) plOrderId = file;
			if (p.resumeStore().lastFolder == old) p.setLastFolder(file);
			if (tabDir[TAB_PLAYLISTS] == plDirFor(old)) tabDir[TAB_PLAYLISTS] = plDirFor(file);
			if (browseDir == plDirFor(old))
			{
				int c = cursor, s = scroll;
				enterFolder(plDirFor(file), p);
				cursor = c;
				scroll = s;
				fitList();
			}
			else if (browseDir == PLROOT)
			{
				enterFolder(PLROOT, p);
				for (size_t i = 0; i < browsePaths.size(); i++)
					if (browsePaths[i] == file) cursor = (int)i;
				fitList();
			}
			return;
		}
		// what's wrong, under the field; the keyboard stays up to put it right
		std::string taken = name;
		for (auto& n : names)
			if (checkPlaylistName(name, {n}) == PlNameError::Taken) taken = n;
		switch (e)
		{
		// (the store says Empty also when the file couldn't be written)
		case PlNameError::Empty: plNameNote = name.empty() ? "Type a name" : "Couldn't save the playlist"; break;
		case PlNameError::TooLong: plNameNote = "40 characters at most"; break;
		case PlNameError::BadChar: plNameNote = "A name can't have / in it"; break;
		case PlNameError::Taken: plNameNote = "You have one called " + quoted(taken); break;
		default: plNameNote = "That name can't be used"; break;
		}
		kbActiveAt = SDL_GetTicks();
	}

	// ---- playlists: selecting, moving, adding ----

	void Ui::selStart(int mode, const std::string& dest)
	{
		selEnd();
		selMode = mode;
		selDest = dest;
		selDestName.clear();
		if (plStore && !dest.empty())
			for (auto& i : plStore->list())
				if (i.file == dest) selDestName = i.name;
		dropTouch();
		holdAt = 0;
	}

	void Ui::selEnd()
	{
		selMode = SEL_NONE;
		plSel.clear();
		pickOrder.clear();
		picked.clear();
		selDest.clear();
		selDestName.clear();
		dropArmed = 0;
		removeArmed = false;
		moving = moveFromMenu = false;
		plDrag.on = false;
		selSerial++;
		browseVersion++;
	}

	void Ui::selLeave(Player& p)
	{
		std::string back = selDest;
		bool add = selMode == SEL_ADD || selMode == SEL_PICK;
		selEnd();
		if (add && !back.empty()) returnToPlaylist(back, -1, p);
	}

	void Ui::returnToPlaylist(const std::string& file, int item, Player& p)
	{
		if (browseTab != TAB_PLAYLISTS) tabDir[browseTab] = browseDir;
		browseTab = TAB_PLAYLISTS;
		if (!exists(file)) { enterFolder(PLROOT, p); return; }
		enterFolder(plDirFor(file), p);
		if (item >= 0 && item < (int)browseRows.size())
		{
			cursor = item;
			// the first added in view, as near the top as the list allows (what came with it below it)
			scroll = std::min(item, std::max(0, (int)browseRows.size() - ROWS_SHOWN));
		}
		fitList();
	}

	bool Ui::selTickable(int row) const
	{
		if (row < 0 || row >= (int)browseRows.size()) return false;
		if (selMode == SEL_EDIT) return isPlDir(browseDir) && browseRows[row].id == PL_ITEM;
		// a file (never a photo: playlists are videos and songs)
		return selMode == SEL_ADD && browseRows[row].id == 2 && !isPhotoFile(browsePaths[row]);
	}

	bool Ui::selTicked(int row) const
	{
		if (!selTickable(row)) return false;
		return selMode == SEL_EDIT ? plSel.count(row) > 0 : picked.count(browsePaths[row]) > 0;
	}

	int Ui::selFolderFiles(int& ticked) const
	{
		int n = 0;
		ticked = 0;
		for (int i = 0; i < (int)browseRows.size(); i++)
			if (selTickable(i))
			{
				n++;
				if (selTicked(i)) ticked++;
			}
		return n;
	}

	bool Ui::pickable() const
	{
		// a folder (not a first page, Recent, or a server's list of shares)
		if (browseDir.empty() || pseudoDir(browseDir)) return false;
		return !isNetPath(browseDir) || (!netBaseName(browseDir).empty() && !netLoading);
	}

	std::string Ui::folderName(const std::string& dir) const
	{
		if (dir.empty() || pseudoDir(dir)) return "";
		if (isNetPath(dir)) return netBaseName(dir);
		for (auto& r : roots)
			if (r.path == dir) return r.label;
		return baseName(dir);
	}

	std::vector<PlaylistEntry> Ui::pickedEntries() const
	{
		std::vector<PlaylistEntry> out;
		for (auto& path : pickOrder)
			if (picked.count(path))
			{
				PlaylistEntry e;
				e.path = path;
				e.title = entryTitle(path);
				out.push_back(e);
			}
		return out;
	}

	bool Ui::selChoose(int row, Player& p)
	{
		if (row < 0 || row >= (int)browseRows.size()) return true;
		removeArmed = false;
		dropArmed = 0;
		if (selMode == SEL_EDIT)
		{
			if (selTickable(row))
			{
				if (!plSel.erase(row)) plSel.insert(row);
				selSerial++;
			}
			return true;
		}
		// Add and Pick: a folder, a connection, a place opens as ever; a file is ticked (Add), or nothing (Pick)
		if (browseRows[row].id != 2) return false;
		if (selMode == SEL_ADD && selTickable(row))
		{
			const std::string& path = browsePaths[row];
			if (picked.erase(path)) pickOrder.erase(std::remove(pickOrder.begin(), pickOrder.end(), path), pickOrder.end());
			else
			{
				picked.insert(path);
				pickOrder.push_back(path);
			}
			selSerial++;
		}
		else if (selMode == SEL_ADD && isPhotoFile(browsePaths[row])) p.osd("Playlists take videos and songs");
		return true;
	}

	void Ui::selAll(Player&)
	{
		removeArmed = false;
		if (selMode == SEL_EDIT)
		{
			// All, or None when all are
			bool all = (int)plSel.size() == (int)plCur.entries.size();
			plSel.clear();
			if (!all)
				for (int i = 0; i < (int)plCur.entries.size(); i++) plSel.insert(i);
		}
		else if (selMode == SEL_ADD)
		{
			// this folder's files: all, or none when all are (the other folders' ticks stay)
			int ticked = 0, n = selFolderFiles(ticked);
			if (!n) return;
			for (int i = 0; i < (int)browseRows.size(); i++)
			{
				if (!selTickable(i)) continue;
				const std::string& path = browsePaths[i];
				if (ticked == n)
				{
					picked.erase(path);
					pickOrder.erase(std::remove(pickOrder.begin(), pickOrder.end(), path), pickOrder.end());
				}
				else if (picked.insert(path).second) pickOrder.push_back(path);
			}
		}
		selSerial++;
	}

	void Ui::selBack(Player& p, bool button)
	{
		removeArmed = false;
		if (selMode == SEL_EDIT) { selEnd(); return; }
		// B goes up a folder (Cancel doesn't); at a first page it leaves, files ticked asking twice
		if (!button && !atFirstPage()) { browseUp(p); dropArmed = 0; return; }
		if (selMode == SEL_ADD && !picked.empty() && !(dropArmed && SDL_GetTicks() - dropArmed < QUIT_WINDOW_MS))
		{
			dropArmed = SDL_GetTicks();
			return;
		}
		selLeave(p);
	}

	void Ui::selAct(Player& p)
	{
		if (selMode == SEL_EDIT)
		{
			if (plSel.empty()) return;
			// twice: the first press says how many go
			if (!removeArmed) { removeArmed = true; return; }
			int n = (int)plSel.size();
			auto remap = removeEntries(plCur.entries, plSel);
			plSel.clear();
			plSaved(p, remap);
			p.osd("Removed " + std::to_string(n) + " from " + quoted(plCur.name));
			selEnd();
			return;
		}
		if (selMode == SEL_ADD)
		{
			if (picked.empty()) return;
			if (selDest.empty()) { openPicker("", p); return; }
			addTo(selDest, pickedEntries(), 0, "", true, p);
			return;
		}
		if (selMode == SEL_PICK && pickable()) addFolderTo(browseDir, selDest, true, p);
	}

	bool Ui::selKey(Button b, Player& p)
	{
		if (b != Button::X) removeArmed = false;
		if (b != Button::B) dropArmed = 0;
		if (moving)
		{
			// the block: a row, a page; A drops it, B puts it back
			switch (b)
			{
			case Button::Up: case Button::Down: moveStep(b == Button::Up ? -1 : 1, 1, p); break;
			case Button::Left: case Button::Right: moveStep(b == Button::Left ? -1 : 1, ROWS_SHOWN, p); break;
			case Button::A: moveEnd(true, p); break;
			case Button::B: moveEnd(false, p); break;
			default: break;
			}
			return true;
		}
		switch (b)
		{
		// the list's own: moving the cursor, A (choose: a tick, or a folder opened)
		case Button::Up: case Button::Down: case Button::Left: case Button::Right: case Button::A: return false;
		case Button::Select: selAll(p); return true;
		case Button::Y:
			if (selMode == SEL_EDIT && !plSel.empty())
			{
				moveFromMenu = false;
				moveStart(p);
			}
			return true;
		case Button::X: selAct(p); return true;
		case Button::B: selBack(p, false); return true;
		case Button::L: case Button::R:
			// adding: the cards and the network, the playlists' tab left out (and the connection's form, the search:
			// the connections instead)
			if (selMode == SEL_EDIT) return true;
			if (tabDir[TAB_NETWORK] == FORMDIR || tabDir[TAB_NETWORK] == FINDNET) tabDir[TAB_NETWORK] = NETROOT;
			switchTab(b == Button::R ? TAB_NETWORK : TAB_LOCAL, p);
			return true;
		default: return true;	// START and the rest: nothing while selecting
		}
	}

	void Ui::moveFollow()
	{
		if (plSel.empty()) return;
		int first = *plSel.begin(), last = *plSel.rbegin(), n = (int)browseRows.size();
		cursor = first;
		if (last >= scroll + ROWS_SHOWN) scroll = last - ROWS_SHOWN + 1;
		if (first < scroll) scroll = first;
		scroll = std::clamp(scroll, 0, std::max(0, n - ROWS_SHOWN));
	}

	void Ui::moveStart(Player& p)
	{
		if (plSel.empty() || !plLoaded) return;
		moveOrig = plCur.entries;
		moveOrigSel = plSel;
		// the ticked ones together, at the first one's place, at once
		moveRemap = gatherSelection(plCur.entries, plSel);
		plSel = remapSelection(plSel, moveRemap);
		moving = true;
		removeArmed = false;
		plRefresh(p);
		moveFollow();
	}

	void Ui::moveStep(int dir, int times, Player& p)
	{
		if (plSel.empty()) return;
		int n = (int)plCur.entries.size();
		bool moved = false;
		for (int k = 0; k < times; k++)
		{
			if ((dir < 0 && *plSel.begin() == 0) || (dir > 0 && *plSel.rbegin() + 1 >= n)) break;
			auto r = moveSelection(plCur.entries, plSel, dir);
			for (int& m : moveRemap) m = m >= 0 && m < (int)r.size() ? r[m] : -1;
			plSel = remapSelection(plSel, r);
			moved = true;
		}
		if (!moved) return;
		plRefresh(p);
		moveFollow();
	}

	void Ui::moveEnd(bool keep, Player& p)
	{
		moving = false;
		if (keep)
		{
			bool same = true;
			for (int i = 0; i < (int)moveRemap.size(); i++) same = same && moveRemap[i] == i;
			// (the cursor stays on the block: plSaved would take it for an old index)
			int c = cursor, s = scroll;
			if (!same) plSaved(p, moveRemap);
			cursor = c;
			scroll = s;
			fitList();
		}
		else
		{
			plCur.entries = moveOrig;
			plSel = moveOrigSel;
			plRefresh(p);
			if (!plSel.empty()) cursor = *plSel.begin();
			fitList();
		}
		moveOrig.clear();
		moveRemap.clear();
		// from an item's menu: back to the list as it was; from Select: the same ticks
		if (moveFromMenu)
		{
			int c = cursor, s = scroll;
			selEnd();
			cursor = c;
			scroll = s;
		}
		selSerial++;
	}

	void Ui::dragBegin(int row, int y, Player& p)
	{
		// a ticked row takes the ticked ones with it, gathered at once; else the row alone
		std::set<int> block = plSel.count(row) ? plSel : std::set<int>{row};
		moveRemap = gatherSelection(plCur.entries, block);
		dragSel = remapSelection(block, moveRemap);
		plSel = remapSelection(plSel, moveRemap);
		plRefresh(p);
		plDrag.on = true;
		plDrag.y = y;
		plDrag.scrollAt = SDL_GetTicks();
		removeArmed = false;
		holdAt = 0;
	}

	int Ui::dragSlot() const
	{
		int n = (int)browseRows.size();
		return std::clamp(scroll + (int)std::lround((plDrag.y - ROW_Y) / (double)ROW_H), 0, n);
	}

	int Ui::dragTarget() const
	{
		if (dragSel.empty()) return -1;
		int start = *dragSel.begin(), len = (int)dragSel.size(), s = dragSlot();
		return s <= start ? s : s >= start + len ? s - len : start;
	}

	void Ui::dragEnd(Player& p)
	{
		plDrag.on = false;
		if (dragSel.empty()) return;
		int start = *dragSel.begin(), to = dragTarget();
		int dir = to < start ? -1 : 1;
		for (int k = 0; k < std::abs(to - start); k++)
		{
			auto r = moveSelection(plCur.entries, dragSel, dir);
			for (int& m : moveRemap) m = m >= 0 && m < (int)r.size() ? r[m] : -1;
			dragSel = remapSelection(dragSel, r);
			plSel = remapSelection(plSel, r);
		}
		bool same = true;
		for (int i = 0; i < (int)moveRemap.size(); i++) same = same && moveRemap[i] == i;
		int first = *dragSel.begin(), s = scroll;
		if (!same) plSaved(p, moveRemap);
		moveRemap.clear();
		dragSel.clear();
		cursor = first;
		scroll = s;
		fitList();
		selSerial++;
	}

	void Ui::openPicker(const std::string& folder, Player& p)
	{
		if (!plStore) return;
		plFlush(p);
		pickerList = plStore->list();
		pickerFolder = folder;
		// none yet: the one to make is the only choice
		if (pickerList.empty()) { pickerNew(p); return; }
		pickerOpen = true;
		pickerCursor = pickerScroll = 0;
		pressedBtn = -1;
	}

	void Ui::pickerNew(Player&)
	{
		// named after the folder (a ticked file's, the one the menu was for)
		pickerOpen = false;
		plNameOpen(false, "", folderName(pickerFolder.empty() ? browseDir : pickerFolder));
		plNameThenAdd = true;
		plNameAddFolder = pickerFolder;
	}

	void Ui::pickerChoose(int i, Player& p)
	{
		pickerOpen = false;
		pressedBtn = -1;
		if (i == 0) { pickerNew(p); return; }
		if (i - 1 < (int)pickerList.size()) pickerAdd(pickerList[i - 1].file, p);
	}

	void Ui::pickerAdd(const std::string& dest, Player& p)
	{
		// a folder's media (its menu's Add to playlist...), or the files ticked (then done selecting, still here)
		if (!pickerFolder.empty())
		{
			addFolderTo(pickerFolder, dest, false, p);
			return;
		}
		// (nothing added, the playlist full: the ticks stay, for another one)
		if (addTo(dest, pickedEntries(), 0, "", false, p)) selEnd();
	}

	bool Ui::pickerKey(Button b, Player& p)
	{
		int n = (int)pickerList.size() + 1;
		switch (b)
		{
		case Button::Up: pickerCursor = (pickerCursor + n - 1) % n; break;
		case Button::Down: pickerCursor = (pickerCursor + 1) % n; break;
		case Button::Left: pickerCursor = std::max(0, pickerCursor - PICKER_SHOWN); break;
		case Button::Right: pickerCursor = std::min(n - 1, pickerCursor + PICKER_SHOWN); break;
		case Button::A: pickerChoose(pickerCursor, p); return true;
		case Button::B: pickerOpen = false; return true;
		default: return true;
		}
		if (pickerCursor < pickerScroll) pickerScroll = pickerCursor;
		if (pickerCursor >= pickerScroll + PICKER_SHOWN) pickerScroll = pickerCursor - PICKER_SHOWN + 1;
		return true;
	}

	bool Ui::pickerPointer(const Pointer& pt, int x, int y, Player& p)
	{
		int n = (int)pickerList.size() + 1;
		if (pt.kind == Pointer::Down)
		{
			// outside the box: closed; on a row, chosen as the finger lifts there (a drag scrolls)
			if (!pickerBox().hit(x, y)) { pickerOpen = false; swallowTouch = true; return false; }
			pressedBtn = -1;
			for (int i = pickerScroll; i < n && i < pickerScroll + PICKER_SHOWN; i++)
				if (pickerRow(i).hit(x, y)) pressedBtn = PICK_ROW + i;
			pickerTouchY = y;
			pickerScrollStart = pickerScroll;
			pickerScrolled = false;
			return false;
		}
		if (pt.kind == Pointer::Move && (pickerScrolled || std::abs(y - pickerTouchY) > 12))
		{
			pickerScrolled = true;
			pressedBtn = -1;
			pickerScroll = std::clamp(pickerScrollStart + (int)std::lround((pickerTouchY - y) / 46.0), 0, std::max(0, n - PICKER_SHOWN));
			pickerCursor = std::clamp(pickerCursor, pickerScroll, pickerScroll + PICKER_SHOWN - 1);
			return false;
		}
		if (pt.kind == Pointer::Up)
		{
			int id = pressedBtn;
			pressedBtn = -1;
			if (!pickerScrolled && id >= PICK_ROW && pickerRow(id - PICK_ROW).hit(x, y))
			{
				pickerCursor = id - PICK_ROW;
				pickerChoose(pickerCursor, p);
			}
		}
		return false;
	}

	void Ui::addFolderTo(const std::string& dir, const std::string& dest, bool back, Player& p)
	{
		// the folder's own videos and songs (not its folders'), in name order
		std::vector<std::string> names;
		if (isNetPath(dir))
		{
			auto it = netNames.find(dir);
			if (it == netNames.end())
			{
				// never listed: listed now (a moment; the busy dialog can cancel it), added when it comes
				NetPath np;
				const NetServer* sv = netStore && parseNetPath(dir, np) ? netStore->find(np.server) : nullptr;
				if (!sv || !netWorker) { p.osd("This connection is gone"); return; }
				addPending = {dir, dest, ++addSerial, back};
				netWorker->list(addSerial, *sv, netStore->password(*sv), dir);
				return;
			}
			names = it->second;
		}
		else
		{
			bool ok;
			for (auto& e : listFolder(dir, ok, true))
				if (!e.dir) names.push_back(e.name);
			if (!ok) { p.osd("This folder can't be opened"); return; }
		}
		int photos = 0;
		auto add = folderEntries(dir, names, photos);
		addTo(dest, add, photos, folderName(dir), back, p);
	}

	bool Ui::addTo(const std::string& destRef, const std::vector<PlaylistEntry>& add, int photos, const std::string& from,
		bool back, Player& p)
	{
		std::string dest = destRef;	// (a copy: it can be selDest, which selEnd clears)
		auto many = [](int n, const char* one, const char* more) { return std::to_string(n) + " " + (n == 1 ? one : more); };
		std::string photoNote = photos ? " \xC2\xB7 skipped " + many(photos, "photo", "photos") : "";
		if (add.empty())
		{
			p.osd("Nothing to add in " + quoted(from) + photoNote);
			return false;
		}
		Playlist pl;
		int skipped = 0;
		plFlush(p);
		if (!plStore || !plStore->load(dest, pl, skipped)) { p.osd("Couldn't read the playlist"); return false; }
		int before = (int)pl.entries.size(), dropped = 0;
		auto remap = addEntries(pl.entries, add, -1, PL_MAX_ENTRIES, dropped);
		int added = (int)add.size() - dropped;
		if (added <= 0) { p.osd("Playlist is full: " + quoted(pl.name) + " has " + std::to_string(PL_MAX_ENTRIES) + " items"); return false; }
		if (!plStore->save(pl)) { p.osd("Couldn't save the playlist"); return false; }
		plTellPlayer(pl, remap, p);
		std::string note = "Added " + std::to_string(added) + " to " + quoted(pl.name) + photoNote;
		if (dropped) note += " \xC2\xB7 Playlist is full, " + std::to_string(dropped) + " left out";
		p.osd(note);
		if (back)
		{
			selEnd();
			returnToPlaylist(dest, before, p);
		}
		else browseVersion++;
		return true;
	}

	void Ui::cancelAdd(Player& p)
	{
		addPending = {};
		pressedBtn = -1;
		if (!netWorker) return;
		// everything asked of the worker goes: the player's sidecar folders (its file opens without them), a file
		// waiting for its folder (opened alone), the folder being listed here (asked again)
		netWorker->cancelAll();
		for (auto& s : sidePending) p.netFolderListed(s.second, {}, false);
		sidePending.clear();
		if (!netOpenPending.empty())
		{
			openRequest = netOpenPending;
			netOpenPending.clear();
		}
		if (netLoading && isNetPath(browseDir)) enterFolder(browseDir, p);
	}

	// ---- playlists: drawing the select mode, the picker, the busy dialog, the Main page's source ----

	std::string Ui::whereOf(const std::string& dir) const
	{
		std::string where = dir;
		if (isNetPath(dir))
		{
			NetPath np;
			parseNetPath(dir, np);
			const NetServer* sv = netStore ? netStore->find(np.server) : nullptr;
			where = (sv ? serverName(*sv) : std::string("?")) + (np.path == "/" ? "" : np.path);
		}
		else
		{
			size_t best = 0;
			for (auto& r : roots)
				if (r.path.size() > best && (dir == r.path || dir.rfind(r.path + "/", 0) == 0))
				{
					best = r.path.size();
					where = r.label + dir.substr(r.path.size());
				}
		}
		// the folders apart: "share / Movies"
		std::string out;
		for (char c : where) out += c == '/' ? std::string(" / ") : std::string(1, c);
		return out;
	}

	void Ui::drawWhere(int x, int maxW)
	{
		std::string where = elideLeft(browseWhere(), 0, maxW);
		size_t cut = where.rfind(" / ");
		if (cut == std::string::npos) drawTextMid(where, 0, CREAM, x, SUB_Y);
		else
		{
			std::string head = where.substr(0, cut + 3);
			drawTextMid(head, 0, DIM, x, SUB_Y);
			drawTextMid(where.substr(cut + 3), 0, CREAM, x + (int)std::lround(textWidth(head, 0) / sx), SUB_Y);
		}
	}

	void Ui::drawSelHeader(const Player& p)
	{
		drawBackIcon();
		drawBattery(616, 24, p);
		int n = selCount();
		auto items = [](int k) { return std::to_string(k) + (k == 1 ? " item" : " items"); };
		std::string title, count;
		SDL_Color countInk = DIM;
		bool move = moving || plDrag.on;
		if (move)
		{
			// "Rows 5–7 of 12": where the block is now
			const std::set<int>& block = moving ? plSel : dragSel;
			int first = block.empty() ? 0 : *block.begin() + 1, last = block.empty() ? 0 : *block.rbegin() + 1;
			title = "Move " + items((int)block.size());
			count = first == last ? "Row " + std::to_string(first) + " of " + std::to_string(plCur.entries.size())
				: "Rows " + std::to_string(first) + "\xE2\x80\x93" + std::to_string(last) + " of " + std::to_string(plCur.entries.size());
		}
		else
		{
			title = selMode == SEL_EDIT ? std::string("Select items") : selMode == SEL_PICK ? "Add a folder to " + quoted(selDestName)
				: selDest.empty() ? std::string("Select files") : "Add to " + quoted(selDestName);
			if (selMode != SEL_PICK)
			{
				count = n ? std::to_string(n) + " selected" : "None yet";
				countInk = n ? YELLOW : DIM;
			}
		}
		int cw = count.empty() ? 0 : (int)std::lround(textWidth(count, 1) / sx);
		if (!count.empty()) drawTextMid(count, 1, countInk, 496, 32, 0, 1);
		drawTextMid(title, 2, CREAM, 72, 32, std::max(40, 496 - (cw ? cw + 16 : 0) - 72));
		// the line under it: Move's keys; adding, where it is (a card or the network); a playlist's, as ever
		if (moving)
		{
			int x = 24;
			for (auto& part : std::vector<std::pair<std::string, std::string>>{{"UP/DOWN", " move"}, {" \xC2\xB7 ", ""}, {"A", " drop"},
					 {" \xC2\xB7 ", ""}, {"B", " cancel"}})
			{
				bool sep = part.second.empty();
				drawTextMid(part.first, 0, sep ? DIM : YELLOW, x, SUB_Y);
				x += (int)std::lround(textWidth(part.first, 0) / sx);
				if (!sep)
				{
					drawTextMid(part.second, 0, DIM, x, SUB_Y);
					x += (int)std::lround(textWidth(part.second, 0) / sx);
				}
			}
			return;
		}
		if (selMode == SEL_EDIT)
		{
			std::string c = items((int)plCur.entries.size());
			drawTextMid(c, 0, DIM, 616, SUB_Y, 0, 1);
			drawWhere(24, 592 - (int)std::lround(textWidth(c, 0) / sx) - 24);
			return;
		}
		bool net = browseTab == TAB_NETWORK;
		drawGlyphMid(net ? G_NETWORK : G_CARD, 33, SUB_Y, DIM);
		drawWhere(52, 564);
	}

	void Ui::drawBarButton(int id, const Glyph* g, const std::string& label, const char* key, SDL_Color ink, bool filled, bool off)
	{
		Rect r = buttonRect(id);
		bool down = pressedBtn == id && !off;
		bool lit = filled && !off;
		fill(r, down ? PRESSED : lit ? YELLOW : TEAL);
		SDL_Color c = off ? DIM : lit && !down ? BG : ink;
		SDL_Color k = off ? DIM : lit && !down ? BG : YELLOW;
		// the key at the right; the glyph and the label in the middle of what's left
		int kw = key ? (int)std::lround(textWidth(key, 0) / sx) : 0;
		if (key) drawTextMid(key, 0, k, r.x + r.w - 12, r.y + r.h / 2, 0, 1);
		int room = r.w - 24 - (kw ? kw + 8 : 0);
		int gw = g ? glyphW(*g) : 0;
		int tw = std::min(room - (gw ? gw + 8 : 0), (int)std::lround(textWidth(label, 0) / sx));
		int x = r.x + 12 + std::max(0, (room - (gw ? gw + 8 : 0) - tw) / 2);
		if (g)
		{
			drawGlyphMid(*g, x + gw / 2, r.y + r.h / 2, c);
			x += gw + 8;
		}
		drawTextMid(label, 0, c, x, r.y + r.h / 2, tw + 1);
	}

	void Ui::drawSelBar()
	{
		auto many = [](int n) { return std::to_string(n); };
		int n = selCount();
		if (moving)
		{
			drawBarButton(MOVE_CANCEL, &G_CROSS, "Cancel", "B", CREAM, false, false);
			drawBarButton(MOVE_DONE, &G_CHECK, "Done", "A", CREAM, true, false);
			return;
		}
		if (selMode == SEL_EDIT)
		{
			bool all = !plCur.entries.empty() && (int)plSel.size() == (int)plCur.entries.size();
			drawBarButton(SEL_ALL, &G_BOX_ON, all ? "None" : "All", "SELECT", CREAM, false, plCur.entries.empty());
			drawBarButton(SEL_MOVE, &G_MOVE, "Move", "Y", CREAM, false, n == 0);
			// (armed: the words alone, room for them)
			drawBarButton(SEL_ACT, removeArmed ? nullptr : &G_MINUS, removeArmed ? "Again: remove " + many(n) : std::string("Remove"), "X",
				removeArmed ? YELLOW : RED, false, n == 0);
			return;
		}
		bool dropping = dropArmed && SDL_GetTicks() - dropArmed < QUIT_WINDOW_MS;
		if (selMode == SEL_ADD)
		{
			int ticked = 0, files = selFolderFiles(ticked);
			drawBarButton(SEL_ALL, &G_BOX_ON, files && ticked == files ? "None" : "All", "SELECT", CREAM, false, files == 0);
			drawBarButton(SEL_CANCEL, dropping ? nullptr : &G_CROSS, dropping ? "Again: drop " + many(n) : std::string("Cancel"), "B",
				dropping ? YELLOW : CREAM, false, false);
			drawBarButton(SEL_ACT, &G_PLUS, selDest.empty() ? std::string("Add to\xE2\x80\xA6") : "Add " + many(n), "X", CREAM, n > 0, n == 0);
			return;
		}
		drawBarButton(SEL_CANCEL, &G_CROSS, "Cancel", "B", CREAM, false, false);
		drawBarButton(SEL_ACT, &G_FOLDER, "Add this folder", "X", CREAM, true, !pickable());
	}

	void Ui::drawPicker()
	{
		Rect box = pickerBox();
		int n = (int)pickerList.size() + 1;
		drawDialog(box, pickerFolder.empty() ? "Add " + std::to_string(picked.size()) + " to\xE2\x80\xA6"
			: "Add " + quoted(folderName(pickerFolder)) + " to\xE2\x80\xA6");
		for (int i = pickerScroll; i < n && i < pickerScroll + PICKER_SHOWN; i++)
		{
			Rect r = pickerRow(i);
			bool at = i == pickerCursor, down = pressedBtn == PICK_ROW + i;
			if (down) fill(r, PRESSED);
			else if (at) drawFocus(r);
			SDL_Color ink = at ? YELLOW : CREAM;
			int mid = r.y + r.h / 2;
			if (i == 0)
			{
				drawGlyphMid(G_PLUS, r.x + 31, mid, ink);
				drawTextMid("New playlist\xE2\x80\xA6", 1, ink, r.x + 56, mid, r.w - 68);
				continue;
			}
			auto& pl = pickerList[i - 1];
			std::string count = std::to_string(pl.count) + (pl.count == 1 ? " item" : " items");
			int cw = (int)std::lround(textWidth(count, 0) / sx);
			drawTextMid(count, 0, at ? DIM_TEAL : DIM, 512, mid, 0, 1);
			drawGlyphMid(G_PLAYLIST, r.x + 31, mid, ink);
			drawTextMid(pl.name, 1, ink, r.x + 56, mid, 512 - cw - 12 - (r.x + 56));
		}
		// where the list is, when it's longer than the box
		if (n > PICKER_SHOWN)
		{
			int top = pickerRow(pickerScroll).y, height = PICKER_SHOWN * 46 - 4;
			fill({532, top, 4, height}, SCREEN);
			int h = std::max(16, height * PICKER_SHOWN / n);
			fill({532, top + (height - h) * pickerScroll / std::max(1, n - PICKER_SHOWN), 4, h}, DIM);
		}
		drawHints({{"A", "Add"}, {"B", "Cancel"}});
	}

	void Ui::drawBusy()
	{
		Rect box{136, 132, 368, 168};
		drawDialog(box, "Reading folder\xE2\x80\xA6");
		drawTextMid(folderName(addPending.dir), 0, DIM, W / 2, box.y + 64, 328, 2);
		Rect c = buttonRect(BUSY_CANCEL);
		drawDialogRow(c, &G_CROSS, "Cancel", true, pressedBtn == BUSY_CANCEL, CREAM);
		drawHints({{"B", "Cancel"}});
	}

	void Ui::drawSource(const Player& p)
	{
		const PlaySource& src = p.source();
		if (src.id.empty()) return;
		// on the right where it comes from (drawn first: the left gets what's left): a playlist's item, the card or
		// the server it's on
		bool pl = src.kind == SourceKind::Playlist, net = isNetPath(p.currentPath());
		std::string right = !pl ? (src.kind == SourceKind::NetFolder ? "Network" : "Local") : !net ? std::string("Local") : "";
		if (pl && net)
		{
			NetPath np;
			const NetServer* sv = netStore && parseNetPath(p.currentPath(), np) ? netStore->find(np.server) : nullptr;
			right = sv ? serverName(*sv) : "Network";
		}
		int rw = std::min(200, (int)std::lround(textWidth(right, 0) / sx));
		drawTextMid(right, 0, DIM, 616, SOURCE_Y, 200, 1);
		int rightW = rw;
		if (pl)
		{
			const Glyph& g = net ? G_NETWORK : G_CARD;
			drawGlyphMid(g, 616 - rw - 8 - glyphW(g) / 2, SOURCE_Y, DIM);
			rightW += glyphW(g) + 8;
		}
		int maxW = 616 - 52 - rightW - 24;
		if (pl)
		{
			// the playlist's name, cut at its end
			drawGlyphMid(G_PLAYLIST, 35, SOURCE_Y, YELLOW);
			drawTextMid(src.title, 0, CREAM, 52, SOURCE_Y, maxW);
			return;
		}
		// the folder, as the browser says it, its end kept
		drawGlyphMid(src.kind == SourceKind::NetFolder ? G_NETWORK : G_CARD, 35, SOURCE_Y, DIM);
		std::string where = elideLeft(whereOf(src.id), 0, maxW);
		size_t cut = where.rfind(" / ");
		if (cut == std::string::npos) drawTextMid(where, 0, CREAM, 52, SOURCE_Y);
		else
		{
			std::string head = where.substr(0, cut + 3);
			drawTextMid(head, 0, DIM, 52, SOURCE_Y);
			drawTextMid(where.substr(cut + 3), 0, CREAM, 52 + (int)std::lround(textWidth(head, 0) / sx), SOURCE_Y);
		}
	}

	// ---- music: the Display popup (SELECT over a song) ----

	void Ui::displaySet(int row, int value, Player& p)
	{
		Settings s = p.settings;
		if (row == 0) s.musicShow = (value % SHOWS + SHOWS) % SHOWS;
		else s.visualizer = (value % VISUALIZERS + VISUALIZERS) % VISUALIZERS;
		if (s.musicShow != p.settings.musicShow || s.visualizer != p.settings.visualizer) p.setSettings(s);
	}

	bool Ui::displayKey(Button b, Player& p)
	{
		int now = displayRow ? p.settings.visualizer : p.settings.musicShow;
		switch (b)
		{
		case Button::Up: case Button::Down: displayRow = 1 - displayRow; break;
		case Button::Left: case Button::Right: displaySet(displayRow, now + (b == Button::Right ? 1 : -1), p); break;
		case Button::Select: displaySet(0, p.settings.musicShow + 1, p); break;
		case Button::A: case Button::B: displayOpen = false; break;
		default: break;
		}
		return false;
	}

	bool Ui::displayPointer(const Pointer& pt, int x, int y, Player& p)
	{
		if (pt.kind == Pointer::Down)
		{
			pressedBtn = -1;
			// outside: closed ("<" too), the finger lifting presses nothing
			if (!Rect{32, 84, 576, 300}.hit(x, y))
			{
				displayOpen = false;
				swallowTouch = true;
				return false;
			}
			for (int r = 0; r < 2; r++)
				for (int i = 0; i < 4; i++)
					if (displayChip(r, i).hit(x, y))
					{
						pressedBtn = CHIP_BTN + r * 4 + i;
						displayRow = r;
					}
			return false;
		}
		if (pt.kind == Pointer::Up && pressedBtn >= CHIP_BTN && pressedBtn < CHIP_BTN + 8)
		{
			int id = pressedBtn - CHIP_BTN;
			pressedBtn = -1;
			if (displayChip(id / 4, id % 4).hit(x, y)) displaySet(id / 4, id % 4, p);
		}
		return false;
	}

	void Ui::drawDisplay(const Player& p)
	{
		static const char* SHOW_NAMES[SHOWS] = {"Album art", "Art + viz", "Visualizer", "None"};
		static const char* STYLE_NAMES[VISUALIZERS] = {"Bars", "Waves", "Spectrum", "Circle"};
		drawDialog({32, 84, 576, 300}, "Display");
		int show = std::clamp(p.settings.musicShow, 0, SHOWS - 1), vis = std::clamp(p.settings.visualizer, 0, VISUALIZERS - 1);
		// the art alone, or the title alone: no visualizer, its style muted (still changeable, for later)
		bool muted = show == SHOW_ART || show == SHOW_NONE;
		for (int r = 0; r < 2; r++)
		{
			int rowY = displayChip(r, 0).y;
			bool focus = r == displayRow;
			drawTextMid(r ? "Style" : "Show", 0, focus ? YELLOW : CREAM, 52, rowY - 14, 200);
			if (focus) fill({36, rowY, 4, 64}, YELLOW);
			for (int i = 0; i < 4; i++)
			{
				Rect c = displayChip(r, i);
				bool sel = i == (r ? vis : show), dim = r && muted;
				SDL_Color bg = pressedBtn == CHIP_BTN + r * 4 + i ? PRESSED : sel ? (dim ? DIM : YELLOW) : dim ? SDL_Color{0x2A, 0x29, 0x33, 255} : TEAL;
				SDL_Color ink = sel ? BG : dim ? DIM : CREAM;
				fill(c, bg);
				const Glyph* g = r ? VIS_GLYPHS[i] : i == SHOW_BOTH ? &G_ARTVIZ : showGlyph(i, vis);
				drawGlyphMid(*g, c.x + c.w / 2, c.y + 20, ink);
				drawTextMid(r ? STYLE_NAMES[i] : SHOW_NAMES[i], 0, ink, c.x + c.w / 2, c.y + 46, 122, 2);
			}
		}
		const char* help = show == SHOW_ART ? "The style shows with Art + viz or Visualizer"
			: show == SHOW_BOTH ? (vis == VIS_CIRCLE ? "Bars round a ring, the art inside it" : "The visualizer over the bottom of the art")
			: show == SHOW_VIS ? "The visualizer alone" : "Just the title: the lightest on the battery";
		drawTextMid(help, 0, DIM, W / 2, 352, 544, 2);
		drawHints({{"D-PAD", "Change"}, {"SELECT", "Show"}, {"B", "Done"}});
	}

	// ---- music: the Sound page (Equalizer, Effects) ----

	void Ui::openSound(Player& p, bool fromSettings)
	{
		// from Settings (START's menu or not): B goes back there; the page has tabs of its own, so the menu's are put away
		soundFromSettings = fromSettings;
		soundFromMenu = fromSettings && inMenu;
		inMenu = false;
		styleFromSubs = false;
		page = Page::Sound;
		eqDialog = EQD_NONE;
		eqTouch = eqListTouch = false;
		pressedBtn = -1;
		fxCursor = std::clamp((int)p.settings.soundFx, 0, SOUNDFX_COUNT - 1);
	}

	void Ui::closeSound(Player& p)
	{
		// the curve as it stands made what mpv keeps, and written
		p.commitEq();
		eqDialog = EQD_NONE;
		eqTouch = false;
		if (!soundFromSettings)
		{
			page = home(p);
			return;
		}
		page = Page::Settings;
		inMenu = soundFromMenu;
		if (inMenu) menuTab = TAB_SETTINGS;
		setGroup = GROUP_MUSIC;
		cursor = std::max(0, rowsIndexOf(settingRows(), SOUND_ROW));
		scroll = 0;
	}

	bool Ui::eqIsUser(const Player& p, const std::string& name) const
	{
		for (auto& u : p.eqUserPresets())
			if (u.name == name) return true;
		return false;
	}

	bool Ui::eqOriginIsUser(const Player& p) const
	{
		return eqCustom(p) && !p.settings.eqOrigin.empty() && eqIsUser(p, p.settings.eqOrigin);
	}

	bool Ui::eqCanReset(const Player& p) const
	{
		if (!eqCustom(p) || p.settings.eqOrigin.empty()) return false;
		for (auto& e : p.eqPresets())
			if (e.name == p.settings.eqOrigin) return true;
		return false;
	}

	std::vector<std::string> Ui::eqCycle(const Player& p) const
	{
		std::vector<std::string> out;
		for (auto& e : p.eqPresets()) out.push_back(e.name);
		if (eqCustom(p) || stashOn) out.push_back(CUSTOM_NAME);
		return out;
	}

	void Ui::eqPick(const std::string& name, Player& p)
	{
		Settings s = p.settings;
		if (name == CUSTOM_NAME)
		{
			// Custom back as it was put aside
			if (!eqCustom(p) && stashOn)
			{
				s.eqPreset.clear();
				s.eqOrigin = stashOrigin;
				s.eqGains = stashGains;
				stashOn = false;
			}
		}
		else
		{
			const EqPreset* pr = nullptr;
			for (auto& e : p.eqPresets())
				if (e.name == name) pr = &e;
			if (!pr) return;
			// Custom put aside, not lost: still in the cycle and the list
			if (eqCustom(p))
			{
				stashOn = true;
				stashOrigin = s.eqOrigin;
				stashGains = s.eqGains;
			}
			s.eqPreset = pr->name;
			s.eqOrigin.clear();
			s.eqGains = pr->gains;
		}
		s.eqOn = true;	// picking one is wanting it on
		p.setSettings(s);
		p.showEqCard();
	}

	void Ui::eqStep(int dir, Player& p)
	{
		auto c = eqCycle(p);
		int n = (int)c.size(), at = -1;
		if (eqCustom(p)) at = n - 1;
		else
			for (int i = 0; i < n; i++)
				if (c[i] == p.settings.eqPreset) at = i;
		if (n == 0) return;
		eqPick(c[at < 0 ? (dir > 0 ? 0 : n - 1) : (at + dir + n) % n], p);
	}

	void Ui::eqSetBand(int band, double gain, Player& p)
	{
		if (band < 0 || band >= EQ_BANDS) return;
		gain = clampEqGain(gain);
		Settings s = p.settings;
		if (s.eqGains[band] == gain) return;
		// the first edit of a preset: Custom, made from it (a Custom put aside before is replaced: there's one)
		if (!eqCustom(p))
		{
			s.eqOrigin = s.eqPreset;
			s.eqPreset.clear();
			stashOn = false;
		}
		s.eqOn = true;
		s.eqGains[band] = gain;
		// heard at once, written when the page closes (commitEq): a drag is many of these
		p.setSettings(s, false);
	}

	void Ui::eqReset(Player& p)
	{
		if (!eqCanReset(p)) return;
		Settings s = p.settings;
		for (auto& e : p.eqPresets())
			if (e.name == s.eqOrigin) s.eqGains = e.gains;
		s.eqPreset = s.eqOrigin;
		s.eqOrigin.clear();
		stashOn = false;
		p.setSettings(s);
		p.showEqCard();
	}

	void Ui::eqToggle(Player& p)
	{
		Settings s = p.settings;
		s.eqOn = !s.eqOn;
		p.setSettings(s);
		p.showEqCard();
	}

	void Ui::eqSave(Player& p)
	{
		if (!eqCustom(p)) return;	// Saved: nothing to save
		if (eqOriginIsUser(p))
		{
			// over the preset of yours it came from: Custom goes, that one plays
			std::string name = p.settings.eqOrigin;
			if (p.saveEqPreset(name, p.settings.eqGains, true) == EqNameError::None) p.osd("Saved " + quoted(name));
			return;
		}
		eqNameOpen(false, p);
	}

	void Ui::eqNameOpen(bool rename, Player& p)
	{
		eqRenaming = rename;
		eqNameNote.clear();
		eqNameSubmit = false;
		std::string text;
		if (rename)
		{
			eqRenameFrom = p.settings.eqPreset;
			text = eqRenameFrom;
		}
		else
		{
			// "Rock 2" (or the first number free); a curve from nowhere, "My preset"
			std::string base = eqCustom(p) ? p.settings.eqOrigin : p.settings.eqPreset;
			bool none = base.empty();
			if (none) base = "My preset";
			text = none ? base : base + " 2";
			for (int n = 2; n < 100 && checkEqPresetName(text, p.eqUserPresets()) == EqNameError::Taken; n++)
				text = base + " " + std::to_string(none ? n : n + 1);
		}
		openKeyboard(EQ_NAME, text);
	}

	void Ui::eqNameDone(Player& p)
	{
		eqNameSubmit = false;
		std::string name = trimmed(kbEdit.text);
		EqNameError e = eqRenaming ? p.renameEqPreset(eqRenameFrom, name) : p.saveEqPreset(name, p.settings.eqGains, false);
		switch (e)
		{
		case EqNameError::None:
			eqNameNote.clear();
			kbOpen = false;
			kbPressed = -1;
			if (eqRenaming && stashOrigin == eqRenameFrom) stashOrigin = name;
			p.osd(eqRenaming ? "Renamed to " + quoted(name) : "Saved " + quoted(name));
			return;
		// what's wrong, under the field; the keyboard stays up to put it right
		case EqNameError::Empty: eqNameNote = "Type a name"; break;
		case EqNameError::TooLong: eqNameNote = "24 characters at most"; break;
		case EqNameError::Builtin: eqNameNote = quoted(name) + " is a built-in name"; break;
		case EqNameError::Taken: eqNameNote = "You have one called " + quoted(name); break;
		default: eqNameNote = "That name can't be used"; break;
		}
		kbActiveAt = SDL_GetTicks();
	}

	std::vector<int> Ui::eqOptions(const Player& p) const
	{
		if (eqCustom(p))
		{
			std::vector<int> o = {EQO_LIST};
			if (eqOriginIsUser(p)) o.push_back(EQO_SAVE_TO);
			o.push_back(EQO_SAVE_AS);
			if (eqCanReset(p)) o.push_back(EQO_RESET);
			o.push_back(EQO_CANCEL);
			return o;
		}
		if (eqIsUser(p, p.settings.eqPreset)) return {EQO_LIST, EQO_SAVE_AS, EQO_RENAME, EQO_DELETE, EQO_CANCEL};
		return {EQO_LIST, EQO_SAVE_AS, EQO_CANCEL};
	}

	void Ui::eqChooseOption(int item, Player& p)
	{
		auto items = eqOptions(p);
		int what = item >= 0 && item < (int)items.size() ? items[item] : EQO_CANCEL;
		eqDialog = EQD_NONE;
		switch (what)
		{
		case EQO_LIST: openEqList(p); break;
		case EQO_SAVE_TO: eqSave(p); break;
		case EQO_SAVE_AS: eqNameOpen(false, p); break;
		case EQO_RENAME: eqNameOpen(true, p); break;
		case EQO_DELETE:
			eqDialog = EQD_DELETE;
			eqDlgCursor = 1;	// on Cancel: a preset gone for good wants a step more
			break;
		case EQO_RESET: eqReset(p); break;
		default: break;
		}
	}

	std::vector<Ui::EqEntry> Ui::eqEntries(const Player& p) const
	{
		std::vector<EqEntry> e;
		e.push_back({0, "Built-in"});
		for (auto& pr : p.eqPresets())
			if (pr.builtin) e.push_back({1, pr.name});
		if (!p.eqUserPresets().empty())
		{
			e.push_back({0, "Yours"});
			for (auto& pr : p.eqUserPresets()) e.push_back({1, pr.name});
		}
		if (eqCustom(p) || stashOn)
		{
			e.push_back({0, "Unsaved"});
			e.push_back({2, CUSTOM_NAME});
		}
		return e;
	}

	// the list's room: y 84 to 404, a header 28 tall, a preset 38
	static const int EQ_LIST_TOP = 84, EQ_LIST_END = 404;

	int Ui::eqListBottom(const std::vector<EqEntry>& e, int first) const
	{
		int y = EQ_LIST_TOP, i = first;
		for (; i < (int)e.size(); i++)
		{
			int h = e[i].kind ? 38 : 28;
			if (y + h > EQ_LIST_END) break;
			y += h;
		}
		return i;
	}

	void Ui::eqListFit(const Player& p)
	{
		auto e = eqEntries(p);
		int n = (int)e.size(), c = std::clamp(eqDlgCursor, 0, std::max(0, n - 1));
		int maxScroll = 0;
		while (maxScroll < n && eqListBottom(e, maxScroll) < n) maxScroll++;
		// its section's header in view with it, where it can be
		if (c < eqListScroll + 1) eqListScroll = c > 0 && e[c - 1].kind == 0 ? c - 1 : c;
		while (eqListScroll < c && eqListBottom(e, eqListScroll) <= c) eqListScroll++;
		eqListScroll = std::clamp(eqListScroll, 0, maxScroll);
	}

	void Ui::eqListMove(int dir, const Player& p)
	{
		auto e = eqEntries(p);
		int n = (int)e.size();
		for (int k = 0, c = eqDlgCursor; k < n; k++)
		{
			c = (c + dir + n) % n;
			if (e[c].kind) { eqDlgCursor = c; break; }
		}
		eqListFit(p);
	}

	void Ui::openEqList(const Player& p)
	{
		eqDialog = EQD_LIST;
		eqListScroll = 0;
		eqDlgCursor = 1;
		auto e = eqEntries(p);
		for (int i = 0; i < (int)e.size(); i++)
			if ((e[i].kind == 2 && eqCustom(p)) || (e[i].kind == 1 && !eqCustom(p) && e[i].name == p.settings.eqPreset)) eqDlgCursor = i;
		eqListFit(p);
	}

	void Ui::fxChoose(int i, Player& p)
	{
		if (i < 0 || i >= SOUNDFX_COUNT) return;
		fxCursor = i;
		Settings s = p.settings;
		if (s.soundFx != (SoundFx)i)
		{
			s.soundFx = (SoundFx)i;
			p.setSettings(s);
		}
		p.showEqCard();
	}

	bool Ui::soundKey(Button b, Player& p)
	{
		if (eqDialog == EQD_OPTIONS || eqDialog == EQD_DELETE)
		{
			int n = eqDialog == EQD_OPTIONS ? (int)eqOptions(p).size() : 2;
			eqDlgCursor = std::clamp(eqDlgCursor, 0, n - 1);
			switch (b)
			{
			case Button::Up: eqDlgCursor = (eqDlgCursor + n - 1) % n; break;
			case Button::Down: eqDlgCursor = (eqDlgCursor + 1) % n; break;
			case Button::A:
				if (eqDialog == EQD_OPTIONS) eqChooseOption(eqDlgCursor, p);
				else
				{
					eqDialog = EQD_NONE;
					if (eqDlgCursor == 0)
					{
						// the one playing: Custom now, the same curve (nothing heard changes)
						std::string name = p.settings.eqPreset;
						if (p.deleteEqPreset(name))
						{
							if (stashOrigin == name) stashOrigin.clear();
							p.showEqCard();
							p.osd("Deleted " + quoted(name));
						}
					}
				}
				break;
			case Button::B: case Button::Start: eqDialog = EQD_NONE; break;
			default: break;
			}
			return false;
		}
		if (eqDialog == EQD_LIST)
		{
			switch (b)
			{
			case Button::Up: case Button::Down: eqListMove(b == Button::Down ? 1 : -1, p); break;
			case Button::A:
			{
				auto e = eqEntries(p);
				eqDialog = EQD_NONE;
				if (eqDlgCursor >= 0 && eqDlgCursor < (int)e.size() && e[eqDlgCursor].kind) eqPick(e[eqDlgCursor].name, p);
				break;
			}
			case Button::B: case Button::Start: eqDialog = EQD_NONE; break;
			default: break;
			}
			return false;
		}
		// L/R: the tabs (L the left one, R the right one), on either tab
		if (b == Button::L || b == Button::R)
		{
			soundTab = b == Button::R ? SOUND_FX : SOUND_EQ;
			return false;
		}
		if (b == Button::B)
		{
			closeSound(p);
			return false;
		}
		if (soundTab == SOUND_FX)
		{
			switch (b)
			{
			case Button::Up: fxCursor = (fxCursor + SOUNDFX_COUNT - 1) % SOUNDFX_COUNT; break;
			case Button::Down: fxCursor = (fxCursor + 1) % SOUNDFX_COUNT; break;
			case Button::A: fxChoose(fxCursor, p); break;
			default: break;
			}
			return false;
		}
		switch (b)
		{
		case Button::Left: eqBand = std::max(0, eqBand - 1); break;
		case Button::Right: eqBand = std::min(EQ_BANDS - 1, eqBand + 1); break;
		case Button::Up: case Button::Down: eqSetBand(eqBand, p.settings.eqGains[eqBand] + (b == Button::Up ? 1 : -1), p); break;
		// Y (west) the preset before, X the one after
		case Button::Y: case Button::X: eqStep(b == Button::X ? 1 : -1, p); break;
		case Button::Select: eqToggle(p); break;
		case Button::A: eqSave(p); break;
		case Button::Start:
			eqDialog = EQD_OPTIONS;
			eqDlgCursor = 0;
			break;
		default: break;
		}
		return false;
	}

	bool Ui::soundPointer(const Pointer& pt, int x, int y, Player& p)
	{
		Uint32 now = SDL_GetTicks();
		auto offBox = [&](Rect box) {
			// a finger outside a dialog closes it (and its lift presses nothing)
			if (box.hit(x, y)) return false;
			eqDialog = EQD_NONE;
			pressedBtn = -1;
			swallowTouch = true;
			return true;
		};
		if (eqDialog == EQD_LIST)
		{
			auto e = eqEntries(p);
			auto entryAt = [&](int py) {
				int ey = EQ_LIST_TOP, end = eqListBottom(e, eqListScroll);
				for (int i = eqListScroll; i < end; i++)
				{
					int h = e[i].kind ? 38 : 28;
					if (py >= ey && py < ey + h) return e[i].kind ? i : -1;
					ey += h;
				}
				return -1;
			};
			if (pt.kind == Pointer::Down)
			{
				if (offBox({96, 22, 448, 392})) return false;
				pressedBtn = -1;
				eqListTouch = true;
				listScrolled = false;
				touchStartY = y;
				scrollStart = eqListScroll;
				int i = entryAt(y);
				if (i >= 0) pressedBtn = DLG_ROW + i;
				return false;
			}
			if (!eqListTouch) return false;
			if (pt.kind == Pointer::Move && (listScrolled || std::abs(y - touchStartY) > 12))
			{
				// a drag scrolls it
				listScrolled = true;
				pressedBtn = -1;
				int maxScroll = 0, n = (int)e.size();
				while (maxScroll < n && eqListBottom(e, maxScroll) < n) maxScroll++;
				eqListScroll = std::clamp(scrollStart + (int)std::lround((touchStartY - y) / 38.0), 0, maxScroll);
			}
			else if (pt.kind == Pointer::Up)
			{
				eqListTouch = false;
				int i = pressedBtn - DLG_ROW;
				pressedBtn = -1;
				if (!listScrolled && i >= 0 && entryAt(y) == i)
				{
					eqDlgCursor = i;
					eqDialog = EQD_NONE;
					eqPick(e[i].name, p);
				}
			}
			return false;
		}
		if (eqDialog == EQD_OPTIONS || eqDialog == EQD_DELETE)
		{
			bool opts = eqDialog == EQD_OPTIONS;
			int n = opts ? (int)eqOptions(p).size() : 2;
			auto row = [&](int i) { return opts ? eqOptRow(i, p) : Rect{128, 196 + i * 52, 384, 46}; };
			if (pt.kind == Pointer::Down)
			{
				if (offBox(opts ? eqOptBox(p) : Rect{112, 116, 416, 204})) return false;
				pressedBtn = -1;
				for (int i = 0; i < n; i++)
					if (row(i).hit(x, y)) pressedBtn = DLG_ROW + i;
			}
			else if (pt.kind == Pointer::Up && pressedBtn >= DLG_ROW)
			{
				int i = pressedBtn - DLG_ROW;
				pressedBtn = -1;
				if (i < n && row(i).hit(x, y))
				{
					eqDlgCursor = i;
					soundKey(Button::A, p);
				}
			}
			return false;
		}
		if (eqTouch)
		{
			// a finger on a band: the gain follows it
			if (pt.kind == Pointer::Move)
			{
				if (std::abs(y - eqDownY) > 8) eqMoved = true;
				eqSetBand(eqBand, std::round((264 - y) / 8.0), p);
			}
			else if (pt.kind == Pointer::Up)
			{
				eqTouch = false;
				// two taps on it, no drag: back to 0 dB
				if (!eqMoved && eqTapAt && now - eqTapAt < 350 && eqTapBand == eqBand)
				{
					eqSetBand(eqBand, 0, p);
					eqTapAt = 0;
				}
				else
				{
					eqTapAt = eqMoved ? 0 : now;
					eqTapBand = eqBand;
				}
			}
			return false;
		}
		if (pressedBtn >= 0)
		{
			// a button acts as the finger lifts on it
			if (pt.kind != Pointer::Up) return false;
			int id = pressedBtn;
			pressedBtn = -1;
			if (!buttonRect(id).hit(x, y)) return false;
			if (id == BACK_ICON) closeSound(p);
			else if (id == SOUND_TAB0 || id == SOUND_TAB1) soundTab = id - SOUND_TAB0;
			else if (id >= FX_ROW && id < FX_ROW + SOUNDFX_COUNT) fxChoose(id - FX_ROW, p);
			else if (id == EQ_SWITCH) eqToggle(p);
			else if (id == EQ_PREV || id == EQ_NEXT) eqStep(id == EQ_NEXT ? 1 : -1, p);
			else if (id == EQ_NAME_ZONE) openEqList(p);
			else if (id == EQ_RESET) eqReset(p);
			else if (id == EQ_SAVE) eqSave(p);
			else if (id == EQ_OPTIONS)
			{
				eqDialog = EQD_OPTIONS;
				eqDlgCursor = 0;
			}
			return false;
		}
		if (pt.kind != Pointer::Down) return false;
		if (buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
		for (int t = 0; t < 2; t++)
			if (t != soundTab && soundTabRect(t).hit(x, y)) { pressedBtn = SOUND_TAB0 + t; return false; }
		if (soundTab == SOUND_FX)
		{
			for (int i = 0; i < SOUNDFX_COUNT; i++)
				if (buttonRect(FX_ROW + i).hit(x, y))
				{
					fxCursor = i;
					pressedBtn = FX_ROW + i;
				}
			return false;
		}
		for (int id : {EQ_SWITCH, EQ_PREV, EQ_NEXT, EQ_NAME_ZONE, EQ_RESET, EQ_SAVE, EQ_OPTIONS})
			if (buttonRect(id).hit(x, y)) { pressedBtn = id; return false; }
		// a band's column: focused, its gain where the finger is (and follows it)
		if (y >= 134 && y < 396)
			for (int i = 0; i < EQ_BANDS; i++)
			{
				int cx = 88 + 56 * i;
				if (x < cx - 28 || x >= cx + 28) continue;
				if (eqTapBand != i) eqTapAt = 0;
				eqBand = i;
				eqTouch = true;
				eqMoved = false;
				eqDownY = y;
				eqSetBand(i, std::round((264 - y) / 8.0), p);
			}
		return false;
	}

	void Ui::drawSoundHeader(const Player& p)
	{
		drawBackIcon();
		// as Audio & Subtitles' tabs: the open one lit; the other with its key on its outer side (L left, R right)
		static const char* NAMES[2] = {"Equalizer", "Effects"};
		for (int t = 0; t < 2; t++)
		{
			Rect r = soundTabRect(t);
			if (t == soundTab) { drawTab(r, NAMES[t], true); continue; }
			fill(r, pressedBtn == SOUND_TAB0 + t ? PRESSED : TEAL);
			const char* key = t == SOUND_EQ ? "L" : "R";
			int kw = (int)std::lround(textWidth(key, 0) / sx), mid = r.y + r.h / 2;
			int left = r.x, right = r.x + r.w;
			if (t == SOUND_EQ)
			{
				drawTextMid(key, 0, YELLOW, r.x + 14, mid);
				left = r.x + 14 + kw;
			}
			else
			{
				drawTextMid(key, 0, YELLOW, r.x + r.w - 14, mid, 0, 1);
				right = r.x + r.w - 14 - kw;
			}
			drawTextMid(NAMES[t], 1, CREAM, (left + right) / 2, mid, right - left - 8, 2);
		}
		drawBattery(616, 24, p);
	}

	// a band's gain as its label: "+3", "0", "-4" ("+2.5" between whole ones)
	static std::string gainText(double g)
	{
		char b[16];
		int whole = (int)std::lround(g);
		if (std::fabs(g - whole) >= 0.05) snprintf(b, sizeof b, "%+.1f", g);
		else if (whole) snprintf(b, sizeof b, "%+d", whole);
		else return "0";
		return b;
	}

	void Ui::drawEqualizer(const Player& p)
	{
		static const char* FREQS[EQ_BANDS] = {"31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"};
		const Settings& st = p.settings;
		bool on = st.eqOn, custom = eqCustom(p);
		// the switch: a dark track, the knob lit yellow on the right when on, grey on the left when off (the colour
		// sits where the switch points, as a phone's does)
		fill(buttonRect(EQ_SWITCH), pressedBtn == EQ_SWITCH ? PRESSED : TEAL);
		fill({30, 84, 48, 24}, SCREEN);
		fill({on ? 54 : 32, 86, 22, 20}, on ? YELLOW : DIM);
		drawTextMid(on ? "On" : "Off", 1, on ? CREAM : DIM, 92, 96);
		// the preset: Y and X either side of its name, what it is under it
		fill({160, 70, 464, 52}, TEAL);
		for (int id : {EQ_PREV, EQ_NEXT, EQ_NAME_ZONE})
			if (pressedBtn == id) fill(buttonRect(id), PRESSED);
		drawTextMid("Y", 0, YELLOW, 174, 96);
		drawGlyphMid(G_LEFT, 196, 96, CREAM);
		drawGlyphMid(G_RIGHT, 588, 96, CREAM);
		drawTextMid("X", 0, YELLOW, 610, 96);
		drawTextMid(custom ? CUSTOM_NAME : st.eqPreset, 1, on ? CREAM : DIM, 392, 88, 340, 2);
		if (custom)
			drawTextMid(st.eqOrigin.empty() ? std::string("Unsaved") : "Unsaved \xC2\xB7 from " + st.eqOrigin, 0, YELLOW, 392, 110, 340, 2);
		else drawTextMid(eqIsUser(p, st.eqPreset) ? "Yours" : "Built-in", 0, DIM_TEAL, 392, 110, 340, 2);
		// the scale, and the band the cursor is on lit behind it
		int fx = 88 + 56 * eqBand;
		fill({fx - 27, 134, 54, 262}, SURFACE);
		fill({fx - 27, 394, 54, 3}, YELLOW);
		drawTextMid("dB", 0, DIM, 54, 148, 0, 1);
		drawTextMid("Hz", 0, DIM, 54, 382, 0, 1);
		static const struct { const char* label; int y; } SCALE[] = {{"+12", 168}, {"+6", 216}, {"0", 264}, {"-6", 312}, {"-12", 360}};
		for (auto& m : SCALE)
		{
			drawTextMid(m.label, 0, DIM, 54, m.y, 0, 1);
			if (m.y != 264) fill({60, m.y, 560, 1}, {0x2A, 0x28, 0x33, 255});
		}
		fill({60, 263, 560, 2}, SCREEN);
		for (int i = 0; i < EQ_BANDS; i++)
		{
			int cx = 88 + 56 * i;
			double g = st.eqGains[i];
			int ky = 264 - (int)std::lround(g * 8);
			bool focus = i == eqBand;
			fill({cx - 2, 168, 4, 192}, SCREEN);
			// from 0 dB to the gain
			SDL_Color bar = !on ? DIM : focus ? YELLOW : PRESSED;
			if (ky < 264) fill({cx - 4, ky, 8, 264 - ky}, bar);
			else if (ky > 264) fill({cx - 4, 264, 8, ky - 264}, bar);
			fill({cx - 16, ky - 6, 32, 12}, !on ? DIM : focus ? YELLOW : CREAM);
			fill({cx - 10, ky - 1, 20, 2}, BG);
			drawTextMid(gainText(g), 0, focus ? YELLOW : std::fabs(g) >= 0.05 ? CREAM : DIM, cx, 148, 0, 2);
			drawTextMid(FREQS[i], 0, focus ? YELLOW : DIM, cx, 382, 0, 2);
		}
		// Reset (touch, or Options), Save, Options: each its key on its right
		auto action = [&](int id, const Glyph& g, const std::string& label, const char* key, bool dim) {
			Rect r = buttonRect(id);
			fill(r, pressedBtn == id && !dim ? PRESSED : TEAL);
			SDL_Color ink = dim ? DIM : CREAM;
			int gw = glyphW(g), kw = key ? (int)std::lround(textWidth(key, 0) / sx) : 0, mid = r.y + r.h / 2;
			drawGlyphMid(g, r.x + 14 + gw / 2, mid, ink);
			drawTextMid(label, 0, ink, r.x + 14 + gw + 8, mid, r.w - 14 - gw - 8 - (key ? kw + 20 : 10));
			if (key) drawTextMid(key, 0, dim ? DIM : YELLOW, r.x + r.w - 12, mid, 0, 1);
		};
		action(EQ_RESET, G_REFRESH, "Reset", nullptr, !eqCanReset(p));
		action(EQ_SAVE, G_CHECK, !custom ? "Saved" : eqOriginIsUser(p) ? "Save" : "Save as...", "A", !custom);
		action(EQ_OPTIONS, G_GEAR, "Options", "START", false);
		drawHints({{"D-PAD", "Adjust"}, {"Y/X", "Preset"}, {"SELECT", "On/Off"}, {"R", "Effects"}, {"B", "Back"}});
	}

	void Ui::drawEffects(const Player& p)
	{
		int inUse = (int)p.settings.soundFx;
		for (int i = 0; i < SOUNDFX_COUNT; i++)
		{
			Rect r = buttonRect(FX_ROW + i);
			int mid = r.y + r.h / 2;
			bool at = i == fxCursor;
			if (pressedBtn == FX_ROW + i) fill(r, PRESSED);
			else if (at) drawFocus(r);
			// the one in use: a filled ring (the cursor is only where it is)
			drawGlyphMid(i == inUse ? G_RADIO_ON : G_RADIO, 41, mid, i == inUse ? YELLOW : at ? CREAM : DIM);
			drawTextMid(soundFxName((SoundFx)i), 1, at ? YELLOW : CREAM, 66, mid, 270);
			drawTextMid(FX_ABOUT[i], 0, at ? DIM_TEAL : DIM, 608, mid, 260, 1);
		}
		if (!p.playingAudio()) drawTextMid("Plays on music, not videos", 0, DIM, W / 2, 436, 0, 2);
		drawHints({{"A", "Choose"}, {"L", "Equalizer"}, {"B", "Back"}});
	}

	void Ui::drawSound(const Player& p)
	{
		drawSoundHeader(p);
		if (soundTab == SOUND_FX) drawEffects(p);
		else drawEqualizer(p);
		const Settings& st = p.settings;
		std::string name = eqCustom(p) ? CUSTOM_NAME : st.eqPreset;
		if (eqDialog == EQD_OPTIONS)
		{
			// as the browser's menu: its rows, the preset's name over them
			drawDialog(eqOptBox(p), name);
			auto items = eqOptions(p);
			for (int i = 0; i < (int)items.size(); i++)
			{
				int it = items[i];
				std::string label = it == EQO_LIST ? "All presets..." : it == EQO_SAVE_TO ? "Save to " + quoted(st.eqOrigin)
					: it == EQO_SAVE_AS ? "Save as new preset..." : it == EQO_RENAME ? "Rename..." : it == EQO_DELETE ? "Delete..."
					: it == EQO_RESET ? "Reset to " + quoted(st.eqOrigin) : "Cancel";
				const Glyph* g = it == EQO_LIST ? &G_NOTE : it == EQO_SAVE_TO ? &G_CHECK : it == EQO_SAVE_AS ? &G_PLUS
					: it == EQO_RENAME ? &G_PENCIL : it == EQO_DELETE ? &G_TRASH : it == EQO_RESET ? &G_REFRESH : &G_CROSS;
				drawDialogRow(eqOptRow(i, p), g, label, i == eqDlgCursor, pressedBtn == DLG_ROW + i,
					it == EQO_DELETE ? RED : it == EQO_CANCEL ? DIM : CREAM);
			}
			drawHints({{"A", "Choose"}, {"B", "Close"}});
		}
		else if (eqDialog == EQD_DELETE)
		{
			drawDialog({112, 116, 416, 204}, "Delete " + quoted(name) + "?");
			drawTextMid("Your preset goes for good", 0, DIM, W / 2, 176, 380, 2);
			drawDialogRow({128, 196, 384, 46}, &G_TRASH, "Delete", eqDlgCursor == 0, pressedBtn == DLG_ROW, RED);
			drawDialogRow({128, 248, 384, 46}, &G_CROSS, "Cancel", eqDlgCursor == 1, pressedBtn == DLG_ROW + 1, DIM);
			drawHints({{"A", "Choose"}, {"B", "Cancel"}});
		}
		else if (eqDialog == EQD_LIST)
		{
			drawDialog({96, 22, 448, 392}, "Presets");
			auto e = eqEntries(p);
			int y = EQ_LIST_TOP, end = eqListBottom(e, eqListScroll);
			for (int i = eqListScroll; i < end; i++)
			{
				if (!e[i].kind)
				{
					// a section's name: not a row to choose
					drawTextMid(e[i].name, 0, DIM, 120, y + 14);
					y += 28;
					continue;
				}
				Rect r{112, y, 416, 38};
				bool at = i == eqDlgCursor;
				bool active = e[i].kind == 2 ? eqCustom(p) : !eqCustom(p) && e[i].name == st.eqPreset;
				if (pressedBtn == DLG_ROW + i) fill(r, PRESSED);
				else if (at) drawFocus(r);
				drawGlyphMid(active ? G_RADIO_ON : G_RADIO, 132, r.y + r.h / 2, active ? YELLOW : at ? CREAM : DIM);
				drawTextMid(e[i].name, 1, at ? YELLOW : CREAM, 156, r.y + r.h / 2, 356);
				y += 38;
			}
			// a long list: where its window is
			int n = (int)e.size(), maxScroll = 0;
			while (maxScroll < n && eqListBottom(e, maxScroll) < n) maxScroll++;
			if (maxScroll > 0)
			{
				fill({532, EQ_LIST_TOP, 4, EQ_LIST_END - EQ_LIST_TOP}, SCREEN);
				int shown = end - eqListScroll, h = std::max(16, (EQ_LIST_END - EQ_LIST_TOP) * shown / n);
				fill({532, EQ_LIST_TOP + (EQ_LIST_END - EQ_LIST_TOP - h) * eqListScroll / maxScroll, 4, h}, DIM);
			}
			drawHints({{"A", "Choose"}, {"B", "Close"}});
		}
	}
}
