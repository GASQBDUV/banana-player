// The handheld's address on the Wi-Fi, for the web dashboard's URL and QR code.
#include "web/netif.h"
#include "webpath.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

namespace bp
{
	std::string deviceAddress()
	{
		ifaddrs* list = nullptr;
		if (getifaddrs(&list) != 0) return "";
		std::string best;
		int bestRank = 0;
		for (ifaddrs* i = list; i; i = i->ifa_next)
		{
			if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || !i->ifa_name) continue;
			if (!(i->ifa_flags & IFF_UP) || !(i->ifa_flags & IFF_RUNNING) || (i->ifa_flags & IFF_LOOPBACK)) continue;
			// rank 0: loopback, USB gadget, containers: not where a phone finds the handheld
			int rank = interfaceRank(i->ifa_name);
			if (rank <= bestRank) continue;
			char buf[INET_ADDRSTRLEN];
			if (!inet_ntop(AF_INET, &((sockaddr_in*)i->ifa_addr)->sin_addr, buf, sizeof buf)) continue;
			// a self-assigned address (no DHCP answer yet) can't be reached from a phone on the router's network
			if (std::string(buf).rfind("169.254.", 0) == 0) continue;
			best = buf;
			bestRank = rank;
		}
		freeifaddrs(list);
		return best;
	}
}
