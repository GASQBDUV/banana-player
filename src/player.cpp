#include "player.h"
#include "format.h"
#include "logo.h"
#include "playlist.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace bp
{
	enum Observed : uint64_t { TIME_POS = 1, DURATION, PAUSE, TRACK_LIST, MEDIA_TITLE, AID, SID, SPEED };

	static std::string readFile(const std::string& path)
	{
		std::ifstream in(path);
		std::stringstream text;
		text << in.rdbuf();
		return text.str();
	}

	bool Player::init(const PlayerOptions& o, void (*wakeup)(void*), void* ctx)
	{
		opts = o;
		mpv = mpv_create();
		if (!mpv) return false;
		auto opt = [&](const char* k, const char* v) { mpv_set_option_string(mpv, k, v); };
		opt("config", "no");
		opt("vo", "libmpv");
		// Software decoding on four Cortex-A55s (BananaOS's kernel has no V4L2: no hardware decoders to ask), so
		// everything cheap that keeps the picture right: mpv's fast profile (bilinear scaling, no dithering nor
		// high-quality downscaling: the panels are small), FFmpeg's fast decoding, the loop filter left out on
		// frames no other frame is built from (a flaw lasts one frame), and frames dropped in the decoder too when
		// it falls behind, so the sound and the picture keep together
		opt("hwdec", "no");
		opt("profile", "fast");
		opt("vd-lavc-threads", "0");		// one per core
		opt("vd-lavc-fast", "yes");
		opt("vd-lavc-skiploopfilter", "nonref");
		opt("framedrop", "decoder+vo");
		opt("video-sync", "audio");
		opt("audio-client-name", "banana-player");
		opt("input-default-bindings", "no");
		opt("input-vo-keyboard", "no");
		opt("osc", "no");
		opt("sub-auto", "fuzzy");			// movie.srt, movie.en.srt... next to movie.mkv
		// the style from settings.conf is for plain subtitles; ASS/SSA keep their own (only sub-scale reaches
		// them, and we leave it)
		opt("sub-ass-override", "scale");
		if (!o.fontsDir.empty())
		{
			// the bundled fonts for the subtitles and mpv's own messages: BananaOS has no fontconfig, so "sans-serif"
			// would find nothing there
			opt("sub-fonts-dir", o.fontsDir.c_str());
			opt("osd-fonts-dir", o.fontsDir.c_str());
			opt("osd-font", "Inter");
		}
		opt("osd-font-size", "40");
		opt("idle", "yes");				// stay up between files and after the last: we decide when to leave
		opt("keep-open", "no");
		opt("resume-playback", "no");		// resume.tsv instead: each of position, audio and subtitles can be off
		mpv_request_log_messages(mpv, o.verbose ? "v" : "warn");
		if (mpv_initialize(mpv) < 0) return false;

		if (!o.stateDir.empty())
		{
			// and the folders above it (/data/home/.config on a card that never ran a Store app)
			for (size_t i = 1; i <= o.stateDir.size(); i++)
				if (i == o.stateDir.size() || o.stateDir[i] == '/') mkdir(o.stateDir.substr(0, i).c_str(), 0755);
			settings = parseSettings(readFile(o.stateDir + "/settings.conf"));
			resume.parse(readFile(o.stateDir + "/resume.tsv"));
			if (settings.keepHistory) played.parse(readFile(o.stateDir + "/history.tsv"));
		}

		mpv_observe_property(mpv, TIME_POS, "time-pos", MPV_FORMAT_DOUBLE);
		mpv_observe_property(mpv, DURATION, "duration", MPV_FORMAT_DOUBLE);
		mpv_observe_property(mpv, PAUSE, "pause", MPV_FORMAT_FLAG);
		mpv_observe_property(mpv, TRACK_LIST, "track-list", MPV_FORMAT_NONE);
		mpv_observe_property(mpv, MEDIA_TITLE, "media-title", MPV_FORMAT_STRING);
		mpv_observe_property(mpv, AID, "aid", MPV_FORMAT_NONE);
		mpv_observe_property(mpv, SID, "sid", MPV_FORMAT_NONE);
		mpv_observe_property(mpv, SPEED, "speed", MPV_FORMAT_DOUBLE);
		mpv_set_wakeup_callback(mpv, wakeup, ctx);
		setAspect(aspect);
		for (auto& p : subStyleProperties(settings.style)) prop(p.first.c_str(), p.second);
		return true;
	}

	bool Player::initRender(void* (*getProc)(void*, const char*), void* procCtx, void (*update)(void*), void* ctx)
	{
		mpv_opengl_init_params gl{getProc, procCtx};
		mpv_render_param params[] = {
			{MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
			{MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl},
			{MPV_RENDER_PARAM_INVALID, nullptr},
		};
		if (mpv_render_context_create(&rc, mpv, params) < 0) return false;
		mpv_render_context_set_update_callback(rc, update, ctx);
		// an audio file has no picture of its own: the window stays, for its cover, the visualizer and the title.
		// Set once the render context is there: mpv opens its window at once, and it needs the context
		prop("force-window", "yes");
		return true;
	}

	void Player::shutdown()
	{
		// the render context goes first, while its GL context is still current
		if (rc) mpv_render_context_free(rc);
		rc = nullptr;
		if (mpv) mpv_terminate_destroy(mpv);
		mpv = nullptr;
	}

	void Player::prop(const char* name, const std::string& value)
	{
		mpv_set_property_string(mpv, name, value.c_str());
	}

	void Player::open(const std::vector<std::string>& list, int index)
	{
		playlist = list;
		current = index;
		// a video picked plays, whatever the one before was left at
		prop("pause", "no");
		load();
	}

	bool Player::next()
	{
		if (current + 1 >= count()) return false;
		saveResume();
		current++;
		load();
		return true;
	}

	bool Player::prev()
	{
		if (current <= 0) return false;
		saveResume();
		current--;
		load();
		return true;
	}

	void Player::load()
	{
		const std::string& path = playlist[current];
		loaded = finished = false;
		error.clear();
		title.clear();
		tracks.clear();
		position = duration = -1;
		seenAid = seenSid = -1;

		showIdle(false);
		// the last file's visualizer off first: with a filter graph on them, mpv's tracks can't be chosen (aid, sid
		// below)
		prop("lavfi-complex", "");

		// what this file is remembered with; mpv's options are global, so each one is set for every file
		const ResumeEntry* e = resume.find(path);
		char start[32] = "none";
		if (settings.rememberPosition && e && e->position >= 0) snprintf(start, sizeof start, "%.3f", e->position);
		prop("start", start);

		wantAid = settings.rememberAudio && e && e->aid > 0 ? e->aid : 0;
		prop("aid", wantAid ? std::to_string(wantAid) : "auto");
		std::string alang = opts.lang;
		if (settings.rememberAudio && !resume.lastAudioLang.empty()) alang = resume.lastAudioLang + (alang.empty() ? "" : "," + alang);
		prop("alang", alang);

		wantSid = 0;
		std::string sid = "auto";
		if (settings.rememberSubs && e && e->sid >= 0)
		{
			wantSid = e->sid;
			sid = e->sid ? std::to_string(e->sid) : "no";
		}
		else if (settings.rememberSubs && !e && resume.lastSubOff) sid = "no";
		prop("sid", sid);
		std::string slang = opts.lang;
		if (settings.rememberSubs && !resume.lastSubLang.empty()) slang = resume.lastSubLang + (slang.empty() ? "" : "," + slang);
		prop("slang", slang);

		// a visualizer is set before the file opens (Auto's waits to see if there's a cover); a video has none
		isAudio = isAudioFile(path);
		// (Auto and Cover + bars wait for the file: they depend on its cover)
		bool vis = isAudio && settings.audioDisplay != AUDIO_AUTO && settings.audioDisplay != AUDIO_COVER_BARS;
		if (vis) prop("lavfi-complex", audioVisualizerGraph(settings.audioDisplay));

		if (settings.keepHistory)
		{
			played.add(path);
			if (!opts.stateDir.empty()) writeFile(opts.stateDir + "/history.tsv", played.serialize());
		}

		const char* cmd[] = {"loadfile", path.c_str(), "replace", nullptr};
		mpv_command(mpv, cmd);
	}

	void Player::pollEvents()
	{
		while (mpv)
		{
			mpv_event* ev = mpv_wait_event(mpv, 0);
			if (ev->event_id == MPV_EVENT_NONE) break;
			switch (ev->event_id)
			{
			case MPV_EVENT_PROPERTY_CHANGE:
			{
				auto* p = (mpv_event_property*)ev->data;
				switch (ev->reply_userdata)
				{
				case TIME_POS: position = p->format == MPV_FORMAT_DOUBLE ? *(double*)p->data : -1; break;
				case DURATION: duration = p->format == MPV_FORMAT_DOUBLE ? *(double*)p->data : -1; break;
				case PAUSE: paused = p->format == MPV_FORMAT_FLAG && *(int*)p->data; break;
				case SPEED: if (p->format == MPV_FORMAT_DOUBLE) speed = *(double*)p->data; break;
				case MEDIA_TITLE: title = p->format == MPV_FORMAT_STRING ? *(char**)p->data : ""; break;
				case TRACK_LIST: case AID: case SID: refreshTracks(); break;
				}
				break;
			}
			case MPV_EVENT_FILE_LOADED:
			{
				loaded = true;
				audioChecks = 0;
				audioCheckAt = ticks() + 1500;
				refreshTracks();
				// a remembered track that isn't there any more (a subtitle file taken away): mpv's own choice instead
				auto missing = [&](TrackType type, int64_t id) {
					for (auto& t : tracks)
						if (t.type == type && t.id == id) return false;
					return true;
				};
				if (wantAid > 0 && missing(TrackType::Audio, wantAid)) prop("aid", "auto");
				if (wantSid > 0 && missing(TrackType::Sub, wantSid)) prop("sid", "auto");
				if (isAudio && (settings.audioDisplay == AUDIO_AUTO || settings.audioDisplay == AUDIO_COVER_BARS)) applyAudioDisplay();
				updateOverlay();
				break;
			}
			case MPV_EVENT_END_FILE:
			{
				auto* e = (mpv_event_end_file*)ev->data;
				if (e->reason == MPV_END_FILE_REASON_ERROR) error = mpv_error_string(e->error);
				else if (e->reason == MPV_END_FILE_REASON_EOF) finished = true;
				break;
			}
			case MPV_EVENT_SHUTDOWN:
				quit = true;
				break;
			case MPV_EVENT_LOG_MESSAGE:
			{
				auto* m = (mpv_event_log_message*)ev->data;
				fprintf(stderr, "[%s] %s: %s", m->level, m->prefix, m->text);
				break;
			}
			default: break;
			}
		}
	}

	void Player::refreshTracks()
	{
		mpv_node node;
		if (mpv_get_property(mpv, "track-list", MPV_FORMAT_NODE, &node) < 0) return;
		tracks = parseTrackList(&node);
		mpv_free_node_contents(&node);
		if (loaded && !tracks.empty())
		{
			const Track* a = selected(TrackType::Audio);
			const Track* s = selected(TrackType::Sub);
			seenAid = a ? a->id : -1;
			seenSid = s ? s->id : 0;
		}
	}

	bool Player::frameDue()
	{
		return rc && (mpv_render_context_update(rc) & MPV_RENDER_UPDATE_FRAME);
	}

	void Player::render(int w, int h)
	{
		mpv_opengl_fbo fbo{0, w, h, 0};
		int flip = 1;
		mpv_render_param params[] = {
			{MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
			{MPV_RENDER_PARAM_FLIP_Y, &flip},
			{MPV_RENDER_PARAM_INVALID, nullptr},
		};
		mpv_render_context_render(rc, params);
	}

	void Player::reportSwap()
	{
		if (rc) mpv_render_context_report_swap(rc);
	}

	uint64_t Player::ticks()
	{
		return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	void Player::checkAudio()
	{
		// a file with sound that came out silent: the sound device wasn't free when mpv opened it (and mpv doesn't
		// try again by itself). Opened again, three times at most, each in the log
		if (!loaded || !audioCheckAt || ticks() < audioCheckAt) return;
		audioCheckAt = 0;
		if (!selected(TrackType::Audio) || !get("current-ao").empty()) return;
		if (audioChecks >= 3)
		{
			fprintf(stderr, "banana-player: still no sound output after %d tries\n", audioChecks);
			return;
		}
		audioChecks++;
		fprintf(stderr, "banana-player: no sound output; opening the sound device again (try %d)\n", audioChecks);
		const char* cmd[] = {"ao-reload", nullptr};
		mpv_command(mpv, cmd);
		audioCheckAt = ticks() + 1500;
	}

	void Player::togglePause()
	{
		const char* cmd[] = {"cycle", "pause", nullptr};
		mpv_command(mpv, cmd);
	}

	void Player::pauseNow()
	{
		// libmpv's calls are thread-safe; the pause property's change reaches pollEvents as any other
		int yes = 1;
		if (mpv) mpv_set_property(mpv, "pause", MPV_FORMAT_FLAG, &yes);
	}

	void Player::seekRelative(double seconds)
	{
		std::string s = std::to_string(seconds);
		const char* cmd[] = {"seek", s.c_str(), "relative", nullptr};
		mpv_command(mpv, cmd);
	}

	void Player::scrub(double fraction)
	{
		std::string s = std::to_string(fraction * 100.0);
		const char* cmd[] = {"seek", s.c_str(), "absolute-percent+keyframes", nullptr};
		mpv_command(mpv, cmd);
	}

	void Player::seekFraction(double fraction)
	{
		std::string s = std::to_string(fraction * 100.0);
		const char* cmd[] = {"seek", s.c_str(), "absolute-percent+exact", nullptr};
		mpv_command(mpv, cmd);
	}

	void Player::setSpeed(double s)
	{
		mpv_set_property(mpv, "speed", MPV_FORMAT_DOUBLE, &s);
	}

	void Player::selectTrack(TrackType type, int64_t id)
	{
		const char* name = type == TrackType::Audio ? "aid" : type == TrackType::Sub ? "sid" : "vid";
		prop(name, id > 0 ? std::to_string(id) : "no");
		std::string lang;
		for (auto& t : tracks)
			if (t.type == type && t.id == id) lang = t.lang;
		if (type == TrackType::Audio && settings.rememberAudio && !lang.empty()) resume.lastAudioLang = lang;
		if (type == TrackType::Sub && settings.rememberSubs)
		{
			resume.lastSubOff = id <= 0;
			if (id > 0 && !lang.empty()) resume.lastSubLang = lang;
		}
	}

	void Player::setAspect(AspectMode m)
	{
		aspect = m;
		for (auto& p : aspectProperties(m)) prop(p.first, p.second);
	}

	void Player::writeFile(const std::string& path, const std::string& text)
	{
		// written beside and renamed over: a power cut never leaves half a file
		std::string tmp = path + ".tmp";
		{
			std::ofstream out(tmp, std::ios::trunc);
			out << text;
			if (!out.flush()) return;
		}
		rename(tmp.c_str(), path.c_str());
	}

	void Player::setSettings(const Settings& s)
	{
		for (auto& p : subStyleProperties(s.style)) prop(p.first.c_str(), p.second);
		bool displayChanged = s.audioDisplay != settings.audioDisplay;
		settings = s;
		if (displayChanged && isAudio && loaded)
		{
			applyAudioDisplay();
			updateOverlay();
		}
		// switched off: nothing kept from now, and what was kept goes
		if (!s.keepHistory) clearHistory();
		// switched off: the folder kept so far goes too
		if (!s.rememberFolder && !resume.lastFolder.empty()) setLastFolder("");
		if (!opts.stateDir.empty()) writeFile(opts.stateDir + "/settings.conf", serializeSettings(s));
	}

	void Player::showLevel(const std::string& label, int percent)
	{
		// an ASS card in the middle of a 960x720 canvas (the panel's 4:3; the title is at the top, subtitles at the
		// bottom): a dark box, the label and percent, a bar in yellow on teal.
		// ASS colours are &HBBGGRR&
		percent = std::max(0, std::min(100, percent));
		int fill = 260 * percent / 100;
		char ev[1024];
		snprintf(ev, sizeof ev,
			"{\\an7\\pos(330,318)\\bord0\\shad0\\1c&H1C1617&\\1a&H20&\\p1}m 0 0 l 300 0 300 84 0 84{\\p0}\n"
			"{\\an7\\pos(350,328)\\bord0\\shad0\\fs30\\1c&HE2EDF1&}%s  %d%%\n"
			"{\\an7\\pos(350,372)\\bord0\\shad0\\1c&H464323&\\p1}m 0 0 l 260 0 260 12 0 12{\\p0}\n"
			"{\\an7\\pos(350,372)\\bord0\\shad0\\1c&H29B4F0&\\p1}m 0 0 l %d 0 %d 12 0 12{\\p0}",
			label.c_str(), percent, std::max(fill, 1), std::max(fill, 1));
		const char* cmd[] = {"osd-overlay", "2", "ass-events", ev, "960", "720", "10", "no", "no", nullptr};
		mpv_command(mpv, cmd);
	}

	void Player::showIdle(bool on)
	{
		if (!on)
		{
			const char* cmd[] = {"osd-overlay", "3", "none", "", nullptr};
			mpv_command(mpv, cmd);
			return;
		}
		// the icon as ASS drawings, a run of pixels a rectangle, one event a colour; on a 960x720 canvas
		const int px = 6, x0 = 480 - 16 * px, y0 = 110;
		std::string ev;
		const struct { char c; const char* colour; } inks[] = {{'.', "&H181314&"}, {'Y', "&H1CC2F8&"}, {'W', "&HEAF4F7&"}};
		for (auto& ink : inks)
		{
			std::string path;
			for (int y = 0; y < 32; y++)
				for (int x = 0; x < 32;)
				{
					if (LOGO[y][x] != ink.c) { x++; continue; }
					int e = x;
					while (e < 32 && LOGO[y][e] == ink.c) e++;
					char r[96];
					snprintf(r, sizeof r, "m %d %d l %d %d %d %d %d %d ", x * px, y * px, e * px, y * px, e * px, (y + 1) * px, x * px, (y + 1) * px);
					path += r;
					x = e;
				}
			char head[128];
			snprintf(head, sizeof head, "{\\an7\\pos(%d,%d)\\bord0\\shad0\\1c%s\\p1}", x0, y0, ink.colour);
			ev += head + path + "{\\p0}\n";
		}
		ev += "{\\an8\\pos(480,330)\\bord0\\shad0\\fnPixelify Sans\\fs72\\1c&H29B4F0&}Banana Player\n";
		ev += "{\\an8\\pos(480,420)\\bord0\\shad0\\fnInter\\fs30\\1c&H7C868A&}Pick a video or a song on the bottom screen";
		const char* cmd[] = {"osd-overlay", "3", "ass-events", ev.c_str(), "960", "720", "0", "no", "no", nullptr};
		mpv_command(mpv, cmd);
	}

	void Player::hideLevel()
	{
		const char* cmd[] = {"osd-overlay", "2", "none", "", nullptr};
		mpv_command(mpv, cmd);
	}

	void Player::osd(const std::string& text, int ms)
	{
		std::string d = std::to_string(ms);
		const char* cmd[] = {"show-text", text.c_str(), d.c_str(), nullptr};
		mpv_command(mpv, cmd);
	}

	void Player::saveResume()
	{
		if (!loaded || playlist.empty() || opts.stateDir.empty()) return;
		ResumeEntry e;
		e.path = playlist[current];
		// played to the end (or as good as): next time from the start
		if (settings.rememberPosition && !finished && worthResuming(position, duration)) e.position = position;
		// the tracks as last seen open (at the end, mpv's list is already gone: it isn't "subtitles off")
		if (settings.rememberAudio && seenAid > 0) e.aid = seenAid;
		if (settings.rememberSubs && seenSid >= 0) e.sid = seenSid;
		resume.put(e);
		writeFile(opts.stateDir + "/resume.tsv", resume.serialize());
	}

	void Player::stop()
	{
		saveResume();
		playlist.clear();
		current = 0;
		loaded = finished = false;
		error.clear();
		title.clear();
		tracks.clear();
		position = duration = -1;
		const char* cmd[] = {"stop", nullptr};
		mpv_command(mpv, cmd);
		isAudio = false;
		updateOverlay();	// the title off the top panel
		showIdle(true);
		prop("lavfi-complex", "");
	}

	void Player::clearHistory()
	{
		played.clear();
		if (!opts.stateDir.empty()) unlink((opts.stateDir + "/history.tsv").c_str());
	}

	void Player::setLastFolder(const std::string& dir)
	{
		std::string keep = settings.rememberFolder ? dir : "";
		if (keep == resume.lastFolder || opts.stateDir.empty()) return;
		resume.lastFolder = keep;
		writeFile(opts.stateDir + "/resume.tsv", resume.serialize());
	}

	static bool hasCover(const std::vector<Track>& tracks)
	{
		for (auto& t : tracks)
			if (t.type == TrackType::Video && t.albumArt) return true;
		return false;
	}

	int Player::shownAudioDisplay() const
	{
		// Auto: the cover when the file (or its folder) has one, the bars otherwise; Cover + bars: the bars alone
		// when there's no cover
		bool cover = hasCover(tracks);
		switch (settings.audioDisplay)
		{
		case AUDIO_AUTO: return cover ? AUDIO_COVER : AUDIO_BARS;
		case AUDIO_COVER_BARS: return cover ? AUDIO_COVER_BARS : AUDIO_BARS;
		default: return settings.audioDisplay;
		}
	}

	void Player::applyAudioDisplay()
	{
		int d = shownAudioDisplay();
		std::string graph = audioVisualizerGraph(d);
		if (get("lavfi-complex") != graph) prop("lavfi-complex", graph);
	}

	// ASS text: braces and backslashes would be its codes
	static std::string assText(std::string s)
	{
		for (char& c : s)
		{
			if (c == '{') c = '(';
			else if (c == '}') c = ')';
			else if (c == '\\') c = '/';
		}
		return s;
	}

	void Player::updateOverlay()
	{
		if (!isAudio)
		{
			const char* cmd[] = {"osd-overlay", "1", "none", "", nullptr};
			mpv_command(mpv, cmd);
			return;
		}
		std::string title = get("media-title"), artist = get("metadata/by-key/Artist"), album = get("metadata/by-key/Album");
		int d = shownAudioDisplay();
		bool cover = d == AUDIO_COVER && hasCover(tracks);
		// over a visualizer: at the top; over a cover: at the bottom; on black: in the middle, bigger
		const char* at = d == AUDIO_COVER ? (cover ? "{\\an2}" : "{\\an5}") : "{\\an8}";
		int big = cover || d != AUDIO_COVER ? 40 : 56;
		std::string text = std::string(at) + "{\\bord3}{\\fs" + std::to_string(big) + "}{\\b1}" + assText(title) + "{\\b0}";
		if (!artist.empty()) text += "\\N{\\fs" + std::to_string(big * 3 / 4) + "}" + assText(artist);
		if (!album.empty()) text += "\\N{\\fs" + std::to_string(big * 2 / 3) + "}{\\c&HB4B4B4&}" + assText(album);
		const char* cmd[] = {"osd-overlay", "1", "ass-events", text.c_str(), "0", "720", "0", "no", "no", nullptr};
		mpv_command(mpv, cmd);
	}

	std::string Player::get(const char* name) const
	{
		char* v = mpv ? mpv_get_property_string(mpv, name) : nullptr;
		std::string out = v ? v : "";
		mpv_free(v);
		return out;
	}

	std::vector<Player::InfoRow> Player::mediaInfo() const
	{
		std::vector<InfoRow> rows;
		auto header = [&](const std::string& h) { rows.push_back({h, "", true}); };
		auto row = [&](const std::string& label, const std::string& value) {
			if (!value.empty()) rows.push_back({label, value, false});
		};
		auto num = [&](const char* name) { std::string v = get(name); return v.empty() ? -1.0 : atof(v.c_str()); };
		// a track's codec as mpv names it in full ("H.264 / AVC / ..."), or its short name
		auto codec = [&](const char* track) {
			std::string desc = get((std::string("current-tracks/") + track + "/codec-desc").c_str());
			return desc.empty() ? codecName(get((std::string("current-tracks/") + track + "/codec").c_str())) : desc;
		};

		std::string path = get("path");
		size_t slash = path.rfind('/');
		header("File");
		row("Name", slash == std::string::npos ? path : path.substr(slash + 1));
		row("Folder", slash == std::string::npos ? "" : slash == 0 ? "/" : path.substr(0, slash));
		double size = num("file-size");
		if (size >= 0) row("Size", formatBytes((long long)size));
		row("Container", get("file-format"));
		if (duration > 0) row("Duration", formatTime(duration));
		double left = timeLeft(position, duration, speed);
		if (left >= 0) row("Time left", formatTime(left) + (speed != 1 ? " at " + speedLabel(speed) : ""));
		double chapters = num("chapters");
		if (chapters > 0) row("Chapters", std::to_string((int)chapters));

		if (!get("current-tracks/video/id").empty())
		{
			// an audio file's picture is its cover
			header(get("current-tracks/video/albumart") == "yes" ? "Cover art" : "Video");
			row("Codec", codec("video"));
			std::string w = get("video-params/w"), h = get("video-params/h");
			if (w.empty()) { w = get("current-tracks/video/demux-w"); h = get("current-tracks/video/demux-h"); }
			if (!w.empty()) row("Resolution", w + " x " + h);
			std::string dw = get("video-params/dw"), dh = get("video-params/dh");
			if (!dw.empty() && (dw != w || dh != h)) row("Shown as", dw + " x " + dh);
			double fps = num("container-fps");
			if (fps > 0)
			{
				char buf[32];
				snprintf(buf, sizeof buf, "%.3g fps", fps);
				row("Frame rate", buf);
			}
			row("Pixel format", get("video-params/pixelformat"));
			std::string prim = get("video-params/primaries"), gamma = get("video-params/gamma");
			if (!prim.empty()) row("Colour", prim + (gamma.empty() ? "" : ", " + gamma));
			double vb = num("video-bitrate");
			if (vb > 0) row("Bitrate", formatBitrate(vb));
			std::string hw = get("hwdec-current");
			row("Decoding", hw.empty() || hw == "no" ? "Software" : "Hardware (" + hw + ")");
			double drops = num("frame-drop-count"), decDrops = num("decoder-frame-drop-count");
			row("Dropped frames", std::to_string((long)std::max(0.0, drops) + (long)std::max(0.0, decDrops)));
		}

		// a selected track's label, numbered within its kind as the menus number it
		auto label = [&](const Track* t) {
			auto list = tracksOf(t->type);
			int n = 1;
			for (size_t i = 0; i < list.size(); i++)
				if (list[i].id == t->id) n = (int)i + 1;
			return trackLabel(*t, n);
		};
		const Track* a = selected(TrackType::Audio);
		header("Audio");
		if (isAudio)
		{
			// what the top panel shows, as mpv has it now
			std::string graph = get("lavfi-complex");
			row("Top screen", graph.find("[vid1]") != std::string::npos ? "Cover + bars"
				: graph.find("showfreqs") != std::string::npos ? "Bars"
				: graph.find("showwaves") != std::string::npos      ? "Waves"
				: graph.find("showspectrum") != std::string::npos   ? "Spectrum"
				: hasCover(tracks)                                  ? "Cover"
																	: "Title");
		}
		if (a)
		{
			row("Track", label(a));
			row("Codec", codec("audio"));
			double rate = num("audio-params/samplerate");
			if (rate > 0)
			{
				char buf[32];
				snprintf(buf, sizeof buf, "%g kHz", rate / 1000);
				row("Sample rate", buf);
			}
			row("Channels", get("audio-params/hr-channels"));
			double ab = num("audio-bitrate");
			if (ab > 0) row("Bitrate", formatBitrate(ab));
		}
		else row("Track", "Off");

		const Track* s = selected(TrackType::Sub);
		header("Subtitles");
		row("Track", s ? label(s) : "Off");

		header("Tracks");
		const char* names[] = {"Video", "Audio", "Subtitle"};
		TrackType types[] = {TrackType::Video, TrackType::Audio, TrackType::Sub};
		for (int t = 0; t < 3; t++)
		{
			auto list = tracksOf(types[t]);
			for (size_t i = 0; i < list.size(); i++)
			{
				std::string label = types[t] == TrackType::Video ? codecName(list[i].codec) : trackLabel(list[i], (int)i + 1);
				row(std::string(names[t]) + " " + std::to_string(i + 1), (list[i].selected ? "* " : "") + label);
			}
		}

		// the file's own tags: title, encoder, date...
		mpv_node tags;
		if (mpv && mpv_get_property(mpv, "metadata", MPV_FORMAT_NODE, &tags) >= 0)
		{
			if (tags.format == MPV_FORMAT_NODE_MAP && tags.u.list->num > 0)
			{
				header("Tags");
				for (int i = 0; i < tags.u.list->num; i++)
					if (tags.u.list->values[i].format == MPV_FORMAT_STRING) row(tags.u.list->keys[i], tags.u.list->values[i].u.string);
			}
			mpv_free_node_contents(&tags);
		}
		return rows;
	}

	std::vector<Track> Player::tracksOf(TrackType type) const
	{
		std::vector<Track> out;
		for (auto& t : tracks)
			if (t.type == type && !t.albumArt) out.push_back(t);
		return out;
	}

	const Track* Player::selected(TrackType type) const
	{
		for (auto& t : tracks)
			if (t.type == type && t.selected) return &t;
		return nullptr;
	}
}
