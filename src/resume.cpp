#include "resume.h"
#include <cstdlib>
#include <sstream>

namespace bp
{
	// resume.tsv: "position<TAB>aid<TAB>sid<TAB>path" a line (the path last: it may hold a tab), and "#last-audio",
	// "#last-sub" lines for the languages
	void ResumeStore::parse(const std::string& text)
	{
		entries.clear();
		std::istringstream in(text);
		std::string line;
		while (std::getline(in, line))
		{
			if (line.rfind("#last-audio\t", 0) == 0) { lastAudioLang = line.substr(12); continue; }
			if (line.rfind("#last-folder\t", 0) == 0) { lastFolder = line.substr(13); continue; }
			if (line.rfind("#last-sub\t", 0) == 0)
			{
				std::string v = line.substr(10);
				lastSubOff = v == "-";
				lastSubLang = lastSubOff ? "" : v;
				continue;
			}
			if (line.empty() || line[0] == '#') continue;
			size_t t1 = line.find('\t'), t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1),
				   t3 = t2 == std::string::npos ? t2 : line.find('\t', t2 + 1);
			if (t3 == std::string::npos) continue;
			ResumeEntry e;
			e.position = atof(line.substr(0, t1).c_str());
			e.aid = atoll(line.substr(t1 + 1, t2 - t1 - 1).c_str());
			e.sid = atoll(line.substr(t2 + 1, t3 - t2 - 1).c_str());
			e.path = line.substr(t3 + 1);
			if (!e.path.empty() && entries.size() < MAX_ENTRIES && !find(e.path)) entries.push_back(e);
		}
	}

	std::string ResumeStore::serialize() const
	{
		std::string out = "# banana-player: where each video was left (position, audio track, subtitle track, file)\n";
		if (!lastAudioLang.empty()) out += "#last-audio\t" + lastAudioLang + "\n";
		if (!lastFolder.empty()) out += "#last-folder\t" + lastFolder + "\n";
		if (lastSubOff) out += "#last-sub\t-\n";
		else if (!lastSubLang.empty()) out += "#last-sub\t" + lastSubLang + "\n";
		for (auto& e : entries)
		{
			char buf[64];
			snprintf(buf, sizeof buf, "%.3f\t%lld\t%lld\t", e.position, (long long)e.aid, (long long)e.sid);
			out += buf + e.path + "\n";
		}
		return out;
	}

	const ResumeEntry* ResumeStore::find(const std::string& path) const
	{
		for (auto& e : entries)
			if (e.path == path) return &e;
		return nullptr;
	}

	void ResumeStore::put(const ResumeEntry& e)
	{
		for (size_t i = 0; i < entries.size(); i++)
			if (entries[i].path == e.path)
			{
				entries.erase(entries.begin() + i);
				break;
			}
		entries.insert(entries.begin(), e);
		if (entries.size() > MAX_ENTRIES) entries.resize(MAX_ENTRIES);
	}

	bool worthResuming(double position, double duration)
	{
		if (position < 5) return false;
		if (duration > 0 && (position > duration - 15 || position > duration * 0.97)) return false;
		return true;
	}
}
