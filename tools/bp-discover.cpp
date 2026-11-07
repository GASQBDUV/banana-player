// bp-discover [seconds] | --resolve NAME | --selftest: the LAN discovery on its own, for trying it on a real network.
#include "../src/discovery.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <thread>

using namespace bp;

static int failures = 0;
#define CHECK(cond) \
	do { if (!(cond)) { failures++; printf("FAIL line %d: %s\n", __LINE__, #cond); } } while (0)

static void be16(std::string& s, unsigned v) { s += (char)(v >> 8); s += (char)(v & 255); }
static void be32(std::string& s, unsigned v) { be16(s, v >> 16); be16(s, v & 0xffff); }
static std::string label(const std::string& l) { return std::string(1, (char)l.size()) + l; }

static std::string nbName(const std::string& n, unsigned suffix, unsigned flags)
{
	std::string s = n;
	s.resize(15, ' ');
	s += (char)suffix;
	be16(s, flags);
	return s;
}

static int selftest()
{
	// NetBIOS: our node-status query is the RFC 1002 one (the name "*" encodes as CK + 30 A)
	std::string q = disc::nbStatQuery(0x1234);
	CHECK(q.size() == 50);
	CHECK(q.substr(12, 34) == std::string("\x20") + "CKAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" + std::string(1, '\0'));
	CHECK(q.substr(46) == std::string("\x00\x21\x00\x01", 4));
	CHECK(disc::nbEncode("A", 0x20, ' ') == "EBCACACACACACACACACACACACACACACA");

	// a real-shaped node-status reply: no question, answer name compressed to nothing (full name), 3 names
	std::string r;
	be16(r, 0x1234); be16(r, 0x8400); be16(r, 0); be16(r, 1); be16(r, 0); be16(r, 0);
	r += std::string("\x20") + "CKAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA" + std::string(1, '\0');
	be16(r, 0x21); be16(r, 1); be32(r, 0);
	std::string data(1, (char)3);
	data += nbName("DESKTOP-ABC123", 0x00, 0x0400);   // workstation, unique
	data += nbName("WORKGROUP", 0x00, 0x8400);        // the workgroup, a group
	data += nbName("DESKTOP-ABC123", 0x20, 0x0400);   // file server, unique
	data.append(46, '\0');                            // the statistics
	be16(r, (unsigned)data.size());
	r += data;
	std::string name;
	CHECK(disc::parseNbStat(r, name) && name == "DESKTOP-ABC123");
	// a reply whose count claims more names than there are
	std::string bad = r;
	bad[r.size() - data.size() + 0] = (char)200;
	disc::parseNbStat(bad, name);
	CHECK(!disc::parseNbStat(r.substr(0, 40), name));

	// a name query reply: the address after the 2 flag bytes
	std::string nr;
	be16(nr, 7); be16(nr, 0x8500); be16(nr, 0); be16(nr, 1); be16(nr, 0); be16(nr, 0);
	nr += std::string("\x20") + disc::nbEncode("MY-PC", 0x20, ' ') + std::string(1, '\0');
	be16(nr, 0x20); be16(nr, 1); be32(nr, 300000); be16(nr, 6); be16(nr, 0); be32(nr, 0xC0A80105);
	std::string ip;
	CHECK(disc::parseNbNameReply(nr, ip) && ip == "192.168.1.5");

	// mDNS: PTR for _smb._tcp.local, then SRV and A in the additionals, all names compressed
	std::string m;
	be16(m, 0); be16(m, 0x8400); be16(m, 0); be16(m, 1); be16(m, 0); be16(m, 2);
	size_t svc = m.size();
	m += label("_smb") + label("_tcp") + label("local") + std::string(1, '\0');
	be16(m, 12); be16(m, 1); be32(m, 4500);
	size_t instOff = m.size() + 2;
	std::string inst = label("Tonys Mac mini") + "\xC0" + std::string(1, (char)svc);
	be16(m, (unsigned)inst.size());
	m += inst;
	// SRV, owner = the instance name (a pointer to it)
	m += "\xC0" + std::string(1, (char)instOff);
	be16(m, 33); be16(m, 0x8001); be32(m, 120);
	size_t localOff = svc + 1 + 4 + 1 + 4;
	std::string srv;
	be16(srv, 0); be16(srv, 0); be16(srv, 445);
	size_t targetOff = m.size() + 2 + srv.size();
	srv += label("Tonys-Mac-mini") + "\xC0" + std::string(1, (char)localOff);
	be16(m, (unsigned)srv.size());
	m += srv;
	m += "\xC0" + std::string(1, (char)targetOff);
	be16(m, 1); be16(m, 0x8001); be32(m, 120); be16(m, 4); be32(m, 0xC0A80142);
	std::vector<FoundHost> found;
	disc::parseMdnsSmb(m, "192.168.1.99", found);
	CHECK(found.size() == 1 && found[0].name == "Tonys-Mac-mini" && found[0].ip == "192.168.1.66" && found[0].source == "mDNS");
	CHECK(disc::parseDnsAddr(m, "tonys-mac-mini.local", ip) && ip == "192.168.1.66");
	// hostile: a pointer loop, and truncations at every length, mustn't crash or loop
	std::string loop;
	be16(loop, 0); be16(loop, 0x8400); be16(loop, 0); be16(loop, 1); be16(loop, 0); be16(loop, 0);
	loop += "\xC0\x0C";
	be16(loop, 12); be16(loop, 1); be32(loop, 1); be16(loop, 0);
	found.clear();
	disc::parseMdnsSmb(loop, "1.2.3.4", found);
	CHECK(found.empty());
	for (size_t n = 0; n < m.size(); n++)
	{
		found.clear();
		disc::parseMdnsSmb(m.substr(0, n), "1.2.3.4", found);
		disc::parseNbStat(r.substr(0, n % r.size()), name);
	}

	// our mDNS query: QU bit set on the class
	std::string mq = disc::dnsQuery("_smb._tcp.local", 12, 0x8001);
	CHECK(mq.size() == 12 + 17 + 4 && (uint8_t)mq[mq.size() - 2] == 0x80 && mq[12] == 4);

	// WS-Discovery: our Probe carries the types as wsdd's does; a ProbeMatch, then the Get answer
	std::string probe = disc::wsdProbe("00000000-0000-4000-a000-000000000000");
	CHECK(probe.find("<wsd:Types>wsdp:Device pub:Computer</wsd:Types>") != std::string::npos);
	CHECK(probe.find("discovery/Probe</wsa:Action>") != std::string::npos);
	std::string pm =
	    "<?xml version=\"1.0\"?><soap:Envelope xmlns:soap=\"http://www.w3.org/2003/05/soap-envelope\" "
	    "xmlns:wsa=\"http://schemas.xmlsoap.org/ws/2004/08/addressing\" xmlns:wsd=\"http://schemas.xmlsoap.org/ws/2005/04/discovery\">"
	    "<soap:Header><wsa:To>urn:uuid:aaaa</wsa:To><wsa:Action>http://schemas.xmlsoap.org/ws/2005/04/discovery/ProbeMatches</wsa:Action>"
	    "</soap:Header><soap:Body><wsd:ProbeMatches><wsd:ProbeMatch><wsa:EndpointReference>"
	    "<wsa:Address>urn:uuid:11111111-2222-3333-4444-555555555555</wsa:Address></wsa:EndpointReference>"
	    "<wsd:Types>wsdp:Device pub:Computer</wsd:Types>"
	    "<wsd:XAddrs>http://192.168.1.5:5357/11111111-2222-3333-4444-555555555555/</wsd:XAddrs>"
	    "<wsd:MetadataVersion>1</wsd:MetadataVersion></wsd:ProbeMatch></wsd:ProbeMatches></soap:Body></soap:Envelope>";
	std::string xaddrs, ep;
	CHECK(disc::parseProbeMatch(pm, xaddrs, ep));
	CHECK(xaddrs == "http://192.168.1.5:5357/11111111-2222-3333-4444-555555555555/");
	CHECK(ep == "urn:uuid:11111111-2222-3333-4444-555555555555");
	std::string get = disc::wsdGet(ep, "00000000-0000-4000-a000-000000000001");
	CHECK(get.find("transfer/Get</wsa:Action>") != std::string::npos && get.find("<wsa:To>" + ep + "</wsa:To>") != std::string::npos);
	std::string resp =
	    "HTTP/1.1 200 OK\r\nContent-Type: application/soap+xml\r\n\r\n<soap:Envelope xmlns:soap=\"x\" xmlns:pub=\"y\"><soap:Body>"
	    "<wsx:Metadata><wsx:MetadataSection Dialect=\"http://schemas.microsoft.com/windows/pub/2005/07\">"
	    "<pub:Computer>DESKTOP-ABC123/Workgroup:WORKGROUP</pub:Computer></wsx:MetadataSection></wsx:Metadata></soap:Body></soap:Envelope>";
	CHECK(disc::parseComputerName(resp) == "DESKTOP-ABC123");
	CHECK(disc::parseComputerName("<a><pub:Computer>HOST/Domain:corp</pub:Computer></a>") == "HOST");
	CHECK(disc::parseComputerName("<a><b/></a>").empty());
	CHECK(!disc::parseProbeMatch("<a>garbage", xaddrs, ep));
	disc::parseComputerName("<<<<pub:Computer");

	CHECK(resolveHost("192.168.1.9") == "192.168.1.9");

	printf(failures ? "selftest: %d FAILED\n" : "selftest: all passed\n", failures);
	return failures ? 1 : 0;
}

int main(int argc, char** argv)
{
	if (argc >= 2 && !strcmp(argv[1], "--selftest")) return selftest();
	if (argc >= 3 && !strcmp(argv[1], "--resolve"))
	{
		std::string ip = resolveHost(argv[2]);
		printf("%s  %s\n", argv[2], ip.empty() ? "(not found)" : ip.c_str());
		return ip.empty() ? 1 : 0;
	}
	int seconds = argc >= 2 ? atoi(argv[1]) : 4;
	if (seconds < 1) seconds = 4;
	std::mutex m;
	Discovery d;
	d.start([&](const FoundHost& h) {
		std::lock_guard<std::mutex> lock(m);
		printf("%-24s %-16s %s\n", h.name.c_str(), h.ip.c_str(), h.source.c_str());
		fflush(stdout);
	}, seconds);
	while (d.running()) std::this_thread::sleep_for(std::chrono::milliseconds(100));
	return 0;
}
