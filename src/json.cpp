#include "json.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bp
{
	void JsonWriter::pre()
	{
		if (afterKey_) { afterKey_ = false; return; }
		if (!any_.empty())
		{
			if (any_.back()) out_ += ',';
			any_.back() = true;
		}
	}

	JsonWriter& JsonWriter::obj()
	{
		pre();
		out_ += '{';
		closers_.push_back('}');
		any_.push_back(false);
		return *this;
	}

	JsonWriter& JsonWriter::arr()
	{
		pre();
		out_ += '[';
		closers_.push_back(']');
		any_.push_back(false);
		return *this;
	}

	JsonWriter& JsonWriter::end()
	{
		if (closers_.empty()) return *this;
		out_ += closers_.back();
		closers_.pop_back();
		any_.pop_back();
		return *this;
	}

	JsonWriter& JsonWriter::key(const std::string& k)
	{
		pre();
		out_ += escape(k);
		out_ += ':';
		afterKey_ = true;
		return *this;
	}

	JsonWriter& JsonWriter::str(const std::string& s)
	{
		pre();
		out_ += escape(s);
		return *this;
	}

	JsonWriter& JsonWriter::num(double v)
	{
		pre();
		if (!std::isfinite(v)) { out_ += "null"; return *this; }
		char buf[40];
		for (int digits = 15; digits <= 17; digits++)	// the shortest that reads back the same
		{
			snprintf(buf, sizeof buf, "%.*g", digits, v);
			if (strtod(buf, nullptr) == v) break;
		}
		for (char* p = buf; *p; p++)
			if (*p == ',') *p = '.';	// a locale with a decimal comma
		out_ += buf;
		return *this;
	}

	JsonWriter& JsonWriter::numInt(int64_t v)
	{
		pre();
		out_ += std::to_string(v);
		return *this;
	}

	JsonWriter& JsonWriter::boolean(bool b)
	{
		pre();
		out_ += b ? "true" : "false";
		return *this;
	}

	JsonWriter& JsonWriter::null()
	{
		pre();
		out_ += "null";
		return *this;
	}

	JsonWriter& JsonWriter::raw(const std::string& json)
	{
		pre();
		out_ += json.empty() ? "null" : json;
		return *this;
	}

	// Length of the valid UTF-8 sequence at s[i], 0 if there isn't one (overlong, surrogate and > U+10FFFF included)
	static size_t utf8Len(const std::string& s, size_t i)
	{
		unsigned char c = s[i];
		size_t n = s.size() - i;
		auto cont = [&](size_t k) { return (unsigned char)s[i + k] >= 0x80 && (unsigned char)s[i + k] < 0xC0; };
		if (c < 0x80) return 1;
		if (c >= 0xC2 && c <= 0xDF) return n >= 2 && cont(1) ? 2 : 0;
		if (c >= 0xE0 && c <= 0xEF)
		{
			if (n < 3 || !cont(1) || !cont(2)) return 0;
			unsigned char c1 = s[i + 1];
			if (c == 0xE0 && c1 < 0xA0) return 0;
			if (c == 0xED && c1 >= 0xA0) return 0;
			return 3;
		}
		if (c >= 0xF0 && c <= 0xF4)
		{
			if (n < 4 || !cont(1) || !cont(2) || !cont(3)) return 0;
			unsigned char c1 = s[i + 1];
			if (c == 0xF0 && c1 < 0x90) return 0;
			if (c == 0xF4 && c1 >= 0x90) return 0;
			return 4;
		}
		return 0;
	}

	std::string JsonWriter::escape(const std::string& s)
	{
		std::string o = "\"";
		for (size_t i = 0; i < s.size();)
		{
			unsigned char c = s[i];
			size_t len = utf8Len(s, i);
			if (len == 0) { o += "\xEF\xBF\xBD"; i++; continue; }
			if (len > 1) { o.append(s, i, len); i += len; continue; }
			i++;
			switch (c)
			{
			case '"': o += "\\\""; break;
			case '\\': o += "\\\\"; break;
			case '\b': o += "\\b"; break;
			case '\f': o += "\\f"; break;
			case '\n': o += "\\n"; break;
			case '\r': o += "\\r"; break;
			case '\t': o += "\\t"; break;
			default:
				if (c < 0x20)
				{
					char b[8];
					snprintf(b, sizeof b, "\\u%04x", c);
					o += b;
				}
				else o += char(c);
			}
		}
		return o + "\"";
	}
}
