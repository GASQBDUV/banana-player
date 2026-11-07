#pragma once
#include <string>

namespace bp
{
	// a PNG of w x h RGBA pixels (rows top to bottom, 4 bytes a pixel); false when it couldn't be written
	bool savePng(const std::string& path, int w, int h, const unsigned char* rgba);
}
