#include "webauth.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <openssl/evp.h>
#include <openssl/rand.h>

namespace bp
{
	const char WEB_PIN_ALPHABET[] = "34679ACDEFHJKMNPRTWXY";
	static const uint32_t ALPHABET_N = 21;
	static const int FAILS_PER_IP = 5;
	static const int64_t FIRST_LOCK_MS = 60 * 1000, MAX_LOCK_MS = 15 * 60 * 1000;
	static const int GLOBAL_FAILS = 30;
	static const int64_t GLOBAL_WINDOW_MS = 10 * 60 * 1000;

	static uint32_t randomWord()
	{
		uint32_t v;
		// a PIN from a failed generator would be guessable: stop rather than carry on
		if (RAND_bytes((unsigned char*)&v, sizeof v) != 1) std::abort();
		return v;
	}

	std::string makePin(const std::function<uint32_t()>& rng)
	{
		auto next = rng ? rng : std::function<uint32_t()>(randomWord);
		// 2^32 isn't a multiple of 21: values from `limit` up would favour the first characters
		const uint64_t limit = (uint64_t(1) << 32) - ((uint64_t(1) << 32) % ALPHABET_N);
		std::string pin;
		while (int(pin.size()) < WEB_PIN_LENGTH)
		{
			uint32_t v = next();
			if (v >= limit) continue;
			pin += WEB_PIN_ALPHABET[v % ALPHABET_N];
		}
		return pin;
	}

	static char upper(char c) { return c >= 'a' && c <= 'z' ? char(c - 32) : c; }

	bool validPin(const std::string& s)
	{
		if (int(s.size()) != WEB_PIN_LENGTH) return false;
		for (char c : s)
		{
			char u = upper(c);
			if (u == 0 || !std::char_traits<char>::find(WEB_PIN_ALPHABET, ALPHABET_N, u)) return false;
		}
		return true;
	}

	bool pinMatches(const std::string& typed, const std::string& pin)
	{
		// every position is looked at whatever the lengths are, so the time says nothing about the PIN
		unsigned diff = typed.size() != pin.size();
		size_t n = std::max(typed.size(), pin.size());
		for (size_t i = 0; i < n; i++)
		{
			unsigned char a = i < typed.size() ? upper(typed[i]) : 0, b = i < pin.size() ? upper(pin[i]) : 0;
			diff |= a ^ b;
		}
		return diff == 0;
	}

	PinResult PinGate::attempt(const std::string& ip, bool correct, int64_t nowMs)
	{
		Ip& e = ips_[ip];
		if (nowMs < e.lockedUntilMs)
			return {false, 0, int((e.lockedUntilMs - nowMs + 999) / 1000), false};
		if (correct)
		{
			ips_.erase(ip);
			return {true, FAILS_PER_IP, 0, false};
		}
		PinResult r{false, 0, 0, false};
		if (windowFails_ == 0 || nowMs - windowStartMs_ >= GLOBAL_WINDOW_MS)
		{
			windowStartMs_ = nowMs;
			windowFails_ = 0;
		}
		if (++windowFails_ >= GLOBAL_FAILS)
		{
			r.rotate = true;
			windowFails_ = 0;
		}
		if (++e.fails >= FAILS_PER_IP)
		{
			int64_t lock = FIRST_LOCK_MS;
			for (int i = 0; i < e.lockouts && lock < MAX_LOCK_MS; i++) lock *= 2;
			lock = std::min(lock, MAX_LOCK_MS);
			e.lockouts++;
			e.fails = 0;
			e.lockedUntilMs = nowMs + lock;
			r.retryAfterS = int(lock / 1000);
		}
		else
			r.left = FAILS_PER_IP - e.fails;
		return r;
	}

	int PinGate::lockedFor(const std::string& ip, int64_t nowMs) const
	{
		auto it = ips_.find(ip);
		if (it == ips_.end() || nowMs >= it->second.lockedUntilMs) return 0;
		return int((it->second.lockedUntilMs - nowMs + 999) / 1000);
	}

	void PinGate::reset()
	{
		ips_.clear();
		windowFails_ = 0;
		windowStartMs_ = 0;
	}

	static std::string hex(const unsigned char* b, size_t n)
	{
		static const char H[] = "0123456789abcdef";
		std::string s;
		for (size_t i = 0; i < n; i++) { s += H[b[i] >> 4]; s += H[b[i] & 15]; }
		return s;
	}

	std::string newToken(const std::function<uint32_t()>& rng)
	{
		auto next = rng ? rng : std::function<uint32_t()>(randomWord);
		unsigned char b[32];
		for (int i = 0; i < 8; i++)
		{
			uint32_t v = next();
			for (int k = 0; k < 4; k++) b[i * 4 + k] = (unsigned char)(v >> (8 * k));
		}
		return hex(b, 32);
	}

	std::string hashToken(const std::string& token)
	{
		unsigned char md[EVP_MAX_MD_SIZE];
		unsigned len = 0;
		if (EVP_Digest(token.data(), token.size(), md, &len, EVP_sha256(), nullptr) != 1) return "";
		return hex(md, len);
	}

	static bool hexHash(const std::string& h)
	{
		if (h.size() != 64) return false;
		for (char c : h)
			if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
		return true;
	}

	void SessionStore::parse(const std::string& text)
	{
		s_.clear();
		size_t pos = 0;
		while (pos < text.size())
		{
			size_t nl = text.find('\n', pos);
			std::string line = text.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
			pos = nl == std::string::npos ? text.size() : nl + 1;
			size_t tab = line.find('\t');
			if (tab == std::string::npos) continue;
			std::string h = line.substr(0, tab), e = line.substr(tab + 1);
			if (!hexHash(h) || e.empty() || e.size() > 18) continue;
			bool digits = std::all_of(e.begin(), e.end(), [](char c) { return c >= '0' && c <= '9'; });
			if (digits) s_[h] = std::atoll(e.c_str());
		}
	}

	std::string SessionStore::serialize() const
	{
		std::string out;
		for (auto& kv : s_) out += kv.first + "\t" + std::to_string(kv.second) + "\n";
		return out;
	}

	void SessionStore::add(const std::string& hash, int64_t expiresAtS) { s_[hash] = expiresAtS; }

	bool SessionStore::check(const std::string& hash, int64_t nowS, bool& renew) const
	{
		renew = false;
		auto it = s_.find(hash);
		if (it == s_.end() || it->second <= nowS) return false;
		renew = it->second - nowS < WEB_SESSION_S / 2;
		return true;
	}

	void SessionStore::renew(const std::string& hash, int64_t expiresAtS)
	{
		auto it = s_.find(hash);
		if (it != s_.end()) it->second = expiresAtS;
	}

	void SessionStore::remove(const std::string& hash) { s_.erase(hash); }
	void SessionStore::clear() { s_.clear(); }

	std::string cookieValue(const std::string& header, const std::string& name)
	{
		size_t pos = 0;
		while (pos <= header.size())
		{
			size_t semi = header.find(';', pos);
			std::string part = header.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
			pos = semi == std::string::npos ? header.size() + 1 : semi + 1;
			size_t a = part.find_first_not_of(" \t");
			if (a == std::string::npos) continue;
			part = part.substr(a);
			size_t eq = part.find('=');
			if (eq == std::string::npos || part.compare(0, eq, name) != 0 || eq != name.size()) continue;
			std::string v = part.substr(eq + 1);
			size_t b = v.find_last_not_of(" \t");
			return b == std::string::npos ? std::string() : v.substr(0, b + 1);
		}
		return "";
	}
}
