#include "update.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <vector>

namespace bp
{
	// the string value of the first "key" at or after from: "key": "value" (JSON's escapes of a URL or a tag are
	// none but \/, taken out)
	static std::string jsonString(const std::string& j, const std::string& key, size_t from, size_t* at = nullptr)
	{
		size_t k = j.find("\"" + key + "\"", from);
		if (k == std::string::npos) return "";
		size_t colon = j.find(':', k + key.size() + 2);
		size_t q = colon == std::string::npos ? colon : j.find('"', colon);
		if (q == std::string::npos) return "";
		std::string v;
		for (size_t i = q + 1; i < j.size() && j[i] != '"'; i++)
		{
			if (j[i] == '\\' && i + 1 < j.size()) i++;
			v += j[i];
		}
		if (at) *at = k;
		return v;
	}

	bool parseRelease(const std::string& json, Release& out)
	{
		out = Release();
		std::string tag = jsonString(json, "tag_name", 0);
		if (tag.empty()) return false;
		out.version = tag[0] == 'v' || tag[0] == 'V' ? tag.substr(1) : tag;
		std::string zipName = "BananaPlayer-" + out.version + "-aarch64.zip";
		// the assets: each a "name", then its "size", then its "browser_download_url"
		for (size_t at = json.find("\"name\""); at != std::string::npos; at = json.find("\"name\"", at + 1))
		{
			std::string name = jsonString(json, "name", at);
			if (name != zipName && name != zipName + ".sha256") continue;
			size_t urlAt = 0;
			std::string url = jsonString(json, "browser_download_url", at, &urlAt);
			if (name == zipName)
			{
				out.zipUrl = url;
				size_t sz = json.find("\"size\"", at);
				if (sz != std::string::npos && sz < urlAt)
				{
					size_t c = json.find(':', sz);
					out.zipSize = c == std::string::npos ? 0 : atoll(json.c_str() + c + 1);
				}
			}
			else out.shaUrl = url;
		}
		return !out.zipUrl.empty();
	}

	int compareVersions(const std::string& a, const std::string& b)
	{
		size_t i = a.size() && (a[0] == 'v' || a[0] == 'V'), j = b.size() && (b[0] == 'v' || b[0] == 'V');
		while (i < a.size() || j < b.size())
		{
			long x = 0, y = 0;
			while (i < a.size() && isdigit((unsigned char)a[i])) x = x * 10 + (a[i++] - '0');
			while (j < b.size() && isdigit((unsigned char)b[j])) y = y * 10 + (b[j++] - '0');
			if (x != y) return x < y ? -1 : 1;
			// past the dot (anything else ends the number too)
			if (i < a.size()) i++;
			if (j < b.size()) j++;
		}
		return 0;
	}

	// SHA-256 (FIPS 180-4)
	namespace
	{
		struct Sha256
		{
			uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
			unsigned char buf[64];
			size_t used = 0;
			uint64_t total = 0;

			static uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

			void block(const unsigned char* p)
			{
				static const uint32_t K[64] = {
					0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
					0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
					0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
					0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
					0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
					0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
					0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
					0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
				uint32_t w[64];
				for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
				for (int i = 16; i < 64; i++)
				{
					uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
					uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
					w[i] = w[i - 16] + s0 + w[i - 7] + s1;
				}
				uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
				for (int i = 0; i < 64; i++)
				{
					uint32_t t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
					uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
					hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
				}
				h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
			}

			void add(const unsigned char* p, size_t n)
			{
				total += n;
				while (n)
				{
					size_t take = std::min(n, sizeof buf - used);
					memcpy(buf + used, p, take);
					used += take;
					p += take;
					n -= take;
					if (used == 64)
					{
						block(buf);
						used = 0;
					}
				}
			}

			std::string hex()
			{
				uint64_t bits = total * 8;
				unsigned char pad = 0x80;
				add(&pad, 1);
				unsigned char zero = 0;
				while (used != 56) add(&zero, 1);
				unsigned char len[8];
				for (int i = 0; i < 8; i++) len[i] = (unsigned char)(bits >> (56 - 8 * i));
				add(len, 8);
				char out[65];
				for (int i = 0; i < 8; i++) snprintf(out + i * 8, 9, "%08x", h[i]);
				return out;
			}
		};
	}

	std::string sha256Hex(const std::string& data)
	{
		Sha256 s;
		s.add((const unsigned char*)data.data(), data.size());
		return s.hex();
	}

	std::string sha256File(const std::string& path)
	{
		std::ifstream in(path, std::ios::binary);
		if (!in) return "";
		Sha256 s;
		std::vector<char> b(1 << 16);
		while (in)
		{
			in.read(b.data(), (std::streamsize)b.size());
			if (in.gcount() > 0) s.add((const unsigned char*)b.data(), (size_t)in.gcount());
		}
		return s.hex();
	}

	std::string parseSha256File(const std::string& text)
	{
		std::string hex;
		for (char c : text)
		{
			if (isxdigit((unsigned char)c)) hex += (char)tolower((unsigned char)c);
			else break;
		}
		return hex.size() == 64 ? hex : "";
	}

	time_t nextMidnight(time_t now)
	{
		struct tm t;
		localtime_r(&now, &t);
		t.tm_mday += 1;
		t.tm_hour = t.tm_min = t.tm_sec = 0;
		t.tm_isdst = -1;
		return mktime(&t);
	}
}
