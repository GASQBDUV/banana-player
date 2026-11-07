#include "netpath.h"
#include <random>
#include <vector>

namespace bp
{
	static const char SCHEME[] = "bpnet://";
	static const size_t SCHEME_LEN = sizeof(SCHEME) - 1;

	bool isNetPath(const std::string& s)
	{
		return s.compare(0, SCHEME_LEN, SCHEME) == 0;
	}

	std::string netEncode(const std::string& s)
	{
		static const char* HEX = "0123456789ABCDEF";
		std::string out;
		for (unsigned char c : s)
		{
			if (c == '%' || c == '?' || c == '#' || c < 0x20 || c == 0x7f)
			{
				out += '%';
				out += HEX[c >> 4];
				out += HEX[c & 15];
			}
			else out += (char)c;
		}
		return out;
	}

	static int hexVal(char c)
	{
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return c - 'a' + 10;
		if (c >= 'A' && c <= 'F') return c - 'A' + 10;
		return -1;
	}

	std::string netDecode(const std::string& s)
	{
		std::string out;
		for (size_t i = 0; i < s.size(); i++)
		{
			int h, l;
			if (s[i] == '%' && i + 2 < s.size() && (h = hexVal(s[i + 1])) >= 0 && (l = hexVal(s[i + 2])) >= 0)
			{
				out += (char)(h * 16 + l);
				i += 2;
			}
			else out += s[i];
		}
		return out;
	}

	bool parseNetPath(const std::string& s, NetPath& out)
	{
		if (!isNetPath(s)) return false;
		size_t slash = s.find('/', SCHEME_LEN);
		out.server = s.substr(SCHEME_LEN, slash == std::string::npos ? std::string::npos : slash - SCHEME_LEN);
		if (out.server.empty()) return false;
		out.path = slash == std::string::npos ? "/" : netDecode(s.substr(slash));
		return true;
	}

	static std::string tidy(const std::string& path)
	{
		std::vector<std::string> parts;
		size_t i = 0;
		while (i <= path.size())
		{
			size_t j = path.find('/', i);
			if (j == std::string::npos) j = path.size();
			std::string p = path.substr(i, j - i);
			if (p == "..") { if (!parts.empty()) parts.pop_back(); }
			else if (!p.empty() && p != ".") parts.push_back(p);
			i = j + 1;
		}
		std::string out;
		for (auto& p : parts) out += "/" + p;
		return out.empty() ? "/" : out;
	}

	std::string makeNetPath(const std::string& server, const std::string& path)
	{
		std::string t = tidy(path);
		return SCHEME + server + (t == "/" ? "/" : netEncode(t));
	}

	std::string netParent(const std::string& s)
	{
		NetPath n;
		if (!parseNetPath(s, n)) return s;
		return makeNetPath(n.server, n.path.substr(0, n.path.rfind('/')));
	}

	std::string netBaseName(const std::string& s)
	{
		NetPath n;
		if (!parseNetPath(s, n)) return "";
		return n.path.substr(n.path.rfind('/') + 1);
	}

	std::string netJoin(const std::string& dir, const std::string& name)
	{
		NetPath n;
		if (!parseNetPath(dir, n)) return "";
		return makeNetPath(n.server, n.path + "/" + name);
	}

	std::string newServerId()
	{
		static const char* HEX = "0123456789abcdef";
		std::random_device rd;
		unsigned v = rd();
		std::string id;
		for (int i = 0; i < 8; i++) id += HEX[(v >> (i * 4)) & 15];
		return id;
	}
}
