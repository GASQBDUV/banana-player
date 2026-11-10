#include "durprobe.h"
#include "durations.h"
#include <algorithm>
#include <chrono>
#include <sys/stat.h>

extern "C"
{
#include <libavformat/avformat.h>
}

namespace bp
{
	namespace
	{
		using Clock = std::chrono::steady_clock;
		struct Deadline
		{
			Clock::time_point end;
			bool hit = false;
		};

		int interrupted(void* opaque)
		{
			auto* d = static_cast<Deadline*>(opaque);
			if (Clock::now() > d->end) d->hit = true;
			return d->hit;
		}

		double headerDuration(const AVFormatContext* f)
		{
			if (f->duration != AV_NOPTS_VALUE && f->duration > 0) return double(f->duration) / AV_TIME_BASE;
			double d = -1;
			// read_header alone fills the streams' own (an MP3's Xing frame, an MP4's moov) but not always the context's
			for (unsigned i = 0; i < f->nb_streams; i++)
			{
				const AVStream* s = f->streams[i];
				if (s->disposition & AV_DISPOSITION_ATTACHED_PIC) continue;
				if (s->duration != AV_NOPTS_VALUE && s->duration > 0 && s->time_base.num > 0 && s->time_base.den > 0)
					d = std::max(d, double(s->duration) * av_q2d(s->time_base));
			}
			return d;
		}

		// -1 unknown; deadline->hit tells a slow read from a file that has none
		double probe(const std::string& path, long long size, Deadline& dl)
		{
			AVFormatContext* ctx = avformat_alloc_context();
			if (!ctx) return -1;
			ctx->interrupt_callback.callback = interrupted;
			ctx->interrupt_callback.opaque = &dl;
			AVDictionary* opts = nullptr;
			av_dict_set(&opts, "probesize", "1048576", 0);	// (the format's detection only)
			int r = avformat_open_input(&ctx, path.c_str(), nullptr, &opts);
			av_dict_free(&opts);
			if (r < 0) return -1;	// (ctx freed by avformat_open_input)
			// the header's word only: never avformat_find_stream_info, which decodes and may read on (or to the end)
			// to guess one
			double d = saneDuration(headerDuration(ctx), size);
			if (d <= 0 && !dl.hit && ctx->iformat && ctx->iformat->name && std::string(ctx->iformat->name) == "mp3")
			{
				// an MP3 with no Xing/Info/VBRI frame: a CBR one's bitrate over the bytes from its first frame on, from the
				// first 16 KB or so (the demuxer's packets are the file's bytes as they come). VBR: none
				AVPacket* pkt = av_packet_alloc();
				std::string head;
				long long start = -1;
				while (pkt && head.size() < 16384 && av_read_frame(ctx, pkt) >= 0)
				{
					if (start < 0) start = pkt->pos;
					if (pkt->size > 0) head.append((const char*)pkt->data, pkt->size);
					av_packet_unref(pkt);
				}
				av_packet_free(&pkt);
				int bps = mp3CbrBitrate((const unsigned char*)head.data(), head.size());
				long long bytes = size - std::max<long long>(0, start);
				if (bps > 0 && start >= 0 && bytes > 0) d = saneDuration(double(bytes) * 8 / bps, size);
			}
			avformat_close_input(&ctx);
			return d;
		}
	}

	double probeDuration(const std::string& realPath, double* ms)
	{
		if (ms) *ms = 0;
		struct stat st;
		if (stat(realPath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return -1;
		std::string key = durationKey(realPath, (long long)st.st_size, (long long)st.st_mtime);
		DurationCache& c = DurationCache::shared();
		double secs;
		if (c.find(key, secs)) return secs;
		if (st.st_size <= 0 || !durationInHeader(realPath))
		{
			c.put(key, -1);
			return -1;
		}
		auto t0 = Clock::now();
		Deadline dl;
		dl.end = t0 + std::chrono::seconds(2);
		secs = probe(realPath, (long long)st.st_size, dl);
		if (ms) *ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
		// a probe cut short (a slow card, a busy disk) isn't the file's fault: asked again next time
		if (!dl.hit) c.put(key, secs);
		return secs;
	}
}
