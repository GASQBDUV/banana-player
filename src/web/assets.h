// The web dashboard's files, compiled in by cmake/EmbedWeb.cmake (which writes web_assets.cpp in the build folder).
#pragma once

#include <cstddef>

namespace bp {

struct WebAsset {
	const char* path;            // relative to web/: "index.html", "vendor/Sortable.min.js"; fonts as "fonts/..."
	const char* mime;
	const unsigned char* data;
	size_t len;
	const char* etag;            // a short hash of the content, unquoted: the server sends it in quotes
};

extern const WebAsset WEB_ASSETS[];
extern const size_t WEB_ASSET_COUNT;

}  // namespace bp
