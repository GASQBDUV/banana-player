#pragma once
#include <cstdint>
#include <ctime>
#include <string>

namespace bp
{
	// The built-in updater's parts with no network, files or threads (updater.* has those): GitHub's answer for the
	// latest release, versions, SHA-256, "not until tomorrow".

	// A release, from GitHub's /releases/latest JSON: its version (the tag without its "v"), the package's zip
	// (BananaPlayer-<version>-aarch64.zip) and its .sha256 beside it
	struct Release
	{
		std::string version, zipUrl, shaUrl;
		int64_t zipSize = 0;
	};
	// false: not a release with the package (no tag, no zip)
	bool parseRelease(const std::string& json, Release& out);

	// "0.2.10" after "0.2.9"; a leading "v" left out; < 0, 0, > 0
	int compareVersions(const std::string& a, const std::string& b);

	// lowercase hex
	std::string sha256Hex(const std::string& data);
	// a file's, read in pieces; "" when it can't be read
	std::string sha256File(const std::string& path);
	// the hex digest at the start of a .sha256 file ("<64 hex>  name"), lowercase; "" when there's none
	std::string parseSha256File(const std::string& text);

	// the start of the next day, local time: "not until tomorrow"
	time_t nextMidnight(time_t now);
}
