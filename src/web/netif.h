// The handheld's address on the Wi-Fi, for the web dashboard's URL and QR code.
#pragma once

#include <string>

namespace bp
{
	// the best IPv4 address of an interface that's up and running: Wi-Fi before Ethernet before the rest
	// (interfaceRank); "" with none (no Wi-Fi). Cheap (getifaddrs): read again while the Web page shows
	std::string deviceAddress();
}
