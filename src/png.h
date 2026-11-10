#pragma once
#include <cstdint>
#include <string>

namespace bp
{
	// a PNG of w x h RGBA pixels (rows top to bottom, 4 bytes a pixel); false when it couldn't be written
	bool savePng(const std::string& path, int w, int h, const unsigned char* rgba);
	// a paletted PNG in memory: w x h palette indices (rows `stride` bytes apart) and up to 256 colours as 0xAARRGGBB
	// (libavcodec's subtitle palettes); "" when it can't
	std::string pngIndexed(int w, int h, const uint8_t* idx, int stride, const uint32_t* argb, int colors);
}
