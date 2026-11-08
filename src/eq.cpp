#include "eq.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace bp
{
	const int EQ_FREQS[EQ_BANDS] = {31, 62, 125, 250, 500, 1000, 2000, 4000, 8000, 16000};

	namespace
	{
		const char* CUSTOM = "Custom";
		const size_t NAME_MAX_BYTES = 24;

		std::string lower(std::string s)
		{
			for (auto& c : s)
				if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
			return s;
		}

		std::string trim(const std::string& s)
		{
			size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
			return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
		}

		bool hasControl(const std::string& s)
		{
			for (unsigned char c : s)
				if (c < 0x20 || c == 0x7f) return true;
			return false;
		}

		// "%.1f" with no snprintf-made "-0.0" and no ".0" tail
		std::string num(double g, bool compact)
		{
			char buf[32];
			snprintf(buf, sizeof buf, "%.1f", g + 0.0);
			std::string s = buf;
			if (s == "-0.0") s = "0.0";
			if (compact && s.size() > 2 && s.compare(s.size() - 2, 2, ".0") == 0) s.resize(s.size() - 2);
			return s;
		}
	}

	const std::vector<EqPreset>& builtinEqPresets()
	{
		static const std::vector<EqPreset> list = [] {
			static const struct { const char* name; EqGains g; } T[] = {
				{"Flat", {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
				{"Bass Boost", {3, 5, 5, 3, 1, 0, 0, 0, 0, 0}},
				{"Bass Cut", {-3, -5, -4, -2, 0, 0, 0, 0, 0, 0}},
				{"Treble Boost", {0, 0, 0, 0, 0, 1, 2, 4, 5, 5}},
				{"Loudness", {4, 4, 2, 0, -1, -1, 0, 2, 3, 4}},
				{"Vocal", {-3, -2, -1, 1, 3, 4, 3, 2, 0, -1}},
				{"Spoken Word", {-4, -3, -2, 0, 2, 4, 4, 3, 1, -2}},
				{"Rock", {4, 3, 2, 0, -1, -1, 1, 2, 3, 4}},
				{"Pop", {-1, 1, 3, 4, 2, 0, -1, -1, 0, 1}},
				{"Jazz", {3, 2, 1, 2, -1, -1, 0, 1, 2, 3}},
				{"Classical", {4, 3, 2, 1, 0, 0, 0, 1, 2, 3}},
				{"Electronic", {5, 4, 2, 0, -1, 1, 0, 1, 3, 4}},
			};
			std::vector<EqPreset> v;
			for (auto& t : T)
			{
				EqPreset p;
				p.name = t.name;
				p.gains = t.g;
				p.builtin = true;
				v.push_back(p);
			}
			return v;
		}();
		return list;
	}

	double clampEqGain(double g)
	{
		if (std::isnan(g)) return 0;
		g = std::max(-EQ_MAX_DB, std::min(EQ_MAX_DB, g));
		return std::round(g * 10) / 10 + 0.0;	// + 0.0: no negative zero
	}

	bool eqFlat(const EqGains& g)
	{
		for (double v : g)
			if (std::fabs(v) >= 0.05) return false;
		return true;
	}

	std::string formatEqGains(const EqGains& g)
	{
		std::string out;
		for (int i = 0; i < EQ_BANDS; i++) out += (i ? "," : "") + num(g[i], true);
		return out;
	}

	bool parseEqGains(const std::string& text, EqGains& out)
	{
		EqGains g{};
		size_t pos = 0;
		for (int i = 0; i < EQ_BANDS; i++)
		{
			size_t end = text.find(',', pos);
			if (end == std::string::npos) end = text.size();
			if (i < EQ_BANDS - 1 && end == text.size()) return false;	// too few
			std::string part = trim(text.substr(pos, end - pos));
			if (part.empty()) return false;
			char* stop = nullptr;
			double v = strtod(part.c_str(), &stop);
			if (*stop || !std::isfinite(v)) return false;
			g[i] = clampEqGain(v);
			pos = end + 1;
		}
		if (pos <= text.size()) return false;	// text left after the tenth: too many
		out = g;
		return true;
	}

	double eqResponseDb(const EqGains& gains, double hz, double rate)
	{
		const double pi = 3.14159265358979323846;
		double wr = 2 * pi * hz / rate;
		double cr = std::cos(wr), c2r = std::cos(2 * wr), sr = std::sin(wr), s2r = std::sin(2 * wr);
		double db = 0;
		for (int i = 0; i < EQ_BANDS; i++)
		{
			if (gains[i] == 0) continue;
			double A = std::pow(10.0, gains[i] / 40);
			double w0 = 2 * pi * EQ_FREQS[i] / rate;
			double alpha = std::sin(w0) * std::sinh(std::log(2.0) / 2 * w0 / std::sin(w0));
			double b0 = 1 + alpha * A, b1 = -2 * std::cos(w0), b2 = 1 - alpha * A;
			double a0 = 1 + alpha / A, a1 = b1, a2 = 1 - alpha / A;
			// |H(e^jw)|^2 = |b0 + b1 e^-jw + b2 e^-2jw|^2 / |a0 + a1 e^-jw + a2 e^-2jw|^2
			double nr = b0 + b1 * cr + b2 * c2r, ni = -(b1 * sr + b2 * s2r);
			double dr = a0 + a1 * cr + a2 * c2r, di = -(a1 * sr + a2 * s2r);
			db += 10 * std::log10((nr * nr + ni * ni) / (dr * dr + di * di));
		}
		return db;
	}

	double eqPeakDb(const EqGains& gains)
	{
		double peak = -1e9, step = std::pow(2.0, 1.0 / 12);
		for (double f = 20; f <= 20000 * 1.0001; f *= step) peak = std::max(peak, eqResponseDb(gains, f));
		return peak;
	}

	std::string eqBandTarget(int band) { return "equalizer@b" + std::to_string(band); }

	std::string eqPreGainArg(const EqGains& g)
	{
		double peak = std::max(0.0, eqPeakDb(g));
		// up to the next 0.1 dB: the rounded value must not leave the peak above 0 dBFS
		return num(-std::ceil(peak * 10 - 1e-9) / 10, false) + "dB";
	}

	std::string eqFilterString(const EqGains& g)
	{
		if (eqFlat(g)) return "";
		// float samples, so the pre-gain's cut and the boosts can't clip between the filters
		std::string s = "@bpeq:lavfi=[aformat=sample_fmts=fltp,volume@pre=volume=" + eqPreGainArg(g) + ":precision=float";
		for (int i = 0; i < EQ_BANDS; i++)
			s += "," + std::string("equalizer@b") + std::to_string(i) + "=f=" + std::to_string(EQ_FREQS[i]) + ":t=o:w=1:g="
				+ num(g[i], false) + ":precision=f64";
		return s + "]";
	}

	EqNameError checkEqPresetName(const std::string& raw, const std::vector<EqPreset>& user, const std::string& renaming)
	{
		std::string name = trim(raw);
		if (name.empty()) return EqNameError::Empty;
		if (name.size() > NAME_MAX_BYTES) return EqNameError::TooLong;
		if (hasControl(name)) return EqNameError::BadChar;
		std::string key = lower(name);
		if (key == lower(CUSTOM)) return EqNameError::Builtin;
		for (auto& b : builtinEqPresets())
			if (lower(b.name) == key) return EqNameError::Builtin;
		for (auto& u : user)
			if (lower(u.name) == key && u.name != renaming) return EqNameError::Taken;
		return EqNameError::None;
	}

	void EqPresetStore::rebuild()
	{
		std::stable_sort(user_.begin(), user_.end(), [](const EqPreset& a, const EqPreset& b) {
			std::string la = lower(a.name), lb = lower(b.name);
			return la != lb ? la < lb : a.name < b.name;
		});
		all_ = builtinEqPresets();
		all_.insert(all_.end(), user_.begin(), user_.end());
	}

	void EqPresetStore::parse(const std::string& text)
	{
		user_.clear();
		size_t pos = 0;
		while (pos < text.size())
		{
			size_t end = text.find('\n', pos);
			if (end == std::string::npos) end = text.size();
			std::string line = text.substr(pos, end - pos);
			pos = end + 1;
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line.empty() || line[0] == '#') continue;
			// ten gains, then the name (which may hold anything but a control character)
			size_t at = 0;
			std::string gains;
			bool ok = true;
			for (int i = 0; i < EQ_BANDS && ok; i++)
			{
				size_t tab = line.find('\t', at);
				if (tab == std::string::npos) ok = false;
				else
				{
					gains += (i ? "," : "") + line.substr(at, tab - at);
					at = tab + 1;
				}
			}
			EqGains g;
			if (!ok || !parseEqGains(gains, g)) continue;
			std::string name = trim(line.substr(at));
			if (name.empty() || hasControl(name)) continue;
			if (name.size() > NAME_MAX_BYTES) continue;
			// a name the app now owns: "Rock (2)", or the next free number
			std::string key = lower(name);
			bool clash = key == lower(CUSTOM);
			for (auto& b : builtinEqPresets()) clash = clash || lower(b.name) == key;
			if (clash)
			{
				std::string base = name;
				for (int n = 2;; n++)
				{
					name = base + " (" + std::to_string(n) + ")";
					key = lower(name);
					bool used = false;
					for (auto& u : user_) used = used || lower(u.name) == key;
					if (!used) break;
				}
			}
			bool dup = false;
			for (auto& u : user_) dup = dup || lower(u.name) == lower(name);
			if (dup) continue;	// the first one stays
			EqPreset p;
			p.name = name;
			p.gains = g;
			user_.push_back(p);
		}
		rebuild();
	}

	std::string EqPresetStore::serialize() const
	{
		std::string out = "# banana-player: equalizer presets (ten gains in dB, 31 Hz to 16 kHz, then the name)\n";
		for (auto& p : user_)
		{
			for (double g : p.gains) out += num(g, true) + "\t";
			out += p.name + "\n";
		}
		return out;
	}

	const EqPreset* EqPresetStore::find(const std::string& name) const
	{
		for (auto& p : all_)
			if (p.name == name) return &p;
		return nullptr;
	}

	EqNameError EqPresetStore::put(const std::string& raw, const EqGains& gains)
	{
		std::string name = trim(raw);
		// overwriting one of yours is fine: it's its own name
		EqNameError e = checkEqPresetName(name, user_, name);
		if (e != EqNameError::None) return e;
		EqGains g;
		for (int i = 0; i < EQ_BANDS; i++) g[i] = clampEqGain(gains[i]);
		for (auto& u : user_)
			if (u.name == name)
			{
				u.gains = g;
				rebuild();
				return EqNameError::None;
			}
		EqPreset p;
		p.name = name;
		p.gains = g;
		user_.push_back(p);
		rebuild();
		return EqNameError::None;
	}

	EqNameError EqPresetStore::rename(const std::string& from, const std::string& to)
	{
		std::string name = trim(to);
		EqNameError e = checkEqPresetName(name, user_, from);
		if (e != EqNameError::None) return e;
		for (auto& u : user_)
			if (u.name == from)
			{
				u.name = name;
				rebuild();
				return EqNameError::None;
			}
		return EqNameError::Builtin;	// not one of yours (a built-in's name, or none): nothing to rename
	}

	bool EqPresetStore::remove(const std::string& name)
	{
		for (size_t i = 0; i < user_.size(); i++)
			if (user_[i].name == name)
			{
				user_.erase(user_.begin() + i);
				rebuild();
				return true;
			}
		return false;
	}
}
