#include "soundfx.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace bp
{
	namespace
	{
		const int RATE = 48000;

		struct Reflection
		{
			double ms, gain, pan;	// pan: -1 left .. 1 right
		};

		struct Room
		{
			double t60, preMs;
			double lpStart, lpEnd, lpTau;	// the tail's one-pole low-pass: Hz at the start, Hz it falls to, seconds
			double tailEnergy;				// the tail's energy against the direct sound's (1), per channel
			double earlyGain;			// the reflections' scale (each is ~1 at gain 1, the direct sound's level)
			double dry, wet;			// afir's input and output gains: its auto-gain leaves the response ~28-36 dB down, these bring a song back to its level (about 1.5 dB under, for headroom)
			std::vector<Reflection> early;
		};

		const Room& roomOf(SoundFx fx)
		{
			static const Room room = {0.5, 5, 9000, 2500, 0.25, 0.3, 0.5, 4.5, 4.5,
				{{7, 0.9, -0.6}, {11, 0.7, 0.7}, {16, 0.55, -0.3}, {23, 0.45, 0.4}, {31, 0.3, -0.8}}};
			static const Room hall = {2.2, 20, 7000, 1800, 1.2, 0.5, 0.4, 7.3, 7.3,
				{{27, 0.7, -0.7}, {41, 0.6, 0.6}, {58, 0.5, -0.2}, {77, 0.45, 0.8}, {96, 0.35, -0.5}, {123, 0.3, 0.3}}};
			static const Room church = {3.5, 30, 5000, 900, 1.8, 0.5, 0.4, 7.8, 7.8,
				{{38, 0.65, -0.8}, {63, 0.55, 0.7}, {91, 0.5, -0.3}, {127, 0.4, 0.5}, {171, 0.35, -0.6}, {229, 0.3, 0.2}}};
			static const Room cave = {4.0, 15, 4500, 1100, 2.0, 0.5, 0.6, 7.6, 7.6,
				{{60, 1.0, -0.7}, {140, 0.85, 0.7}, {260, 0.7, -0.4}, {410, 0.45, 0.5}, {23, 0.5, 0.3}}};
			switch (fx)
			{
			case SoundFx::Room: return room;
			case SoundFx::Hall: return hall;
			case SoundFx::Church: return church;
			default: return cave;
			}
		}

		// xorshift64*: fixed-seed, so an IR is the same bytes everywhere (the standard's generators aren't)
		struct Rng
		{
			uint64_t s;
			uint64_t next()
			{
				s ^= s >> 12;
				s ^= s << 25;
				s ^= s >> 27;
				return s * 2685821657736338717ULL;
			}
			// -1..1
			double uni() { return (double)(next() >> 11) / 4503599627370496.0 - 1.0; }
			// roughly normal: four uniforms
			double gauss() { return (uni() + uni() + uni() + uni()) * 0.8660254; }
		};

		void put16(std::string& s, int v)
		{
			s += (char)(v & 0xff);
			s += (char)((v >> 8) & 0xff);
		}

		void put32(std::string& s, uint32_t v)
		{
			for (int i = 0; i < 4; i++) s += (char)((v >> (8 * i)) & 0xff);
		}

		// '\' ' and the graph's own characters, with a backslash
		std::string escapeOnce(const std::string& s, const char* special)
		{
			std::string out;
			for (char c : s)
			{
				if (c && strchr(special, c)) out += '\\';
				out += c;
			}
			return out;
		}

		std::string num(double v)
		{
			char buf[32];
			snprintf(buf, sizeof buf, "%g", v);
			return buf;
		}
	}

	const char* soundFxName(SoundFx fx)
	{
		switch (fx)
		{
		case SoundFx::Room: return "Room";
		case SoundFx::Hall: return "Concert hall";
		case SoundFx::Church: return "Church";
		case SoundFx::Cave: return "Cave";
		case SoundFx::Wide: return "Wide stereo";
		case SoundFx::Headphones: return "Headphones";
		default: return "None";
		}
	}

	const char* soundFxKey(SoundFx fx)
	{
		switch (fx)
		{
		case SoundFx::Room: return "room";
		case SoundFx::Hall: return "hall";
		case SoundFx::Church: return "church";
		case SoundFx::Cave: return "cave";
		case SoundFx::Wide: return "wide";
		case SoundFx::Headphones: return "headphones";
		default: return "none";
		}
	}

	SoundFx soundFxFromKey(const std::string& key)
	{
		for (int i = 0; i < SOUNDFX_COUNT; i++)
			if (key == soundFxKey((SoundFx)i)) return (SoundFx)i;
		return SoundFx::None;
	}

	bool soundFxIsReverb(SoundFx fx)
	{
		return fx == SoundFx::Room || fx == SoundFx::Hall || fx == SoundFx::Church || fx == SoundFx::Cave;
	}

	std::string soundFxIrWav(SoundFx fx)
	{
		if (!soundFxIsReverb(fx)) return "";
		const Room& r = roomOf(fx);
		const int n = (int)(r.t60 * 1.15 * RATE);	// a little past -60 dB, so it ends in silence not a click
		const int pre = (int)(r.preMs * RATE / 1000);
		std::vector<double> ch[2] = {std::vector<double>(n, 0.0), std::vector<double>(n, 0.0)};

		Rng rng{0x9e3779b97f4a7c15ULL ^ (uint64_t)(fx) * 0x100000001b3ULL};
		for (int c = 0; c < 2; c++)
		{
			double lp = 0;
			for (int i = pre; i < n; i++)
			{
				double t = (double)(i - pre) / RATE;
				double env = std::pow(10.0, -3.0 * t / r.t60);
				// darker as it goes: the cutoff falls from lpStart toward lpEnd
				double fc = r.lpEnd + (r.lpStart - r.lpEnd) * std::exp(-t / r.lpTau);
				double a = 1 - std::exp(-2 * M_PI * fc / RATE);
				// the tail swells in over ~15 ms so it doesn't start with a burst
				double fade = std::min(1.0, t / 0.015);
				lp += a * (rng.gauss() - lp);
				ch[c][i] += lp * env * fade;
			}
			// the tail's loudness is set here, not by the noise's scale: a long tail is more energy at the same level
			double e = 0;
			for (double x : ch[c]) e += x * x;
			if (e > 0)
				for (double& x : ch[c]) x *= std::sqrt(r.tailEnergy / e);
		}
		// afir's dry is an input gain, not a mix, so the direct sound has to be in the response
		ch[0][0] += 1;
		ch[1][0] += 1;
		for (auto& e : r.early)
		{
			int at = pre + (int)(e.ms * RATE / 1000);
			if (at >= n) continue;
			// constant-power pan, so a reflection is as loud as its gain says
			double th = (e.pan + 1) * M_PI / 4;
			ch[0][at] += e.gain * std::cos(th) * 1.4142 * r.earlyGain;
			ch[1][at] += e.gain * std::sin(th) * 1.4142 * r.earlyGain;
		}

		double peak = 0;
		for (auto& v : ch)
			for (double x : v) peak = std::max(peak, std::fabs(x));
		if (peak <= 0) peak = 1;

		std::string out;
		uint32_t bytes = (uint32_t)n * 4;
		out.reserve(44 + bytes);
		out += "RIFF";
		put32(out, 36 + bytes);
		out += "WAVEfmt ";
		put32(out, 16);
		put16(out, 1);	// PCM
		put16(out, 2);
		put32(out, RATE);
		put32(out, RATE * 4);
		put16(out, 4);
		put16(out, 16);
		out += "data";
		put32(out, bytes);
		for (int i = 0; i < n; i++)
			for (int c = 0; c < 2; c++) put16(out, (int)std::lround(ch[c][i] / peak * 0.9 * 32767));
		return out;
	}

	std::string soundFxFilter(SoundFx fx, const std::string& irPath)
	{
		if (soundFxIsReverb(fx))
		{
			const Room& r = roomOf(fx);
			// as the option's value (\ ' :), then in the graph (\ ' [ ] , ;)
			std::string path = escapeOnce(escapeOnce(irPath, "\\':"), "\\'[],;");
			return "@bpfx:lavfi=[amovie=" + path + "[ir];[in][ir]afir=dry=" + num(r.dry) + ":wet=" + num(r.wet) + ",alimiter=limit=0.98:level=0]";
		}
		if (fx == SoundFx::Wide) return "@bpfx:lavfi=[volume=-2.5dB,extrastereo=m=1.6:c=0,alimiter=limit=0.98:level=0]";
		if (fx == SoundFx::Headphones) return "@bpfx:lavfi=[crossfeed]";
		return "";
	}
}
