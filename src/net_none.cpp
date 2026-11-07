// A build with no network support (BP_NET off): no server can be reached, and the browser says so.
#include "net.h"

namespace bp
{
	std::unique_ptr<NetBackend> makeBackend(const NetServer&, const std::string&, int)
	{
		return nullptr;
	}
}
