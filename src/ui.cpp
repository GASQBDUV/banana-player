#include "ui.h"
#include "format.h"
#include "playlist.h"
#include <algorithm>
#include <cmath>
#include <ctime>
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

	// the settings' on/off rows (TOGGLE_ROW + their index)
	static const char* REMEMBER_LABELS[11] = {"Remember position", "Remember audio track", "Remember subtitles",
		"Remember last folder", "Keep play history", "Button hotkeys", "Show hidden folders", "Battery percentage",
		"Keep zoom between photos", "Check automatically", "Same for both screens"};
	static bool Settings::*const REMEMBER[11] = {&Settings::rememberPosition, &Settings::rememberAudio, &Settings::rememberSubs,
		&Settings::rememberFolder, &Settings::keepHistory, &Settings::buttonHotkeys, &Settings::showHiddenFolders,
		&Settings::batteryPercent, &Settings::keepPhotoZoom, &Settings::checkUpdates, &Settings::sameBrightness};
	static const struct { const char* name; const char* about; } GROUP_NAMES[] = {
		{"Subtitles", "Font, size, colour, border"},
		{"Music", "What's shown, visualizer"},
		{"Photos", "Zoom, pan area"},
		{"Remember", "Position, audio track, subtitles"},
		{"Files", "Last folder, hidden folders, play history"},
		{"Controls & screen", "Brightness, button hotkeys, battery percentage"},
		{"Updates", "Check for a new version, update"},
	};
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
		if (pg == Page::Settings) { setGroup = -1; return; }
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
		if (r[i].id == STYLE_ROW)
		{
			openMainMenu(p, TAB_SETTINGS);
			openGroup(GROUP_SUBS);
			return;
		}
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
		dropTouch();
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
			// a song's (kept by an earlier version) left out: songs start from their start
			return e && e->position >= 0 && !isAudioFile(path) ? "at " + formatTime(e->position) : std::string();
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
		case GROUP_MUSIC: return {MUSIC_ROW, VIS_ROW};
		case GROUP_PHOTOS: return {T + 8, PAN_ROW, DRAG_ROW};
		case GROUP_REMEMBER: return {T + 0, T + 1, T + 2};
		case GROUP_FILES: return {T + 3, T + 6, T + 4, CLEAR_ROW};
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
		fill({0, 0, W, H}, {0, 0, 0, 170});
		Rect pr = quickPanel();
		fill(pr, TEAL);
		fill({pr.x + 4, pr.y + 4, pr.w - 8, pr.h - 8}, BG);
		drawText("Quick settings", 2, YELLOW, pr.x + 20, pr.y + 12);
		auto rows = quickRows();
		for (int i = 0; i < (int)rows.size(); i++)
		{
			Rect r = quickRowRect(i);
			bool at = i == quickCursor;
			int row = rows[i];
			if (at) fill(r, {0x23, 0x43, 0x46, 120});
			if (row == Q_QUIT)
			{
				fill(r, pressedBtn == 302 + i * 4 ? PRESSED : RED);
				drawText("Quit Banana Player", 1, CREAM, r.x + r.w / 2, r.y + 12, r.w - 10, 2);
				if (at) { fill({r.x, r.y, r.w, 3}, YELLOW); fill({r.x, r.y + r.h - 3, r.w, 3}, YELLOW); }
				continue;
			}
			if (row == Q_SAME)
			{
				// a tick box
				Rect box{r.x + 12, r.y + 13, 28, 28};
				fill(box, CREAM);
				fill({box.x + 3, box.y + 3, box.w - 6, box.h - 6}, BG);
				if (p.settings.sameBrightness) fill({box.x + 7, box.y + 7, box.w - 14, box.h - 14}, YELLOW);
				drawText("Same brightness for both screens", 1, at ? YELLOW : CREAM, r.x + 54, r.y + 14, r.w - 64);
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
			drawText(label, 1, at ? YELLOW : CREAM, r.x + 12, r.y + 6, 220);
			drawText(pct < 0 ? "-" : std::to_string(pct) + "%", 0, DIM, r.x + 12, r.y + 32, 220);
			for (int k = 0; k < 2; k++)
			{
				Rect bt = k ? quickPlus(i) : quickMinus(i);
				fill(bt, pressedBtn == 300 + i * 4 + k ? PRESSED : TEAL);
				drawText(k ? "+" : "-", 2, CREAM, bt.x + bt.w / 2, bt.y + 6, 0, 2);
			}
			Rect bar = quickBar(i);
			fill(bar, TEAL);
			if (pct > 0) fill({bar.x, bar.y, bar.w * std::min(pct, 100) / 100, bar.h}, YELLOW);
		}
		drawText(p.settings.buttonHotkeys ? "UP/DOWN Choose   LEFT/RIGHT Change   F or B Close" : "Tap outside to close", 0, DIM,
			W / 2, pr.y + pr.h - 26, 0, 2);
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
		sameBrightness = p.settings.sameBrightness;
		if (quickOpen) return quickKey(b, p);
		// Button hotkeys off: the buttons do nothing over a playing video (touch does it all); the pages opened from
		// there by touch still take them
		if (page == Page::Main && !p.settings.buttonHotkeys) return false;
		// the buttons play on with the panel dark; the ones that open a page bring it back
		if (!panel.on() && (b == Button::X || b == Button::Y || b == Button::Start)) panel.set(true);
		if (b != Button::B) quitArmed = 0;
		if (inMenu)
		{
			if (b == Button::L || b == Button::R) { showTab((menuTab + (b == Button::R ? 1 : TABS - 1)) % TABS, p); return false; }
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
		if (page == Page::Settings)
		{
			auto rows = settingRows();
			int n = setGroup < 0 ? GROUPS : (int)rows.size();
			cursor = std::min(cursor, n - 1);	// a group's rows can change (the updater's)
			switch (b)
			{
			case Button::Up: cursor = (cursor + n - 1) % n; break;
			case Button::Down: cursor = (cursor + 1) % n; break;
			case Button::Left: if (setGroup >= 0) changeSetting(rows[cursor], -1, p); break;
			case Button::Right: case Button::A:
				if (setGroup < 0) openGroup(cursor);
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
		if (p.playingPhoto() && photoKey(b, p)) return false;
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
		if (id >= PHOTO_BTN) return {0, 0, 0, 0};	// photoBtn's: they move with the pan area
		if (id == QUIT_BTN) return {508, 434, 108, 38};
		if (id == BACK_BTN) return {508, 10, 108, 40};
		if (id == SCREEN_BTN) return {388, 434, 108, 38};
		if (id == INFO_BTN) return {268, 434, 108, 38};
		if (id == LOCK_BTN) return {148, 434, 108, 38};
		if (id == EXIT_BTN) return {170, 200, 300, 80};
		if (id == BACK_ICON) return {8, 12, 48, 48};
		return {24 + id % 5 * BTN_STEP, id / 5 ? ROW2_Y : ROW1_Y, BTN_W, BTN_H};
	}

	bool Ui::act(int id, Player& p)
	{
		// "<" over what plays closes it at once (the place kept); B and Close ask twice, a pocket's press being one
		if (id == BACK_ICON && page == Page::Main && p.playing()) return true;
		if (id == QUIT_BTN || id == BACK_ICON) return pressAnyway(Button::B, p);
		if (id == BACK_BTN) return press(Button::B, p);	// up a folder, out of a page, or (the first page) leave
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

		if (quickOpen) return quickPointer(pt, x, y, p);
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
		if (panTouch) return photoPointer(pt, x, y, p);
		if (listTouch)
		{
			// a list (the tracks, the browser, the settings): a drag scrolls it, a tap acts as the finger lifts
			bool set = page == Page::Settings, info = page == Page::Info || page == Page::About;
			int n = set ? (setGroup < 0 ? GROUPS : (int)settingRows().size()) : info ? (int)infoLines.size() : (int)rows(p).size();
			int rowH = set ? (setGroup < 0 ? GROUP_H : SET_H) : info ? INFO_H : ROW_H, shown = set ? SET_SHOWN : info ? INFO_SHOWN : ROWS_SHOWN;
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
			(backIconShown(p) && buttonRect(BACK_ICON).hit(x, y));
		if (!quitTap) quitArmed = 0;
		// "<": B (up a folder; twice, out of what plays or, from the first page, out of the player)
		if (backIconShown(p) && buttonRect(BACK_ICON).hit(x, y)) { pressedBtn = BACK_ICON; return false; }
		// the pages' own Back (the browser has "<" instead)
		if (page != Page::Main && !(page == Page::Browse && !inMenu) && buttonRect(BACK_BTN).hit(x, y)) { pressedBtn = BACK_BTN; return false; }
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
			int top = setGroup < 0 ? GROUP_Y : SET_Y, h = setGroup < 0 ? GROUP_H : SET_H;
			int n = setGroup < 0 ? GROUPS : (int)settingRows().size();
			int row = scroll + (y - top) / h;
			if (y < top || y >= top + SET_SHOWN * h || row >= n) return false;
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
		std::string photo;
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
		// the dots of "Loading..." go round
		if (p.loading() || p.thumbPending()) photo += "|loading" + std::to_string(SDL_GetTicks() / 400 % 4);
		return buf + photo + p.title + "|" + p.error + "|" + serializeSettings(p.settings) + "|bat" + std::to_string(battery.percent) + (battery.charging ? "c" : "") + (inMenu ? "|menu" + std::to_string(menuTab) : "") + (ctxOpen ? "|ctx" + std::to_string(ctxCursor) + std::to_string(pressedBtn) : "") + upd;
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
		if (popupShown(p)) drawUpdatePopup();
		if (quickOpen) drawQuick(p);
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
		fill({0, 0, W, H}, {0, 0, 0, 170});
		auto items = popupItems();
		int top = 150, bottom = popupRow((int)items.size() - 1).y + 60;
		fill({112, top, 416, bottom - top}, TEAL);
		fill({116, top + 4, 408, bottom - top - 8}, BG);
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
		drawText(title, 1, YELLOW, W / 2, top + 18, 392, 2);
		drawText(sub, 0, CREAM, W / 2, top + 56, 392, 2);
		if (updater->state() == Updater::State::Downloading)
		{
			// the bar
			int pct = std::max(0, updater->progress());
			fill({156, top + 84, 328, 10}, TEAL);
			fill({156, top + 84, 328 * pct / 100, 10}, YELLOW);
		}
		for (int i = 0; i < (int)items.size(); i++)
		{
			Rect r = popupRow(i);
			bool at = i == popupCursor;
			fill(r, pressedBtn == 200 + i ? PRESSED : at ? TEAL : BG);
			drawText(items[i], 1, at ? YELLOW : CREAM, W / 2, r.y + 9, r.w - 10, 2);
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

	void Ui::drawBackIcon()
	{
		Rect b = buttonRect(BACK_ICON);
		// armed (the browser's first page: once more quits) in yellow
		bool armed = quitArmed && page == Page::Browse;
		fill(b, pressedBtn == BACK_ICON ? PRESSED : armed ? YELLOW : TEAL);
		drawText("<", 2, armed ? BG : CREAM, b.x + b.w / 2, b.y + 6, 0, 2);
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
				drawBackIcon();
				drawText(title.empty() ? browseDir : title, 2, YELLOW, 68, 24, battery.percent >= 0 ? 548 - BATTERY_W : 548);
				drawBattery(616, 26, p);
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
			// Recent's Clear row says how it stands now (the list was made before the first tap expired)
			std::string note = page == Page::Browse && r[i].id == 3 ? (clearArmed ? "again to clear" : "tap twice") : r[i].note;
			if (!note.empty())
			{
				// a resume time, a count, a root's path: right-aligned and short
				drawText(note, 0, clearArmed && r[i].id == 3 ? YELLOW : DIM, 608, y + 12, 200, 1);
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
		if (setGroup < 0)
		{
			// the groups: a name, what's in it
			for (int g = 0; g < GROUPS; g++)
			{
				int y = GROUP_Y + g * GROUP_H;
				bool at = g == cursor;
				if (at) fill({16, y, 608, GROUP_H - 4}, TEAL);
				drawText(GROUP_NAMES[g].name, 1, at ? YELLOW : CREAM, 32, y + 4, 540);
				drawText(GROUP_NAMES[g].about, 0, DIM, 32, y + 31, 540);
				drawText(">", 1, at ? YELLOW : DIM, 608, y + 14, 0, 1);
			}
			drawText("A or tap to open   L/R Tabs   B Back", 0, DIM, 320, 444, 0, 2);
			return;
		}
		drawText(GROUP_NAMES[setGroup].name, 1, YELLOW, 24, 58, 420);
		auto rows = settingRows();
		for (int i = scroll; i < (int)rows.size() && i < scroll + SET_SHOWN; i++)
		{
			int row = rows[i];
			int y = SET_Y + (i - scroll) * SET_H;
			bool at = i == cursor;
			if (at) fill({16, y, 608, SET_H - 4}, TEAL);
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
				drawText(label, 1, CREAM, 32, y + 9, 320);
				drawText(value, 1, at ? YELLOW : DIM, 608, y + 9, 260, 1);
				continue;
			}
			if (row == CLEAR_ROW)
			{
				// an action, not a value: no arrows
				size_t n = p.history().items().size();
				drawText("Clear play history", 1, CREAM, 32, y + 9, 320);
				drawText(clearArmed ? "Again to clear" : n ? std::to_string(n) + (n == 1 ? " file" : " files") : "Empty", 1,
					clearArmed ? YELLOW : at ? YELLOW : DIM, 608, y + 9, 0, 1);
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
			drawText(label, 1, CREAM, 32, y + 9, 330);
			// arrows on every row: they are where a finger steps back and on
			drawText("<", 1, at ? YELLOW : DIM, SET_ARROW_X, y + 9);
			drawText(value, 1, at ? YELLOW : CREAM, 490, y + 9, 196, 2);
			drawText(">", 1, at ? YELLOW : DIM, 608, y + 9, 0, 1);
		}
		std::string hint = "Tap < > or LEFT/RIGHT to change   B Back";
		SDL_Color hintInk = DIM;
		if (setGroup == GROUP_SUBS)
		{
			Rect box{24, SET_Y + (int)rows.size() * SET_H + 8, 592, 56};
			drawPreview(p.settings.style, box);
			const Track* s = p.selected(TrackType::Sub);
			if (s && (s->codec == "ass" || s->codec == "ssa"))
			{
				hint = "This video's subtitles are styled (ASS): they keep their own look";
				hintInk = YELLOW;
			}
		}
		if (setGroup == GROUP_UPDATES && updater)
		{
			hint = updater->fromStore() ? "Installed from the BananaOS Store: the Store updates it"
				: updater->state() == Updater::State::Off ? "Updates are for the handheld's Ports install"
				: updater->state() == Updater::State::Failed ? updater->message()
				: updater->state() == Updater::State::Ready ? "Restart now, or it's put in place at the next start"
				: "Downloads from GitHub, checked before anything changes";
			hintInk = updater->state() == Updater::State::Failed ? YELLOW : DIM;
		}
		drawText(hint, 0, hintInk, 320, 444, 600, 2);
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
		drawBackIcon();
		drawText(!p.title.empty() ? p.title : !p.error.empty() ? name : "Opening...", 2, CREAM, 68, 24, battery.percent >= 0 ? 548 - BATTERY_W : 548);
		drawBattery(616, 26, p);
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

		Rect q = buttonRect(QUIT_BTN);
		fill(q, pressedBtn == QUIT_BTN ? PRESSED : TEAL);
		drawText(quitArmed ? "Again?" : browserMode ? "Close" : "Quit", 1, quitArmed ? YELLOW : CREAM, q.x + q.w / 2, q.y + 6, q.w - 6, 2);
		const char* leave = browserMode ? "close" : "quit";
		std::string hint = quitArmed ? std::string("Again to ") + leave
			: hints ? std::string("A Fit/2x   Double tap B to ") + leave
					: std::string("Tap ") + (browserMode ? "Close" : "Quit") + " twice";
		drawText(hint, 0, quitArmed ? YELLOW : DIM, 24, 444, 470);
	}

	void Ui::drawMain(const Player& p)
	{
		if (p.playingPhoto()) { drawPhoto(p); return; }
		std::string name = p.currentPath().substr(p.currentPath().rfind('/') + 1);
		drawBackIcon();
		drawText(!p.title.empty() ? p.title : !p.error.empty() ? name : "Opening...", 2, CREAM, 68, 24, battery.percent >= 0 ? 548 - BATTERY_W : 548);
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
			std::string status = p.loading() ? "Loading" + std::string(SDL_GetTicks() / 400 % 4, '.') : p.paused ? "Paused" : "Playing";
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
