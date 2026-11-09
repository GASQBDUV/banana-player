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
	// ---- Home: the Recent strip and the sections' tiles; a section's header ----

	void Ui::listRecent(Player& p)
	{
		recent.clear();
		if (!p.settings.keepHistory) return;
		for (auto& path : p.history().items())
		{
			// a file still there (on a card taken out: not), or a network one whose connection is still saved (whether
			// the file is there is only known by asking its server: when it's opened)
			bool net = isNetPath(path);
			NetPath np;
			const NetServer* sv = net && netStore && parseNetPath(path, np) ? netStore->find(np.server) : nullptr;
			if (net ? !sv : !exists(path) || (!p.settings.showHiddenFolders && insideHiddenFolder(path))) continue;
			RecentItem it;
			it.path = path;
			it.net = net;
			// its name without the extension, as a card's title
			it.name = baseName(path);
			size_t dot = it.name.rfind('.');
			if (dot != std::string::npos && dot > 0) it.name.erase(dot);
			it.kind = isAudioFile(path) ? &G_NOTE : isPhotoFile(path) ? &G_PICTURE : &G_FILM;
			// how far into it: where it was left (a song always starts at its start: played), of how long it is
			const ResumeEntry* e = p.resumeStore().find(path);
			double len = e ? e->duration : -1, pos = e && e->position >= 0 && !isAudioFile(path) ? e->position : -1;
			if (pos >= 0)
			{
				it.at = formatTime(pos);
				it.sub = len > 0 ? it.at + " / " + formatTime(len) : "at " + it.at;
				it.frac = len > 0 ? std::clamp(pos / len, 0.0, 1.0) : 0;
			}
			else
			{
				it.sub = len > 0 ? "Played \xC2\xB7 " + formatTime(len) : "Played";
				it.frac = 1;
			}
			// where it is: the server, or the place on the cards ("Videos", "This card")
			if (sv) it.where = serverName(*sv);
			else
			{
				std::string w = whereOf(parentFolder(path));
				it.where = w.substr(0, w.find(" / "));
			}
			recent.push_back(it);
		}
	}

	void Ui::setStrip(int i, bool animate)
	{
		homeStrip = std::clamp(i, 0, stripCount() - 1);
		double to = stripTarget(homeStrip);
		if (!animate || to == stripOff)
		{
			stripOff = to;
			stripT0 = 0;
			return;
		}
		stripFrom = stripOff;
		stripTo = to;
		stripT0 = std::max<Uint32>(1, SDL_GetTicks());
	}

	void Ui::stripStep()
	{
		if (!stripT0) return;
		// eased out over 120 ms: quick to start, settling into place
		double k = std::min(1.0, (SDL_GetTicks() - stripT0) / 120.0), e = 1 - std::pow(1 - k, 3);
		stripOff = stripFrom + (stripTo - stripFrom) * e;
		if (k >= 1)
		{
			stripOff = stripTo;
			stripT0 = 0;
		}
	}

	Ui::Rect Ui::tileRect(int g) const
	{
		// two by two under the strip; taller with no Recent (a line in its place)
		bool has = !recent.empty();
		int y0 = has ? 200 : 96, h = has ? 110 : 162;
		return {16 + g % 2 * 312, y0 + g / 2 * (h + 8), 296, h};
	}

	std::string Ui::tileSub(int g) const
	{
		// made again when what they say changes: the playlists, the connections, the places
		int key = (plStore ? plStore->serial() : -1) * 7919 + (netStore ? netStore->version() : -1) * 31 + (int)roots.size();
		if (key != tileSubsKey)
		{
			tileSubsKey = key;
			auto join = [](const std::vector<std::string>& parts) {
				std::string out;
				for (auto& s : parts) out += (out.empty() ? "" : " \xC2\xB7 ") + s;
				return out;
			};
			// Files: the places, two cards or more counted ("Videos · 2 cards")
			int cards = 0;
			for (auto& r : roots)
				if (r.label.size() >= 4 && r.label.compare(r.label.size() - 4, 4, "card") == 0) cards++;
			std::vector<std::string> parts;
			bool counted = false;
			for (auto& r : roots)
			{
				bool card = r.label.size() >= 4 && r.label.compare(r.label.size() - 4, 4, "card") == 0;
				if (!card || cards < 2) parts.push_back(r.label);
				else if (!counted)
				{
					parts.push_back(std::to_string(cards) + " cards");
					counted = true;
				}
			}
			tileSubs[TAB_LOCAL] = parts.empty() ? "No folders" : join(parts);
			size_t n = plStore ? plStore->addTargets().size() : 0;	// the user's own (Liked is always there)
			tileSubs[TAB_PLAYLISTS] = !plStore ? "Not available" : n == 0 ? "Liked"
				: "Liked \xC2\xB7 " + std::to_string(n) + (n == 1 ? " playlist" : " playlists");
			// Network: the connections by name, the most recently used first
			std::vector<NetServer> list = netStore ? netStore->servers() : std::vector<NetServer>();
			std::stable_sort(list.begin(), list.end(), [](const NetServer& a, const NetServer& b) { return a.lastUsed > b.lastUsed; });
			parts.clear();
			for (auto& sv : list) parts.push_back(serverName(sv));
			tileSubs[TAB_NETWORK] = parts.empty() ? "No connections yet" : join(parts);
			tileSubs[TAB_EXT] = "Coming soon";
		}
		return g >= 0 && g < BROWSE_TABS ? tileSubs[g] : "";
	}

	void Ui::goHome(int zone, int idx, Player& p)
	{
		enterFolder(HOMEDIR, p);
		if (zone == HOME_GRID || recent.empty())
		{
			homeZone = HOME_GRID;
			homeGrid = zone == HOME_GRID ? std::clamp(idx, 0, BROWSE_TABS - 1) : 0;
			homeCol = homeGrid % 2;
			// back on Home from a section: the Recent strip from its start, newest first (left where it was scrolled
			// to, it showed old cards first and read as a different list). A refresh while on Home keeps its place
			homeStrip = 0;
			stripOff = stripTarget(0);
			stripT0 = 0;
			return;
		}
		homeZone = HOME_STRIP;
		setStrip(idx, false);
	}

	void Ui::homeOpen(Player& p)
	{
		if (homeZone == HOME_GRID) { openSection(homeGrid, p); return; }
		if (homeStrip < (int)recent.size()) { openFile(recent[homeStrip].path, p); return; }
		// All recent: the list, from its top
		enterFolder(RECENT, p);
	}

	bool Ui::homeKey(Button b, Player& p)
	{
		if (b == Button::B)
		{
			// over what plays: back to it; else twice leaves
			if (p.playing()) { page = Page::Main; return false; }
			if (quitArmed && SDL_GetTicks() - quitArmed < QUIT_WINDOW_MS) return true;
			quitArmed = SDL_GetTicks();
			return false;
		}
		if (b == Button::Start) { openMainMenu(p); return false; }
		if (b == Button::X) { openHomeOptions(); return false; }
		bool has = !recent.empty();
		if (homeZone == HOME_STRIP && has)
		{
			switch (b)
			{
			case Button::Left: if (homeStrip > 0) setStrip(homeStrip - 1); break;
			case Button::Right: if (homeStrip < stripCount() - 1) setStrip(homeStrip + 1); break;
			// down to the column left last
			case Button::Down: homeZone = HOME_GRID; homeGrid = homeCol; break;
			case Button::A: homeOpen(p); break;
			case Button::Y: if (homeStrip < (int)recent.size()) openRecentMenu(recent[homeStrip].path); break;
			default: break;
			}
			return false;
		}
		homeZone = HOME_GRID;
		int g = homeGrid;
		switch (b)
		{
		case Button::Left: if (g % 2) homeGrid--; break;
		case Button::Right: if (g % 2 == 0) homeGrid++; break;
		case Button::Down: if (g < 2) homeGrid += 2; break;
		// up from the top row: the strip, on the card left there
		case Button::Up: if (g >= 2) homeGrid -= 2; else if (has) homeZone = HOME_STRIP; break;
		case Button::A: homeOpen(p); break;
		default: break;
		}
		if (homeZone == HOME_GRID) homeCol = homeGrid % 2;
		return false;
	}

	bool Ui::homePointer(const Pointer& pt, int x, int y, Player& p)
	{
		bool has = !recent.empty();
		auto cardAt = [&](int px, int py) {
			if (!has || py < CARD_Y || py >= CARD_Y + CARD_H) return -1;
			int rel = px - 16 + (int)std::lround(stripOff);
			int i = rel >= 0 ? rel / STRIDE : -1;
			return i >= 0 && i < stripCount() && rel - i * STRIDE < CARD_W ? i : -1;
		};
		if (pt.kind == Pointer::Down)
		{
			homeTouch = true;
			homeMoved = false;
			homeDownX = x;
			homeDownOff = stripOff;
			homePress = -1;
			homeHoldAt = 0;
			if (has && y >= STRIP_TOP && y < STRIP_TOP + STRIP_H)
			{
				// on the strip: a card (a hold on a file's opens its menu); a drag anywhere along it scrolls
				homePress = cardAt(x, y);
				if (homePress >= 0 && homePress < (int)recent.size()) homeHoldAt = SDL_GetTicks();
				return false;
			}
			for (int g = 0; g < BROWSE_TABS; g++)
				if (tileRect(g).hit(x, y)) homePress = HOME_TILE + g;
			if (homePress < 0) homeTouch = false;
			return false;
		}
		if (!homeTouch) return false;
		bool onStrip = homePress < HOME_TILE;
		if (pt.kind == Pointer::Move)
		{
			if (!homeMoved && std::abs(x - homeDownX) > 8)
			{
				homeMoved = true;
				homeHoldAt = 0;
				if (onStrip) homePress = -1;
			}
			if (homeMoved && onStrip)
			{
				stripT0 = 0;
				stripOff = std::clamp(homeDownOff - (x - homeDownX), 0.0, (double)stripMax());
			}
			return false;
		}
		if (pt.kind != Pointer::Up) return false;
		homeTouch = false;
		homeHoldAt = 0;
		int id = homePress;
		homePress = -1;
		if (homeMoved && onStrip)
		{
			// the focus follows a swipe: onto the card now in the second slot
			homeZone = HOME_STRIP;
			setStrip((int)std::lround(stripOff / STRIDE) + (stripOff > 0 ? 1 : 0));
			return false;
		}
		if (id >= HOME_TILE)
		{
			if (!tileRect(id - HOME_TILE).hit(x, y)) return false;
			homeZone = HOME_GRID;
			homeGrid = id - HOME_TILE;
			homeCol = homeGrid % 2;
			homeOpen(p);
		}
		else if (id >= 0 && cardAt(x, y) == id)
		{
			homeZone = HOME_STRIP;
			setStrip(id);
			homeOpen(p);
		}
		return false;
	}

	void Ui::openFile(const std::string& path, Player& p)
	{
		(void)p;
		// a network file whose folder wasn't listed (Recent): listed first, a moment, for the files that play after it,
		// its subtitles and its cover (netListed opens it)
		NetPath np;
		const NetServer* sv = isNetPath(path) && parseNetPath(path, np) && netStore ? netStore->find(np.server) : nullptr;
		if (sv && netWorker && netNames.find(netParent(path)) == netNames.end())
		{
			netOpenPending = path;
			netWorker->list(++netSerial, *sv, netStore->password(*sv), netParent(path));
		}
		else openRequest = path;
	}

	void Ui::showInFolder(const std::string& path, Player& p)
	{
		// its folder, in Files or Network, the cursor on it (a network one's once the listing comes)
		if (isNetPath(path))
		{
			setSection(TAB_NETWORK);
			netSelect = path;
			enterFolder(netParent(path), p);
			return;
		}
		setSection(TAB_LOCAL);
		enterFolder(parentFolder(path), p);
		for (size_t i = 0; i < browsePaths.size(); i++)
			if (browsePaths[i] == path) cursor = (int)i;
		fitList();
	}

	void Ui::openRecentMenu(const std::string& path)
	{
		const RecentItem* it = nullptr;
		for (auto& x : recent)
			if (x.path == path) it = &x;
		if (!it) return;
		ctxRecent = path;
		std::vector<int> items = {CTX_R_PLAY};
		if (!it->at.empty()) items.push_back(CTX_R_START);
		items.push_back(CTX_R_SHOW);
		if (plStore && PlaylistStore::likable(path)) items.push_back(CTX_R_LIKE);
		if (plStore) items.push_back(CTX_R_ADD);
		items.push_back(CTX_R_REMOVE);
		ctxOpen = true;
		ctxCursor = 0;
		ctxItems = items;
		ctxServer.clear();
		ctxStatus.clear();
		ctxArmed = false;
		ctxPath.clear();
		ctxFolder.clear();
		ctxTitle = it->name;
		ctxNote.clear();
		ctxPlFile.clear();
		ctxPlItem = -1;
		quitArmed = 0;
	}

	void Ui::openHomeOptions()
	{
		ctxRecent.clear();
		ctxOpen = true;
		ctxCursor = 0;
		ctxItems = {CTX_CLEAR_RECENT, CTX_HIDDEN, CTX_SETTINGS};
		ctxServer.clear();
		ctxStatus.clear();
		ctxArmed = false;
		ctxPath.clear();
		ctxFolder.clear();
		ctxTitle = "Options";
		ctxNote.clear();
		ctxPlFile.clear();
		ctxPlItem = -1;
		quitArmed = 0;
	}

	void Ui::drawHome(const Player& p)
	{
		drawHeader("Banana Player", p);
		bool has = !recent.empty();
		// the focus shows with nothing over the page
		bool lit = !ctxOpen && !pickerOpen && !kbOpen && !popupShown(p);
		if (has)
		{
			drawTextMid("Recent", 0, DIM, 16, SUB_Y);
			// the strip: cards side by side, cut at the panel's edges
			SDL_Rect clip{0, (int)std::lround(STRIP_TOP * sy), (int)std::lround(W * sx), (int)std::lround(STRIP_H * sy)};
			SDL_RenderSetClipRect(ren, &clip);
			int off = (int)std::lround(stripOff);
			for (int i = 0; i < stripCount(); i++)
			{
				int x = 16 + i * STRIDE - off;
				if (x + CARD_W < 0 || x > W) continue;
				Rect r{x, CARD_Y, CARD_W, CARD_H};
				bool at = lit && homeZone == HOME_STRIP && homeStrip == i;
				drawBox(r, homePress == i ? PRESSED : at ? BUTTON : SURFACE, R_TILE, at);
				SDL_Color ink = at ? ACCENT : TEXT, soft = at ? DIM_ON : DIM;
				if (i < (int)recent.size())
				{
					// the name on up to two lines; under it, what kind, how far, and a network mark; a bar along the foot
					const RecentItem& it = recent[i];
					auto lines = wrapText(it.name, 1, CARD_W - 28, 2);
					for (size_t k = 0; k < lines.size(); k++)
						drawTextMid(lines[k], 1, ink, x + 14, CARD_Y + (lines.size() == 1 ? 34 : 24) + (int)k * 26, CARD_W - 28);
					drawGlyphMid(*it.kind, x + 25, CARD_Y + 78, soft);
					// a liked one: a small heart, left of the network mark
					bool liked = plStore && plStore->isLiked(it.path);
					drawTextMid(it.sub, 0, soft, x + 44, CARD_Y + 78, CARD_W - 58 - (it.net ? 30 : 0) - (liked ? 22 : 0));
					int mx = x + CARD_W - 22;
					if (it.net) { drawGlyphMid(G_NETWORK, mx - 3, CARD_Y + 78, soft); mx -= 30; }
					if (liked) drawGlyphMid(G_HEART_S, mx, CARD_Y + 78, ACCENT);
					// along the card's foot, in its corners: over its edge, or inside the cursor's yellow one
					int w = (int)std::lround(CARD_W * it.frac);
					Rect in = at ? Rect{x + theme::EDGE, CARD_Y + theme::EDGE, CARD_W - 2 * theme::EDGE, CARD_H - 2 * theme::EDGE} : r;
					if (w > 0)
						fillRoundBand(in, at ? R_TILE - theme::EDGE : R_TILE, CARD_Y + CARD_H - 4, in.y + in.h, it.at.empty() ? DIM : ACCENT, x + w);
				}
				else
				{
					// the last: the whole list
					drawGlyphMid(G_CLOCK, x + CARD_W / 2, CARD_Y + 30, at ? ACCENT : DIM, 3);
					drawTextMid("All recent", 1, ink, x + CARD_W / 2, CARD_Y + 66, CARD_W - 20, 2);
					drawTextMid(std::to_string(recent.size()) + " played", 0, soft, x + CARD_W / 2, CARD_Y + 88, CARD_W - 20, 2);
				}
			}
			SDL_RenderSetClipRect(ren, nullptr);
		}
		else
		{
			// nothing played yet: a line in the strip's place, the tiles taller
			drawGlyphMid(G_CLOCK, 30, SUB_Y + 4, DIM);
			drawTextMid(p.settings.keepHistory ? "Nothing played yet \xC2\xB7 what you play shows up here"
				: "Play history is off \xC2\xB7 Settings, Files", 0, DIM, 50, SUB_Y + 4, 570);
		}
		for (int g = 0; g < BROWSE_TABS; g++)
		{
			Rect t = tileRect(g);
			bool at = lit && homeZone == HOME_GRID && homeGrid == g, soon = g == TAB_EXT;
			drawBox(t, homePress == HOME_TILE + g ? PRESSED : at ? BUTTON : SURFACE, R_TILE, at);
			SDL_Color ink = at ? ACCENT : TEXT, soft = at ? DIM_ON : DIM, mark = at ? ACCENT : soon ? DIM : TEXT;
			const Glyph& gl = *SECTION_GLYPHS[g];
			if (has)
			{
				// side by side: the mark, then the name over what's in it
				drawGlyphMid(gl, t.x + 54, t.y + t.h / 2, mark, 4);
				drawTextMid(SECTION_NAMES[g], 2, ink, t.x + 104, t.y + t.h / 2 - 14, t.w - 114);
				drawTextMid(tileSub(g), 0, soft, t.x + 104, t.y + t.h / 2 + 20, t.w - 114);
			}
			else
			{
				// taller: stacked and centred
				drawGlyphMid(gl, t.x + t.w / 2, t.y + 52, mark, 5);
				drawTextMid(SECTION_NAMES[g], 2, ink, t.x + t.w / 2, t.y + 106, t.w - 20, 2);
				drawTextMid(tileSub(g), 0, soft, t.x + t.w / 2, t.y + 138, t.w - 20, 2);
			}
		}
		if (quitArmed) { drawFooterText("Press B again to quit", ACCENT); return; }
		if (homeZone == HOME_STRIP && has && homeStrip < (int)recent.size())
			drawHints({{"A", "Play"}, {"Y", "Menu"}, {"X", "Options"}, {"B", "Quit"}, {"START", "Menu"}});
		else if (homeZone == HOME_STRIP && has) drawHints({{"A", "Open"}, {"X", "Options"}, {"B", "Quit"}, {"START", "Menu"}});
		else drawHints({{"D-PAD", "Move"}, {"A", "Open"}, {"X", "Options"}, {"B", "Quit"}, {"START", "Menu"}});
	}

	Ui::Rect Ui::pipRect(int k, const Player& p) const
	{
		// as the mock-up has them (x 344..500, R at 508), moved left only when the battery, the web's globe and the
		// clock need it
		int left = statusLeft(p);
		int rw = (int)std::lround(textWidth("R", 0) / sx);
		int x0 = std::min(344, left - 8 - rw - 8 - 156);
		return {x0 + k * 40, 14, 36, 36};
	}

	void Ui::drawSectionHeader(const Player& p)
	{
		// "<", the section's name, its markers (L, Files Playlists Network Extensions, R: this one lit), the battery
		drawBackIcon();
		Rect first = pipRect(0, p);
		// (inside Liked: its name, as its line has no place to say it)
		bool liked = isPlDir(browseDir) && plIsLiked(plFileOf(browseDir));
		drawTextMid(liked ? PlaylistStore::LIKED_NAME : SECTION_NAMES[browseTab], 2, TEXT, 72, 32, std::max(40, first.x - 22 - 72));
		drawTextMid("L", 0, ACCENT, first.x - 10, 32, 0, 1);
		for (int k = 0; k < BROWSE_TABS; k++)
		{
			Rect r = pipRect(k, p);
			bool on = k == browseTab;
			drawBox(r, on ? ACCENT : SURFACE, R_BUTTON);
			drawGlyphMid(*SECTION_GLYPHS[k], r.x + r.w / 2, r.y + r.h / 2, on ? ON_ACCENT : DIM);
		}
		drawTextMid("R", 0, ACCENT, first.x + 164, 32);
		drawBattery(616, 24, p);
	}
}
