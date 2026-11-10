#include "durations.h"
#include "format.h"
#include <cctype>
#include <cmath>
#include <sys/stat.h>

namespace bp
{
	std::string durationKey(const std::string& realPath, long long size, long long mtime)
	{
		return realPath + '\n' + std::to_string(size) + '\n' + std::to_string(mtime);
	}

	bool DurationCache::find(const std::string& key, double& secs)
	{
		std::lock_guard<std::mutex> g(m);
		auto it = index.find(key);
		if (it == index.end()) return false;
		order.splice(order.begin(), order, it->second);
		secs = it->second->second;
		return true;
	}

	void DurationCache::put(const std::string& key, double secs)
	{
		std::lock_guard<std::mutex> g(m);
		auto it = index.find(key);
		if (it != index.end())
		{
			it->second->second = secs;
			order.splice(order.begin(), order, it->second);
			return;
		}
		order.emplace_front(key, secs);
		index[key] = order.begin();
		while (order.size() > cap && !order.empty())
		{
			index.erase(order.back().first);
			order.pop_back();
		}
	}

	size_t DurationCache::size()
	{
		std::lock_guard<std::mutex> g(m);
		return order.size();
	}

	DurationCache& DurationCache::shared()
	{
		static DurationCache* c = new DurationCache(5000);
		return *c;
	}

	double saneDuration(double secs, long long size)
	{
		// a week: longer is a header that lies, not a recording
		if (!std::isfinite(secs) || secs <= 0 || secs >= 7 * 24 * 3600.0) return -1;
		if (size >= 0 && double(size) * 8 / secs < 64) return -1;
		return secs;
	}

	std::string fileNote(const std::string& at, double duration, long long size)
	{
		static const std::string DOT = " \xC2\xB7 ";
		std::string len = duration > 0 ? formatTime(duration) : "";
		std::string head = at.empty() ? len : len.empty() ? at : at + " / " + len;
		std::string sz = size >= 0 ? formatBytes(size) : "";
		return head.empty() ? sz : sz.empty() ? head : head + DOT + sz;
	}

	bool durationInHeader(const std::string& name)
	{
		size_t dot = name.rfind('.');
		if (dot == std::string::npos) return true;
		std::string e = name.substr(dot + 1);
		for (auto& c : e) c = (char)tolower((unsigned char)c);
		for (const char* x : {"ts", "m2ts", "mts", "m2t", "tp", "trp", "mpg", "mpeg", "vob", "m2v",
				 "aac", "ac3", "eac3", "dts", "h264", "264", "hevc", "h265", "265"})
			if (e == x) return false;
		return true;
	}

	int mp3FrameBitrate(const unsigned char* p, size_t n)
	{
		if (n < 4 || p[0] != 0xff || (p[1] & 0xe0) != 0xe0) return 0;
		int version = (p[1] >> 3) & 3, layer = (p[1] >> 1) & 3, index = p[2] >> 4, rate = (p[2] >> 2) & 3;
		// version 1 reserved; layer 1 is Layer III; index 0 "free", 15 bad; sample rate 3 reserved
		if (version == 1 || layer != 1 || index == 0 || index == 15 || rate == 3) return 0;
		static const int MPEG1[] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
		static const int MPEG2[] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};	// (and 2.5)
		return (version == 3 ? MPEG1 : MPEG2)[index] * 1000;
	}

	int mp3CbrBitrate(const unsigned char* p, size_t n)
	{
		int rate = 0, frames = 0;
		size_t at = 0;
		while (at + 4 <= n)
		{
			int bps = mp3FrameBitrate(p + at, n - at);
			if (!bps || (rate && bps != rate)) return 0;
			rate = bps;
			int version = (p[at + 1] >> 3) & 3, sr = (p[at + 2] >> 2) & 3, pad = (p[at + 2] >> 1) & 1;
			static const int RATES[] = {44100, 48000, 32000};
			int hz = RATES[sr] >> (version == 3 ? 0 : version == 2 ? 1 : 2);
			size_t len = (size_t)((version == 3 ? 144 : 72) * (long long)bps / hz + pad);
			if (len < 4 || at + len > n) break;	// (the last, cut by the read)
			at += len;
			frames++;
		}
		return frames >= 8 ? rate : 0;
	}

	bool cachedDuration(const std::string& realPath, double& secs, long long* size)
	{
		struct stat st;
		if (stat(realPath.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
		if (size) *size = (long long)st.st_size;
		return DurationCache::shared().find(durationKey(realPath, (long long)st.st_size, (long long)st.st_mtime), secs);
	}
}
