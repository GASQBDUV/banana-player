// Media durations for the file lists (the bottom panel's rows, the dashboard's Up next and Files): one cache shared by
// the handheld's background prober (durworker) and the web server's pool threads, both probing through durprobe.
#pragma once
#include <cstddef>
#include <list>
#include <mutex>
#include <string>
#include <unordered_map>

namespace bp
{
	// a file as it is now: a file replaced or rewritten (another size or mtime) is another key, so nothing goes stale
	std::string durationKey(const std::string& realPath, long long size, long long mtime);

	// seconds by key; -1 is a known "no duration" (unreadable, corrupt, not media), kept so it's never probed again
	class DurationCache
	{
	public:
		explicit DurationCache(size_t cap = 5000) : cap(cap) {}
		bool find(const std::string& key, double& secs);
		void put(const std::string& key, double secs);
		size_t size();
		// the one the player and the server share (never destroyed: a probe may still run at exit)
		static DurationCache& shared();

	private:
		using Order = std::list<std::pair<std::string, double>>;	// most recently used first
		size_t cap;
		std::mutex m;
		Order order;
		std::unordered_map<std::string, Order::iterator> index;
	};

	// a probe's answer made safe: NaN, negative, zero or absurd -> -1. Absurd: over a week, or (size >= 0) under 64 bits
	// a second over the file's bytes, as a truncated file whose header still claims the whole length gives
	double saneDuration(double secs, long long size = -1);

	// a file row's note: its size, with its length when known and where it was left ("at 12:03 / 1:45:00 · 1.2 GB",
	// "3:34 · 3.5 MB", "at 0:42 · 13.9 MB"); size < 0 leaves the size out, duration <= 0 the length
	std::string fileNote(const std::string& at, double duration, long long size);

	// false for the kinds whose headers carry no length: MPEG transport and program streams (.ts .m2ts .mts .m2t .tp
	// .trp .mpg .mpeg .vob .m2v) and raw streams with no container (.aac .ac3 .eac3 .dts .h264 .264 .hevc .h265 .265).
	// Theirs would take reading through the file (or a guess from the bitrate), so they're never probed: no length shown
	bool durationInHeader(const std::string& name);

	// an MPEG audio Layer III frame header's bitrate (bits a second), 0 when the 4 bytes aren't one
	int mp3FrameBitrate(const unsigned char* p, size_t n);
	// An MP3 with no Xing/Info/VBRI frame has no length in its header; a CBR one's is its bytes over its bitrate. Walks
	// the frames from p (the first frame's start): their one bitrate when at least 8 whole frames agree, 0 when they
	// don't (VBR: no length) or there aren't enough
	int mp3CbrBitrate(const unsigned char* p, size_t n);

	// a local file's cached duration (stat()s it: size and mtime make the key); false: not cached (or not a file)
	bool cachedDuration(const std::string& realPath, double& secs, long long* size = nullptr);
}
