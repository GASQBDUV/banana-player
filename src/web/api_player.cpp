// The web dashboard's player routes: play (a file with its folder, a selection, a playlist; at a position, for the
// hand-off from a browser), the transport, volume, the equalizer and effects, "Play in this browser", and the
// snapshot of what plays. Every change runs on the main thread (WebServer::call) and uses Player's asynchronous
// calls only: nothing here waits for mpv.
#include "web/server.h"
#include "json.h"
#include "player.h"
#include "playlist.h"
#include "playlists.h"
#include "photo.h"
#include "substyle.h"
#include "tracks.h"
#include "ui.h"
#include "webstate.h"
#include <httplib.h>
#include <SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <dirent.h>
#include <random>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		// a form's values: "p[]" (the web app's arrays) first, else plain "p"
		std::vector<std::string> values(const httplib::Request& req, const std::string& name)
		{
			std::vector<std::string> out;
			std::string arr = name + "[]";
			size_t n = req.get_param_value_count(arr);
			for (size_t i = 0; i < n; i++) out.push_back(req.get_param_value(arr, i));
			if (out.empty())
			{
				n = req.get_param_value_count(name);
				for (size_t i = 0; i < n; i++) out.push_back(req.get_param_value(name, i));
			}
			return out;
		}

		std::string value(const httplib::Request& req, const std::string& name)
		{
			return req.has_param(name) ? req.get_param_value(name) : "";
		}

		// a page's browser-play / browser-pos older than its last one (seq 0: a page from before seq, always taken). Another
		// tab's word is taken: that page plays now
		bool staleBrowserWord(Ui& u, const std::string& tab, long long seq)
		{
			if (seq <= 0) return false;
			if (tab == u.browserTab && seq < u.browserSeq) return true;
			u.browserTab = tab;
			u.browserSeq = seq;
			return false;
		}

		std::string error(const std::string& message, const std::string& code = "")
		{
			JsonWriter j;
			j.obj().key("error").str(message);
			if (!code.empty()) j.key("code").str(code);
			return j.end().out();
		}

		bool isDir(const std::string& path)
		{
			struct stat st;
			return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
		}

		// a folder's videos and songs, in name order (as the handheld adds a folder to a playlist)
		std::vector<std::string> folderMedia(const std::string& dir)
		{
			std::vector<std::string> names;
			if (DIR* d = opendir(dir.c_str()))
			{
				while (dirent* e = readdir(d)) names.push_back(e->d_name);
				closedir(d);
			}
			int photos = 0;
			std::vector<std::string> out;
			for (auto& e : folderEntries(dir, names, photos))
				if (!isDir(e.path)) out.push_back(e.path);
			return out;
		}

		std::string baseOf(const std::string& p)
		{
			size_t s = p.rfind('/');
			return s == std::string::npos ? p : p.substr(s + 1);
		}

		// a playlist's id: its file's name less ".m3u"
		std::string playlistId(const std::string& file)
		{
			std::string b = baseOf(file);
			if (b.size() > 4 && b.compare(b.size() - 4, 4, ".m3u") == 0) b.resize(b.size() - 4);
			return b;
		}

		// the request's paths made real (on the server's thread: the checks touch the card); false: answered
		bool resolveAll(WebServer& ws, const std::vector<std::string>& in, std::vector<std::string>& out, httplib::Response& res)
		{
			for (auto& v : in)
			{
				std::string real;
				if (!ws.resolve(v, real, false, res)) return false;
				out.push_back(real);
			}
			return true;
		}

		void answer(httplib::Response& res, const std::string& body, int status)
		{
			WebServer::json(res, body, status);
		}
	}

	// Ui's insides the routes need (a friend of Ui's), all on the main thread
	struct WebPlayerGlue
	{
		// the source's queue as it plays: its files and where the one playing is among them
		static std::vector<std::string> queue(Player& p, Ui& u, int& index)
		{
			index = 0;
			if (!p.playing()) return {};
			const PlaySource& src = p.source();
			if (src.kind == SourceKind::Playlist && !src.id.empty())
			{
				Playlist pl;
				int skipped = 0;
				if (!u.plStore || !u.plStore->load(src.id, pl, skipped)) return {p.currentPath()};
				std::vector<std::string> out;
				if (u.plOrderId == src.id && (int)u.plOrder.size() == p.count())
				{
					for (int i : u.plOrder)
						if (i >= 0 && i < (int)pl.entries.size()) out.push_back(pl.entries[i].path);
				}
				else
					for (auto& e : pl.entries)
						if (!isPhotoFile(e.path)) out.push_back(e.path);
				index = p.index();
				if (index >= (int)out.size() || out[index] != p.currentPath())
				{
					auto it = std::find(out.begin(), out.end(), p.currentPath());
					index = it == out.end() ? 0 : (int)(it - out.begin());
				}
				return out;
			}
			if (src.kind == SourceKind::Playlist)
			{
				// a web selection (or a shuffled folder)
				auto it = std::find(u.webSelection.begin(), u.webSelection.end(), p.currentPath());
				if (it == u.webSelection.end()) return {p.currentPath()};
				index = (int)(it - u.webSelection.begin());
				return u.webSelection;
			}
			if (src.kind == SourceKind::Folder)
			{
				int i = 0;
				std::vector<std::string> out = siblingVideos(p.currentPath(), i);
				index = i;
				return out;
			}
			return {p.currentPath()};
		}

		static WebSnapshot snapshot(WebServer& ws, Player& p, Ui& u)
		{
			WebSnapshot s;
			if (p.playing())
			{
				std::string path = p.currentPath();
				s.path = ws.toVirtual(path);
				s.title = webTitle(path, p.title);
				s.artist = p.artist();
				s.album = p.album();
				s.kind = p.playingPhoto() ? "photo" : p.playingAudio() ? "audio" : "video";
				// the handheld's bottom screen: its player, or the browser with this playing behind it
				s.screen = u.playerShown(p) ? "player" : "browser";
				const PlaySource& src = p.source();
				s.sourceKind = src.kind == SourceKind::Playlist ? (src.id.empty() ? "selection" : "playlist") : "folder";
				s.sourceTitle = src.title;
				s.sourceId = src.kind == SourceKind::Playlist ? (src.id.empty() ? "" : playlistId(src.id)) : ws.toVirtual(src.id);
				s.liked = u.plStore && u.plStore->isLiked(path);
				s.index = p.index();
				s.count = p.count();
				s.loading = p.loading();
				s.paused = p.paused;
				s.position = std::max(0.0, p.position);
				s.duration = std::max(0.0, p.duration);
				s.speed = p.speed;
				s.shuffled = src.shuffled;
				// "Chapter 4", not mpv's "(unnamed)" (as the handheld's list says it)
				for (size_t i = 0; i < p.chapters.size(); i++) s.chapters.push_back({chapterLabel(p.chapters[i], (int)i + 1), p.chapters[i].time});
				s.chapter = p.chapters.empty() ? -1 : p.currentChapter();
				// a video's audio and subtitles, from the track list mpv already sent (nothing asked of it here)
				if (s.kind == "video")
				{
					s.hasTracks = true;
					auto add = [&](TrackType type, std::vector<WebTrack>& out, int64_t& chosen) {
						auto list = p.tracksOf(type);
						for (size_t i = 0; i < list.size(); i++)
						{
							const Track& t = list[i];
							out.push_back({t.id, trackLabel(t, (int)i + 1), t.selected, t.isDefault, t.forced, t.external});
							if (t.selected) chosen = t.id;
						}
					};
					add(TrackType::Audio, s.audioTracks, s.aid);
					add(TrackType::Sub, s.subTracks, s.sid);
				}
			}
			s.positionAtMs = SDL_GetTicks();
			s.volume = u.volumeLevel();
			s.eqOn = p.settings.eqOn;
			s.eqPreset = p.settings.eqPreset;
			for (int i = 0; i < EQ_BANDS; i++) s.eqGains[i] = p.settings.eqGains[i];
			s.fx = soundFxKey(p.settings.soundFx);
			s.loop = p.settings.loop == LOOP_ALL ? "all" : p.settings.loop == LOOP_ONE ? "one" : "no";
			s.browserPlaying = u.browserPlaying;
			s.clients = ws.clients();
			s.playlistSerial = u.playlistSerial();
			s.showHiddenFolders = p.settings.showHiddenFolders;
			return s;
		}

		// the snapshot as the page gets it, with the battery (for the desktop's sidebar)
		static std::string snapshotJson(const WebSnapshot& s, const Ui& u)
		{
			std::string j = toJson(s);
			if (!j.empty() && j.back() == '}') j.insert(j.size() - 1, ",\"battery\":" + std::to_string(u.battery.percent));
			return j;
		}

		// a position to go to once the file that opens (or is open) knows its length
		static void seekWhenOpen(Player& p, Ui& u, const std::string& path, double t)
		{
			if (t <= 0) return;
			if (p.currentPath() == path && p.loaded && p.duration > 0)
			{
				p.seekFraction(std::clamp(t / p.duration, 0.0, 1.0));
				return;
			}
			u.seekPath = path;
			u.seekTo = t;
			u.seekUntil = SDL_GetTicks() + 5000;
		}

		// one file with the files beside it, as the browser opens it (at t: the hand-off); already open: there, playing.
		// aid > 0: with that audio track (the browser's "Change it on the handheld")
		static void openFile(Player& p, Ui& u, const std::string& path, double t, int64_t aid = 0)
		{
			if (p.playing() && p.currentPath() == path)
			{
				if (t > 0) seekWhenOpen(p, u, path, t);
				if (aid > 0) chooseAudio(p, u, aid);
				if (p.paused) p.unpause();
				u.showPlayer();
				return;
			}
			u.flushPlaylist(p);
			int index = 0;
			std::vector<std::string> list = siblingVideos(path, index);
			p.nextAid = aid;
			p.open(list, index);
			u.showPlayer();
			if (t > 0) seekWhenOpen(p, u, path, t);
		}

		// the file open already: its track, as the handheld's list chooses it (one it doesn't have: left as it is)
		static void chooseAudio(Player& p, Ui& u, int64_t aid)
		{
			const Track* cur = p.selected(TrackType::Audio);
			if (!cur || cur->id != aid) u.chooseTrack(p, TrackType::Audio, aid);
		}

		static void openSelection(Player& p, Ui& u, std::vector<std::string> paths, const std::string& title, bool shuffle)
		{
			u.flushPlaylist(p);
			if (shuffle)
			{
				std::random_device rd;
				std::vector<int> order = shuffleOrder((int)paths.size(), -1, ((uint64_t)rd() << 32) ^ rd() ^ SDL_GetTicks());
				std::vector<std::string> out;
				for (int i : order) out.push_back(paths[i]);
				paths.swap(out);
			}
			PlaySource src;
			src.kind = SourceKind::Playlist;
			src.title = title;
			src.shuffled = shuffle;
			u.webSelection = paths;
			p.openPlaylist(paths, 0, src);
			u.showPlayer();
		}

		// a saved playlist from item index (shuffled: from a random one), at t if given
		static std::string playPlaylist(Player& p, Ui& u, const std::string& id, int index, bool shuffle, double t, int64_t aid = 0)
		{
			std::string file = u.plWebFile(id);
			if (file.empty())
			{
				WebCall::setStatus(404);
				return error("No such playlist");
			}
			u.plFlush(p);
			Playlist pl;
			int skipped = 0;
			if (!u.plStore->load(file, pl, skipped))
			{
				WebCall::setStatus(500);
				return error("The playlist couldn't be read");
			}
			if (pl.entries.empty())
			{
				WebCall::setStatus(409);
				return error(pl.name + " is empty");
			}
			index = std::clamp(index, 0, (int)pl.entries.size() - 1);
			// that item of it already playing (the hand-off back): only the position
			if (!shuffle && p.playing() && p.source().kind == SourceKind::Playlist && p.source().id == file &&
				p.currentPath() == pl.entries[index].path)
			{
				seekWhenOpen(p, u, p.currentPath(), t);
				if (aid > 0) chooseAudio(p, u, aid);
				if (p.paused) p.unpause();
				u.showPlayer();
				return "{\"ok\":true}";
			}
			// (main opens it as the loop comes round: Ui's play request, as the browser's own Play; the track waits for
			// that load)
			if (!shuffle) p.nextAid = aid;
			u.plPlay(pl, index, shuffle, p);
			u.showPlayer();
			if (t > 0 && !shuffle) seekWhenOpen(p, u, pl.entries[index].path, t);
			return "{\"ok\":true}";
		}

		// paused, not toggled: a page posts browser-play on every resume, and two in a row before mpv says it paused
		// would have played it again
		static void pauseHandheld(Player& p)
		{
			if (p.playing() && !p.playingPhoto()) p.pause();
		}

		static std::string eqJson(const Player& p)
		{
			JsonWriter j;
			j.obj().key("on").boolean(p.settings.eqOn).key("preset").str(p.settings.eqPreset).key("gains").arr();
			for (double g : p.settings.eqGains) j.num(g);
			return j.end().end().out();
		}

		static void setVolume(Player& p, Ui& u, int v)
		{
			// as the quick settings do it (BananaOS's script, on its thread), and its card over the top panel
			u.volumePct = v;
			if (u.volume.present()) u.volume.set(v);
			p.showLevel("Volume", v);
			u.levelMs = 1500;
		}

		static void eqGains(Player& p, Ui& u, const EqGains& g)
		{
			Settings s = p.settings;
			if (s.eqGains == g && s.eqOn) return;
			// the first edit of a preset: Custom, made from it (as a band moved on the handheld)
			if (!u.eqCustom(p))
			{
				s.eqOrigin = s.eqPreset;
				s.eqPreset.clear();
				u.stashOn = false;
			}
			s.eqOn = true;
			s.eqGains = g;
			// heard at once; written a second after the last change (tick)
			p.setSettings(s, false);
			u.eqWebDirtyAt = SDL_GetTicks();
		}

		static std::string subStyleJson(const Player& p)
		{
			JsonWriter j;
			j.obj().key("settings").arr();
			const auto& all = subStyleSettings();
			for (size_t k = 0; k < all.size(); k++)
			{
				const StyleSetting& st = all[k];
				j.obj().key("key").str(st.key).key("label").str(st.label).key("choices").arr();
				for (auto& c : st.choices) j.obj().key("id").str(c.id).key("label").str(c.label).end();
				int pick = p.settings.style.pick[k];
				j.end().key("value").str(pick >= 0 && pick < (int)st.choices.size() ? st.choices[pick].id : "").end();
			}
			j.end();
			const Track* sub = p.selected(TrackType::Sub);
			j.key("styled").boolean(sub && (sub->codec == "ass" || sub->codec == "ssa"));
			return j.end().out();
		}

		static bool stopIfPlaying(Player& p, Ui& u, const std::string& real)
		{
			if (!p.playing() || real.empty()) return false;
			const std::string& cur = p.currentPath();
			std::string dir = real.back() == '/' ? real : real + "/";
			if (cur != real && cur.compare(0, dir.size(), dir) != 0) return false;
			// as the web's Stop: main stops it as the loop comes round (back to the browser if the player showed)
			u.stopRequest = true;
			u.browserPlaying = false;
			return true;
		}

		static void routes(WebServer& ws);
	};

	WebSnapshot webSnapshot(WebServer& ws, Player& p, Ui& u) { return WebPlayerGlue::snapshot(ws, p, u); }
	std::string webSnapshotJson(const WebSnapshot& s, const Ui& u) { return WebPlayerGlue::snapshotJson(s, u); }

	// (a member of the glue: its handlers reach Ui's insides too)
	void WebPlayerGlue::routes(WebServer& ws)
	{
		httplib::Server& s = ws.http();
		WebServer* w = &ws;

		// play: p (a file with its folder, or a folder's media), p[] (several: a list of their own; a folder's media in
		// its place), playlist + index; shuffle; t (seconds: the hand-off from a browser)
		auto play = [w](const httplib::Request& req, httplib::Response& res) {
			double t = atof(value(req, "t").c_str());
			int64_t aid = std::max(0LL, atoll(value(req, "aid").c_str()));
			bool shuffle = value(req, "shuffle") == "1" || value(req, "shuffle") == "true";
			if (req.has_param("playlist"))
			{
				std::string id = value(req, "playlist");
				int index = atoi(value(req, "index").c_str());
				int status = 200;
				std::string body = w->call([=](Player& p, Ui& u) { return WebPlayerGlue::playPlaylist(p, u, id, index, shuffle, t, aid); }, 2000, status);
				answer(res, body, status);
				return;
			}
			std::vector<std::string> real;
			if (!resolveAll(*w, values(req, "p"), real, res)) return;
			if (real.empty())
			{
				WebServer::fail(res, 400, "Nothing to play");
				return;
			}
			// one file: it and the files beside it, as the handheld's browser opens it
			if (real.size() == 1 && !isDir(real[0]))
			{
				std::string path = real[0];
				if (!isMediaFile(path))
				{
					answer(res, error("That isn't something the player plays", "type"), 415);
					return;
				}
				int status = 200;
				std::string body = w->call([=](Player& p, Ui& u) {
					WebPlayerGlue::openFile(p, u, path, t, aid);
					return std::string("{\"ok\":true}");
				}, 2000, status);
				answer(res, body, status);
				return;
			}
			// a folder, or several: their media in order (a folder's own, not its folders'); the card read here
			std::vector<std::string> list;
			for (auto& r : real)
			{
				if (isDir(r))
					for (auto& m : folderMedia(r)) list.push_back(m);
				else if (isVideoFile(r) || isAudioFile(r)) list.push_back(r);
			}
			if (list.empty())
			{
				answer(res, error("Nothing to play there", "type"), 404);
				return;
			}
			// a single folder plays as the handheld plays a folder (its first file, the rest after); with shuffle, or
			// several, as a list of their own
			bool oneFolder = real.size() == 1 && !shuffle;
			std::string title = real.size() == 1 ? baseOf(real[0]) : "Selection";
			int status = 200;
			std::string body = w->call([=](Player& p, Ui& u) {
				if (oneFolder) WebPlayerGlue::openFile(p, u, list[0], 0);
				else WebPlayerGlue::openSelection(p, u, list, title, shuffle);
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		};
		s.Post("/api/play", play);
		// the hand-off to the handheld by its own name (to=handheld): /api/play with t
		s.Post("/api/handoff", play);

		// the transport
		s.Post("/api/cmd", [w](const httplib::Request& req, httplib::Response& res) {
			std::string c = value(req, "c");
			double t = atof(value(req, "t").c_str()), d = atof(value(req, "d").c_str());
			int dir = atoi(value(req, "dir").c_str()), i = atoi(value(req, "i").c_str());
			std::string v = value(req, "v");
			static const char* KNOWN[] = {"toggle", "pause", "play", "next", "prev", "stop", "seek", "seekrel", "chapter",
				"chapterto", "loop", "shuffle"};
			if (std::find_if(std::begin(KNOWN), std::end(KNOWN), [&](const char* k) { return c == k; }) == std::end(KNOWN))
			{
				WebServer::fail(res, 400, "Unknown command");
				return;
			}
			int status = 200;
			std::string body = w->call([=](Player& p, Ui& u) -> std::string {
				if (c == "loop")
				{
					int loop = v == "all" ? LOOP_ALL : v == "one" ? LOOP_ONE : v == "no" || v == "off" ? LOOP_OFF : -1;
					if (loop < 0)
					{
						WebCall::setStatus(400);
						return error("Loop is no, all or one");
					}
					Settings st = p.settings;
					st.loop = loop;
					p.setSettings(st);
					if (p.playingAudio()) p.osd(loopLabel(loop));
					return "{\"ok\":true}";
				}
				if (!p.playing())
				{
					WebCall::setStatus(409);
					return error("Nothing is playing on the handheld", "playing");
				}
				if (c == "toggle") p.togglePause();
				else if (c == "pause") { if (!p.paused) p.togglePause(); }
				else if (c == "play") { if (p.paused) p.unpause(); }
				else if (c == "next") { if (!p.next()) p.osd("This is the last one"); }
				else if (c == "prev") { if (!p.prev()) p.osd("This is the first one"); }
				else if (c == "stop") u.stopRequest = true;
				else if (c == "seek") { if (p.duration > 0) p.seekFraction(std::clamp(t / p.duration, 0.0, 1.0)); }
				else if (c == "seekrel") p.seekRelative(d);
				else if (c == "chapter") p.stepChapter(dir < 0 ? -1 : 1);
				else if (c == "chapterto")
				{
					if (i < 0 || i >= (int)p.chapters.size())
					{
						WebCall::setStatus(400);
						return error("No such chapter");
					}
					p.seekChapter(i);
					p.showChapterCard(i);
				}
				else if (c == "shuffle")
				{
					// the source again in a new order, from its first
					const PlaySource& src = p.source();
					if (src.kind == SourceKind::Playlist && !src.id.empty())
						return WebPlayerGlue::playPlaylist(p, u, playlistId(src.id), 0, true, 0);
					int at = 0;
					std::vector<std::string> q = WebPlayerGlue::queue(p, u, at);
					std::string title = src.title;
					WebPlayerGlue::openSelection(p, u, q, title, true);
				}
				return "{\"ok\":true}";
			}, 2000, status);
			answer(res, body, status);
		});

		// the source's queue: Up next, and "Continue here" in a browser
		s.Get("/api/source", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([w](Player& p, Ui& u) {
				int index = 0;
				std::vector<std::string> q = WebPlayerGlue::queue(p, u, index);
				JsonWriter j;
				j.obj().key("items").arr();
				// the one playing by its tag; the others (their tags unread: the card isn't probed for a queue) by name
				std::string cur = p.playing() ? p.currentPath() : "";
				for (auto& path : q)
					j.obj().key("p").str(w->toVirtual(path)).key("title").str(path == cur ? webTitle(path, p.title) : webTitle(path)).end();
				j.end().key("index").num(index);
				return j.end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// key=showHiddenFolders value=0|1: the handheld's Settings toggle (saved to settings.conf)
		s.Post("/api/setting", [w](const httplib::Request& req, httplib::Response& res) {
			std::string key = value(req, "key"), v = value(req, "value");
			if (key != "showHiddenFolders")
			{
				WebServer::fail(res, 400, "Unknown setting");
				return;
			}
			if (v != "0" && v != "1")
			{
				WebServer::fail(res, 400, "value is 0 or 1");
				return;
			}
			bool on = v == "1";
			int status = 200;
			std::string body = w->call([on](Player& p, Ui& u) {
				if (p.settings.showHiddenFolders != on)
				{
					Settings st = p.settings;
					st.showHiddenFolders = on;
					p.setSettings(st);
					// the browser re-lists where it stands (a hidden folder in or out)
					u.folderChanged(u.browseFolder(), p);
				}
				return std::string("{\"showHiddenFolders\":") + (on ? "true" : "false") + "}";
			}, 2000, status);
			answer(res, body, status);
		});

		s.Post("/api/volume", [w](const httplib::Request& req, httplib::Response& res) {
			if (!req.has_param("v"))
			{
				WebServer::fail(res, 400, "No volume");
				return;
			}
			int v = std::clamp(atoi(value(req, "v").c_str()), 0, 100);
			int status = 200;
			std::string body = w->call([v](Player& p, Ui& u) {
				WebPlayerGlue::setVolume(p, u, v);
				return "{\"volume\":" + std::to_string(v) + "}";
			}, 2000, status);
			answer(res, body, status);
		});

		// the presets: the built-ins, then yours
		s.Get("/api/eq", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([](Player& p, Ui&) {
				JsonWriter j;
				j.obj().key("presets").arr();
				for (auto& e : p.eqPresets())
				{
					j.obj().key("name").str(e.name).key("gains").arr();
					for (double g : e.gains) j.num(g);
					j.end().key("builtin").boolean(e.builtin).end();
				}
				return j.end().end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// on=0|1, preset=<name> ("" or Custom: Custom back), gains=<ten dB values>
		s.Post("/api/eq", [w](const httplib::Request& req, httplib::Response& res) {
			bool hasOn = req.has_param("on"), hasPreset = req.has_param("preset"), hasGains = req.has_param("gains");
			bool on = value(req, "on") == "1" || value(req, "on") == "true";
			std::string preset = value(req, "preset");
			EqGains g{};
			if (hasGains && !parseEqGains(value(req, "gains"), g))
			{
				WebServer::fail(res, 400, "The equalizer needs ten values");
				return;
			}
			int status = 200;
			std::string body = w->call([=](Player& p, Ui& u) {
				if (hasPreset)
				{
					std::string name = preset.empty() ? "Custom" : preset;
					bool known = name == "Custom";
					for (auto& e : p.eqPresets()) known = known || e.name == name;
					if (!known)
					{
						WebCall::setStatus(404);
						return error("No preset called " + preset, "name");
					}
					u.eqPick(name, p);
				}
				if (hasGains) WebPlayerGlue::eqGains(p, u, g);
				if (hasOn && on != p.settings.eqOn) u.eqToggle(p);
				return WebPlayerGlue::eqJson(p);
			}, 2000, status);
			answer(res, body, status);
		});

		s.Post("/api/fx", [w](const httplib::Request& req, httplib::Response& res) {
			std::string key = value(req, "key");
			SoundFx fx = soundFxFromKey(key);
			if (key != soundFxKey(fx))
			{
				WebServer::fail(res, 400, "No such effect");
				return;
			}
			int status = 200;
			std::string body = w->call([fx](Player& p, Ui& u) {
				u.fxChoose((int)fx, p);
				return "{\"fx\":\"" + std::string(soundFxKey(fx)) + "\"}";
			}, 2000, status);
			answer(res, body, status);
		});

		// a video's audio track or subtitles on the handheld: kind=audio|sub, id (mpv's, as the snapshot's tracks have
		// it; sub 0: off). As its Audio & Subtitles page chooses (the language remembered, the top panel says it)
		s.Post("/api/track", [w](const httplib::Request& req, httplib::Response& res) {
			std::string kind = value(req, "kind");
			if ((kind != "audio" && kind != "sub") || !req.has_param("id"))
			{
				WebServer::fail(res, 400, "kind is audio or sub, with an id");
				return;
			}
			int64_t id = atoll(value(req, "id").c_str());
			TrackType type = kind == "audio" ? TrackType::Audio : TrackType::Sub;
			int status = 200;
			std::string body = w->call([=](Player& p, Ui& u) -> std::string {
				if (!p.playing() || p.playingAudio() || p.playingPhoto())
				{
					WebCall::setStatus(409);
					return error("No video is playing on the handheld", "playing");
				}
				if (!u.chooseTrack(p, type, id))
				{
					WebCall::setStatus(404);
					return error(type == TrackType::Audio ? "No such audio track" : "No such subtitles", "track");
				}
				return JsonWriter().obj().key("ok").boolean(true).key("kind").str(kind).key("id").num(id).end().out();
			}, 2000, status);
			answer(res, body, status);
		});

		// how plain subtitles look (Settings › Subtitles on the handheld): the settings with their choices and what's
		// chosen; styled: the subtitles showing are ASS/SSA, which keep their own look
		s.Get("/api/substyle", [w](const httplib::Request&, httplib::Response& res) {
			int status = 200;
			std::string body = w->call([](Player& p, Ui&) { return WebPlayerGlue::subStyleJson(p); }, 2000, status);
			answer(res, body, status);
		});

		// key (font, size, color, border, border-color) = value (a choice's id): kept in the settings and on screen at
		// once, as the handheld's own setting
		s.Post("/api/substyle", [w](const httplib::Request& req, httplib::Response& res) {
			std::string key = value(req, "key"), v = value(req, "value");
			const auto& all = subStyleSettings();
			int k = -1, c = -1;
			for (size_t i = 0; i < all.size() && k < 0; i++)
				if (key == all[i].key)
				{
					k = (int)i;
					for (size_t n = 0; n < all[i].choices.size() && c < 0; n++)
						if (v == all[i].choices[n].id) c = (int)n;
				}
			if (k < 0 || c < 0)
			{
				WebServer::fail(res, 400, k < 0 ? "No such subtitle setting" : "No such choice");
				return;
			}
			int status = 200;
			std::string body = w->call([k, c](Player& p, Ui&) {
				if (p.settings.style.pick[k] != c)
				{
					Settings st = p.settings;
					st.style.pick[k] = c;
					p.setSettings(st);
				}
				return WebPlayerGlue::subStyleJson(p);
			}, 2000, status);
			answer(res, body, status);
		});

		// a page plays a file itself: the handheld paused, and told so (browserPlaying) till it plays again; stop=1: the
		// page stopped
		// a page playing a file says where it is (play, pause, seek, and every 10 s while it plays): play pressed on the
		// handheld picks up there
		s.Post("/api/browser-pos", [w](const httplib::Request& req, httplib::Response& res) {
			std::string real;
			if (!w->resolve(value(req, "p"), real, false, res)) return;
			double t = atof(value(req, "t").c_str());
			bool playing = value(req, "playing") == "1";
			std::string tab = value(req, "tab");
			long long seq = atoll(value(req, "seq").c_str());
			int status = 200;
			std::string body = w->call([real, t, playing, tab, seq](Player& p, Ui& u) {
				// the handheld's paused copy isn't moved (a seek decodes a frame): its bar runs on from here (Ui::shownPosition)
				(void)p;
				// late (a newer word from that page came first), or about a file the page has left
				if (staleBrowserWord(u, tab, seq) || (!u.browserFile.empty() && real != u.browserFile))
					return std::string("{\"ok\":true,\"stale\":true}");
				u.browserPos = {real, t, SDL_GetTicks(), playing};
				// what A asked of it, done: its word counts again (till then, an older word can't undo the press)
				if (u.browserWant == (int)playing) u.browserWant = -1;
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});

		s.Post("/api/browser-play", [w](const httplib::Request& req, httplib::Response& res) {
			bool stop = value(req, "stop") == "1";
			// the page's file, opened on the handheld paused (when it isn't the one there): its screens show what plays,
			// and A plays that here
			std::string real;
			std::string vp = value(req, "p");
			if (!stop && !vp.empty() && !w->resolve(vp, real, false, res)) return;
			std::string tab = value(req, "tab");
			long long seq = atoll(value(req, "seq").c_str());
			int status = 200;
			std::string body = w->call([stop, real, tab, seq](Player& p, Ui& u) {
				// a page skipping fast sends one of these per file: one overtaken by the next must not open its older file
				if (staleBrowserWord(u, tab, seq))
				{
					fprintf(stderr, "web: browser-play dropped (late, seq %lld < %lld): %s\n", seq, u.browserSeq, real.substr(real.rfind('/') + 1).c_str());
					return std::string("{\"ok\":true,\"stale\":true}");
				}
				if (stop)
				{
					// the page stopped: the handheld's paused copy of its file was only a mirror, unloaded too (main stops
					// it; the screens change only if the player showed)
					bool held = u.browserPlaying && p.playing() && p.paused && !u.browserFile.empty() && p.currentPath() == u.browserFile;
					u.browserPlaying = false;
					u.browserFile.clear();
					if (held) u.stopRequest = true;
				}
				else
				{
					if (!real.empty()) u.browserFile = real;
					if (!real.empty() && isMediaFile(real) && !isPhotoFile(real) && !(p.playing() && p.currentPath() == real))
					{
						u.flushPlaylist(p);
						int index = 0;
						std::vector<std::string> list = siblingVideos(real, index);
						p.open(list, index);
						u.showPlayer();
						p.holdPaused();	// queued after the open: it never sounds
					}
					else WebPlayerGlue::pauseHandheld(p);
					u.browserPlaying = true;
					u.browserPlayAt = SDL_GetTicks();
					u.browserSawPause = false;
				}
				return std::string("{\"ok\":true}");
			}, 2000, status);
			answer(res, body, status);
		});
	}

	void registerPlayerRoutes(WebServer& ws) { WebPlayerGlue::routes(ws); }

	bool stopIfPlaying(WebServer& ws, const std::string& real)
	{
		int status = 200;
		std::string body = ws.call([real](Player& p, Ui& u) {
			return std::string(WebPlayerGlue::stopIfPlaying(p, u, real) ? "{\"stopped\":true}" : "{\"stopped\":false}");
		}, 2000, status);
		return status == 200 && body.find("true") != std::string::npos;
	}
}
