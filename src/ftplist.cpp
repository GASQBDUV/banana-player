#include "ftplist.h"
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace bp
{
	namespace
	{
		std::vector<std::string> lines(const std::string& text)
		{
			std::vector<std::string> out;
			size_t i = 0;
			while (i < text.size())
			{
				size_t e = text.find('\n', i);
				if (e == std::string::npos) e = text.size();
				size_t end = e;
				if (end > i && text[end - 1] == '\r') end--;
				if (end > i) out.push_back(text.substr(i, end - i));
				i = e + 1;
			}
			return out;
		}

		bool special(const std::string& n) { return n == "." || n == ".."; }

		bool allDigits(const std::string& s)
		{
			if (s.empty()) return false;
			for (char c : s) if (!isdigit((unsigned char)c)) return false;
			return true;
		}

		bool ieq(const std::string& a, const char* b)
		{
			size_t i = 0;
			for (; i < a.size() && b[i]; i++) if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
			return i == a.size() && !b[i];
		}

		struct Token { std::string s; size_t begin, end; };

		std::vector<Token> tokens(const std::string& line)
		{
			std::vector<Token> t;
			size_t i = 0;
			while (i < line.size())
			{
				while (i < line.size() && isspace((unsigned char)line[i])) i++;
				size_t b = i;
				while (i < line.size() && !isspace((unsigned char)line[i])) i++;
				if (i > b) t.push_back({ line.substr(b, i - b), b, i });
			}
			return t;
		}

		bool month(const std::string& s)
		{
			static const char* m[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
			for (const char* x : m) if (ieq(s, x)) return true;
			return false;
		}

		// the part of the line after the token and the blanks that follow it: a name's own spaces are kept
		std::string after(const std::string& line, const Token& t)
		{
			size_t i = t.end;
			while (i < line.size() && isspace((unsigned char)line[i])) i++;
			return line.substr(i);
		}
	}

	std::vector<NetEntry> parseMlsd(const std::string& text)
	{
		std::vector<NetEntry> out;
		for (const std::string& line : lines(text))
		{
			// "type=file;size=12; name": the facts end at the first blank, and the name is all after it
			size_t sp = line.find(' ');
			if (sp == std::string::npos || line.find('=') > sp) continue;
			std::string facts = line.substr(0, sp), name = line.substr(sp + 1);
			if (name.empty() || special(name)) continue;
			NetEntry e;
			e.name = name;
			bool skip = false;
			size_t i = 0;
			while (i < facts.size())
			{
				size_t semi = facts.find(';', i);
				if (semi == std::string::npos) semi = facts.size();
				std::string f = facts.substr(i, semi - i);
				i = semi + 1;
				size_t eq = f.find('=');
				if (eq == std::string::npos) continue;
				std::string k = f.substr(0, eq), v = f.substr(eq + 1);
				for (char& c : k) c = (char)tolower((unsigned char)c);
				for (char& c : v) c = (char)tolower((unsigned char)c);
				if (k == "type")
				{
					if (v == "cdir" || v == "pdir") skip = true;
					else if (v == "dir") e.dir = true;
					// (a link, "OS.unix=slink:target", is a file here: it opens, or it fails to)
				}
				else if (k == "size" && allDigits(v)) e.size = atoll(v.c_str());
			}
			if (skip) continue;
			if (e.dir) e.size = -1;
			out.push_back(e);
		}
		return out;
	}

	std::vector<NetEntry> parseFtpList(const std::string& text)
	{
		std::vector<NetEntry> out;
		for (const std::string& line : lines(text))
		{
			std::vector<Token> t = tokens(line);
			NetEntry e;
			if (t.size() >= 4 && isdigit((unsigned char)line[0]))
			{
				// IIS: "11-07-26  10:00AM       <DIR>          Name"
				if (t[0].s.find('-') == std::string::npos && t[0].s.find('/') == std::string::npos) continue;
				const std::string& kind = t[2].s;
				if (kind == "<DIR>") e.dir = true;
				else if (allDigits(kind)) e.size = atoll(kind.c_str());
				else continue;
				e.name = after(line, t[2]);
			}
			else if (t.size() >= 8 && t[0].s.size() >= 10 && strchr("-dlbcps", t[0].s[0]))
			{
				// Unix: "drwxr-xr-x 2 owner group 4096 Nov  7 10:00 Name" (some leave out the group): find the date,
				// the size is before it and the name after its third part
				size_t m = 0;
				for (size_t i = 3; i + 3 < t.size(); i++)
					if (month(t[i].s) && allDigits(t[i - 1].s) && allDigits(t[i + 1].s)) { m = i; break; }
				if (!m) continue;
				e.dir = t[0].s[0] == 'd';
				if (!e.dir) e.size = atoll(t[m - 1].s.c_str());
				e.name = after(line, t[m + 2]);
				if (t[0].s[0] == 'l')
				{
					size_t arrow = e.name.find(" -> ");
					if (arrow != std::string::npos)
					{
						// a link to a folder, when its target says so
						if (e.name.back() == '/') e.dir = true;
						e.name = e.name.substr(0, arrow);
					}
				}
			}
			else continue;
			if (e.name.empty() || special(e.name)) continue;
			out.push_back(e);
		}
		return out;
	}
}
