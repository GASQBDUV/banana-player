// libavformat probe, chapters and cover art for the web dashboard. Runs on the calling (server pool) thread, never
// the main one: opens the file with a small probesize, no find_stream_info and a 2 s limit.
#pragma once
#include <string>
#include <vector>

namespace bp
{
	struct MediaChapter { std::string title; double start = 0; };

	// an audio or subtitle stream of the file, from the headers
	struct MediaStream
	{
		int index = 0;					// libavformat's stream index
		std::string lang, title, codec;	// lang: the tag as stored ("eng"); codec: avcodec's name
		int channels = 0;
		bool isDefault = false, forced = false;
	};

	struct MediaInfo
	{
		std::string container, vcodec, acodec;	// libavformat's / avcodec's names; "" for no such stream
		double duration = 0;					// seconds; 0 when the container doesn't say
		std::string mime, playable;				// playable: "yes" | "maybe" | "no"
		std::vector<MediaChapter> chapters;
		std::string title, artist, album;
		bool hasCover = false;
		std::string year, genre, track, disc, albumArtist, composer;	// tags; "" when absent
		long long size = 0;						// bytes
		long long bitrate = 0;					// bps; 0 unknown
		int sampleRate = 0, channels = 0;		// the default audio stream's; 0 unknown
		std::string channelLayout;				// "stereo", "5.1"...
		long long audioBitrate = 0;				// bps; 0 unknown
		int width = 0, height = 0;				// the first (non-cover) video stream's
		double fps = 0;							// 0 unknown
		long long videoBitrate = 0;
		std::string pixFmt;
		std::vector<MediaStream> audio, subs;	// acodec is the default audio stream's (else the first's)
	};

	// false: it couldn't be opened (not a media file, or it took over 2 s). Cached (256 entries, by path + mtime + size)
	bool probeMedia(const std::string& realPath, MediaInfo& out);

	// the picture to show for a file: the attached picture inside it, else cover/folder/front .jpg/.jpeg/.png/.webp beside
	// it. false: none
	bool probeCover(const std::string& realPath, std::string& bytes, std::string& mime);
}
