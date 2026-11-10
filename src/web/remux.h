// Direct Stream for the web dashboard's player: the file's video and one chosen audio track, copied (no decoding, no
// encoding) into a fragmented MP4 from a time on, for a browser that can't switch audio tracks itself. Runs on the
// server thread that answers the request, never touches Player or Ui.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace bp
{
	// where a stream asked for at t starts (the keyframe at or before it), in seconds; false: it can't be remuxed
	// (why: shown to the user as is)
	bool remuxStart(const std::string& real, int audioStream, double t, double& start, std::string& why);

	// writes the stream to `write` (false from it: the client went) until the file ends, write fails or `cancelled`
	// says so. Timestamps start at 0 at remuxStart's keyframe. Logs "remux: <file> audio <n> from <t>" with its CPU time
	bool remuxRun(const std::string& real, int audioStream, double t, const std::function<bool(const uint8_t*, size_t)>& write,
		const std::function<bool()>& cancelled, std::string& err);
}
