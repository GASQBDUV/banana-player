#include "sidecar.h"
#include <algorithm>
#include <cctype>

namespace bp
{
	static std::string lower(std::string s)
	{
		std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
		return s;
	}

	static void split(const std::string& name, std::string& base, std::string& ext)
	{
		size_t d = name.rfind('.');
		if (d == std::string::npos) { base = name; ext = ""; return; }
		base = name.substr(0, d);
		ext = lower(name.substr(d + 1));
	}

	static bool oneOf(const std::string& s, std::initializer_list<const char*> l)
	{
		for (auto x : l) if (s == x) return true;
		return false;
	}

	std::vector<std::string> subtitleFilesFor(const std::string& videoName, const std::vector<std::string>& names)
	{
		std::vector<std::string> out;
		std::string vbase, vext;
		split(videoName, vbase, vext);
		vbase = lower(vbase);
		if (vbase.empty()) return out;
		for (auto& n : names)
		{
			std::string base, ext;
			split(n, base, ext);
			if (!oneOf(ext, {"srt", "ass", "ssa", "vtt", "sub", "idx", "sup"})) continue;
			if (lower(base).find(vbase) != std::string::npos) out.push_back(n);
		}
		return out;
	}

	std::string coverFileFor(const std::vector<std::string>& names)
	{
		static const char* BASES[] = {"albumart", "album", "cover", "front", "albumartsmall", "folder", ".folder", "thumb"};
		for (auto b : BASES)
			for (auto& n : names)
			{
				std::string base, ext;
				split(n, base, ext);
				if (lower(base) == b && oneOf(ext, {"jpg", "jpeg", "png", "webp", "gif", "bmp"})) return n;
			}
		return "";
	}
}
