// Picture subtitles (VobSub, PGS, DVB) for the web dashboard's player, a window of the film at a time (see subs.h).
// A 4K film is ~17 GB: reading it through for its subtitles would take minutes on the handheld, so each request seeks to
// its window, reads only that stretch (the video's packets are skipped, though the demuxer still reads past them), and
// decodes just the subtitle packets into paletted PNGs. Runs on server pool threads, never touching Player or Ui.
#include "web/subs.h"
#include "json.h"
#include "picsubs.h"
#include "png.h"
#include "secret.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <list>
#include <map>
#include <mutex>
#include <sys/stat.h>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace bp
{
	namespace
	{
		const double BACK = 10;			// read from this much before the window: a cue still showing as it starts
		const double LOOK = 10;			// and this much past it, for the end of the last cue
		const double MAX_WINDOW = 120;
		const size_t MAX_BODY = (size_t)8 << 20;	// base64 PNGs in one answer
		const size_t CACHE_MAX = (size_t)16 << 20;
		const int DEADLINE_S = 8;

		struct Interrupt { std::chrono::steady_clock::time_point end; bool hit = false; };
		int interrupted(void* o)
		{
			auto* in = static_cast<Interrupt*>(o);
			if (std::chrono::steady_clock::now() > in->end) in->hit = true;
			return in->hit;
		}

		// two windows decode at a time at most (the page asks one at a time; a second tab or a seek may add one)
		std::mutex gateM;
		std::condition_variable gateCv;
		int gateN = 0;
		struct Gate
		{
			Gate() { std::unique_lock<std::mutex> g(gateM); gateCv.wait(g, [] { return gateN < 2; }); gateN++; }
			~Gate() { { std::lock_guard<std::mutex> g(gateM); gateN--; } gateCv.notify_one(); }
		};

		// answers by window, newest first, to 16 MB (a seek back, a second tab, the page reloaded)
		std::mutex cacheM;
		std::list<std::pair<std::string, std::string>> cache;
		size_t cacheSize = 0;
		bool cached(const std::string& key, std::string& body)
		{
			std::lock_guard<std::mutex> g(cacheM);
			for (auto it = cache.begin(); it != cache.end(); ++it)
				if (it->first == key)
				{
					body = it->second;
					cache.splice(cache.begin(), cache, it);
					return true;
				}
			return false;
		}
		void remember(const std::string& key, const std::string& body)
		{
			std::lock_guard<std::mutex> g(cacheM);
			cache.emplace_front(key, body);
			cacheSize += key.size() + body.size();
			while (cacheSize > CACHE_MAX && cache.size() > 1)
			{
				cacheSize -= cache.back().first.size() + cache.back().second.size();
				cache.pop_back();
			}
		}

		struct Bitmap { int x = 0, y = 0, w = 0, h = 0, colors = 0; std::vector<uint8_t> idx; uint32_t pal[256]; };

		SubsAnswer fail(int status, const std::string& msg)
		{
			SubsAnswer a;
			a.status = status;
			a.type = "application/json; charset=utf-8";
			a.body = JsonWriter().obj().key("error").str(msg).end().out();
			return a;
		}

		double r3(double v) { return std::round(v * 1000) / 1000; }
	}

	bool pictureCodec(const std::string& c)
	{
		return c == "dvd_subtitle" || c == "hdmv_pgs_subtitle" || c == "dvb_subtitle";
	}

	SubsAnswer pictureSubs(const std::string& real, const SubEntry& sub, double reqFrom, double reqTo)
	{
		if (sub.kind != "picture") return fail(415, "Not picture subtitles");
		double from, to;
		if (!pictureWindow(reqFrom, reqTo, MAX_WINDOW, from, to)) return fail(400, "bad window");
		const std::string path = sub.external ? sub.file : real;
		struct stat st;
		if (stat(path.c_str(), &st) != 0) return fail(404, "not found");
		char kb[96];
		snprintf(kb, sizeof kb, "\n%lld\n%lld\n%d\n%.3f\n%.3f", (long long)st.st_mtime, (long long)st.st_size, sub.stream, from, to);
		const std::string key = path + kb;
		SubsAnswer a;
		a.type = "application/json; charset=utf-8";
		if (cached(key, a.body)) return a;

		Gate gate;
		auto t0 = std::chrono::steady_clock::now();
		Interrupt intr;
		intr.end = t0 + std::chrono::seconds(DEADLINE_S);
		AVFormatContext* f = avformat_alloc_context();
		if (!f) return fail(500, "out of memory");
		f->interrupt_callback.callback = interrupted;
		f->interrupt_callback.opaque = &intr;
		if (avformat_open_input(&f, path.c_str(), nullptr, nullptr) < 0) return fail(422, "can't open the file");
		struct Closer { AVFormatContext*& f; ~Closer() { avformat_close_input(&f); } } closer{f};

		// the track: an embedded one by its stream index; a .idx/.sup beside the video has it as its first
		int index = sub.external ? -1 : sub.stream;
		if (index < 0)
			for (unsigned i = 0; i < f->nb_streams && index < 0; i++)
				if (f->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) index = (int)i;
		if (index < 0 || index >= (int)f->nb_streams || f->streams[index]->codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE)
			return fail(404, "no such subtitle track");
		AVStream* ss = f->streams[index];
		const AVCodec* codec = avcodec_find_decoder(ss->codecpar->codec_id);
		if (!codec) return fail(415, "can't decode these subtitles");
		AVCodecContext* cc = avcodec_alloc_context3(codec);
		if (!cc) return fail(500, "out of memory");
		struct CcFree { AVCodecContext*& c; ~CcFree() { avcodec_free_context(&c); } } ccFree{cc};
		avcodec_parameters_to_context(cc, ss->codecpar);
		cc->pkt_timebase = ss->time_base;
		if (avcodec_open2(cc, codec, nullptr) < 0) return fail(415, "can't decode these subtitles");

		// subtitle packets are sparse: a stream that comes often (audio, else video) says how far the reading got
		int clock = -1, video = -1;
		for (unsigned i = 0; i < f->nb_streams; i++)
		{
			AVStream* s = f->streams[i];
			if (s->disposition & AV_DISPOSITION_ATTACHED_PIC) continue;
			if (s->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && clock < 0) clock = (int)i;
			if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && video < 0) video = (int)i;
		}
		if (clock < 0) clock = video;
		// the seek first, by the video's index (Matroska's Cues usually list only the video), with every stream still
		// wanted: a seek on a stream with no index (or one already discarded) reads through the file to find the
		// time, minutes of a 4K film from the card (the 8 s deadline hit before a packet was read)
		auto ts0 = std::chrono::steady_clock::now();
		if (from - BACK > 0.5)
		{
			int64_t ts = (int64_t)((from - BACK) * AV_TIME_BASE);
			if (f->start_time != AV_NOPTS_VALUE) ts += f->start_time;
			int r = -1;
			if (video >= 0)
			{
				AVStream* vs = f->streams[video];
				r = av_seek_frame(f, video, av_rescale_q(ts, AV_TIME_BASE_Q, vs->time_base), AVSEEK_FLAG_BACKWARD);
			}
			if (r < 0) av_seek_frame(f, -1, ts, AVSEEK_FLAG_BACKWARD);
			avcodec_flush_buffers(cc);
		}
		long seekMs = (long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - ts0).count();
		for (unsigned i = 0; i < f->nb_streams; i++)
			if ((int)i != index && (int)i != clock) f->streams[i]->discard = AVDISCARD_ALL;

		int64_t pos0 = f->pb ? avio_tell(f->pb) : 0;
		std::vector<PicEvent> events;
		std::vector<std::vector<Bitmap>> pics;
		double clockT = -1, lastSub = -1;
		bool after = false, eof = false;
		int maxX = 0, maxY = 0;
		size_t kept = 0;
		AVPacket* pkt = av_packet_alloc();
		while (true)
		{
			int r = av_read_frame(f, pkt);
			if (r < 0)
			{
				eof = r == AVERROR_EOF && !intr.hit;
				break;
			}
			int64_t pts = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
			double t = pts == AV_NOPTS_VALUE ? -1 : double(pts) * av_q2d(f->streams[pkt->stream_index]->time_base);
			if (pkt->stream_index == clock && t >= 0) clockT = std::max(clockT, t);
			if (pkt->stream_index == index)
			{
				AVSubtitle s;
				int got = 0;
				if (avcodec_decode_subtitle2(cc, &s, &got, pkt) >= 0 && got)
				{
					double base = s.pts != AV_NOPTS_VALUE ? double(s.pts) / AV_TIME_BASE : t;
					if (base >= 0)
					{
						PicEvent e;
						e.start = base + s.start_display_time / 1000.0;
						if (s.end_display_time > s.start_display_time && s.end_display_time != UINT32_MAX) e.end = base + s.end_display_time / 1000.0;
						else if (pkt->duration > 0) e.end = t + double(pkt->duration) * av_q2d(ss->time_base);
						std::vector<Bitmap> rects;
						// (the ones that can't show in this window aren't kept: a long seek back reads many)
						bool want = e.start < to + LOOK && (e.end < 0 || e.end > from - 1);
						for (unsigned k = 0; k < s.num_rects; k++)
						{
							AVSubtitleRect* rc = s.rects[k];
							if (rc->type != SUBTITLE_BITMAP || rc->w <= 0 || rc->h <= 0 || !rc->data[0] || !rc->data[1]) continue;
							if (rc->w > 8192 || rc->h > 8192) continue;
							maxX = std::max(maxX, rc->x + rc->w);
							maxY = std::max(maxY, rc->y + rc->h);
							if (!want) { rects.emplace_back(); continue; }
							Bitmap b;
							b.x = rc->x; b.y = rc->y; b.w = rc->w; b.h = rc->h;
							b.colors = std::max(1, std::min(256, rc->nb_colors));
							memcpy(b.pal, rc->data[1], b.colors * 4);
							b.idx.resize((size_t)b.w * b.h);
							for (int y = 0; y < b.h; y++) memcpy(&b.idx[(size_t)y * b.w], rc->data[0] + (size_t)y * rc->linesize[0], b.w);
							kept += b.idx.size();
							rects.push_back(std::move(b));
						}
						e.clear = rects.empty();
						e.id = (int)pics.size();
						pics.push_back(want ? std::move(rects) : std::vector<Bitmap>());
						events.push_back(e);
						lastSub = std::max(lastSub, e.start);
						if (e.start >= to) after = true;
					}
					avsubtitle_free(&s);
				}
			}
			av_packet_unref(pkt);
			// past the window: the next event ends the last cue, or LOOK seconds tell it goes on
			if (after || clockT > to + LOOK || t > to + LOOK) break;
			if (kept > ((size_t)64 << 20)) break;	// (pictures nobody could read in one window)
		}
		av_packet_free(&pkt);
		double readMb = f->pb ? double(avio_tell(f->pb) - pos0) / (1 << 20) : 0;

		// how far this answer covers: a deadline hit says up to where it read (the page asks on from there)
		double covered = to, known = std::max(clockT, lastSub);
		if (intr.hit) covered = std::max(from, std::min(to, known));
		std::vector<PicCue> cues = pictureCues(events, from, covered, after ? to + LOOK : known, eof);

		// the canvas the positions are in: PGS's presentation size, VobSub's from the .idx/CodecPrivate, else the video's
		int cw = cc->width, ch = cc->height;
		if (cw <= 0 || ch <= 0)
		{
			AVCodecParameters* vp = video >= 0 ? f->streams[video]->codecpar : nullptr;
			if (ss->codecpar->width > 0 && ss->codecpar->height > 0) cw = ss->codecpar->width, ch = ss->codecpar->height;
			else if (ss->codecpar->codec_id == AV_CODEC_ID_DVB_SUBTITLE || !vp || vp->width < maxX || vp->height < maxY)
				cw = std::max(720, maxX), ch = std::max(maxY <= 480 ? 480 : 576, maxY);
			else cw = vp->width, ch = vp->height;
		}

		JsonWriter w;
		w.obj().key("from").num(r3(from)).key("w").num(cw).key("h").num(ch).key("cues").arr();
		size_t body = 0;
		for (auto& c : cues)
		{
			auto& rects = pics[c.id];
			if (rects.empty() || rects[0].idx.empty()) continue;
			if (body > MAX_BODY)
			{
				covered = std::min(covered, c.start);	// the rest in the next answer
				break;
			}
			w.obj().key("s").num(r3(c.start)).key("e").num(r3(c.end)).key("r").arr();
			for (auto& b : rects)
			{
				std::string png = pngIndexed(b.w, b.h, b.idx.data(), b.w, b.pal, b.colors);
				if (png.empty()) continue;
				std::string b64 = base64Encode(png);
				body += b64.size();
				w.obj().key("x").num(b.x).key("y").num(b.y).key("w").num(b.w).key("h").num(b.h).key("png").str(b64).end();
			}
			w.end().end();
		}
		w.end().key("to").num(r3(covered)).key("eof").boolean(eof).end();
		a.body = w.out();
		double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
		long openMs = (long)std::chrono::duration_cast<std::chrono::milliseconds>(ts0 - t0).count();
		fprintf(stderr, "picsubs: %s stream %d %.0f-%.0f s: %zu cues, %zu KB in %.0f ms (open %ld, seek %ld; read %.1f MB)%s\n", path.c_str(), sub.stream, from, covered,
			cues.size(), a.body.size() >> 10, ms, openMs, seekMs, readMb, intr.hit ? " (deadline)" : "");
		if (!intr.hit) remember(key, a.body);
		return a;
	}
}
