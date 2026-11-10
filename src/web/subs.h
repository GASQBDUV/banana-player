// Subtitles for the web dashboard's player: the tracks a file has (embedded, and the files beside it) and their text as
// WebVTT. Embedded ones need the whole file read, so that runs on a worker thread, one at a time, and lands in a cache
// folder (LRU, 50 MB); the request answers 202 with the progress until it's there. Runs on server pool threads.
#pragma once
#include "web/probe.h"
#include <string>
#include <vector>

namespace bp
{
	struct SubEntry
	{
		int i = 0;						// what /api/subs takes: the embedded ones in stream order, then the files beside it
		std::string label, lang, codec;	// label as the handheld's trackLabel; lang as stored ("eng"), "" when unknown
		std::string kind;				// "text" | "image" (picture subtitles: the handheld only)
		bool external = false, isDefault = false, forced = false;
		bool styled = false;			// ASS/SSA: the browser can draw it with libass (JASSUB) in its own look
		int stream = -1;				// embedded: libavformat's stream index
		std::string file;				// external: the real path
	};

	// the file's audio tracks, labelled as the handheld's menu does
	struct AudioEntry { int i = 0; std::string label, codec, lang; int channels = 0, stream = -1; bool isDefault = false; };
	std::vector<AudioEntry> listAudio(const MediaInfo& m);
	std::vector<SubEntry> listSubtitles(const std::string& real, const MediaInfo& m);

	// where cached WebVTT goes: <dir>/subs/. Call with <stateDir>/webcache at start; env BP_WEB_CACHE_DIR overrides;
	// unset, it is <XDG_CONFIG_HOME or the player's default state dir>/banana-player/webcache
	void setSubsCacheDir(const std::string& dir);

	struct SubsAnswer
	{
		int status = 200;		// 200 body is the VTT; 202 body is {"progress":0.4}; 415 picture subtitles; 5xx failed
		std::string body, type;	// type: the content type
	};
	SubsAnswer subtitleVtt(const std::string& real, const SubEntry& sub);
	// an ASS/SSA track as the whole script (header, styles, events) for JASSUB: an external file as it is, an embedded
	// track read out of the file once (202 meanwhile, as subtitleVtt) and cached as .ass
	SubsAnswer subtitleAss(const std::string& real, const SubEntry& sub);

	// the fonts a file carries (Matroska attachments) for its ASS subtitles; i is libavformat's stream index
	struct FontAttachment { int i = 0; std::string name, mime; size_t size = 0; };
	std::vector<FontAttachment> listFonts(const std::string& real);
	bool fontAttachment(const std::string& real, int i, std::string& bytes, std::string& mime);
}
