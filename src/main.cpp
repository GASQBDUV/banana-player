// banana-player: a video player for BananaOS. The picture on the top panel, the controls on the bottom one.
//
//   banana-player [--press A,X,Down,...] [--windowed] [--aspect fit|fill|stretch] [--lang eng,en] [--state-dir DIR] [--font TTF] [-v] FILE

#include "format.h"
#include "input.h"
#include "player.h"
#include "playlist.h"
#include "png.h"
#include "sleepwatch.h"
#include "levelwatch.h"
#include "touch.h"
#include "ui.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <sstream>
#include <sys/stat.h>
#include <vector>

using namespace bp;

static Uint32 evMpv, evRender, evTouch, evLevel;
static std::atomic<bool> mpvPending{false}, renderPending{false};

static void onMpvWakeup(void*)
{
	if (mpvPending.exchange(true)) return;
	SDL_Event e{};
	e.type = evMpv;
	SDL_PushEvent(&e);
}

static void onMpvRender(void*)
{
	if (renderPending.exchange(true)) return;
	SDL_Event e{};
	e.type = evRender;
	SDL_PushEvent(&e);
}

static void* glProc(void*, const char* name)
{
	return SDL_GL_GetProcAddress(name);
}

static bool exists(const std::string& p)
{
	struct stat st;
	return !p.empty() && stat(p.c_str(), &st) == 0;
}

static std::string findFont()
{
	std::string base;
	if (char* b = SDL_GetBasePath())
	{
		base = b;
		SDL_free(b);
	}
	// installed (the App Store package: bin/ and share/ side by side), then the source tree
	for (std::string p : {base + "../share/banana-player/fonts/PixelifySans-Regular.ttf",
			 base + "assets/fonts/PixelifySans-Regular.ttf",
			 std::string(BP_ASSETS_DIR) + "/fonts/PixelifySans-Regular.ttf"})
		if (exists(p)) return p;
	return "";
}

static std::string defaultStateDir()
{
	// a Store app's files go under /data/home (run-store-app sets XDG_CONFIG_HOME there)
	if (const char* cfg = getenv("XDG_CONFIG_HOME")) return std::string(cfg) + "/banana-player";
	if (exists("/data/banana")) return "/data/home/.config/banana-player";
	if (const char* home = getenv("HOME")) return std::string(home) + "/.local/state/banana-player";
	return "";
}

static void usage()
{
	fprintf(stderr,
		"banana-player " BP_VERSION "\n"
		"usage: banana-player [options] [FILE]\n"
		"  with no FILE (or a folder), a browser on the bottom panel picks the videos\n"
		"  --windowed            two windows on a computer instead of the handheld's two panels\n"
		"  --aspect MODE         fit (default), fill or stretch\n"
		"  --lang LIST           preferred audio and subtitle languages (default: eng,en)\n"
		"  --state-dir DIR       where the settings and resume positions are kept\n"
		"  --font TTF            the bottom panel's font\n"
		"  -v                    mpv's verbose log\n"
		"  --press LIST          test run: once the file is open, do these 0.8 s apart and print the state after\n"
		"                        each, then quit: buttons (A,B,X,Y,L,R,Up,Down,Left,Right,Start,Select), taps\n"
		"                        (tap:X:Y, dtap:X:Y a double-tap) and drags (drag:X1:Y1:X2:Y2) on the bottom panel's 640x480, and info\n"
		"                        (prints the Media info page), hold:X:Y (a finger held 1.1 s), prop:NAME\n"
		"                        (prints one of mpv's properties)\n"
		"buttons: A play/pause, LEFT -5 s, RIGHT +10 s, L/R previous/next video, UP/DOWN speed, X audio,\n"
		"         Y subtitles, SELECT aspect, START settings, B twice quits\n"
		"keyboard: Enter, arrows, PgUp/PgDn (or Q/E), X, Y, Tab, Esc twice\n");
}

int main(int argc, char** argv)
{
	PlayerOptions opts;
	opts.lang = "eng,en";
	opts.stateDir = defaultStateDir();
	std::string file, font;
	std::vector<std::string> steps;	// --press: button names, tap:X:Y, drag:X1:Y1:X2:Y2 (the bottom panel's 640x480)
	bool scripted = false;
	AspectMode aspect = AspectMode::Fit;
	int windowed = -1;	// -1: decide by the video driver
	for (int i = 1; i < argc; i++)
	{
		std::string a = argv[i];
		auto value = [&]() -> std::string {
			if (i + 1 >= argc) { usage(); exit(2); }
			return argv[++i];
		};
		if (a == "--windowed") windowed = 1;
		else if (a == "--fullscreen") windowed = 0;
		else if (a == "--aspect") { if (!parseAspect(value(), aspect)) { usage(); return 2; } }
		else if (a == "--lang") opts.lang = value();
		else if (a == "--state-dir") opts.stateDir = value();
		else if (a == "--font") font = value();
		else if (a == "-v") opts.verbose = true;
		else if (a == "--press")
		{
			std::stringstream list(value());
			std::string step;
			while (std::getline(list, step, ','))
			{
				int n = 0;
				float v[4];
				bool ok = buttonFromName(step) != Button::None || step == "info" || step == "page" || step == "rows" || step == "screenshot" || step.rfind("prop:", 0) == 0 || step.rfind("shot:", 0) == 0 ||
					(sscanf(step.c_str(), "tap:%f:%f%n", &v[0], &v[1], &n) == 2 && n == (int)step.size()) ||
					(sscanf(step.c_str(), "dtap:%f:%f%n", &v[0], &v[1], &n) == 2 && n == (int)step.size()) ||
					(sscanf(step.c_str(), "hold:%f:%f%n", &v[0], &v[1], &n) == 2 && n == (int)step.size()) ||
					(sscanf(step.c_str(), "drag:%f:%f:%f:%f%n", &v[0], &v[1], &v[2], &v[3], &n) == 4 && n == (int)step.size());
				if (!ok) { fprintf(stderr, "unknown step: %s\n", step.c_str()); return 2; }
				steps.push_back(step);
			}
			scripted = true;
		}
		else if (a == "-h" || a == "--help") { usage(); return 0; }
		else if (file.empty()) file = a;
		else { usage(); return 2; }
	}
	// no file, or a folder: the browser picks them (in that folder)
	std::string startFolder;
	{
		struct stat st;
		if (!file.empty() && stat(file.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) startFolder.swap(file);
	}
	bool browserMode = file.empty();
	if (font.empty()) font = findFont();
	// the subtitle fonts are the bottom panel's font's neighbours
	if (!font.empty() && font.rfind('/') != std::string::npos) opts.fontsDir = font.substr(0, font.rfind('/'));

	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0 || TTF_Init() < 0)
	{
		fprintf(stderr, "SDL: %s\n", SDL_GetError());
		return 1;
	}
	evMpv = SDL_RegisterEvents(4);
	evRender = evMpv + 1;
	evTouch = evMpv + 2;
	evLevel = evMpv + 3;

	// On the handheld (SDL's KMSDRM) each panel is a display and each window fills one; elsewhere, two windows
	if (windowed < 0) windowed = strcmp(SDL_GetCurrentVideoDriver(), "KMSDRM") != 0;
	int displays = SDL_GetNumVideoDisplays();
	// the top panel is the DSI-2 connector, as banana-ui has it, if SDL names its displays by their connectors;
	// BP_TOP_DISPLAY and BP_BOTTOM_DISPLAY (indexes) settle it otherwise
	int topDisplay = 0;
	const char* topEnv = getenv("BP_TOP_CONNECTOR") ? getenv("BP_TOP_CONNECTOR") : getenv("BANANA_TOP");
	std::string topConnector = topEnv ? topEnv : "DSI-2";
	for (int i = 0; i < displays; i++)
	{
		const char* name = SDL_GetDisplayName(i);
		fprintf(stderr, "display %d: %s\n", i, name ? name : "?");
		if (name && strstr(name, topConnector.c_str())) topDisplay = i;
	}
	int bottomDisplay = displays > 1 && topDisplay == 0 ? 1 : 0;
	if (getenv("BP_TOP_DISPLAY")) topDisplay = atoi(getenv("BP_TOP_DISPLAY"));
	if (getenv("BP_BOTTOM_DISPLAY")) bottomDisplay = atoi(getenv("BP_BOTTOM_DISPLAY"));
	if (!windowed && displays < 2) fprintf(stderr, "only %d display: the controls share it\n", displays);

#ifdef BP_GLES
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	SDL_Window *top, *bottom;
	if (windowed)
	{
		top = SDL_CreateWindow("banana-player: top", 80, 60, Ui::W, Ui::H, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI);
		bottom = SDL_CreateWindow("banana-player: bottom", 80, 60 + Ui::H + 40, Ui::W, Ui::H, SDL_WINDOW_ALLOW_HIGHDPI);
	}
	else
	{
		top = SDL_CreateWindow("banana-player", SDL_WINDOWPOS_UNDEFINED_DISPLAY(topDisplay), SDL_WINDOWPOS_UNDEFINED_DISPLAY(topDisplay),
			Ui::W, Ui::H, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP);
		bottom = displays < 2 ? nullptr : SDL_CreateWindow("banana-player controls", SDL_WINDOWPOS_UNDEFINED_DISPLAY(bottomDisplay),
			SDL_WINDOWPOS_UNDEFINED_DISPLAY(bottomDisplay), Ui::W, Ui::H, SDL_WINDOW_FULLSCREEN_DESKTOP);
	}
	if (!top)
	{
		fprintf(stderr, "window: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GLContext gl = SDL_GL_CreateContext(top);
	if (!gl)
	{
		fprintf(stderr, "GL context: %s\n", SDL_GetError());
		return 1;
	}
	SDL_GL_SetSwapInterval(1);

	int rc = 0;
	{
		Player player;
		player.aspect = aspect;
		if (!player.init(opts, onMpvWakeup, nullptr) || !player.initRender(glProc, nullptr, onMpvRender, nullptr))
		{
			fprintf(stderr, "mpv could not start\n");
			return 1;
		}

		Ui ui;
		bool haveUi = bottom && ui.init(bottom, opts.fontsDir);
		if (bottom && !haveUi) fprintf(stderr, "the controls could not start: %s\n", SDL_GetError());
		Uint32 bottomId = bottom ? SDL_GetWindowID(bottom) : 0;

		// the lid closed, the handheld slept: paused at once (from the watch's thread), and the place kept
		static std::atomic<bool> slept{false};
		SleepWatch sleepWatch;
		sleepWatch.start([&player] {
			player.pauseNow();
			slept = true;
			onMpvWakeup(nullptr);
		});

		// the bottom touchscreen, read on its own thread; where there's none, SDL's mouse and touch stand in
		BottomTouch bottomTouch;
		bool ownTouch = haveUi && bottomTouch.start(getenv("BP_TOUCH") ? getenv("BP_TOUCH") : getenv("BANANA_TOUCH") ? getenv("BANANA_TOUCH") : "fe5e0000.i2c", [](Pointer pt) {
			SDL_Event ev{};
			ev.type = evTouch;
			ev.user.data1 = new Pointer(pt);
			SDL_PushEvent(&ev);
		});
		SDL_FingerID finger = -1;	// SDL's touches: the one finger that drives

		// the videos beside it are the playlist: L and R, and the next one plays when this one ends
		bool running = true;
		// the videos beside it are the playlist; with no file, the browser first
		ui.browserMode = browserMode;
		if (browserMode)
		{
			ui.startBrowser(player, startFolder);
			player.showIdle(true);
		}
		else
		{
			int index;
			std::vector<std::string> list = siblingVideos(file, index);
			player.open(list, index);
		}
		bool topDark = false;	// the top panel cleared while there's no video
		// the video's Quit (or its end): back to the browser when it opened the video, else out
		auto leave = [&] {
			if (browserMode && player.playing())
			{
				std::string last = player.currentPath();
				player.stop();
				ui.showBrowser(player, last);
				topDark = false;
			}
			else running = false;
		};

		// the top panel's pixels (RGBA, top row first), the frame drawn again for it: screenshots
		auto captureTop = [&]() -> SDL_Surface* {
			SDL_GL_MakeCurrent(top, gl);
			int w, h;
			SDL_GL_GetDrawableSize(top, &w, &h);
			using ReadPixels = void (*)(int, int, int, int, unsigned, unsigned, void*);
			auto readPixels = (ReadPixels)SDL_GL_GetProcAddress("glReadPixels");
			if (!readPixels) return nullptr;
			player.render(w, h);	// a video, or the icon with nothing playing
			std::vector<unsigned char> rgba((size_t)w * h * 4);
			readPixels(0, 0, w, h, 0x1908 /* GL_RGBA */, 0x1401 /* GL_UNSIGNED_BYTE */, rgba.data());
			SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
			if (s)
				for (int y = 0; y < h; y++)	// GL's rows go up
					memcpy((unsigned char*)s->pixels + (size_t)y * s->pitch, &rgba[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4);
			SDL_GL_SwapWindow(top);
			player.reportSwap();
			return s;
		};
		// F + L2 (as in BananaOS's emulators) or, on a computer, F12: both panels, the top over the bottom, as a PNG
		// in the card's screenshots folder
		auto screenshot = [&] {
			SDL_Surface* t = captureTop();
			SDL_Surface* bt = haveUi ? ui.capture(player) : nullptr;
			std::string dir = getenv("BP_SCREENSHOTS") ? getenv("BP_SCREENSHOTS")
							: exists("/data/screenshots") ? "/data/screenshots" : getenv("HOME") ? getenv("HOME") : ".";
			char name[64];
			time_t now = time(nullptr);
			strftime(name, sizeof name, "/Banana Player %Y-%m-%d %H-%M-%S.png", localtime(&now));
			bool ok = false;
			if (t)
			{
				int w = std::max(t->w, bt ? bt->w : 0), h = t->h + (bt ? bt->h : 0);
				std::vector<unsigned char> rgba((size_t)w * h * 4, 0);
				auto put = [&](SDL_Surface* s, int y0) {
					for (int y = 0; y < s->h; y++)
						memcpy(&rgba[((size_t)(y0 + y) * w + (w - s->w) / 2) * 4], (unsigned char*)s->pixels + (size_t)y * s->pitch, (size_t)s->w * 4);
				};
				put(t, 0);
				if (bt) put(bt, t->h);
				ok = savePng(dir + name, w, h, rgba.data());
			}
			if (t) SDL_FreeSurface(t);
			if (bt) SDL_FreeSurface(bt);
			player.osd(ok ? "Screenshot saved" : "Screenshot couldn't be saved");
			fprintf(stderr, "screenshot %s%s: %s\n", dir.c_str(), name, ok ? "saved" : "failed");
		};
		bool fHeld = false, l2Down = false;

		// the volume and brightness keys (bananad's): their level on a card over the top panel for 1.5 s
		LevelWatch levelWatch;
		levelWatch.start([](const std::string& what, int pct) {
			SDL_Event ev{};
			ev.type = evLevel;
			ev.user.code = pct;
			ev.user.data1 = new std::string(what);
			SDL_PushEvent(&ev);
		});
		Uint32 levelUntil = 0;

		Button held = Button::None;	// a pad's button held down: repeated as a key is
		Uint32 heldNext = 0, savedAt = SDL_GetTicks();
		// Both panels drawn again and again for a while after the start and a wake, and every few seconds after: on
		// the handheld (SDL's KMSDRM) the first frames presented don't reach the panels, and a page that doesn't
		// change would otherwise stay black till something did
		Uint32 freshUntil = SDL_GetTicks() + 2000, refreshedAt = 0;
		while (running)
		{
			SDL_Event e;
			bool got = SDL_WaitEventTimeout(&e, ui.animating() ? 16 : held != Button::None || player.loading() ? 20 : SDL_GetTicks() < freshUntil || levelUntil ? 50 : 250);
			while (got)
			{
				// every button in the log (home/.config/banana-player/log.txt on the card), mapped or not: what the
				// handheld's keys send, found by pressing them
				if (e.type == SDL_CONTROLLERBUTTONDOWN)
				{
					const char* name = SDL_GameControllerGetStringForButton((SDL_GameControllerButton)e.cbutton.button);
					fprintf(stderr, "input: pad button %d (%s)\n", e.cbutton.button, name ? name : "?");
				}
				else if (e.type == SDL_JOYBUTTONDOWN) fprintf(stderr, "input: joystick %d button %d\n", e.jbutton.which, e.jbutton.button);
				else if (e.type == SDL_JOYHATMOTION && e.jhat.value) fprintf(stderr, "input: joystick %d hat %d value %d\n", e.jhat.which, e.jhat.hat, e.jhat.value);
				else if (e.type == SDL_KEYDOWN && !e.key.repeat)
					fprintf(stderr, "input: key %s (scancode %d)\n", SDL_GetKeyName(e.key.keysym.sym), e.key.keysym.scancode);
				if (e.type == SDL_QUIT) running = false;
				else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_CLOSE) running = false;
				else if (e.type == SDL_WINDOWEVENT && (e.window.event == SDL_WINDOWEVENT_EXPOSED || e.window.event == SDL_WINDOWEVENT_SHOWN))
				{
					renderPending = true;
					freshUntil = std::max(freshUntil, SDL_GetTicks() + 500);
				}
				else if (e.type == evMpv) mpvPending = false;
				else if (e.type == SDL_CONTROLLERDEVICEADDED || e.type == SDL_CONTROLLERDEVICEREMOVED) handlePadHotplug(e);
				else if (scripted && (e.type == SDL_KEYDOWN || e.type == SDL_CONTROLLERBUTTONDOWN || e.type == evTouch ||
							 (e.type >= SDL_MOUSEMOTION && e.type <= SDL_MOUSEWHEEL) || (e.type >= SDL_FINGERDOWN && e.type <= SDL_FINGERMOTION)))
				{
					// a test run takes only its own steps: the windows can take the keyboard's focus while someone
					// types elsewhere
					if (e.type == evTouch) delete (Pointer*)e.user.data1;
				}
				else if ((e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_CONTROLLERBUTTONUP) && e.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE)
					fHeld = e.type == SDL_CONTROLLERBUTTONDOWN;	// F, for F + L2
				else if (e.type == SDL_CONTROLLERAXISMOTION && e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT)
				{
					// L2 is a trigger to SDL: pressed past half way
					bool down = e.caxis.value > 16000;
					if (down && !l2Down && fHeld) screenshot();
					l2Down = down;
				}
				else if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F12 && !e.key.repeat) screenshot();
				else if (e.type == SDL_CONTROLLERBUTTONUP) held = Button::None;
				// F + START: out at once, from anywhere (the place is kept as the loop ends)
				else if (e.type == SDL_CONTROLLERBUTTONDOWN && fHeld && buttonFromEvent(e) == Button::Start) running = false;
				else if (Button b = buttonFromEvent(e); b != Button::None)
				{
					if (e.type == SDL_CONTROLLERBUTTONDOWN && ui.repeats(b, player))
					{
						held = b;
						heldNext = SDL_GetTicks() + 400;
					}
					if (ui.press(b, player)) leave();
				}
				else if (e.type == evLevel)
				{
					std::string* what = (std::string*)e.user.data1;
					player.showLevel(*what == "brightness" ? "Brightness" : "Volume", e.user.code);
					levelUntil = SDL_GetTicks() + 1500;
					renderPending = true;
					delete what;
				}
				else if (e.type == evTouch)
				{
					Pointer* pt = (Pointer*)e.user.data1;
					if (ui.pointer(*pt, player)) leave();
					delete pt;
				}
				else if (haveUi && !ownTouch)
				{
					// a computer: the mouse in the bottom window, or SDL's touches on it (a touch's copy as a mouse is
					// left out: it came as a finger)
					Pointer pt{Pointer::Move, 0, 0};
					bool use = false;
					int ww, wh;
					SDL_GetWindowSize(bottom, &ww, &wh);
					if ((e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) && e.button.which != SDL_TOUCH_MOUSEID &&
						e.button.windowID == bottomId && e.button.button == SDL_BUTTON_LEFT)
					{
						pt = {e.type == SDL_MOUSEBUTTONDOWN ? Pointer::Down : Pointer::Up, e.button.x / (float)ww, e.button.y / (float)wh};
						use = true;
					}
					else if (e.type == SDL_MOUSEMOTION && e.motion.which != SDL_TOUCH_MOUSEID && e.motion.windowID == bottomId &&
						(e.motion.state & SDL_BUTTON_LMASK))
					{
						pt = {Pointer::Move, e.motion.x / (float)ww, e.motion.y / (float)wh};
						use = true;
					}
					else if ((e.type == SDL_FINGERDOWN || e.type == SDL_FINGERUP || e.type == SDL_FINGERMOTION) &&
						(e.tfinger.windowID == bottomId || e.tfinger.windowID == 0))
					{
						// one finger at a time: the first down, till it lifts
						if (e.type == SDL_FINGERDOWN && finger < 0) finger = e.tfinger.fingerId;
						if (e.tfinger.fingerId == finger)
						{
							pt = {e.type == SDL_FINGERDOWN ? Pointer::Down : e.type == SDL_FINGERUP ? Pointer::Up : Pointer::Move,
								e.tfinger.x, e.tfinger.y};
							use = true;
							if (e.type == SDL_FINGERUP) finger = -1;
						}
					}
					if (use)
					{
						pt.x = std::clamp(pt.x, 0.f, 1.f);
						pt.y = std::clamp(pt.y, 0.f, 1.f);
						if (ui.pointer(pt, player)) leave();
					}
				}
				got = SDL_PollEvent(&e);
			}
			if (held != Button::None && SDL_GetTicks() >= heldNext)
			{
				heldNext = SDL_GetTicks() + 150;
				if (ui.repeats(held, player) && ui.press(held, player)) leave();
			}

			player.pollEvents();
			player.checkAudio();
			player.checkSongEnd();
			player.checkPhoto();
			player.checkLoading();
			if (haveUi) ui.tick(player);
			if (player.quit || ui.quitRequested) running = false;
			if (player.finished)
			{
				// played to its end: next time from the start; on to the next video, or out after the last
				player.saveResume();
				player.finished = false;
				if (!player.advance()) leave();
			}
			// the place is kept now and then too: a battery that runs out, a power-off after sleeping
			bool woke = slept.exchange(false);
			if (woke) freshUntil = SDL_GetTicks() + 2000;	// the panels back from a sleep: drawn again
			// a photo has no Play: an animated one paused by the lid goes on at the wake (a video waits for A)
			if (woke && player.playingPhoto()) player.unpause();
			if (woke || (player.loaded && !player.paused && SDL_GetTicks() - savedAt > 60000))
			{
				player.saveResume();
				savedAt = SDL_GetTicks();
			}
			// a video picked in the browser: it and the videos beside it
			if (std::string pick = ui.takeOpenRequest(); !pick.empty())
			{
				int index;
				std::vector<std::string> list = siblingVideos(pick, index);
				player.open(list, index);
				ui.showPlayer();
			}
			// a test run's steps wait for the file to open, 3 s at most (a file that never does: a loading test)
			static Uint32 waitFrom = 0;
			if (player.loaded || !player.playing() || !player.error.empty()) waitFrom = 0;
			else if (!waitFrom) waitFrom = SDL_GetTicks();
			if (scripted && (!waitFrom || SDL_GetTicks() - waitFrom > 3000) && running)
			{
				static Uint32 next = SDL_GetTicks() + 800;
				static size_t pressed = 0;
				if (SDL_GetTicks() >= next)
				{
					next = SDL_GetTicks() + 800;
					if (pressed == steps.size()) running = false;
					else
					{
						const std::string& step = steps[pressed++];
						float v[4];
						auto at = [&](Pointer::Kind k, float x, float y) {
							if (ui.pointer({k, x / Ui::W, y / Ui::H}, player)) leave();
						};
						if (sscanf(step.c_str(), "dtap:%f:%f", &v[0], &v[1]) == 2)
						{
							// a double-tap: two taps 120 ms apart
							for (int i = 0; i < 2; i++)
							{
								if (i) SDL_Delay(120);
								at(Pointer::Down, v[0], v[1]);
								at(Pointer::Up, v[0], v[1]);
							}
						}
						else if (sscanf(step.c_str(), "tap:%f:%f", &v[0], &v[1]) == 2)
						{
							at(Pointer::Down, v[0], v[1]);
							at(Pointer::Up, v[0], v[1]);
						}
						else if (sscanf(step.c_str(), "hold:%f:%f", &v[0], &v[1]) == 2)
						{
							// a finger held 1.1 s, the panel drawn meanwhile (a hold is timed there)
							at(Pointer::Down, v[0], v[1]);
							for (int i = 0; i < 11; i++)
							{
								SDL_Delay(100);
								ui.draw(player);
							}
							at(Pointer::Up, v[0], v[1]);
						}
						else if (sscanf(step.c_str(), "drag:%f:%f:%f:%f", &v[0], &v[1], &v[2], &v[3]) == 4)
						{
							// a finger's way, in steps 30 ms apart: the scrub sees it move
							at(Pointer::Down, v[0], v[1]);
							for (int i = 1; i <= 10; i++)
							{
								SDL_Delay(30);
								at(Pointer::Move, v[0] + (v[2] - v[0]) * i / 10, v[1] + (v[3] - v[1]) * i / 10);
								player.pollEvents();
							}
							at(Pointer::Up, v[2], v[3]);
						}
						else if (step == "info")
						{
							// what the Media info page shows
							for (auto& r : player.mediaInfo())
								printf(r.header ? "  [%s]\n" : "    %-16s %s\n", r.label.c_str(), r.value.c_str());
						}
						else if (step == "screenshot") screenshot();	// as F + L2 does
						else if (step.rfind("shot:", 0) == 0)
						{
							// both panels: <BP_SHOTS or .>/<name>-top.png and -bottom.png
							std::string base = std::string(getenv("BP_SHOTS") ? getenv("BP_SHOTS") : ".") + "/" + (step.c_str() + 5);
							SDL_Surface* t = captureTop();
							SDL_Surface* bt = haveUi ? ui.capture(player) : nullptr;
							bool ok = t && bt && savePng(base + "-top.png", t->w, t->h, (unsigned char*)t->pixels) &&
									  savePng(base + "-bottom.png", bt->w, bt->h, (unsigned char*)bt->pixels);
							if (t) SDL_FreeSurface(t);
							if (bt) SDL_FreeSurface(bt);
							printf("    shot %s: %s\n", step.c_str() + 5, ok ? "saved" : "failed");
						}
						else if (step.rfind("prop:", 0) == 0)
							printf("    %s = %s\n", step.c_str() + 5, player.property(step.c_str() + 5).c_str());
						else if (step == "rows")
						{
							for (auto& l : ui.browseLabels()) printf("    %s\n", l.c_str());
						}
						else if (step == "page")
						{
							for (auto& l : ui.pageLines()) printf("    %s\n", l.c_str());
						}
						else if (ui.press(buttonFromName(step), player)) leave();
						// (no wait for mpv's answer here: a loop that doesn't draw holds mpv up as it switches files, and
						// the timings would be the harness's; the state line can lag a step)
						player.pollEvents();
						const Track* a = player.selected(TrackType::Audio);
						const Track* s = player.selected(TrackType::Sub);
						if (*ui.menuTabName()) printf("press %zu: menu=%s\n", pressed, ui.menuTabName());
						else if (ui.browsing())
						{
							std::string at = ui.browseSelection();
							printf("press %zu: browse=%s at=%s\n", pressed, ui.browseFolder().c_str(), at.substr(at.rfind('/') + 1).c_str());
						}
						else if (!player.playing()) printf("press %zu: settings (from the browser)\n", pressed);
						else if (player.playingPhoto())
						{
							const Player::PhotoView& v = player.photo();
							printf("press %zu: photo=%d/%d %dx%d zoom=%.2f pan=%.3f,%.3f turn=%d %s thumb=%dx%d screen=%s\n", pressed,
								player.index() + 1, player.count(), player.photoWidth(), player.photoHeight(), v.zoom, v.panX, v.panY,
								v.rotate, v.fill ? "fill" : "fit", player.thumbW, player.thumbH, ui.screenOn() ? "on" : "off");
						}
						else printf("press %zu: file=%d/%d t=%.1f paused=%d speed=%s aspect=%s audio=%s sub=%s screen=%s%s\n", pressed,
							player.index() + 1, player.count(), player.position, player.paused, speedLabel(player.speed).c_str(),
							aspectName(player.aspect), a ? trackLabel(*a, (int)a->id).c_str() : "off",
							s ? trackLabel(*s, (int)s->id).c_str() : "off", ui.screenOn() ? "on" : "off", ui.touchIsLocked() ? " LOCKED" : "");
						fflush(stdout);
					}
				}
			}

			Uint32 now = SDL_GetTicks();
			if (levelUntil && now >= levelUntil)
			{
				player.hideLevel();
				levelUntil = 0;
				topDark = false;	// with no video: black again, without the card
			}
			bool refresh = now - refreshedAt >= (now < freshUntil ? 100u : 2000u);
			if (refresh)
			{
				refreshedAt = now;
				topDark = false;	// the black of no video drawn again too
			}
			// with nothing playing the top panel is drawn on a change only, and mpv may not have the level card yet
			// when it's asked to show: drawn again and again while the card is up
			if (renderPending.exchange(false) || refresh || (!player.playing() && (!topDark || levelUntil)))
			{
				SDL_GL_MakeCurrent(top, gl);
				int w, h;
				SDL_GL_GetDrawableSize(top, &w, &h);
				if (!player.playing())
				{
					// nothing playing: mpv draws its black with the icon (and a level card if one is up)
					player.render(w, h);
					SDL_GL_SwapWindow(top);
					player.reportSwap();
					topDark = true;
				}
				// a new frame, or the same one again (paused, or a refresh)
				else if (player.frameDue() || refresh)
				{
					player.render(w, h);
					SDL_GL_SwapWindow(top);
					player.reportSwap();
				}
			}
			if (haveUi) ui.draw(player, refresh);
		}
		if (!player.error.empty()) rc = 1;
		sleepWatch.stop();
		levelWatch.stop();
		bottomTouch.stop();
		player.saveResume();
		SDL_GL_MakeCurrent(top, gl);
		player.shutdown();
	}
	SDL_GL_DeleteContext(gl);
	if (bottom) SDL_DestroyWindow(bottom);
	SDL_DestroyWindow(top);
	TTF_Quit();
	SDL_Quit();
	return rc;
}
