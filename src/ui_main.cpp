#include "ui.h"
#include "format.h"
#include "playlist.h"
#include "netpath.h"
#include "theme.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>
#ifdef BP_WEB
#include "web/netif.h"
#include "web/server.h"
#include <qrcodegen.hpp>
#endif
#include "ui_internal.h"

namespace bp
{
	bool Ui::predicting(const Player& p) const
	{
		const BrowserPos& b = browserPos;
		return browserPlaying && p.paused && b.t >= 0 && p.loaded && p.duration > 0 && p.currentPath() == b.path;
	}

	double Ui::shownPosition(const Player& p) const
	{
		if (!predicting(p)) return p.position;
		const BrowserPos& b = browserPos;
		double t = b.t + (b.playing ? (SDL_GetTicks() - b.at) / 1000.0 : 0);
		return std::clamp(t, 0.0, p.duration);
	}

	int Ui::shownChapter(const Player& p) const
	{
		if (!predicting(p)) return p.currentChapter();
		double t = shownPosition(p);
		int c = -1;
		for (int i = 0; i < (int)p.chapters.size() && p.chapters[i].time <= t; i++) c = i;
		return c;
	}

	void Ui::noteSeek(const Player& p, double t)
	{
		if (predicting(p)) browserPos = {browserPos.path, std::clamp(t, 0.0, p.duration), SDL_GetTicks(), browserPos.playing};
	}

	void Ui::resumeFromBrowser(Player& p)
	{
		if (!predicting(p)) return;
		p.seekFraction(std::clamp(shownPosition(p) / p.duration, 0.0, 1.0));	// queued before the unpause: no sound from before
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
		if (id == WEB_NEWPIN) return {296, 384, 152, 52};
		if (id == WEB_OFF) return {464, 384, 152, 52};
		if (id >= PLAY_ROW && id < PLAY_ROW + 4) return playingRowRect(id - PLAY_ROW);
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
			drawText("This photo can't be shown:", 0, DANGER, pan.x + 12, pan.y + 12, pan.w - 24);
			drawText(p.error, 0, DANGER, pan.x + 12, pan.y + 40, pan.w - 24);
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
				fill({bx, by, bw, 2}, ACCENT);
				fill({bx, by + bh - 2, bw, 2}, ACCENT);
				fill({bx, by, 2, bh}, ACCENT);
				fill({bx + bw - 2, by, 2, bh}, ACCENT);
			}
		}
		else if (p.thumbPending()) drawText("Loading" + std::string(SDL_GetTicks() / 400 % 4, '.'), 1, DIM, pan.x + pan.w / 2, pan.y + pan.h / 2 - 12, 0, 2);
		else drawText("No preview", 1, DIM, pan.x + pan.w / 2, pan.y + pan.h / 2 - 12, 0, 2);

		// under the pan area: the zoom and the size; how to use it
		std::string zoom = "Zoom " + std::to_string((int)std::lround(v.zoom * 100)) + "%";
		if (p.photoWidth() > 0) zoom += "   " + std::to_string(p.photoWidth()) + " x " + std::to_string(p.photoHeight());
		drawText(zoom, 0, TEXT, pan.x, pan.y + pan.h + 10, pan.w);
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
			drawBox(b, pressedBtn == PHOTO_BTN + i ? PRESSED : BUTTON, R_BUTTON);
			bool hint = hints && *btns[i].hint;
			drawText(btns[i].label, 1, btns[i].off ? DIM : TEXT, b.x + b.w / 2, b.y + (hint ? 5 : 15), b.w - 6, 2);
			if (hint) drawText(btns[i].hint, 0, btns[i].off ? DIM : ACCENT, b.x + b.w / 2, b.y + 33, b.w - 6, 2);
		}
		if (p.count() > 1)
		{
			Rect last = photoBtn(PHOTO_BTNS - 1, p);
			drawText(std::to_string(p.index() + 1) + " / " + std::to_string(p.count()), 0, DIM, last.x + last.w, last.y + last.h + 12, 0, 1);
		}

		drawLeave(p);
		// along the bottom, beside it: what A does here, or what a second B would do
		const char* leave = browserMode ? "close" : "quit";
		if (quitArmed) drawTextMid(std::string(hints ? "Press B again to " : "Tap again to ") + leave, 0, ACCENT, 24, 450, 430);
		else if (hints) drawTextMid("A", 0, ACCENT, 24, 450), drawTextMid("Zoom to 2x, or the whole photo", 0, DIM, 24 + (int)std::lround(textWidth("A", 0) / sx) + 7, 450, 400);
	}

	bool Ui::chapterLineShown(const Player& p) const
	{
		// before the first chapter starts there's none to name (and nothing to tap)
		return p.playing() && !p.playingPhoto() && !p.loading() && p.error.empty() && !p.chapters.empty() &&
			shownChapter(p) >= 0 && shownChapter(p) < (int)p.chapters.size();
	}

	void Ui::drawMain(const Player& p)
	{
		if (p.playingPhoto()) { drawPhoto(p); return; }
		std::string name = p.currentPath().substr(p.currentPath().rfind('/') + 1);
		bool err = !p.error.empty();
		bool actions = headerActions(p);
		drawHeader(!p.title.empty() ? p.title : err ? name : "Opening...", p);
		if (roots.empty()) roots = browseRoots();	// (started with a file: the browser never read them)
		drawSource(p);
		const char* leave = browserMode ? "close" : "quit";
		bool hotkeys = p.settings.buttonHotkeys;
		// the status line: how it plays (or what went wrong), and on its right the count or what a press does now
		std::string right = p.count() > 1 ? std::to_string(p.index() + 1) + " / " + std::to_string(p.count()) : "";
		SDL_Color rightInk = DIM;
		if (quitArmed) { right = std::string(hotkeys ? "Press B again to " : "Tap again to ") + leave; rightInk = ACCENT; }
		else if (touchLocked) { right = "Hold the lock 1 s to unlock"; rightInk = ACCENT; }
		if (err)
		{
			drawGlyphMid(G_ALERT, 35, STATUS_Y, DANGER);
			drawTextMid("Can't play this file", 1, DANGER, 56, STATUS_Y, 300);
			auto lines = wrapText(p.error, 0, 592, 2);
			for (size_t i = 0; i < lines.size(); i++) drawTextMid(lines[i], 0, TEXT, 24, STATUS_Y + 32 + (int)i * 24, 592);
		}
		else
		{
			std::string status = p.loading() ? "Loading" + std::string(SDL_GetTicks() / 400 % 4, '.') : predicting(p) && browserPos.playing ? "Playing in your browser"
				: p.paused ? "Paused" : "Playing";
			if (p.source().shuffled) status += " \xC2\xB7 Shuffle";
			if (std::fabs(p.speed - 1) > 1e-6) status += " \xC2\xB7 " + speedLabel(p.speed);
			if (!hotkeys) status += " \xC2\xB7 buttons off";
			int x = 24;
			if (!p.loading())
			{
				drawGlyphMid(p.paused && !(predicting(p) && browserPos.playing) ? G_PAUSE : G_PLAY, 32, STATUS_Y, ACCENT);
				x = 52;
			}
			drawTextMid(status, 1, ACCENT, x, STATUS_Y, 330);

			// the chapter playing, between the status line and the bar: its title (the Y button has its number), or
			// "Chapter 3 of 12" dim when it has none. A tap opens the list
			if (chapterLineShown(p))
			{
				int c = shownChapter(p), n = (int)p.chapters.size();
				if (pressedBtn == CHAPTER_BTN) drawBox(buttonRect(CHAPTER_BTN), PRESSED, R_BUTTON);
				drawGlyphMid(G_CHAPTERS, 35, CHAPTER_Y, DIM);
				if (chapterUntitled(p.chapters[c].title))
					drawTextMid("Chapter " + std::to_string(c + 1) + " of " + std::to_string(n), 0, DIM, 52, CHAPTER_Y, 540);
				else drawTextMid(p.chapters[c].title, 0, TEXT, 52, CHAPTER_Y, 540);
				drawGlyphMid(G_RIGHT, 604, CHAPTER_Y, DIM);
			}

			double pos = shownPosition(p);
			double frac = dragging ? dragFraction : p.duration > 0 && pos >= 0 ? std::clamp(pos / p.duration, 0.0, 1.0) : 0;
			drawLevel(BAR, (int)(BAR.w * frac), TRACK);
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
			fillRound({BAR.x + (int)(BAR.w * frac) - 4, BAR.y - 6, 8, BAR.h + 12}, KNOB, R_KNOB);
			drawText(formatTime(dragging ? dragFraction * p.duration : pos), 1, TEXT, BAR.x, BAR.y + 32);
			drawText(formatTime(p.duration), 1, DIM, BAR.x + BAR.w, BAR.y + 32, 0, 1);
			// how long is left, at the speed it plays (half at 2x)
			double left = timeLeft(dragging ? dragFraction * p.duration : pos, p.duration, p.speed);
			// dragging the bar of a file with chapters: the chapter under the finger instead ("3 · The Dragon")
			int under = -1;
			if (dragging && p.duration > 0)
				for (int i = 0; i < (int)p.chapters.size(); i++)
					if (p.chapters[i].time <= dragFraction * p.duration) under = i;
			if (under >= 0)
				drawText(std::to_string(under + 1) + " \xC2\xB7 " + chapterLabel(p.chapters[under], under + 1), 0, TEXT,
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
			drawTextMid("L2/R2", 0, ACCENT, x - vw, STATUS_Y, 0, 1);
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
				// Like and Add work on a file that didn't open too (it's still a file)
				case Button::L3: label = likedNow(p) ? "Liked" : "Like"; g = likedNow(p) ? &G_HEART : &G_HEART_O; off = !actions; break;
				case Button::A: label = p.paused || err ? "Play" : "Pause"; g = p.paused || err ? &G_PLAY : &G_PAUSE; break;
				case Button::None: label = "Add"; g = addFlashUntil && SDL_GetTicks() < addFlashUntil ? &G_CHECK : &G_PL_ADD; off = !actions; break;
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
				// a file that didn't open: only what leads elsewhere (the next file, the menu), and Like / Add
				if (err && which != Button::L && which != Button::R && which != Button::Start && which != Button::L3 && which != Button::None) off = true;
				drawBox(b, pressedBtn == r * 5 + c && !off ? PRESSED : BUTTON, R_BUTTON);
				const char* key = hotkeys ? GRID[r][c].hint : nullptr;
				if (which == Button::X && !p.playingAudio())
				{
					// a video's Audio & Subtitles: their two marks, no words
					SDL_Color ink = off ? DIM : TEXT;
					drawGlyphMid(G_SPEAKER, b.x + 40, b.y + 24, ink);
					drawGlyphMid(G_CC, b.x + 66, b.y + 24, ink);
					if (key) drawTextMid(key, 0, off ? DIM : ACCENT, b.x + b.w / 2, b.y + b.h * 4 / 5 - 2, b.w - 6, 2);
				}
				else if (which == Button::Y && (!p.playingAudio() || !p.chapters.empty()))
				{
					// Chapters: the one playing of how many ("3/12"; "3" when that's too wide, "–/12" before the first).
					// None (or not known yet) dims it, though it still opens the page that says so
					int n = (int)p.chapters.size(), c = shownChapter(p);
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
					// a song with no chapters: Sound, by touch only (no key: Y does nothing here). "FX", never dim, whether
					// the equalizer and effect are on or off (dim read as a button that can't be pressed); what's set
					// is on the line under the buttons
					drawButton({b.x, b.y, b.w, b.h * 4 / 5}, &G_EQ, "FX", nullptr, off, 1);
					drawTextMid("Sound", 0, off ? DIM : DIM_ON, b.x + b.w / 2, b.y + b.h * 4 / 5 - 2, b.w - 6, 2);
				}
				else if (which == Button::L3)
				{
					// the heart: yellow once liked; its key only where there's an L3 (the RG DS Plus, the 1024x768 one)
					bool hasL3 = sx > 1.2f;
					drawButton(b, g, label, hasL3 ? key : nullptr, off, 1, likedNow(p) && !off);
				}
				else if (which == Button::None)
				{
					// Add to playlist: no key of its own, so what it adds to under it, as Sound's button says Sound
					drawButton({b.x, b.y, b.w, b.h * 4 / 5}, g, label, nullptr, off, 1, g == &G_CHECK);
					drawTextMid("Playlist", 0, off ? DIM : DIM_ON, b.x + b.w / 2, b.y + b.h * 4 / 5 - 2, b.w - 6, 2);
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
			if (pressedBtn == id) drawBox(buttonRect(id), PRESSED, R_BUTTON);
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
						drawTextMid(tail, 0, TEXT, x, LINE1_Y, 0, 1);
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
						drawTextMid(preset, 0, TEXT, x, LINE1_Y, room, 1);
						x -= w(preset, room);
					}
				}
				drawGlyphMid(G_EQ, x - 8 - glyphW(G_EQ) / 2, LINE1_Y, DIM);
				trackW = x - 8 - glyphW(G_EQ) - 12 - 52;
			}
			drawGlyphMid(G_SPEAKER, 33, LINE1_Y, DIM);
			drawTextMid(label(a, TrackType::Audio), 0, TEXT, 52, LINE1_Y, trackW);
		}
		if (err) {}
		else if (p.playingAudio())
		{
			std::string by = p.artist();
			if (!p.album().empty()) by += (by.empty() ? "" : " \xC2\xB7 ") + p.album();
			if (!by.empty())
			{
				drawGlyphMid(G_NOTE, 33, LINE2_Y, DIM);
				drawTextMid(by, 0, TEXT, 52, LINE2_Y, 564);
			}
		}
		else
		{
			lineEnd(LINE2_BTN, LINE2_Y);
			drawGlyphMid(G_CC, 33, LINE2_Y, DIM);
			drawTextMid(label(s, TrackType::Sub), 0, TEXT, 52, LINE2_Y, 540);
		}

		// the row along the bottom: Lock, Info, Screen off, and leaving
		Rect lk = buttonRect(LOCK_BTN), inf = buttonRect(INFO_BTN), so = buttonRect(SCREEN_BTN), q = buttonRect(QUIT_BTN);
		bool holding = touchLocked && lockHoldAt;
		drawBox(lk, holding ? PRESSED : touchLocked ? ACCENT : pressedBtn == LOCK_BTN ? PRESSED : BUTTON, R_BUTTON);
		if (holding)
		{
			// the hold, filling as it goes (inside the edge, over the lip)
			double f = std::min(1.0, (SDL_GetTicks() - lockHoldAt) / (double)UNLOCK_HOLD_MS);
			fillRound({lk.x + 6, lk.y + lk.h - 7, (int)((lk.w - 12) * f), 4}, ACCENT, 2);
		}
		SDL_Color lkInk = touchLocked && !holding ? ON_ACCENT : TEXT;
		{
			const char* t = touchLocked ? (holding ? "Hold..." : "Locked") : "Lock";
			int tw = (int)std::lround(textWidth(t, 0) / sx), gw = glyphW(G_LOCK), x = lk.x + lk.w / 2 - (gw + 8 + tw) / 2;
			drawGlyphMid(G_LOCK, x + gw / 2, lk.y + lk.h / 2, lkInk);
			drawTextMid(t, 0, lkInk, x + gw + 8, lk.y + lk.h / 2);
		}
		drawBox(inf, pressedBtn == INFO_BTN ? PRESSED : BUTTON, R_BUTTON);
		drawButton(inf, &G_INFO, "Info", nullptr, false, 0);
		drawBox(so, pressedBtn == SCREEN_BTN ? PRESSED : BUTTON, R_BUTTON);
		drawButton(so, &G_MOON, "Screen off", nullptr, false, 0);
		drawLeave(p);
		if (touchLocked)
		{
			// locked: everything but the lock and Screen off dimmed (they're all that a finger does now)
			// from just under the header (its back icon ends at 56): the source line's card mark starts above 64
			SDL_Color veil = sdl(theme::BG, 170);
			fill({0, 58, W, lk.y - 58}, veil);
			fill(inf, veil);
			fill(q, veil);
			// what unlocks it stays readable over the veil (B's "press again" can't come up while locked)
			if (!quitArmed) drawTextMid("Hold the lock 1 s to unlock", 0, ACCENT, 616, STATUS_Y, 250, 1);
		}
	}

	void Ui::drawLeave(const Player& p)
	{
		Rect q = buttonRect(QUIT_BTN);
		bool hotkeys = p.settings.buttonHotkeys;
		drawBox(q, pressedBtn == QUIT_BTN ? PRESSED : BUTTON, R_BUTTON);
		{
			// leaving: B twice (a press in a pocket isn't one), or this twice
			std::string t = quitArmed ? "Again?" : browserMode ? "Close" : "Quit";
			const Glyph& g = browserMode ? G_CROSS : G_POWER;
			std::string key = hotkeys && !quitArmed ? "B\xC3\x97" "2" : "";
			int tw = (int)std::lround(textWidth(t, 0) / sx), gw = glyphW(g), kw = key.empty() ? 0 : (int)std::lround(textWidth(key, 0) / sx) + 10;
			int x = q.x + q.w / 2 - (gw + 8 + tw + kw) / 2;
			SDL_Color ink = quitArmed ? ACCENT : TEXT;
			drawGlyphMid(g, x + gw / 2, q.y + q.h / 2, ink);
			drawTextMid(t, 0, ink, x + gw + 8, q.y + q.h / 2);
			if (!key.empty()) drawTextMid(key, 0, ACCENT, x + gw + 8 + tw + 10, q.y + q.h / 2);
		}
	}
}
