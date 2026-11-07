#pragma once
#include <cstdint>
#include <mpv/client.h>
#include <string>
#include <vector>

namespace bp
{
	enum class TrackType { Video, Audio, Sub, Other };

	// one entry of mpv's track-list
	struct Track
	{
		int64_t id = 0;
		TrackType type = TrackType::Other;
		std::string title, lang, codec;
		int channels = 0;
		bool selected = false, isDefault = false, forced = false, external = false, albumArt = false;
	};

	std::vector<Track> parseTrackList(const mpv_node* list);

	// "English · AC3 5.1 · Director's commentary"; n (1-based, within its type) names a track with nothing else to
	// tell it by: "Track 2 · AAC Stereo"
	std::string trackLabel(const Track& t, int n);

	// ISO 639-1/-2 codes the menu knows -> their names; anything else comes back as it is
	std::string languageName(const std::string& code);

	// "subrip" -> "SRT", "hdmv_pgs_subtitle" -> "PGS", "eac3" -> "E-AC3"...
	std::string codecName(const std::string& codec);
}
