// A saved server's address as the backends need it: BananaOS has no DNS server set and no mDNS or NetBIOS daemon, so
// a name found on the network ("TONY-PC", "nas.local") is asked for by the player itself (discovery.h's resolveHost).
// Kept for a few minutes: each folder listed and each file opened would ask again.
#pragma once
#include <string>

namespace bp
{
	// an IPv4 literal as it is; a name, its address (or the name, when nothing answered: the backend then says it
	// can't find it). Blocks up to about 2 s: never on the main thread
	std::string hostAddress(const std::string& host);
}
