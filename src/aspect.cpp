#include "aspect.h"

namespace bp
{
	AspectMode nextAspect(AspectMode m)
	{
		switch (m)
		{
		case AspectMode::Fit: return AspectMode::Fill;
		case AspectMode::Fill: return AspectMode::Stretch;
		default: return AspectMode::Fit;
		}
	}

	const char* aspectName(AspectMode m)
	{
		switch (m)
		{
		case AspectMode::Fill: return "Fill";
		case AspectMode::Stretch: return "Stretch";
		default: return "Fit";
		}
	}

	bool parseAspect(const std::string& s, AspectMode& out)
	{
		if (s == "fit") out = AspectMode::Fit;
		else if (s == "fill") out = AspectMode::Fill;
		else if (s == "stretch") out = AspectMode::Stretch;
		else return false;
		return true;
	}

	std::vector<std::pair<const char*, const char*>> aspectProperties(AspectMode m)
	{
		switch (m)
		{
		case AspectMode::Fill: return {{"keepaspect", "yes"}, {"panscan", "1.0"}};
		case AspectMode::Stretch: return {{"keepaspect", "no"}, {"panscan", "0.0"}};
		default: return {{"keepaspect", "yes"}, {"panscan", "0.0"}};
		}
	}
}
