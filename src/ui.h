#pragma once
#include "browser.h"
#include "input.h"
#include "panel.h"
#include "player.h"
#include "touch.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <string>
#include <map>
#include <unordered_map>

namespace bp
{
	// The bottom panel: what is playing and where, the transport (previous, -5 s, play, +10 s, next), the choices
	// (audio, subtitles, aspect, speed, settings) and their pages. Laid out on 640x480 (the RG DS's panel) and scaled
	// to the panel it is on.
	class Ui
	{
	public:
		static constexpr int W = 640, H = 480;

		// fontsDir: assets/fonts, the panel's font and the subtitle preview's
		bool init(SDL_Window* window, const std::string& fontsDir);
		~Ui();

		// true when the player should leave
		bool press(Button b, Player& p);
		// a held button that repeats here (seeking, moving in a list)
		bool repeats(Button b) const;
		// a finger (or, on a computer, the mouse) on the bottom panel; true when the player should leave
		bool pointer(const Pointer& pt, Player& p);

		// the browser: the page shown with no video, opened at the last folder (Remember last folder) or the first page
		// folder: where to open (a folder given on the command line), else the remembered one or the first page
		void startBrowser(Player& p, const std::string& folder = "");
		// back to the browser from a video: the folder of the video last played, that video selected (whatever
		// Remember last folder says: that's for the next launch)
		void showBrowser(Player& p, const std::string& lastPlayed);
		// the row the browser's cursor is on, for test runs
		std::string browseSelection() const { return cursor < (int)browsePaths.size() ? browsePaths[cursor] : ""; }
		void showPlayer() { page = Page::Main; inMenu = false; }
		bool browsing() const { return page == Page::Browse; }
		std::string browseFolder() const { return browseDir; }
		// a video picked in the browser, once: the caller opens it
		// the Info or About page's lines as they are drawn, for test runs
		std::vector<std::string> pageLines() const
		{
			std::vector<std::string> out;
			for (auto& l : infoLines) out.push_back(l.header ? "[" + l.label + "]" : l.label + ": " + l.text);
			return out;
		}
		std::string takeOpenRequest() { std::string r; r.swap(openRequest); return r; }
		// started with no file: the video's Quit closes it (back to the browser)
		bool browserMode = false;
		// Settings' Quit Banana Player: out of the player, from the browser or a video
		bool quitRequested = false;

		// the bottom panel's backlight: off from its button, on again at the next touch
		bool screenOn() const { return panel.on(); }
		bool touchIsLocked() const { return touchLocked; }
		// START's menu's open tab ("recent", "settings", "about", "exit"), "" while it's closed: for test runs
		const char* menuTabName() const
		{
			static const char* names[] = {"recent", "settings", "about", "exit"};
			return inMenu ? names[menuTab] : "";
		}

		// redraws when what it would show changed since the last frame
		void draw(const Player& p, bool force = false);
		// the bottom panel's pixels (RGBA, ABGR8888), drawn afresh: screenshots; the caller frees it
		SDL_Surface* capture(const Player& p);

	private:
		enum class Page { Main, Audio, Subs, Settings, Browse, Info, About, Exit };
		struct Rect { int x, y, w, h; bool hit(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; } };
		struct Row { int64_t id; std::string label; bool selected; std::string note = ""; };

		std::vector<Row> rows(const Player& p) const;
		void openMenu(Page pg, const Player& p);
		void choose(int i, Player& p);
		void changeSetting(int row, int dir, Player& p);
		Page home(const Player& p) const { return p.playing() ? Page::Main : Page::Browse; }
		void enterFolder(std::string dir, Player& p);	// "" the first page, RECENT the recent videos
		void browseUp(Player& p);
		// X on a folder (or a finger held on it): Hide folder / Unhide folder (a ".hidden" file in it), Cancel
		bool ctxOpen = false;
		int ctxCursor = 0;
		std::string ctxPath;
		Uint32 holdAt = 0;	// a finger down on a row, for the hold that opens it
		void openFolderMenu(int row);
		void chooseFolderMenu(int item, Player& p);
		void drawFolderMenu();
		bool folderRow(int row) const;
		Rect ctxRow(int item) const { return {136, 214 + item * 52, 368, 46}; }
		// START's menu, over a video or the browser: tabs Recent (the browser's Recently watched), Settings, About,
		// Exit; L/R or a tap change tab, B or START close it
		enum Tab { TAB_RECENT, TAB_SETTINGS, TAB_ABOUT, TAB_EXIT, TABS };
		bool inMenu = false;
		int menuTab = TAB_RECENT;
		std::string menuReturnDir;	// the browser's folder before the menu: back to it after
		void openMainMenu(Player& p, int tab = TAB_RECENT);
		void showTab(int tab, Player& p);
		void closeMenu(Player& p);
		Rect tabRect(int tab) const { return {24 + tab * 121, 8, 113, 40}; }
		void drawTabs();
		void drawExit(const Player& p);
		bool clearHistory(Player& p);	// true when it cleared (the second time)
		bool act(int id, Player& p);	// a touch button let go of on it
		bool pressAnyway(Button b, Player& p);
		Rect buttonRect(int id) const;
		std::string signature(const Player& p) const;

		void renderPage(const Player& p);	// the page into the renderer, not yet shown
		void drawMain(const Player& p);
		void drawTracks(const Player& p);
		void drawBack(const char* label = "Back");
		void drawSettings(const Player& p);
		void drawInfo();
		// the Media info page's lines: a row's value wrapped to the width, read again each second while it's open
		struct InfoLine { std::string label, text; bool header; };
		std::vector<InfoLine> infoLines;
		Uint32 infoAt = 0;
		void readInfo(const Player& p);
		void setInfoLines(const std::vector<Player::InfoRow>& rows);	// wrapped to the page
		void openAbout(const Player& p);
		static constexpr int INFO_Y = 70, INFO_H = 26, INFO_SHOWN = 14, INFO_VALUE_X = 190;
		void drawPreview(const SubStyle& st, Rect box);

		SDL_Texture* text(const std::string& s, int size, SDL_Color c);
		void drawText(const std::string& s, int size, SDL_Color c, int x, int y, int maxW = 0, int align = 0);
		void fill(Rect r, SDL_Color c);

		SDL_Renderer* ren = nullptr;
		TTF_Font* fonts[3] = {};	// small, normal, large
		std::unordered_map<std::string, SDL_Texture*> cache;
		std::string drawn, fontsDir;

		Page page = Page::Main;
		int cursor = 0, scroll = 0;
		Uint32 quitArmed = 0;		// B pressed once: a second press within 2 s leaves
		bool dragging = false;
		double dragFraction = 0, scrubbed = -1;
		Uint32 scrubAt = 0;
		BottomPanel panel;
		// Lock: over a video, touch does nothing but Screen off and a 1 s hold on the lock (its release); the
		// buttons go on as Button hotkeys says
		bool touchLocked = false;
		Uint32 lockHoldAt = 0, lockNoticeAt = 0;
		bool swallowTouch = false;	// the touch that woke the panel: it presses nothing
		int pressedBtn = -1;		// a touch button under the finger: it acts when the finger lifts on it
		bool listTouch = false, listScrolled = false;	// a finger on a track list: a tap chooses, a drag scrolls
		int touchStartY = 0, touchX = 0, scrollStart = 0;
		Uint32 clearArmed = 0;		// Clear play history chosen once: again within 2 s clears it

		std::string browseDir, openRequest;
		std::vector<Row> browseRows;
		std::vector<std::string> browsePaths;	// beside browseRows: what each row opens
		std::vector<BrowseRoot> roots;
		std::map<std::string, std::pair<int, int>> placeIn;	// each folder's cursor and scroll, for coming back
		int browseVersion = 0;

		static constexpr Rect BAR{24, 150, 592, 24};
		static constexpr int BTN_W = 108, BTN_H = 60, BTN_STEP = 121, ROW1_Y = 228, ROW2_Y = 300;
		static constexpr int ROW_Y = 84, ROW_H = 48, ROWS_SHOWN = 7;
		// the settings: the style's rows, five switches, Clear play history; SET_SHOWN of them at a time
		// the settings' rows: Quit, the subtitle style, Audio display, the switches, Clear play history, About
		// the settings' rows: the subtitle style, Audio display, the switches, Clear play history (Quit and About are
		// the menu's own tabs: QUIT_ROW and ABOUT_ROW match no row)
		static constexpr int QUIT_ROW = -1, STYLE_ROW0 = 0, AUDIO_ROW = STYLE_KEYS, TOGGLE_ROW = STYLE_KEYS + 1;
		static constexpr int SET_Y = 54, SET_H = 34, SET_ROWS = STYLE_KEYS + 9, SET_SHOWN = 9,
			CLEAR_ROW = SET_ROWS - 1, ABOUT_ROW = -2;
		static constexpr int64_t STYLE_ROW = -2;	// the subtitle menu's last row: the settings page
		static constexpr int QUIT_BTN = 10, BACK_BTN = 11, SCREEN_BTN = 12, INFO_BTN = 13, LOCK_BTN = 14, EXIT_BTN = 15;
		static constexpr Uint32 UNLOCK_HOLD_MS = 1000;	// buttonRect's ids beside the grid's 0..9
		static constexpr int SET_ARROW_X = 372;	// a settings row's left arrow: a tap left of the value's middle steps back
	};
}
