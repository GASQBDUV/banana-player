#include "photo.h"
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>

namespace bp
{
	bool isPhotoFile(const std::string& name)
	{
		size_t dot = name.rfind('.');
		if (dot == std::string::npos || name[0] == '.') return false;
		std::string ext = name.substr(dot + 1);
		std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
		for (const char* e : {"jpg", "jpeg", "png", "webp", "gif", "bmp", "tif", "tiff", "avif"})
			if (ext == e) return true;
		return false;
	}

	static const double STEPS[] = {1, 1.5, 2, 3, 4, 6, 8};

	double stepZoom(double zoom, int dir)
	{
		if (dir > 0)
		{
			for (double s : STEPS)
				if (s > zoom + 1e-6) return s;
			return MAX_ZOOM;
		}
		double out = 1;
		for (double s : STEPS)
			if (s < zoom - 1e-6) out = s;
		return out;
	}

	PhotoScale photoScale(double imageAspect, double panelAspect, bool fill, double zoom)
	{
		if (!(imageAspect > 0) || !(panelAspect > 0)) return {zoom, zoom};
		// fitted: as wide as the panel (a wider photo) or as tall
		PhotoScale s;
		if (imageAspect > panelAspect) s = {1, panelAspect / imageAspect};
		else s = {imageAspect / panelAspect, 1};
		double f = fill ? std::max(1 / s.x, 1 / s.y) : 1;
		return {s.x * f * zoom, s.y * f * zoom};
	}

	double maxPan(double scale)
	{
		// the shown photo's centre can go (scale - 1) / 2 of the panel off the panel's; in the photo's size
		return scale > 1 ? 0.5 - 0.5 / scale : 0;
	}

	ExifInfo parseExif(const std::string& b)
	{
		ExifInfo out;
		auto u8 = [&](size_t i) -> unsigned { return i < b.size() ? (unsigned char)b[i] : 0; };
		if (u8(0) != 0xFF || u8(1) != 0xD8) return out;	// not a JPEG
		// the segments up to APP1 "Exif"
		size_t i = 2;
		while (i + 4 <= b.size() && u8(i) == 0xFF)
		{
			unsigned marker = u8(i + 1), len = u8(i + 2) << 8 | u8(i + 3);
			if (marker == 0xDA || len < 2) break;	// the picture's data: no EXIF before it
			if (marker == 0xE1 && b.compare(i + 4, 6, std::string("Exif\0\0", 6)) == 0)
			{
				size_t t = i + 10, end = std::min(b.size(), (size_t)i + 2 + len);	// the TIFF header
				bool le = u8(t) == 'I';
				auto u16 = [&](size_t at) -> unsigned {
					return at + 2 > end ? 0 : le ? u8(at) | u8(at + 1) << 8 : u8(at) << 8 | u8(at + 1);
				};
				auto u32 = [&](size_t at) -> uint32_t {
					return at + 4 > end ? 0 : le ? u16(at) | (uint32_t)u16(at + 2) << 16 : (uint32_t)u16(at) << 16 | u16(at + 2);
				};
				// an ASCII value: in the entry itself up to 4 bytes, else at its offset
				auto text = [&](size_t entry) {
					uint32_t n = u32(entry + 4);
					size_t at = n <= 4 ? entry + 8 : t + u32(entry + 8);
					if (at >= end || n > 256) return std::string();
					std::string s = b.substr(at, std::min((size_t)n, end - at));
					while (!s.empty() && (s.back() == '\0' || s.back() == ' ')) s.pop_back();
					return s;
				};
				uint32_t exifIfd = 0;
				auto readIfd = [&](size_t ifd) {
					unsigned n = u16(ifd);
					for (unsigned k = 0; k < n && k < 512; k++)
					{
						size_t e = ifd + 2 + k * 12;
						switch (u16(e))
						{
						case 0x0112: out.orientation = (int)u16(e + 8); break;
						case 0x010F: out.make = text(e); break;
						case 0x0110: out.model = text(e); break;
						case 0x0132: if (out.taken.empty()) out.taken = text(e); break;
						case 0x9003: out.taken = text(e); break;	// when it was taken, over when it was changed
						case 0x8769: exifIfd = u32(e + 8); break;
						}
					}
				};
				readIfd(t + u32(t + 4));
				if (exifIfd) readIfd(t + exifIfd);
				if (out.orientation < 1 || out.orientation > 8) out.orientation = 1;
				return out;
			}
			i += 2 + len;
		}
		return out;
	}

	ExifInfo readExif(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::string bytes(128 * 1024, '\0');
		in.read(&bytes[0], (std::streamsize)bytes.size());
		bytes.resize((size_t)in.gcount());
		return parseExif(bytes);
	}

	int exifRotation(int orientation)
	{
		switch (orientation)
		{
		case 3: case 4: return 180;
		case 5: case 6: return 90;
		case 7: case 8: return 270;
		default: return 0;
		}
	}

	std::string exifDate(const std::string& taken)
	{
		// "YYYY:MM:DD HH:MM:SS"
		if (taken.size() < 16 || taken[4] != ':' || taken[7] != ':') return taken;
		return taken.substr(0, 4) + "-" + taken.substr(5, 2) + "-" + taken.substr(8, 2) + " " + taken.substr(11, 5);
	}
}
