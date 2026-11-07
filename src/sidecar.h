// What mpv would pick up next to a file by itself, for folders it can't list (network ones).
#pragma once
#include <string>
#include <vector>

namespace bp
{
	// The names in the folder that are subtitles for the video (mpv's sub-auto=fuzzy).
	std::vector<std::string> subtitleFilesFor(const std::string& videoName, const std::vector<std::string>& folderNames);
	// The folder's cover picture (mpv's cover-art-auto names), "" if none.
	std::string coverFileFor(const std::vector<std::string>& folderNames);
}
