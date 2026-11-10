#include "netresolve.h"
#include "discovery.h"
#include <arpa/inet.h>
#include <chrono>
#include <map>
#include <mutex>

namespace bp
{
	std::string hostAddress(const std::string& host)
	{
		in_addr a;
		if (host.empty() || inet_pton(AF_INET, host.c_str(), &a) == 1) return host;
		// a URL (a DLNA server's description, "http://nas.local:8200/rootDesc.xml"): only its host part is a name
		size_t scheme = host.find("://");
		if (scheme != std::string::npos)
		{
			size_t a0 = scheme + 3, a1 = host.find_first_of(":/?#", a0);
			if (a1 == std::string::npos) a1 = host.size();
			if (a0 >= a1 || host[a0] == '[') return host;	// no host, or IPv6: as it is
			std::string name = host.substr(a0, a1 - a0), ip = hostAddress(name);
			return host.substr(0, a0) + ip + host.substr(a1);
		}
		static std::mutex lock;
		static std::map<std::string, std::pair<std::string, std::chrono::steady_clock::time_point>> known;
		auto now = std::chrono::steady_clock::now();
		{
			std::lock_guard<std::mutex> g(lock);
			auto it = known.find(host);
			if (it != known.end() && now - it->second.second < std::chrono::minutes(5)) return it->second.first;
		}
		std::string ip = resolveHost(host, 2000);
		if (ip.empty()) return host;	// (not kept: the next try asks again)
		std::lock_guard<std::mutex> g(lock);
		known[host] = {ip, now};
		return ip;
	}
}
