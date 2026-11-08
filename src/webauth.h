// The web dashboard's PIN, its sessions and the PIN gate (bpcore, no window). tests/core_test.cpp covers it.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>

namespace bp
{
	// 21 characters: nothing that looks like another (0/O 1/I/L 2/Z 5/S 8/B G/6 Q U/V)
	extern const char WEB_PIN_ALPHABET[];
	const int WEB_PIN_LENGTH = 4;
	const int64_t WEB_SESSION_S = 7 * 24 * 3600;	// a session's life; check() asks for a renewal past half of it

	// 4 characters of the alphabet, upper case. rng gives uniform 32-bit values ({}: OpenSSL's RAND_bytes);
	// rejection sampling keeps every character equally likely.
	std::string makePin(const std::function<uint32_t()>& rng = {});
	bool validPin(const std::string&);	// 4 characters from the alphabet, any case
	bool pinMatches(const std::string& typed, const std::string& pin);	// case-insensitive, constant time

	// ok: the PIN was right and the IP wasn't locked. left: tries before the lockout (0 when locked). retryAfterS: the
	// lockout's seconds, else 0. rotate: set once when 30 wrong tries from anywhere came within 10 minutes.
	struct PinResult { bool ok; int left; int retryAfterS; bool rotate; };

	class PinGate
	{
	public:
		// `correct` is the caller's own compare. A locked IP is refused whatever it typed, and doesn't count again.
		PinResult attempt(const std::string& ip, bool correct, int64_t nowMs);
		int lockedFor(const std::string& ip, int64_t nowMs) const;	// seconds (rounded up), 0 = not locked
		void reset();	// forgets everything (a new PIN)

	private:
		struct Ip { int fails = 0; int lockouts = 0; int64_t lockedUntilMs = 0; };
		std::map<std::string, Ip> ips_;
		int64_t windowStartMs_ = 0;
		int windowFails_ = 0;
	};

	std::string newToken(const std::function<uint32_t()>& rng = {});	// 64 hex characters (32 random bytes)
	std::string hashToken(const std::string& token);	// SHA-256, 64 lower-case hex characters

	// Sessions by the hash of their token. Text: one "<hash>\t<expiresAtS>" line each.
	class SessionStore
	{
	public:
		void parse(const std::string& text);	// replaces everything; malformed lines are skipped
		std::string serialize() const;
		void add(const std::string& hash, int64_t expiresAtS);
		// true: known and not expired. renew: less than half of WEB_SESSION_S is left.
		bool check(const std::string& hash, int64_t nowS, bool& renew) const;
		void renew(const std::string& hash, int64_t expiresAtS);	// only when known
		void remove(const std::string& hash);
		void clear();
		size_t size() const { return s_.size(); }

	private:
		std::map<std::string, int64_t> s_;
	};

	// The first cookie called `name` in a Cookie header; "" if none.
	std::string cookieValue(const std::string& cookieHeader, const std::string& name);
}
