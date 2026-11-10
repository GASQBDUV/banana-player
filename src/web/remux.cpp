// Direct Stream: the video and one audio track copied into fragmented MP4 (see remux.h).
#include "web/remux.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
}

namespace bp
{
	namespace
	{
		// what a browser can play inside fMP4 (the page asks its own browser about the ones it offers)
		bool videoOk(AVCodecID c) { return c == AV_CODEC_ID_H264 || c == AV_CODEC_ID_HEVC || c == AV_CODEC_ID_VP9 || c == AV_CODEC_ID_AV1; }
		bool audioOk(AVCodecID c)
		{
			return c == AV_CODEC_ID_AAC || c == AV_CODEC_ID_MP3 || c == AV_CODEC_ID_OPUS || c == AV_CODEC_ID_FLAC || c == AV_CODEC_ID_AC3 ||
				c == AV_CODEC_ID_EAC3;
		}

		struct Interrupt
		{
			std::chrono::steady_clock::time_point openBy;
			bool opening = true;
			const std::function<bool()>* cancelled = nullptr;
		};
		int interrupted(void* o)
		{
			auto* in = static_cast<Interrupt*>(o);
			if (in->opening && std::chrono::steady_clock::now() > in->openBy) return 1;
			return in->cancelled && *in->cancelled && (*in->cancelled)();
		}

		// the input, its video stream and the chosen audio one, seeked to the keyframe at or before t
		struct Input
		{
			AVFormatContext* ic = nullptr;
			Interrupt intr;
			int v = -1, a = -1;
			double startTime = 0;	// the container's first timestamp: a browser counts from it
			~Input() { if (ic) avformat_close_input(&ic); }

			bool open(const std::string& path, int audioStream, std::string& why)
			{
				intr.openBy = std::chrono::steady_clock::now() + std::chrono::seconds(10);
				ic = avformat_alloc_context();
				if (!ic) return why = "out of memory", false;
				ic->interrupt_callback.callback = interrupted;
				ic->interrupt_callback.opaque = &intr;
				if (avformat_open_input(&ic, path.c_str(), nullptr, nullptr) < 0)
				{
					ic = nullptr;
					return why = "Can't open the file", false;
				}
				for (unsigned i = 0; i < ic->nb_streams; i++)
				{
					AVStream* s = ic->streams[i];
					if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !(s->disposition & AV_DISPOSITION_ATTACHED_PIC) && v < 0) v = (int)i;
				}
				if (v < 0) return why = "No video in this file", false;
				if (audioStream < 0 || audioStream >= (int)ic->nb_streams || ic->streams[audioStream]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
					return why = "No such audio track", false;
				a = audioStream;
				if (!videoOk(ic->streams[v]->codecpar->codec_id)) return why = "This video's format can't be streamed to a browser", false;
				if (!audioOk(ic->streams[a]->codecpar->codec_id)) return why = "Plays on the handheld only", false;
				AVCodecParameters* vp = ic->streams[v]->codecpar;
				AVCodecParameters* ap = ic->streams[a]->codecpar;
				// the headers usually say it all (mkv, mp4); a transport stream needs a look at the packets
				if (vp->width <= 0 || vp->height <= 0 || ap->sample_rate <= 0 || ap->ch_layout.nb_channels <= 0)
				{
					for (unsigned i = 0; i < ic->nb_streams; i++)
						if ((int)i != v && (int)i != a) ic->streams[i]->discard = AVDISCARD_ALL;
					if (avformat_find_stream_info(ic, nullptr) < 0) return why = "Can't read the file", false;
				}
				for (unsigned i = 0; i < ic->nb_streams; i++)
					if ((int)i != v && (int)i != a) ic->streams[i]->discard = AVDISCARD_ALL;
				if (ic->start_time != AV_NOPTS_VALUE) startTime = double(ic->start_time) / AV_TIME_BASE;
				intr.opening = false;
				return true;
			}

			void seek(double t)
			{
				if (t <= 0.05) return;
				AVStream* s = ic->streams[v];
				int64_t ts = (int64_t)std::llround((t + startTime) / av_q2d(s->time_base));
				if (av_seek_frame(ic, v, ts, AVSEEK_FLAG_BACKWARD) < 0) av_seek_frame(ic, -1, (int64_t)((t + startTime) * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
			}
		};

		int64_t tsOf(const AVPacket* p) { return p->dts != AV_NOPTS_VALUE ? p->dts : p->pts; }

		struct Sink
		{
			const std::function<bool(const uint8_t*, size_t)>* write;
			bool failed = false;
		};
#if LIBAVFORMAT_VERSION_MAJOR < 61
		int writeOut(void* o, uint8_t* buf, int n)
#else
		int writeOut(void* o, const uint8_t* buf, int n)
#endif
		{
			auto* s = static_cast<Sink*>(o);
			if (s->failed || !(*s->write)(buf, (size_t)n))
			{
				s->failed = true;
				return AVERROR(EPIPE);
			}
			return n;
		}

		double cpuSeconds()
		{
			timespec ts{};
			clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
			return ts.tv_sec + ts.tv_nsec / 1e9;
		}

		std::string baseName(const std::string& p)
		{
			size_t i = p.rfind('/');
			return i == std::string::npos ? p : p.substr(i + 1);
		}
	}

	bool remuxStart(const std::string& real, int audioStream, double t, double& start, std::string& why)
	{
		Input in;
		if (!in.open(real, audioStream, why)) return false;
		in.seek(t);
		AVPacket* pkt = av_packet_alloc();
		bool found = false;
		// the first keyframe of the video from there (a few seconds of reading at most)
		for (int n = 0; n < 4000 && !found && av_read_frame(in.ic, pkt) >= 0; n++)
		{
			if (pkt->stream_index == in.v && (pkt->flags & AV_PKT_FLAG_KEY) && tsOf(pkt) != AV_NOPTS_VALUE)
			{
				start = std::max(0.0, tsOf(pkt) * av_q2d(in.ic->streams[in.v]->time_base) - in.startTime);
				found = true;
			}
			av_packet_unref(pkt);
		}
		av_packet_free(&pkt);
		if (!found) why = "Can't find a starting point there";
		return found;
	}

	bool remuxRun(const std::string& real, int audioStream, double t, const std::function<bool(const uint8_t*, size_t)>& write,
		const std::function<bool()>& cancelled, std::string& err)
	{
		double cpu0 = cpuSeconds();
		auto wall0 = std::chrono::steady_clock::now();
		Input in;
		in.intr.cancelled = &cancelled;
		if (!in.open(real, audioStream, err)) return false;
		in.seek(t);
		AVStream* iv = in.ic->streams[in.v];
		AVStream* ia = in.ic->streams[in.a];

		AVFormatContext* oc = nullptr;
		if (avformat_alloc_output_context2(&oc, nullptr, "mp4", nullptr) < 0 || !oc)
		{
			err = "No MP4 muxer in this build";
			return false;
		}
		struct OcFree
		{
			AVFormatContext* oc;
			~OcFree()
			{
				if (oc->pb)
				{
					av_freep(&oc->pb->buffer);
					avio_context_free(&oc->pb);
				}
				avformat_free_context(oc);
			}
		} ocFree{oc};
		// opus and flac in MP4 are still "experimental" to older FFmpegs
		oc->strict_std_compliance = FF_COMPLIANCE_EXPERIMENTAL;
		AVStream* ov = avformat_new_stream(oc, nullptr);
		AVStream* oa = avformat_new_stream(oc, nullptr);
		if (!ov || !oa || avcodec_parameters_copy(ov->codecpar, iv->codecpar) < 0 || avcodec_parameters_copy(oa->codecpar, ia->codecpar) < 0)
		{
			err = "out of memory";
			return false;
		}
		// Safari plays HEVC in MP4 only as hvc1
		ov->codecpar->codec_tag = iv->codecpar->codec_id == AV_CODEC_ID_HEVC ? MKTAG('h', 'v', 'c', '1') : 0;
		oa->codecpar->codec_tag = 0;
		ov->time_base = iv->time_base;
		oa->time_base = ia->time_base;
		if (ia->metadata) av_dict_copy(&oa->metadata, ia->metadata, 0);

		// ADTS AAC (a transport stream's) has to become MP4's AudioSpecificConfig
		AVBSFContext* bsf = nullptr;
		if (ia->codecpar->codec_id == AV_CODEC_ID_AAC && ia->codecpar->extradata_size == 0)
		{
			const AVBitStreamFilter* f = av_bsf_get_by_name("aac_adtstoasc");
			if (f && av_bsf_alloc(f, &bsf) == 0)
			{
				avcodec_parameters_copy(bsf->par_in, ia->codecpar);
				bsf->time_base_in = ia->time_base;
				if (av_bsf_init(bsf) < 0) av_bsf_free(&bsf);
			}
		}
		struct BsfFree { AVBSFContext*& b; ~BsfFree() { av_bsf_free(&b); } } bsfFree{bsf};

		Sink sink{&write};
		const int BUF = 64 * 1024;
		auto* buf = (unsigned char*)av_malloc(BUF);
		oc->pb = avio_alloc_context(buf, BUF, 1, &sink, nullptr, writeOut, nullptr);
		if (!oc->pb)
		{
			av_free(buf);
			err = "out of memory";
			return false;
		}
		oc->flags |= AVFMT_FLAG_CUSTOM_IO;
		AVDictionary* opts = nullptr;
		// fragments at keyframes, and at least every 2 s so the sound flows with a long GOP
		av_dict_set(&opts, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
		av_dict_set(&opts, "frag_duration", "2000000", 0);
		int r = avformat_write_header(oc, &opts);
		av_dict_free(&opts);
		if (r < 0)
		{
			err = sink.failed ? "client gone" : "Can't stream this file";
			return false;
		}

		AVPacket* pkt = av_packet_alloc();
		int64_t vOff = AV_NOPTS_VALUE, aOff = 0;
		bool ok = true;
		double lastT = 0;
		auto writePkt = [&](AVPacket* p, AVStream* is, AVStream* os, int64_t off) {
			if (p->pts != AV_NOPTS_VALUE) p->pts -= off;
			if (p->dts != AV_NOPTS_VALUE) p->dts -= off;
			av_packet_rescale_ts(p, is->time_base, os->time_base);
			p->stream_index = os->index;
			p->pos = -1;
			return av_interleaved_write_frame(oc, p) >= 0 || !sink.failed;	// a bad packet is skipped; a gone client ends it
		};
		while (ok && !cancelled())
		{
			r = av_read_frame(in.ic, pkt);
			if (r < 0) break;
			if (pkt->stream_index == in.v)
			{
				if (vOff == AV_NOPTS_VALUE)
				{
					// nothing before the first keyframe: it's where remuxStart said it starts
					if (!(pkt->flags & AV_PKT_FLAG_KEY) || tsOf(pkt) == AV_NOPTS_VALUE)
					{
						av_packet_unref(pkt);
						continue;
					}
					vOff = tsOf(pkt);
					aOff = av_rescale_q(vOff, iv->time_base, ia->time_base);
				}
				if (tsOf(pkt) != AV_NOPTS_VALUE) lastT = std::max(lastT, tsOf(pkt) * av_q2d(iv->time_base));
				ok = writePkt(pkt, iv, ov, vOff);
			}
			else if (pkt->stream_index == in.a && vOff != AV_NOPTS_VALUE && (pkt->pts == AV_NOPTS_VALUE || pkt->pts >= aOff))
			{
				if (bsf)
				{
					if (av_bsf_send_packet(bsf, pkt) == 0)
						while (ok && av_bsf_receive_packet(bsf, pkt) == 0) ok = writePkt(pkt, ia, oa, aOff);
				}
				else ok = writePkt(pkt, ia, oa, aOff);
			}
			av_packet_unref(pkt);
		}
		av_packet_free(&pkt);
		if (!sink.failed)
		{
			av_write_trailer(oc);
			avio_flush(oc->pb);
		}
		double cpu = cpuSeconds() - cpu0;
		double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall0).count();
		fprintf(stderr, "remux: %s audio stream %d from %.1f: to %.1f, %.2f s CPU in %.0f s (%s)\n", baseName(real).c_str(), audioStream, t,
			lastT - in.startTime, cpu, wall, cancelled() ? "replaced" : sink.failed ? "client gone" : "ended");
		return !sink.failed;
	}
}
