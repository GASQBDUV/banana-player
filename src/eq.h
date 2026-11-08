#pragma once
#include <array>
#include <string>
#include <vector>

namespace bp
{
	// Music's equalizer: ten bands, each a peaking filter (FFmpeg's equalizer, an octave wide) in one mpv `af`
	constexpr int EQ_BANDS = 10;
	constexpr double EQ_MAX_DB = 12;
	extern const int EQ_FREQS[EQ_BANDS];	// 31 62 125 250 500 1000 2000 4000 8000 16000
	using EqGains = std::array<double, EQ_BANDS>;

	struct EqPreset
	{
		std::string name;
		EqGains gains{};
		bool builtin = false;
	};
	// Flat first, then in the order the page lists them
	const std::vector<EqPreset>& builtinEqPresets();

	// within ±EQ_MAX_DB, to 0.1 (what the page steps by and the files keep); NaN gives 0
	double clampEqGain(double);
	bool eqFlat(const EqGains&);
	// "3,5,5,3,1,0,0,0,0,0": the C locale whatever the program's, so a file moves between machines
	std::string formatEqGains(const EqGains&);
	// exactly EQ_BANDS numbers, each wholly read; each clamped. false leaves the gains as they were
	bool parseEqGains(const std::string&, EqGains&);

	// the curve's gain at hz, in dB: the bands summed, each FFmpeg's equalizer biquad (RBJ peaking, bandwidth in
	// octaves: alpha = sin(w0) * sinh(ln2/2 * w * w0/sin(w0)), w = 1)
	double eqResponseDb(const EqGains&, double hz, double rate = 48000);
	// the curve's highest point (1/12-octave steps, 20 Hz to 20 kHz): what the pre-gain takes off so a boost can't clip
	double eqPeakDb(const EqGains&);

	// mpv's `af` value, "" when flat. All bands are always in it when it isn't, so af-command can reach each one
	std::string eqFilterString(const EqGains&);
	// af-command's target for a band: "equalizer@b3"
	std::string eqBandTarget(int band);
	// the pre-gain's value: "-6.5dB" (minus the peak, never above 0 dB, rounded up so it can't clip)
	std::string eqPreGainArg(const EqGains&);

	enum class EqNameError { None, Empty, TooLong, BadChar, Builtin, Taken };
	// trimmed, 1-24 bytes, no control characters, not a built-in's name nor "Custom" nor another user preset's
	// (ASCII case aside); `renaming` is the preset being renamed, which may keep its own name
	EqNameError checkEqPresetName(const std::string&, const std::vector<EqPreset>& user, const std::string& renaming = "");

	// eq-presets.tsv: `# banana-player: ...`, then one line per preset, `g1<TAB>...<TAB>g10<TAB>name`
	class EqPresetStore
	{
	public:
		void parse(const std::string& text);
		std::string serialize() const;
		// yours, alphabetical
		const std::vector<EqPreset>& user() const { return user_; }
		// the built-ins, then yours
		const std::vector<EqPreset>& all() const { return all_; }
		const EqPreset* find(const std::string& name) const;
		// a new user preset, or overwrite one of yours; a built-in's name is refused. Stores the trimmed name
		EqNameError put(const std::string& name, const EqGains&);
		EqNameError rename(const std::string& from, const std::string& to);
		bool remove(const std::string& name);

	private:
		void rebuild();
		std::vector<EqPreset> user_, all_;
	};
}
