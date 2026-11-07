#pragma once
#include <string>

namespace bp
{
	// Photos: by their extension (any case): jpg, png, webp, gif, bmp, tiff, avif
	bool isPhotoFile(const std::string& name);

	// UP/DOWN's zoom steps, as a factor of the photo's Fit (or Fill): 1, 1.5, 2, 3, 4, 6, 8; from between two
	// steps (a double-tap's 2x is one) to the next one either way
	double stepZoom(double zoom, int dir);
	constexpr double MAX_ZOOM = 8;

	// How big the photo is on the top panel, at a zoom, as a multiple of the panel's width and height (1 x 0.75: as
	// wide as the panel, three quarters of its height). imageAspect is the photo's width / height as it's shown
	// (rotated), panelAspect the panel's (4:3); fill: the photo first scaled to cover the panel, not fit in it
	struct PhotoScale
	{
		double x = 1, y = 1;
	};
	PhotoScale photoScale(double imageAspect, double panelAspect, bool fill, double zoom);
	// how far the photo can move off centre, in mpv's video-pan units (fractions of the shown photo's size): its
	// edges stop at the panel's. 0 for a photo no bigger than the panel that way
	double maxPan(double scale);

	// What a JPEG's EXIF says (empty, 1: nothing known)
	struct ExifInfo
	{
		int orientation = 1;		// 1 upright, 3 upside down, 6 to turn 90 clockwise, 8 counterclockwise (2, 4, 5, 7 mirrored)
		std::string taken;			// "2024:05:01 13:45:10", as the camera wrote it
		std::string make, model;	// the camera's
	};
	// from a JPEG's bytes (its first part, where the EXIF is, does: 128 KB)
	ExifInfo parseExif(const std::string& bytes);
	ExifInfo readExif(const std::string& path);
	// the clockwise turn an orientation asks for: 0, 90, 180 or 270 (mirroring left out)
	int exifRotation(int orientation);
	// "2024:05:01 13:45:10" -> "2024-05-01 13:45"
	std::string exifDate(const std::string& taken);
}
