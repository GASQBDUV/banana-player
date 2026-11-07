#pragma once
#include <string>
#include <vector>

namespace bp
{
	// by its extension (any case): mp4, mkv, webm, avi, mov, ts...
	bool isVideoFile(const std::string& name);
	// mp3, flac, ogg, wav, m4a, opus...
	bool isAudioFile(const std::string& name);
	// what the player plays: any of them, photos too (photo.h)
	bool isMediaFile(const std::string& name);

	// "ep2" before "ep10"; letters compared without case
	bool naturalLess(const std::string& a, const std::string& b);

	// the videos and audio files in the file's folder (for a photo: the photos), in natural order, as full paths, and
	// where the file is among them; a file that can't be listed (or isn't one by its name) is a playlist of its own
	std::vector<std::string> siblingVideos(const std::string& path, int& index);
}
