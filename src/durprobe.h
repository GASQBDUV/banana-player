// A local media file's length through libavformat, for the file lists. Runs on whatever thread calls it (never the
// main one: up to the deadline per file on a file that makes libavformat dig).
#pragma once
#include <string>

namespace bp
{
	// seconds, or -1: not a regular file, empty, a kind with no length in its header (durationInHeader: not even opened),
	// not media, corrupt, or no answer within 2 s. The header's length first;
	// only a file whose header has none (an MP3 without a Xing header, MPEG-TS) is read further, a little (512 KB at
	// most). Answers (-1 too) go into DurationCache::shared(), by path + size + mtime; a cached one is returned at once.
	// ms, when given: how long the probe took (0 for a cached answer)
	double probeDuration(const std::string& realPath, double* ms = nullptr);
}
