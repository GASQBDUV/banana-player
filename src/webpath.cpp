#include "webpath.h"
#include "photo.h"
#include "playlist.h"
#include <algorithm>
#include <arpa/inet.h>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

namespace bp
{
	namespace
	{
		bool realPath(const std::string& p, std::string& out)
		{
			char buf[PATH_MAX];
			if (!realpath(p.c_str(), buf)) return false;
			out = buf;
			return true;
		}

		// `p` is `base` or inside it, on a '/' boundary
		bool under(const std::string& p, const std::string& base)
		{
			if (base.empty()) return false;
			if (p == base) return true;
			std::string pre = base.back() == '/' ? base : base + "/";
			return p.compare(0, pre.size(), pre) == 0;
		}

		std::string lower(std::string s)
		{
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
			return s;
		}

		bool denied(const std::vector<std::string>& deny, const std::string& real)
		{
			for (auto d : deny)
			{
				while (d.size() > 1 && d.back() == '/') d.pop_back();
				// a prefix that isn't there yet is resolved by its nearest folder that is (/tmp is /private/tmp on a Mac)
				std::string tail, head = d, r;
				while (!realPath(head, r))
				{
					size_t sl = head.rfind('/');
					if (sl == std::string::npos || sl == 0) { r.clear(); break; }
					tail = head.substr(sl) + tail;
					head.resize(sl);
				}
				if (!r.empty()) d = (r == "/" ? "" : r) + tail;
				if (under(real, d)) return true;
			}
			return false;
		}
	}

	bool resolveVirtual(const std::vector<WebRoot>& roots, const std::vector<std::string>& deny, const std::string& vpath,
		std::string& real, bool forCreate)
	{
		if (vpath.find('\0') != std::string::npos) return false;
		std::vector<std::string> parts;
		size_t pos = 0;
		for (;;)
		{
			size_t sl = vpath.find('/', pos);
			parts.push_back(vpath.substr(pos, sl == std::string::npos ? std::string::npos : sl - pos));
			if (sl == std::string::npos) break;
			pos = sl + 1;
		}
		if (parts.size() > 1 && parts.back().empty()) parts.pop_back();	// the trailing slash
		const std::string& idx = parts[0];
		if (idx.empty() || idx.size() > 6 || !std::all_of(idx.begin(), idx.end(), [](char c) { return c >= '0' && c <= '9'; }))
			return false;
		size_t ri = size_t(std::atoi(idx.c_str()));
		if (ri >= roots.size()) return false;
		for (size_t i = 1; i < parts.size(); i++)
			if (parts[i].empty() || parts[i][0] == '.') return false;	// also "." and ".."

		std::string root;
		if (!realPath(roots[ri].path, root)) return false;
		std::string rel;
		for (size_t i = 1; i < parts.size(); i++) rel += (i > 1 ? "/" : "") + parts[i];

		std::string out;
		if (!forCreate)
		{
			if (!realPath(rel.empty() ? root : root + "/" + rel, out)) return false;
		}
		else
		{
			if (parts.size() < 2) return false;
			const std::string& last = parts.back();
			if (!validName(last)) return false;
			std::string parentRel;
			for (size_t i = 1; i + 1 < parts.size(); i++) parentRel += (i > 1 ? "/" : "") + parts[i];
			std::string parent;
			if (!realPath(parentRel.empty() ? root : root + "/" + parentRel, parent)) return false;
			if (!under(parent, root)) return false;
			out = (parent == "/" ? "" : parent) + "/" + last;
			// an existing symlink there would be followed by whatever creates the file
			struct stat st;
			if (lstat(out.c_str(), &st) == 0 && S_ISLNK(st.st_mode))
			{
				std::string target;
				if (!realPath(out, target) || !under(target, root) || denied(deny, target)) return false;
			}
		}
		if (!under(out, root) || denied(deny, out)) return false;
		real = out;
		return true;
	}

	std::string toVirtual(const std::vector<WebRoot>& roots, const std::string& real)
	{
		std::string r;
		if (!realPath(real, r)) r = real;
		int best = -1;
		std::string bestRoot;
		for (size_t i = 0; i < roots.size(); i++)
		{
			std::string rp;
			if (!realPath(roots[i].path, rp)) rp = roots[i].path;
			if (under(r, rp) && (best < 0 || rp.size() > bestRoot.size()))
			{
				best = int(i);
				bestRoot = rp;
			}
		}
		if (best < 0) return "";
		// a root of "/" would make "/x" start with its own separator
		std::string rel = bestRoot == "/" ? (r == "/" ? "" : r) : r.substr(bestRoot.size());
		return std::to_string(best) + rel;
	}

	bool validName(const std::string& n)
	{
		if (n.empty() || n.size() > 255 || n == "." || n == "..") return false;
		bool blank = true;
		for (unsigned char c : n)
		{
			if (c < 0x20 || c == 0x7f || strchr("/\\<>:\"|?*", c)) return false;
			if (c != ' ') blank = false;
		}
		return !blank;
	}

	bool allowedFileType(const std::string& name)
	{
		if (isMediaFile(name) || isPhotoFile(name)) return true;
		size_t dot = name.rfind('.');
		if (dot == std::string::npos) return false;
		std::string e = lower(name.substr(dot + 1));
		for (const char* s : {"srt", "ass", "ssa", "vtt", "sub", "idx", "lrc"})
			if (e == s) return true;
		return false;
	}

	namespace
	{
		bool v4Private(const unsigned char* b)
		{
			return b[0] == 127 || b[0] == 10 || (b[0] == 172 && (b[1] & 0xF0) == 16) || (b[0] == 192 && b[1] == 168) ||
				(b[0] == 169 && b[1] == 254);
		}
	}

	bool privateAddress(const std::string& ipIn)
	{
		std::string ip = ipIn.substr(0, ipIn.find('%'));
		in_addr a4;
		if (inet_pton(AF_INET, ip.c_str(), &a4) == 1) return v4Private((const unsigned char*)&a4);
		in6_addr a6;
		if (inet_pton(AF_INET6, ip.c_str(), &a6) != 1) return false;
		const unsigned char* b = a6.s6_addr;
		bool loop = true;
		for (int i = 0; i < 15; i++) loop = loop && b[i] == 0;
		if (loop && b[15] == 1) return true;
		if (b[0] == 0xfe && (b[1] & 0xC0) == 0x80) return true;	// fe80::/10
		if ((b[0] & 0xFE) == 0xFC) return true;	// fc00::/7
		bool mapped = true;
		for (int i = 0; i < 10; i++) mapped = mapped && b[i] == 0;
		if (mapped && b[10] == 0xff && b[11] == 0xff) return v4Private(b + 12);
		return false;
	}

	bool hostAllowed(const std::string& host)
	{
		std::string h = host, port;
		bool bracket = false;
		if (!h.empty() && h[0] == '[')
		{
			size_t close = h.find(']');
			if (close == std::string::npos) return false;
			std::string rest = h.substr(close + 1);
			if (!rest.empty())
			{
				if (rest[0] != ':') return false;
				port = rest.substr(1);
				if (port.empty()) return false;
			}
			h = h.substr(1, close - 1);
			bracket = true;
		}
		else
		{
			size_t c = h.find(':');
			if (c != std::string::npos)
			{
				if (h.find(':', c + 1) != std::string::npos) return false;	// a bare IPv6 isn't a valid Host
				port = h.substr(c + 1);
				h = h.substr(0, c);
				if (port.empty()) return false;
			}
		}
		if (!port.empty() && (port.size() > 5 || !std::all_of(port.begin(), port.end(), [](char c) { return c >= '0' && c <= '9'; })))
			return false;
		if (h.empty()) return false;
		in_addr a4;
		in6_addr a6;
		if (bracket) return inet_pton(AF_INET6, h.c_str(), &a6) == 1;
		if (lower(h) == "localhost") return true;
		return inet_pton(AF_INET, h.c_str(), &a4) == 1;
	}

	int interfaceRank(const std::string& n)
	{
		auto starts = [&](const char* p) { return n.compare(0, strlen(p), p) == 0; };
		if (n == "lo" || starts("usb") || starts("docker") || starts("veth")) return 0;
		if (starts("wlan")) return 3;
		if (starts("eth") || starts("en")) return 2;
		return 1;
	}
}
