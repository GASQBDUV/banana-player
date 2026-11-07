#include "player.h"
#include "format.h"
#include "logo.h"
#include "playlist.h"
#include <algorithm>
#include <chrono>
#include <cmath>
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
	// screenshot-raw's answers for the photo's thumbnail: this plus the load they were asked in
	static const uint64_t THUMB_REPLY = 1000;
	// the panels' shape (both are 4:3)
	static const double PANEL_ASPECT = 4.0 / 3;
	// the thumbnail's largest size: the pan area's on the Plus's 1024x768 panel
	static const int THUMB_MAX_W = 560, THUMB_MAX_H = 420;

	static std::string baseName(const std::string& p)
	{
		size_t slash = p.rfind('/');
		return slash == std::string::npos ? p : p.substr(slash + 1);
	}

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
		// stereo out, mixed by mpv: ALSA's plug (BananaOS has no sound server) puts a mono track on the left speaker
		// only, and takes a 5.1 track's first two channels (no centre: no dialogue)
		opt("audio-channels", "stereo");
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
			// tests: libass with no system fonts, as on BananaOS (BP_FONT_PROVIDER=none)
			if (const char* fp = getenv("BP_FONT_PROVIDER")) opt("sub-font-provider", fp);
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

	bool Player::advance()
	{
		if (next()) return true;
		// Loop all (music): from the last back to the first
		if (!isAudio || settings.loop != LOOP_ALL || playlist.empty()) return false;
		saveResume();
		current = 0;
		load();
		return true;
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
		isAudio = isAudioFile(path);
		isPhoto = isPhotoFile(path);
		loadSerial++;
		loadAt = ticks();
		started = false;
		thumb.clear();
		thumbW = thumbH = 0;
		thumbId++;
		thumbAt = 0;
		thumbTries = 0;
		thumbAsked = thumbTurnedByMpv = false;
		photoW = photoH = fileTurn = exifTurn = 0;

		showIdle(false);
		// the last file's visualizer off first: with a filter graph on them, mpv's tracks can't be chosen (aid, sid
		// below)
		prop("lavfi-complex", "");

		// what this file is remembered with; mpv's options are global, so each one is set for every file
		const ResumeEntry* e = isPhoto ? nullptr : resume.find(path);
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

		// a visualizer is set before the file opens; a video has none
		// (the art's choices wait for the file: they depend on whether it has art)
		if (isAudio && settings.musicShow == SHOW_VIS) prop("lavfi-complex", graphFor(settings.visualizer, false));
		if (isAudio && settings.musicShow == SHOW_NONE) prop("lavfi-complex", blankGraph());
		// Loop one: the song again and again (mpv's own); a video never loops; a photo stays (an animated one goes round)
		prop("loop-file", isPhoto || (isAudio && settings.loop == LOOP_ONE) ? "inf" : "no");

		if (isPhoto)
		{
			exif = readExif(path);
			view.rotate = 0;
			if (!settings.keepPhotoZoom)
			{
				view.zoom = 1;
				view.fill = false;
				panShareX = panShareY = 0;
			}
			// a photo has no Play: one that opens paused (the lid closed over the last) would freeze an animation
			prop("pause", "no");
			view.panX = view.panY = 0;
			// Fill is the view's (by zoom, below), so mpv fits it
			for (auto& a : aspectProperties(AspectMode::Fit)) prop(a.first, a.second);
			// a camera's 4000 pixels on the panel's 640: a filter that keeps fine lines, not the fast profile's bilinear
			// (drawn once, not 30 times a second)
			prop("dscale", "hermite");
			prop("correct-downscaling", "yes");
			prop("image-display-duration", "inf");
			// at most 4096 pixels across: the RG DS's GPU may take no picture over 8192 (a 50 MP photo would stay
			// black), and it's a quarter of a 50 MP photo's memory; still more than the panel at 8x zoom
			prop("vf", "lavfi=[scale=w='min(iw,4096)':h='min(ih,4096)':force_original_aspect_ratio=decrease]");
		}
		else
		{
			exif = ExifInfo();
			prop("vf", "");
			for (auto& a : aspectProperties(aspect)) prop(a.first, a.second);
			prop("dscale", "bilinear");
			prop("correct-downscaling", "no");
		}
		applyPhotoView();	// a video's: none

		// photos aren't kept in Recent: fifty of them would push every video out
		if (settings.keepHistory && !isPhoto)
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
				if (isAudio && (settings.musicShow == SHOW_ART || settings.musicShow == SHOW_BOTH)) applyAudioDisplay();
				updateOverlay();
				// a photo's name and its place in the folder, for a moment
				if (isPhoto) osd(baseName(playlist[current]) + (count() > 1 ? "   " + std::to_string(current + 1) + " / " + std::to_string(count()) : ""), 2000);
				break;
			}
			case MPV_EVENT_COMMAND_REPLY:
			{
				if (ev->reply_userdata != THUMB_REPLY + (uint64_t)loadSerial) break;	// an earlier photo's
				thumbAsked = false;
				if (ev->error < 0) thumbAt = ticks() + 250;	// no frame up yet: again soon
				else takeThumb(&((mpv_event_command*)ev->data)->result);
				break;
			}
			case MPV_EVENT_END_FILE:
			{
				auto* e = (mpv_event_end_file*)ev->data;
				if (e->reason == MPV_END_FILE_REASON_ERROR) error = mpv_error_string(e->error);
				else if (e->reason == MPV_END_FILE_REASON_EOF) finished = true;
				break;
			}
			case MPV_EVENT_PLAYBACK_RESTART:
				// this file's (it comes after its FILE_LOADED): one left over from the last file's seek isn't
				if (loaded) started = true;
				break;
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

	void Player::checkSongEnd()
	{
		if (!isAudio || !loaded || finished || !(duration > 0) || position < duration + 0.25) return;
		if (get("lavfi-complex").empty()) return;	// no graph: mpv ends the file itself
		if (settings.loop == LOOP_ONE) seekFraction(0);
		else finished = true;
	}

	bool Player::loading() const
	{
		// till mpv starts it: its first frame up (or its sound playing)
		return playing() && error.empty() && !started;
	}

	void Player::checkLoading()
	{
		bool show = loading() && ticks() - loadAt >= 300;
		if (!show)
		{
			if (spinnerStep >= 0)
			{
				const char* cmd[] = {"osd-overlay", "4", "none", "", nullptr};
				mpv_command(mpv, cmd);
				spinnerStep = -1;
			}
			return;
		}
		int step = (int)(ticks() / 100 % 8);
		if (step == spinnerStep) return;
		spinnerStep = step;
		// eight dots round the middle of a 960x720 canvas, one lit going round, the word under them, on a dark card
		// (the last file's picture can still be up)
		std::string ev = "{\\an7\\pos(390,262)\\bord0\\shad0\\1c&H1C1617&\\1a&H08&\\p1}m 0 0 l 180 0 180 180 0 180{\\p0}\n";
		for (int i = 0; i < 8; i++)
		{
			double a = i * M_PI / 4;
			int x = 480 + (int)std::lround(std::sin(a) * 36), y = 330 - (int)std::lround(std::cos(a) * 36);
			int age = (step - i + 8) % 8;	// the lit one, then fading behind it
			const char* alpha = age == 0 ? "00" : age == 1 ? "40" : age == 2 ? "80" : "B0";
			char d[160];
			snprintf(d, sizeof d, "{\\an7\\pos(%d,%d)\\bord0\\shad0\\1c&H29B4F0&\\1a&H%s&\\p1}m 0 0 l 12 0 12 12 0 12{\\p0}\n", x - 6, y - 6, alpha);
			ev += d;
		}
		ev += "{\\an8\\pos(480,390)\\bord2\\shad0\\fnInter\\fs30\\1c&HE2EDF1&}Loading";
		const char* cmd[] = {"osd-overlay", "4", "ass-events", ev.c_str(), "960", "720", "20", "no", "no", nullptr};
		mpv_command(mpv, cmd);
	}

	void Player::checkPhoto()
	{
		if (!isPhoto || !loaded) return;
		if (!photoW)
		{
			int w = atoi(get("video-params/dw").c_str()), h = atoi(get("video-params/dh").c_str());
			if (w <= 0 || h <= 0) return;
			photoW = w;
			photoH = h;
			// upright: by the file's rotation metadata (mpv's own turn), else by its EXIF orientation, which
			// FFmpeg 7.1 leaves to the player
			fileTurn = atoi(get("video-params/rotate").c_str()) % 360;
			exifTurn = fileTurn ? 0 : exifRotation(exif.orientation);
			// the last photo's zoom, at the same share of the way across
			PhotoScale s = photoScaleNow();
			setPhotoView(view.zoom, panShareX * maxPan(s.x), panShareY * maxPan(s.y));
			thumbAt = ticks() + 100;
		}
		if (thumbW || thumbAsked || !thumbAt || ticks() < thumbAt || thumbTries >= 10) return;
		// the frame as decoded (no OSD, full size), answered later: a big photo's copy doesn't hold the loop up
		thumbAsked = true;
		thumbTries++;
		mpv_node items[2];
		items[0].format = items[1].format = MPV_FORMAT_STRING;
		items[0].u.string = const_cast<char*>("screenshot-raw");
		items[1].u.string = const_cast<char*>("video");
		mpv_node_list list{2, items, nullptr};
		mpv_node args;
		args.format = MPV_FORMAT_NODE_ARRAY;
		args.u.list = &list;
		mpv_command_node_async(mpv, THUMB_REPLY + (uint64_t)loadSerial, &args);
	}

	void Player::takeThumb(mpv_node* r)
	{
		if (!r || r->format != MPV_FORMAT_NODE_MAP) return;
		int64_t w = 0, h = 0, stride = 0;
		const unsigned char* px = nullptr;
		size_t size = 0;
		std::string format;
		for (int i = 0; i < r->u.list->num; i++)
		{
			std::string k = r->u.list->keys[i];
			mpv_node& v = r->u.list->values[i];
			if (v.format == MPV_FORMAT_INT64 && k == "w") w = v.u.int64;
			else if (v.format == MPV_FORMAT_INT64 && k == "h") h = v.u.int64;
			else if (v.format == MPV_FORMAT_INT64 && k == "stride") stride = v.u.int64;
			else if (v.format == MPV_FORMAT_STRING && k == "format") format = v.u.string;
			else if (v.format == MPV_FORMAT_BYTE_ARRAY && k == "data")
			{
				px = (const unsigned char*)v.u.ba->data;
				size = v.u.ba->size;
			}
		}
		if (w <= 0 || h <= 0 || !px || format != "bgr0" || size < (size_t)(stride * (h - 1) + w * 4)) return;
		// the whole photo, or (some builds) already turned by its metadata
		// (by its shape: the screenshot is after the vf's scale, the size isn't the photo's)
		thumbTurnedByMpv = fileTurn % 180 == 90 && photoW != photoH && (w > h) != (photoW > photoH);
		double k = std::max({1.0, w / (double)THUMB_MAX_W, h / (double)THUMB_MAX_H});
		int tw = std::max(1, (int)(w / k)), th = std::max(1, (int)(h / k));
		thumb.assign((size_t)tw * th * 4, 255);
		// each pixel the mean of four of the photo's, spread over its share
		for (int y = 0; y < th; y++)
			for (int x = 0; x < tw; x++)
			{
				unsigned sum[3] = {};
				for (int sy = 0; sy < 2; sy++)
					for (int sx = 0; sx < 2; sx++)
					{
						int64_t ix = std::min(w - 1, (int64_t)((x + 0.25 + sx * 0.5) * w / tw));
						int64_t iy = std::min(h - 1, (int64_t)((y + 0.25 + sy * 0.5) * h / th));
						const unsigned char* q = px + iy * stride + ix * 4;
						for (int c = 0; c < 3; c++) sum[c] += q[c];
					}
				unsigned char* o = &thumb[((size_t)y * tw + x) * 4];
				for (int c = 0; c < 3; c++) o[c] = (unsigned char)(sum[c] / 4);
			}
		thumbW = tw;
		thumbH = th;
		thumbId++;
	}

	int Player::thumbTurn() const
	{
		return (exifTurn + view.rotate + (thumbTurnedByMpv ? 0 : fileTurn)) % 360;
	}

	PhotoScale Player::photoScaleNow() const
	{
		if (!photoW) return {view.zoom, view.zoom};
		return photoScale(photoWidth() / (double)photoHeight(), PANEL_ASPECT, view.fill, view.zoom);
	}

	void Player::applyPhotoView()
	{
		double zoom = 0, px = 0, py = 0;
		int64_t turn = 0;
		if (isPhoto)
		{
			// mpv's zoom is over its Fit: Fill's own factor times the view's
			PhotoScale fit = photoW ? photoScale(photoWidth() / (double)photoHeight(), PANEL_ASPECT, false, 1) : PhotoScale{1, 1};
			zoom = std::log2(photoScaleNow().x / fit.x);
			px = view.panX;
			py = view.panY;
			turn = (exifTurn + view.rotate) % 360;
		}
		mpv_set_property(mpv, "video-zoom", MPV_FORMAT_DOUBLE, &zoom);
		mpv_set_property(mpv, "video-pan-x", MPV_FORMAT_DOUBLE, &px);
		mpv_set_property(mpv, "video-pan-y", MPV_FORMAT_DOUBLE, &py);
		mpv_set_property(mpv, "video-rotate", MPV_FORMAT_INT64, &turn);
	}

	void Player::setPhotoView(double zoom, double panX, double panY)
	{
		view.zoom = std::clamp(zoom, 1.0, MAX_ZOOM);
		PhotoScale s = photoScaleNow();
		double mx = maxPan(s.x), my = maxPan(s.y);
		view.panX = std::clamp(panX, -mx, mx);
		view.panY = std::clamp(panY, -my, my);
		panShareX = mx > 0 ? view.panX / mx : 0;
		panShareY = my > 0 ? view.panY / my : 0;
		applyPhotoView();
	}

	void Player::rotatePhoto()
	{
		view.rotate = (view.rotate + 90) % 360;
		// the same zoom, centred (the place was across the other way)
		setPhotoView(view.zoom, 0, 0);
	}

	void Player::togglePhotoFill()
	{
		view.fill = !view.fill;
		setPhotoView(view.zoom, view.panX, view.panY);
	}

	void Player::unpause()
	{
		prop("pause", "no");
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
		bool displayChanged = s.musicShow != settings.musicShow || s.visualizer != settings.visualizer;
		bool loopChanged = s.loop != settings.loop;
		settings = s;
		if (displayChanged && isAudio && loaded)
		{
			applyAudioDisplay();
			updateOverlay();
		}
		if (loopChanged && isAudio) prop("loop-file", s.loop == LOOP_ONE ? "inf" : "no");
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
		// a photo has no place to come back to
		if (!loaded || playlist.empty() || opts.stateDir.empty() || isPhoto) return;
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
		isAudio = isPhoto = false;
		checkLoading();	// the spinner down
		applyPhotoView();
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

	int Player::shownMusic() const
	{
		// the art's choices need art: a song with none shows the visualizer (None is none either way)
		if (settings.musicShow == SHOW_NONE) return SHOW_NONE;
		return hasCover(tracks) ? settings.musicShow : SHOW_VIS;
	}

	std::string Player::graphFor(int vis, bool overCover)
	{
		// Circle's tables: made the first time it's wanted, beside the settings, else (none kept, or a folder a
		// filter graph can't name) in /tmp. Tried again at the next song when they couldn't be written (a full card)
		if (vis == VIS_CIRCLE && circleMaps.empty())
		{
			std::string dir = lavfiSafePath(opts.stateDir) ? opts.stateDir : "/tmp/banana-player";
			if (writeCircleMaps(dir)) circleMaps = dir;
			else fprintf(stderr, "banana-player: Circle's tables couldn't be written in %s: drawn as Bars\n", dir.c_str());
		}
		return visualizerGraph(vis, overCover, circleMaps);
	}

	void Player::applyAudioDisplay()
	{
		int show = shownMusic();
		std::string graph = show == SHOW_ART ? "" : show == SHOW_NONE ? blankGraph() : graphFor(settings.visualizer, show == SHOW_BOTH);
		bool had = !get("lavfi-complex").empty();
		if (get("lavfi-complex") != graph) prop("lavfi-complex", graph);
		if (graph.empty() && had)
		{
			// a graph taken off leaves no track chosen: the song's sound and its art back
			prop("aid", seenAid > 0 ? std::to_string(seenAid) : "auto");
			// by its id: "auto" while playing doesn't pick the art again
			int64_t art = 0;
			for (auto& t : tracks)
				if (t.type == TrackType::Video && !art) art = t.id;
			prop("vid", art ? std::to_string(art) : "auto");
		}
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
		// over the art alone: at the bottom; with a visualizer (along the bottom): at the top; alone: in the middle, bigger
		int show = shownMusic();
		const char* at = show == SHOW_ART ? "{\\an2}" : show == SHOW_NONE ? "{\\an5}" : "{\\an8}";
		int big = show == SHOW_NONE ? 56 : 40;
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
		if (isPhoto)
		{
			header("Photo");
			row("Name", slash == std::string::npos ? path : path.substr(slash + 1));
			row("Folder", slash == std::string::npos ? "" : slash == 0 ? "/" : path.substr(0, slash));
			double size = num("file-size");
			if (size >= 0) row("Size", formatBytes((long long)size));
			row("Format", codec("video"));
			// the photo's own size (shown at most 4096 across)
			int w = atoi(get("video-dec-params/dw").c_str()), h = atoi(get("video-dec-params/dh").c_str());
			if (turned()) std::swap(w, h);
			if (w > 0 && h > 0) row("Resolution", std::to_string(w) + " x " + std::to_string(h));
			if (duration > 0.05) row("Animation", formatTime(duration) + ", over and over");
			if (!exif.taken.empty()) row("Taken", exifDate(exif.taken));
			// "Canon" and "Canon EOS R6": the model alone
			std::string camera = exif.model.rfind(exif.make, 0) == 0 ? exif.model
				: exif.make + (exif.make.empty() || exif.model.empty() ? "" : " ") + exif.model;
			row("Camera", camera);
			header("View");
			row("Zoom", std::to_string((int)std::lround(view.zoom * 100)) + "%");
			row("Picture size", view.fill ? "Fill" : "Fit");
			if (view.rotate) row("Turned", std::to_string(view.rotate) + " degrees");
			return rows;
		}
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
			std::string vis = graph.find("showwaves") != std::string::npos ? "Waves"
							  : graph.find("showspectrum") != std::string::npos ? "Spectrum"
							  : graph.find("] remap [") != std::string::npos ? "Circle" : "Bars";
			row("Top screen", graph == blankGraph() ? "Title only"
				: graph.find("[vid1]") != std::string::npos ? "Album art + " + vis
				: !graph.empty() ? "Visualizer: " + vis
				: shownMusic() == SHOW_NONE ? "Title only"
				: hasCover(tracks) ? "Album art" : "Title");
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
