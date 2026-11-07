#pragma once
#include <string>
#include <utility>
#include <vector>

namespace bp
{
	// How the picture meets the top panel.
	//   Fit      the whole picture, black bars where its shape differs from the panel's
	//   Fill     the whole panel, the picture's overflow cropped (mpv's panscan)
	//   Stretch  the whole panel and the whole picture, its shape distorted
	enum class AspectMode { Fit, Fill, Stretch };

	AspectMode nextAspect(AspectMode m);
	const char* aspectName(AspectMode m);
	bool parseAspect(const std::string& s, AspectMode& out);

	// the mpv properties that make the mode, as name/value pairs; every mode sets all of them, so they can be applied
	// in any order over any other mode
	std::vector<std::pair<const char*, const char*>> aspectProperties(AspectMode m);
}
