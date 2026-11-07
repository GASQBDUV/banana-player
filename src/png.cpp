#include "png.h"
#include <cstdio>
#include <vector>
#include <zlib.h>

namespace bp
{
	static void put32(std::vector<unsigned char>& v, unsigned x)
	{
		for (int s = 24; s >= 0; s -= 8) v.push_back((unsigned char)(x >> s));
	}

	static void chunk(FILE* f, const char* type, const std::vector<unsigned char>& data)
	{
		std::vector<unsigned char> c;
		put32(c, (unsigned)data.size());
		c.insert(c.end(), type, type + 4);
		c.insert(c.end(), data.begin(), data.end());
		put32(c, (unsigned)crc32(0, c.data() + 4, (uInt)(c.size() - 4)));
		fwrite(c.data(), 1, c.size(), f);
	}

	bool savePng(const std::string& path, int w, int h, const unsigned char* rgba)
	{
		// each row behind a 0: no filter
		std::vector<unsigned char> raw;
		raw.reserve((size_t)h * (w * 4 + 1));
		for (int y = 0; y < h; y++)
		{
			raw.push_back(0);
			raw.insert(raw.end(), rgba + (size_t)y * w * 4, rgba + (size_t)(y + 1) * w * 4);
		}
		uLongf size = compressBound((uLong)raw.size());
		std::vector<unsigned char> packed(size);
		if (compress2(packed.data(), &size, raw.data(), (uLong)raw.size(), 6) != Z_OK) return false;
		packed.resize(size);

		FILE* f = fopen(path.c_str(), "wb");
		if (!f) return false;
		static const unsigned char sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
		fwrite(sig, 1, 8, f);
		std::vector<unsigned char> ihdr;
		put32(ihdr, (unsigned)w);
		put32(ihdr, (unsigned)h);
		ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});	// 8 bits, RGBA
		chunk(f, "IHDR", ihdr);
		chunk(f, "IDAT", packed);
		chunk(f, "IEND", {});
		bool ok = !ferror(f);
		return fclose(f) == 0 && ok;
	}
}
