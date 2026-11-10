#include "picsubs.h"
#include <algorithm>
#include <cmath>

namespace bp
{
	std::vector<PicCue> pictureCues(std::vector<PicEvent> ev, double from, double to, double knownUntil, bool eof, double maxShow)
	{
		std::stable_sort(ev.begin(), ev.end(), [](const PicEvent& a, const PicEvent& b) { return a.start < b.start; });
		std::vector<PicCue> out;
		for (size_t i = 0; i < ev.size(); i++)
		{
			const PicEvent& e = ev[i];
			if (e.clear || !std::isfinite(e.start)) continue;
			// the next event at a later time replaces this one; one at the same time replaces it outright
			size_t j = i + 1;
			if (j < ev.size() && ev[j].start <= e.start) continue;
			double end = e.end > e.start ? e.end : -1;
			if (j < ev.size()) end = end < 0 ? ev[j].start : std::min(end, ev[j].start);
			if (end < 0) end = eof ? e.start + 5 : std::max(knownUntil, e.start + 0.5);
			end = std::min(end, e.start + maxShow);
			if (end <= from || e.start >= to) continue;
			out.push_back({e.start, end, e.id});
		}
		return out;
	}

	bool pictureWindow(double from, double to, double maxLen, double& f, double& t)
	{
		if (!std::isfinite(from) || !std::isfinite(to) || !(maxLen > 0)) return false;
		if (from < 0) from = 0;
		if (from > 1e6 || to <= from) return false;
		f = from;
		t = std::min(to, from + maxLen);
		return true;
	}
}
