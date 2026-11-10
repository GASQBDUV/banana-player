// Picture subtitles (VobSub, PGS, DVB) for the web dashboard's player: the timing rules, apart from the decoding
// (src/web/picsubs.cpp) so they can be tested. A picture subtitle replaces the one before it: PGS has no end times at
// all (an empty event clears the screen), VobSub's stop time is sometimes missing.
#pragma once
#include <string>
#include <vector>

namespace bp
{
	// one decoded event, in file seconds. end < 0: none given; clear: it draws nothing (PGS's end of the one before)
	struct PicEvent { double start = 0, end = -1; bool clear = false; int id = -1; };
	struct PicCue { double start = 0, end = 0; int id = -1; };

	// the cues that show during [from, to): each ends at its own end, the next event's start or, with neither,
	// at `knownUntil` (where reading stopped: a later window says more) or 5 s on at the end of the file. A cue never
	// lasts over `maxShow` seconds. Sorted by start; for events at the same time the last one read wins
	std::vector<PicCue> pictureCues(std::vector<PicEvent> events, double from, double to, double knownUntil, bool eof, double maxShow = 60);

	// the window a request may ask for: from in [0, 1e6], to after it, at most `maxLen` seconds. false: nonsense
	bool pictureWindow(double from, double to, double maxLen, double& outFrom, double& outTo);
}
