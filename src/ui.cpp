#include "ui.h"
#include "format.h"
#include <algorithm>
#include <cmath>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	// BananaOS's colours
	static const SDL_Color BG{0x17, 0x16, 0x1C, 255}, CREAM{0xF1, 0xED, 0xE2, 255}, YELLOW{0xF0, 0xB4, 0x29, 255},
		TEAL{0x23, 0x43, 0x46, 255}, DIM{0x8A, 0x86, 0x7C, 255}, RED{0xE0, 0x5A, 0x4F, 255}, SCREEN{0x3A, 0x3A, 0x44, 255},
		PRESSED{0x3A, 0x6E, 0x72, 255};
	static const int SIZES[3] = {18, 24, 32};
	static const Uint32 QUIT_WINDOW_MS = 2000, SCRUB_MS = 100;
	static const double BACK_S = 5, FORWARD_S = 10;

	constexpr Ui::Rect Ui::BAR;

	static float sx = 1, sy = 1;	// logical 640x480 -> the panel's pixels

	// the two rows of buttons: what each does is the button it stands for
	static const struct { Button button; const char* hint; } GRID[2][5] = {
		{{Button::L, "L"}, {Button::Left, "LEFT"}, {Button::A, "A"}, {Button::Right, "RIGHT"}, {Button::R, "R"}},
		{{Button::X, "X"}, {Button::Y, "Y"}, {Button::Select, "SELECT"}, {Button::Up, "UP/DOWN"}, {Button::Start, "START"}},
	};

	// the settings' on/off rows, after the style's
	static const char* REMEMBER_LABELS[8] = {"Remember position", "Remember audio track", "Remember subtitles",
		"Remember last folder", "Keep play history", "Button hotkeys", "Show hidden folders", "Battery percentage"};
	static bool Settings::*const REMEMBER[8] = {&Settings::rememberPosition, &Settings::rememberAudio, &Settings::rememberSubs,
		&Settings::rememberFolder, &Settings::keepHistory, &Settings::buttonHotkeys, &Settings::showHiddenFolders,
		&Settings::batteryPercent};
	static const char* RECENT = "\x01recent";
	static const char* SHOW_SHORT[SHOWS] = {"Art", "Art+Viz", "Viz", "None"};	// MusicShow on a button	// the browser's recent videos: not a folder

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

	std::vector<std::pair<TTF_Font*, std::string>> Ui::runs(const std::string& s, int size) const
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
		TrackType type = page == Page::Audio ? TrackType::Audio : TrackType::Sub;
		auto list = p.tracksOf(type);
		if (type == TrackType::Sub) out.push_back({0, "Off", p.selected(TrackType::Sub) == nullptr});
		for (size_t i = 0; i < list.size(); i++) out.push_back({list[i].id, trackLabel(list[i], (int)i + 1), list[i].selected});
		if (type == TrackType::Sub) out.push_back({STYLE_ROW, "Subtitle style...", false});
		if (out.empty()) out.push_back({-1, "No audio in this file", false});
		return out;
	}

	void Ui::openMenu(Page pg, const Player& p)
	{
		page = pg;
		cursor = scroll = 0;
		// on the first setting: Quit is one UP away, not under a stray A
		if (pg == Page::Settings) { cursor = STYLE_ROW0; return; }
		auto r = rows(p);
		for (size_t i = 0; i < r.size(); i++)
			if (r[i].selected) cursor = (int)i;
		scroll = std::max(0, cursor - ROWS_SHOWN + 1);
	}

	void Ui::choose(int i, Player& p)
	{
		auto r = rows(p);
		if (i < 0 || i >= (int)r.size()) return;
		if (page == Page::Browse)
		{
			if (r[i].id < 0) return;
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
				openRequest = path;
				if (browseDir != RECENT) p.setLastFolder(browseDir);
			}
			return;
		}
		if (r[i].id == STYLE_ROW) { openMainMenu(p, TAB_SETTINGS); return; }
		if (r[i].id < 0) return;
		TrackType type = page == Page::Audio ? TrackType::Audio : TrackType::Sub;
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
		bool there = p.settings.rememberFolder && !last.empty() && stat(last.c_str(), &st) == 0 && S_ISDIR(st.st_mode) &&
			(p.settings.showHiddenFolders || (!isHiddenFolder(last) && !insideHiddenFolder(last)));
		enterFolder(there ? last : "", p);
	}

	void Ui::showBrowser(Player& p, const std::string& lastPlayed)
	{
		page = Page::Browse;
		touchLocked = false;	// the lock is the video's
		// listed again either way: positions changed, a file may have come or gone
		enterFolder(lastPlayed.empty() ? browseDir : parentFolder(lastPlayed), p);
		for (size_t i = 0; i < browsePaths.size(); i++)
			if (browsePaths[i] == lastPlayed) cursor = (int)i;
		fitList();
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
		placeIn[browseDir] = {cursor, scroll};
		browseDir = dir;
		browseRows.clear();
		browsePaths.clear();
		auto add = [&](int64_t id, const std::string& label, const std::string& path, const std::string& note) {
			browseRows.push_back({id, label, false, note});
			browsePaths.push_back(path);
		};
		auto at = [&](const std::string& path) {
			const ResumeEntry* e = p.resumeStore().find(path);
			return e && e->position >= 0 ? "at " + formatTime(e->position) : std::string();
		};
		if (dir.empty())
		{
			int recent = 0;
			for (auto& path : p.history().items())
				if (exists(path) && (p.settings.showHiddenFolders || !insideHiddenFolder(path))) recent++;
			if (p.settings.keepHistory && recent) add(1, "Recently watched", RECENT, std::to_string(recent));
			for (auto& r : roots) add(1, r.label, r.path, r.path);
		}
		else if (dir == RECENT && !p.settings.keepHistory) add(-1, "Play history is off (Settings)", "", "");
		else if (dir == RECENT)
		{
			// the history (a file taken away or on a card taken out left out), and a way to clear it
			for (auto& path : p.history().items())
				if (exists(path) && (p.settings.showHiddenFolders || !insideHiddenFolder(path))) add(2, baseName(path), path, at(path));
			if (!browseRows.empty()) add(3, "Clear play history", "", clearArmed ? "again to clear" : "tap twice");
		}
		else
		{
			bool ok;
			for (auto& e : listFolder(dir, ok, p.settings.showHiddenFolders))
				add(e.dir ? 1 : 2, e.dir ? e.name + "/" : e.name, e.path, e.dir ? (isHiddenFolder(e.path) ? "hidden" : "") : at(e.path));
			if (!ok) add(-1, "This folder can't be opened", "", "");
		}
		if (browseRows.empty()) add(-1, "Nothing to play here", "", "");
		auto it = placeIn.find(dir);
		cursor = it == placeIn.end() ? 0 : std::min(it->second.first, (int)browseRows.size() - 1);
		scroll = it == placeIn.end() ? 0 : it->second.second;
		// Recent opens at its top: the most recent first, and a place in a list that changed since means little
		if (dir == RECENT) cursor = scroll = 0;
		fitList();
		browseVersion++;
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
		case TAB_SETTINGS: page = Page::Settings; cursor = STYLE_ROW0; break;
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
		return !browseDir.empty() && browseDir != RECENT && row >= 0 && row < (int)browseRows.size() && browseRows[row].id == 1;
	}

	void Ui::openFolderMenu(int row)
	{
		if (!folderRow(row)) return;
		ctxOpen = true;
		ctxCursor = 0;
		ctxPath = browsePaths[row];
	}

	void Ui::chooseFolderMenu(int item, Player& p)
	{
		ctxOpen = false;
		if (item != 0) return;	// Cancel
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
		if (browseDir.empty()) return;
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

	void Ui::changeSetting(int row, int dir, Player& p)
	{
		if (row == QUIT_ROW) { quitRequested = true; return; }
		if (row == CLEAR_ROW) { clearHistory(p); return; }
		if (row == ABOUT_ROW) { openAbout(p); return; }
		Settings s = p.settings;
		if (row - STYLE_ROW0 < STYLE_KEYS) stepStyle(s.style, (StyleKey)(row - STYLE_ROW0), dir);
		else if (row == MUSIC_ROW) s.musicShow = ((s.musicShow + dir) % SHOWS + SHOWS) % SHOWS;
		else if (row == VIS_ROW) s.visualizer = ((s.visualizer + dir) % VISUALIZERS + VISUALIZERS) % VISUALIZERS;
		else s.*REMEMBER[row - TOGGLE_ROW] = !(s.*REMEMBER[row - TOGGLE_ROW]);
		p.setSettings(s);
	}

	bool Ui::repeats(Button b) const
	{
		if (b == Button::Left || b == Button::Right) return page != Page::Audio && page != Page::Subs;
		if (b == Button::L || b == Button::R) return page == Page::Browse && !inMenu;	// the browser's page jumps
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
		Settings s = p.settings;
		std::string said;
		if (b == Button::X)
		{
			s.loop = (s.loop + 1) % LOOPS;
			said = loopLabel(s.loop);
		}
		else if (b == Button::Y)
		{
			s.visualizer = (s.visualizer + 1) % VISUALIZERS;
			said = std::string("Visualizer: ") + visualizerLabel(s.visualizer);
		}
		else if (b == Button::Select)
		{
			s.musicShow = (s.musicShow + 1) % SHOWS;
			said = std::string("Show: ") + musicShowLabel(s.musicShow);
		}
		else return false;
		p.setSettings(s);
		p.osd(said);
		return true;
	}

	bool Ui::press(Button b, Player& p)
	{
		// Button hotkeys off: the buttons do nothing over a playing video (touch does it all); the pages opened from
		// there by touch still take them
		if (page == Page::Main && !p.settings.buttonHotkeys) return false;
		// the buttons play on with the panel dark; the ones that open a page bring it back
		if (!panel.on() && (b == Button::X || b == Button::Y || b == Button::Start)) panel.set(true);
		if (b != Button::B) quitArmed = 0;
		if (inMenu)
		{
			if (b == Button::L || b == Button::R) { showTab((menuTab + (b == Button::R ? 1 : TABS - 1)) % TABS, p); return false; }
			if (b == Button::Start || b == Button::B) { closeMenu(p); return false; }
			if (page == Page::Exit)
			{
				if (b == Button::A) quitRequested = true;
				return false;
			}
			// the tab's page takes the rest (Recent's list, the settings' rows, About's scrolling)
		}
		if (page == Page::Settings)
		{
			switch (b)
			{
			case Button::Up: cursor = (cursor + SET_ROWS - 1) % SET_ROWS; break;
			case Button::Down: cursor = (cursor + 1) % SET_ROWS; break;
			case Button::Left: if (cursor != QUIT_ROW) changeSetting(cursor, -1, p); break;
			case Button::Right: case Button::A: changeSetting(cursor, 1, p); break;
			case Button::B: case Button::Start: page = home(p); break;
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
				if (page == Page::About) { page = Page::Settings; cursor = ABOUT_ROW; scroll = SET_ROWS - SET_SHOWN; }
				else page = home(p);
				break;
			default: break;
			}
			return false;
		}
		if (page == Page::Browse && ctxOpen)
		{
			switch (b)
			{
			case Button::Up: case Button::Down: ctxCursor = 1 - ctxCursor; break;
			case Button::A: chooseFolderMenu(ctxCursor, p); break;
			case Button::B: case Button::X: ctxOpen = false; break;
			default: break;
			}
			return false;
		}
		if (page == Page::Browse && !inMenu && b == Button::X) { openFolderMenu(cursor); return false; }
		if (page == Page::Browse && b == Button::B)
		{
			// opened over a video: back to it from the first page
			if (p.playing() && browseDir.empty()) { page = Page::Main; return false; }
			if (!browseDir.empty()) { browseUp(p); return false; }
			// the first page: B twice leaves
			if (quitArmed && SDL_GetTicks() - quitArmed < QUIT_WINDOW_MS) return true;
			quitArmed = SDL_GetTicks();
			return false;
		}
		if (page == Page::Browse && b == Button::Start) { openMainMenu(p); return false; }
		if (page != Page::Main)
		{
			int n = (int)rows(p).size();
			switch (b)
			{
			case Button::Up: cursor = (cursor + n - 1) % n; break;
			case Button::Down: cursor = (cursor + 1) % n; break;
			// a page at a time (the browser): LEFT/RIGHT, and L/R as BananaOS's lists jump
			case Button::Left: case Button::L: cursor = std::max(0, cursor - ROWS_SHOWN); break;
			case Button::Right: case Button::R: cursor = std::min(n - 1, cursor + ROWS_SHOWN); break;
			case Button::A: choose(cursor, p); break;
			case Button::B: page = home(p); break;
			default: break;
			}
			if (cursor < scroll) scroll = cursor;
			if (cursor >= scroll + ROWS_SHOWN) scroll = cursor - ROWS_SHOWN + 1;
			return false;
		}
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
		case Button::X: if (!musicKey(b, p)) openMenu(Page::Audio, p); break;
		case Button::Y: if (!musicKey(b, p)) openMenu(Page::Subs, p); break;
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

	Ui::Rect Ui::buttonRect(int id) const
	{
		if (id == QUIT_BTN) return {508, 434, 108, 38};
		if (id == BACK_BTN) return {508, 10, 108, 40};
		if (id == SCREEN_BTN) return {388, 434, 108, 38};
		if (id == INFO_BTN) return {268, 434, 108, 38};
		if (id == LOCK_BTN) return {148, 434, 108, 38};
		if (id == EXIT_BTN) return {170, 200, 300, 80};
		return {24 + id % 5 * BTN_STEP, id / 5 ? ROW2_Y : ROW1_Y, BTN_W, BTN_H};
	}

	bool Ui::act(int id, Player& p)
	{
		if (id == QUIT_BTN) return pressAnyway(Button::B, p);
		if (id == BACK_BTN) return press(Button::B, p);	// up a folder, out of a page, or (the first page) leave
		if (id == SCREEN_BTN) { panel.set(false); return false; }
		if (id == EXIT_BTN) { quitRequested = true; return false; }
		if (id == LOCK_BTN)
		{
			touchLocked = true;
			p.osd("Touch locked: hold the lock 1 s to unlock");
			return false;
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
		// a tap on the speed goes round its choices
		double s = stepSpeed(p.speed, 1, true);
		p.setSpeed(s);
		p.osd("Speed " + speedLabel(s));
		return false;
	}

	bool Ui::pointer(const Pointer& pt, Player& p)
	{
		int x = (int)(pt.x * W), y = (int)(pt.y * H);
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

		// locked, over a video: Screen off goes through; a hold on the lock (draw() times it) releases it; nothing else
		if (touchLocked && p.playing() && page == Page::Main && pressedBtn != SCREEN_BTN)
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

		if (ctxOpen)
		{
			// the folder's menu: a row tapped (as the finger lifts), anywhere else closes it
			if (pt.kind == Pointer::Down)
			{
				pressedBtn = -1;
				for (int i = 0; i < 2; i++)
					if (ctxRow(i).hit(x, y)) pressedBtn = 100 + i;
				if (pressedBtn < 0) ctxOpen = false;
			}
			else if (pt.kind == Pointer::Up && pressedBtn >= 100)
			{
				int item = pressedBtn - 100;
				pressedBtn = -1;
				if (ctxRow(item).hit(x, y)) chooseFolderMenu(item, p);
			}
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
		if (listTouch)
		{
			// a list (the tracks, the browser, the settings): a drag scrolls it, a tap acts as the finger lifts
			bool set = page == Page::Settings, info = page == Page::Info || page == Page::About;
			int n = set ? SET_ROWS : info ? (int)infoLines.size() : (int)rows(p).size();
			int rowH = set ? SET_H : info ? INFO_H : ROW_H, shown = set ? SET_SHOWN : info ? INFO_SHOWN : ROWS_SHOWN;
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
				if (set) changeSetting(cursor, touchX >= SET_ARROW_X - 12 && touchX < 490 ? -1 : 1, p);
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
			return buttonRect(id).hit(x, y) && act(id, p);
		}
		if (pt.kind != Pointer::Down) return false;
		if (inMenu && y < 52)
		{
			// the tab bar: a tab, or Back (it closes the menu as the finger lifts)
			if (buttonRect(BACK_BTN).hit(x, y)) { pressedBtn = BACK_BTN; return false; }
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
			(page == Page::Browse && browseDir.empty() && buttonRect(BACK_BTN).hit(x, y));
		if (!quitTap) quitArmed = 0;
		if (page != Page::Main && buttonRect(BACK_BTN).hit(x, y)) { pressedBtn = BACK_BTN; return false; }
		if (page == Page::Info || page == Page::About)
		{
			if (y < INFO_Y) return false;
			listTouch = true;
			listScrolled = false;
			touchStartY = y;
			scrollStart = scroll;
			return false;
		}
		if (page == Page::Settings)
		{
			int row = scroll + (y - SET_Y) / SET_H;
			if (y < SET_Y || y >= SET_Y + SET_SHOWN * SET_H || row >= SET_ROWS) return false;
			cursor = row;
			listTouch = true;
			listScrolled = false;
			touchStartY = y;
			touchX = x;
			scrollStart = scroll;
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
			holdAt = page == Page::Browse && !inMenu && folderRow(i) ? SDL_GetTicks() : 0;
			return false;
		}
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
			p.paused, (long)std::floor(p.position), (long)p.duration, (int)p.aspect, p.loaded, a ? (long long)a->id : 0LL,
			s ? (long long)s->id : 0LL, p.tracks.size(), dragging, (int)(dragFraction * 1000), quitArmed != 0, p.speed,
			p.index(), p.count(), pressedBtn + touchLocked * 100 + (lockHoldAt != 0) * 1000, listTouch * 1000 + browseVersion + (clearArmed != 0) * 100000 + (int)p.history().items().size() * 1000000);
		return buf + p.title + "|" + p.error + "|" + serializeSettings(p.settings) + "|bat" + std::to_string(battery.percent) + (battery.charging ? "c" : "") + (inMenu ? "|menu" + std::to_string(menuTab) : "") + (ctxOpen ? "|ctx" + std::to_string(ctxCursor) + std::to_string(pressedBtn) : "");
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
		int px = (int)(x * sx), py = (int)(y * sy);
		if (align == 1) px -= w;
		else if (align == 2) px -= w / 2;
		SDL_Rect dst{px, py, w, h};
		SDL_RenderCopy(ren, t, nullptr, &dst);
	}

	void Ui::fill(Rect r, SDL_Color c)
	{
		SDL_SetRenderDrawBlendMode(ren, c.a < 255 ? SDL_BLENDMODE_BLEND : SDL_BLENDMODE_NONE);
		SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
		SDL_Rect d{(int)(r.x * sx), (int)(r.y * sy), (int)std::ceil(r.w * sx), (int)std::ceil(r.h * sy)};
		SDL_RenderFillRect(ren, &d);
	}

	void Ui::draw(const Player& p, bool force)
	{
		if (quitArmed && SDL_GetTicks() - quitArmed >= QUIT_WINDOW_MS) quitArmed = 0;
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
		// a finger held on a folder: its menu (the lift that follows chooses nothing)
		if (holdAt && listTouch && !listScrolled && SDL_GetTicks() - holdAt >= 600)
		{
			holdAt = 0;
			listTouch = false;
			swallowTouch = true;
			openFolderMenu(cursor);
		}
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
		if (page == Page::Info || page == Page::About) drawInfo();
		else if (page == Page::Exit) drawExit(p);
		else if (page == Page::Settings) drawSettings(p);
		else if (page == Page::Browse) drawTracks(p);
		else if (page != Page::Main) drawTracks(p);
		else drawMain(p);
		if (inMenu) drawTabs();
		if (ctxOpen && page == Page::Browse) drawFolderMenu();
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

	void Ui::drawFolderMenu()
	{
		fill({0, 0, W, H}, {0, 0, 0, 160});
		fill({120, 150, 400, 236}, TEAL);
		fill({124, 154, 392, 228}, BG);
		std::string name = baseName(ctxPath);
		drawText(name, 1, YELLOW, W / 2, 168, 368, 2);
		const char* items[2] = {isHiddenFolder(ctxPath) ? "Unhide folder" : "Hide folder", "Cancel"};
		for (int i = 0; i < 2; i++)
		{
			Rect r = ctxRow(i);
			bool at = i == ctxCursor;
			fill(r, pressedBtn == 100 + i ? PRESSED : at ? TEAL : BG);
			drawText(items[i], 1, at ? YELLOW : CREAM, W / 2, r.y + 10, r.w - 10, 2);
		}
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

	void Ui::drawTabs()
	{
		static const char* names[TABS] = {"Recent", "Settings", "About", "Exit"};
		fill({0, 0, W, 52}, BG);	// over whatever the page drew there
		for (int t = 0; t < TABS; t++)
		{
			Rect r = tabRect(t);
			fill(r, t == menuTab ? YELLOW : TEAL);
			drawText(names[t], 1, t == menuTab ? BG : CREAM, r.x + r.w / 2, r.y + 7, r.w - 6, 2);
		}
		drawBack();
	}

	void Ui::drawExit(const Player& p)
	{
		drawText("Leave Banana Player?", 2, CREAM, W / 2, 110, 0, 2);
		if (p.playing()) drawText("Your place in what's playing is kept.", 0, DIM, W / 2, 156, 0, 2);
		Rect b = buttonRect(EXIT_BTN);
		fill(b, pressedBtn == EXIT_BTN ? PRESSED : RED);
		drawText("Quit Banana Player", 1, CREAM, b.x + b.w / 2, b.y + 26, b.w - 10, 2);
		drawText("A Quit   L/R Tabs   B Back", 0, DIM, W / 2, 444, 0, 2);
	}

	void Ui::drawBack(const char* label)
	{
		Rect b = buttonRect(BACK_BTN);
		fill(b, pressedBtn == BACK_BTN ? PRESSED : TEAL);
		drawText(label, 1, CREAM, b.x + b.w / 2 - 8, b.y + 7, 0, 2);
		drawText("B", 0, YELLOW, b.x + b.w - 14, b.y + 11, 0, 1);
	}

	void Ui::drawTracks(const Player& p)
	{
		if (page == Page::Browse)
		{
			if (!inMenu)
			{
				std::string title = browseDir.empty() ? "Banana Player" : browseDir == RECENT ? "Recently watched" : baseName(browseDir);
				drawText(title.empty() ? browseDir : title, 2, YELLOW, 24, 24, battery.percent >= 0 ? 470 - BATTERY_W : 470);
				drawBattery(496, 22, p);
				if (!browseDir.empty() && browseDir != RECENT)
				{
					// where it is, from the first page's place it's in: "Videos/Show/Season 1"
					std::string where = browseDir;
					size_t best = 0;
					for (auto& r : roots)
						if (r.path.size() > best && (browseDir == r.path || browseDir.rfind(r.path + "/", 0) == 0))
						{
							best = r.path.size();
							where = r.label + browseDir.substr(r.path.size());
						}
					drawText(where, 0, DIM, 24, 60, 470);
				}
				bool leaving = browseDir.empty() && !p.playing();
				drawBack(leaving ? (quitArmed ? "Again?" : "Quit") : "Back");
			}
		}
		else
		{
			drawText(page == Page::Audio ? "Audio" : "Subtitles", 2, YELLOW, 24, 24);
			drawBack();
		}
		auto r = rows(p);
		for (int i = scroll; i < (int)r.size() && i < scroll + ROWS_SHOWN; i++)
		{
			int y = ROW_Y + (i - scroll) * ROW_H;
			if (i == cursor) fill({16, y, 608, ROW_H - 4}, TEAL);
			if (r[i].selected) drawText("*", 1, YELLOW, 32, y + 8);
			int noteW = 0;
			if (!r[i].note.empty())
			{
				// a resume time, a count, a root's path: right-aligned and short
				drawText(r[i].note, 0, DIM, 608, y + 12, 200, 1);
				noteW = 210;
			}
			drawText(r[i].label, 1, r[i].id == STYLE_ROW ? YELLOW : r[i].id < 0 ? DIM : CREAM, page == Page::Browse ? 32 : 60, y + 8,
				(page == Page::Browse ? 580 : 552) - noteW);
		}
		if (scroll > 0) drawText("^", 1, DIM, 320, ROW_Y - 28, 0, 2);
		if (scroll + ROWS_SHOWN < (int)r.size()) drawText("v", 1, DIM, 320, ROW_Y + ROWS_SHOWN * ROW_H, 0, 2);
		if (page == Page::Browse)
			drawText(browseDir.empty() ? (quitArmed ? "Press B again to quit" : "A Open   START Settings   B twice Quit")
									   : "A Open   B Up   X Folder   L/R Page   START Menu",
				0, quitArmed ? YELLOW : DIM, 320, 444, 600, 2);
		else drawText("Tap to choose, drag to scroll   A Choose   B Back", 0, DIM, 320, 444, 0, 2);
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

	void Ui::drawInfo()
	{
		if (!inMenu)
		{
			drawText("Media info", 2, YELLOW, 24, 12);
			drawBack();
		}
		for (int i = scroll; i < (int)infoLines.size() && i < scroll + INFO_SHOWN; i++)
		{
			int y = INFO_Y + (i - scroll) * INFO_H;
			const InfoLine& l = infoLines[i];
			if (l.header) drawText(l.label, 1, YELLOW, 24, y - 2);
			else
			{
				drawText(l.label, 0, DIM, 32, y + 2, INFO_VALUE_X - 40);
				drawText(l.text, 0, CREAM, INFO_VALUE_X, y + 2);
			}
		}
		if (scroll > 0) drawText("^", 1, DIM, 320, 16, 0, 2);
		if (scroll + INFO_SHOWN < (int)infoLines.size()) drawText("v more: drag or UP/DOWN", 0, DIM, 320, 446, 0, 2);
	}

	void Ui::drawSettings(const Player& p)
	{

		if (clearArmed && SDL_GetTicks() - clearArmed >= QUIT_WINDOW_MS) clearArmed = 0;
		if (scroll > 0) drawText("^", 1, DIM, 320, 16, 0, 2);
		if (scroll + SET_SHOWN < SET_ROWS) drawText("v more", 0, DIM, 320, 424, 0, 2);
		for (int i = scroll; i < SET_ROWS && i < scroll + SET_SHOWN; i++)
		{
			int y = SET_Y + (i - scroll) * SET_H;
			bool at = i == cursor;
			if (at) fill({16, y, 608, SET_H - 4}, TEAL);
			std::string label, value;
			int k = i - STYLE_ROW0;	// a style row's key
			if (i == QUIT_ROW)
			{
				drawText("Quit Banana Player", 1, at ? YELLOW : RED, 32, y + 4, 420);
				continue;
			}
			else if (k < STYLE_KEYS)
			{
				label = std::string(k == BORDER_COLOR ? "" : "Subtitle ") + subStyleSettings()[k].label;
				if (k != BORDER_COLOR) label[9] = (char)std::tolower((unsigned char)label[9]);
				value = styleValueLabel(p.settings.style, (StyleKey)k);
			}
			else if (i == ABOUT_ROW)
			{
				drawText("About Banana Player", 1, CREAM, 32, y + 4, 420);
				drawText(">", 1, at ? YELLOW : DIM, 608, y + 4, 0, 1);
				continue;
			}
			else if (i == CLEAR_ROW)
			{
				// an action, not a value: no arrows
				size_t n = p.history().items().size();
				drawText("Clear play history", 1, CREAM, 32, y + 4, 320);
				drawText(clearArmed ? "Again to clear" : n ? std::to_string(n) + (n == 1 ? " video" : " videos") : "Empty", 1,
					clearArmed ? YELLOW : at ? YELLOW : DIM, 608, y + 4, 0, 1);
				continue;
			}
			else if (i == MUSIC_ROW)
			{
				label = "Music display";
				value = musicShowLabel(p.settings.musicShow);
			}
			else if (i == VIS_ROW)
			{
				label = "Visualizer";
				value = visualizerLabel(p.settings.visualizer);
			}
			else
			{
				label = REMEMBER_LABELS[i - TOGGLE_ROW];
				value = p.settings.*REMEMBER[i - TOGGLE_ROW] ? "On" : "Off";
			}
			drawText(label, 1, CREAM, 32, y + 4, 320);
			// arrows on every row: they are where a finger steps back and on
			drawText("<", 1, at ? YELLOW : DIM, SET_ARROW_X, y + 4);
			drawText(value, 1, at ? YELLOW : CREAM, 490, y + 4, 196, 2);
			drawText(">", 1, at ? YELLOW : DIM, 608, y + 4, 0, 1);
		}
		Rect box{24, SET_Y + SET_SHOWN * SET_H + 4, 592, 56};
		drawPreview(p.settings.style, box);

		const Track* s = p.selected(TrackType::Sub);
		bool styled = s && (s->codec == "ass" || s->codec == "ssa");
		drawText(styled ? "This video's subtitles are styled (ASS): they keep their own look"
						: "Tap < > or LEFT/RIGHT to change",
			0, styled ? YELLOW : DIM, 320, 450, 600, 2);
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

	void Ui::drawMain(const Player& p)
	{
		drawText(p.title.empty() ? "Opening..." : p.title, 2, CREAM, 24, 24, battery.percent >= 0 ? 592 - BATTERY_W : 592);
		drawBattery(616, 26, p);
		if (p.count() > 1)
			drawText(std::to_string(p.index() + 1) + " / " + std::to_string(p.count()), 0, DIM, 616, 90, 0, 1);
		if (!p.error.empty())
		{
			drawText("This file can't be played:", 1, RED, 24, 84);
			drawText(p.error, 1, RED, 24, 116, 592);
		}
		else
		{
			std::string status = !p.loaded ? "Loading" : p.paused ? "Paused" : "Playing";
			if (std::fabs(p.speed - 1) > 1e-6) status += "  " + speedLabel(p.speed);
			if (!p.settings.buttonHotkeys) status += "  (buttons off)";
			drawText(status, 1, YELLOW, 24, 84);

			double frac = dragging ? dragFraction : p.duration > 0 && p.position >= 0 ? std::clamp(p.position / p.duration, 0.0, 1.0) : 0;
			fill(BAR, TEAL);
			fill({BAR.x, BAR.y, (int)(BAR.w * frac), BAR.h}, YELLOW);
			fill({BAR.x + (int)(BAR.w * frac) - 4, BAR.y - 6, 8, BAR.h + 12}, CREAM);
			drawText(formatTime(dragging ? dragFraction * p.duration : p.position), 1, CREAM, BAR.x, BAR.y + 32);
			drawText(formatTime(p.duration), 1, DIM, BAR.x + BAR.w, BAR.y + 32, 0, 1);
			// how long is left, at the speed it plays (half at 2x)
			double left = timeLeft(dragging ? dragFraction * p.duration : p.position, p.duration, p.speed);
			if (left >= 0) drawText(formatTime(left) + " left", 0, DIM, BAR.x + BAR.w / 2, BAR.y + 36, 0, 2);
		}

		for (int r = 0; r < 2; r++)
			for (int c = 0; c < 5; c++)
			{
				Rect b{24 + c * BTN_STEP, r ? ROW2_Y : ROW1_Y, BTN_W, BTN_H};
				Button which = GRID[r][c].button;
				std::string label;
				bool off = false;
				switch (which)
				{
				case Button::L: label = "Prev"; off = p.index() == 0; break;
				case Button::Left: label = "-5s"; break;
				case Button::A: label = p.paused ? "Play" : "Pause"; break;
				case Button::Right: label = "+10s"; break;
				case Button::R: label = "Next"; off = p.index() + 1 >= p.count(); break;
				// music: loop, visualizer, what's shown; a video: audio, subtitles, picture size
				case Button::X: label = p.playingAudio() ? loopLabel(p.settings.loop) : "Audio"; break;
				case Button::Y: label = p.playingAudio() ? visualizerLabel(p.settings.visualizer) : "Subs"; break;
				case Button::Select: label = p.playingAudio() ? SHOW_SHORT[p.settings.musicShow] : aspectName(p.aspect); break;
				case Button::Up: label = speedLabel(p.speed); break;
				default: label = "Menu"; break;
				}
				fill(b, pressedBtn == r * 5 + c ? PRESSED : TEAL);
				// the button it stands for, while the buttons work here (Button hotkeys); else the name alone, centred
				bool hint = p.settings.buttonHotkeys && *GRID[r][c].hint;
				drawText(label, 1, off ? DIM : CREAM, b.x + b.w / 2, b.y + (hint ? 6 : 16), b.w - 6, 2);
				if (hint) drawText(GRID[r][c].hint, 0, off ? DIM : YELLOW, b.x + b.w / 2, b.y + 36, b.w - 6, 2);
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
		drawText("Audio: " + label(a, TrackType::Audio), 0, CREAM, 24, 374, 592);
		if (p.playingAudio())
			drawText(std::string(loopLabel(p.settings.loop)) + "   " + musicShowLabel(p.settings.musicShow) + " · " +
				visualizerLabel(p.settings.visualizer), 0, CREAM, 24, 402, 592);
		else drawText("Subtitles: " + label(s, TrackType::Sub), 0, CREAM, 24, 402, 592);
		Rect lk = buttonRect(LOCK_BTN);
		bool holding = touchLocked && lockHoldAt;
		fill(lk, holding ? PRESSED : touchLocked ? YELLOW : pressedBtn == LOCK_BTN ? PRESSED : TEAL);
		drawText(touchLocked ? (holding ? "Hold..." : "Locked") : "Lock", 0, touchLocked && !holding ? BG : CREAM, lk.x + lk.w / 2, lk.y + 9,
			lk.w - 6, 2);
		Rect inf = buttonRect(INFO_BTN);
		fill(inf, pressedBtn == INFO_BTN ? PRESSED : TEAL);
		drawText("Info", 0, CREAM, inf.x + inf.w / 2, inf.y + 9, inf.w - 6, 2);
		Rect so = buttonRect(SCREEN_BTN);
		fill(so, pressedBtn == SCREEN_BTN ? PRESSED : TEAL);
		drawText("Screen off", 0, CREAM, so.x + so.w / 2, so.y + 9, so.w - 6, 2);
		Rect q = buttonRect(QUIT_BTN);
		fill(q, pressedBtn == QUIT_BTN ? PRESSED : TEAL);
		drawText(quitArmed ? "Again?" : browserMode ? "Close" : "Quit", 1, quitArmed ? YELLOW : CREAM, q.x + q.w / 2, q.y + 6, q.w - 6, 2);
		const char* leave = browserMode ? "close" : "quit";
		// B, or (Button hotkeys off: B does nothing here) the button beside
		std::string hint = quitArmed ? std::string("Again to ") + leave
			: p.settings.buttonHotkeys ? std::string("Double tap B to ") + leave
									   : std::string("Tap ") + (browserMode ? "Close" : "Quit") + " twice";
		if (touchLocked) hint = "Touch locked: hold Locked 1 s to unlock";
		drawText(hint, 0, quitArmed ? YELLOW : DIM, 24, 118, 470);
	}
}
