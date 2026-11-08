#pragma once
#include <string>

namespace bp
{
	// Music's sound effect: one at a time, independent of the equalizer. The reverbs are afir convolutions with
	// impulse responses we synthesize (no recordings, so no licences); Wide and Headphones are plain FFmpeg filters
	enum class SoundFx { None, Room, Hall, Church, Cave, Wide, Headphones };
	constexpr int SOUNDFX_COUNT = 7;

	// "Concert hall": what the page lists
	const char* soundFxName(SoundFx);
	// "hall": what settings.conf keeps (and what names a state-dir IR file)
	const char* soundFxKey(SoundFx);
	// an unknown key gives None
	SoundFx soundFxFromKey(const std::string&);
	bool soundFxIsReverb(SoundFx);

	// the bytes of a 16-bit stereo 48 kHz WAV impulse response, "" for what isn't a reverb. The same bytes every
	// run (a fixed-seed generator). The direct sound isn't in it: afir's dry carries that
	std::string soundFxIrWav(SoundFx);

	// mpv's `af` entry, "" for None. A reverb reads its impulse response from irPath (escaped for lavfi, twice:
	// as an option's value and as part of the graph)
	std::string soundFxFilter(SoundFx, const std::string& irPath);
}
