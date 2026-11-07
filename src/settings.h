#pragma once
#include "substyle.h"
#include <string>

namespace bp
{
	// What the top panel shows for an audio file (mp3, flac...): its cover (embedded, or cover.jpg beside it), or a
	// visualizer drawn from the sound by FFmpeg's filters; the title, artist and album over either.
	enum AudioDisplay { AUDIO_AUTO, AUDIO_COVER_BARS, AUDIO_BARS, AUDIO_WAVES, AUDIO_SPECTRUM, AUDIO_COVER, AUDIO_DISPLAYS };
	// "Auto": the cover when there is one, else the bars. "Cover + bars": the bars over the cover's lower part (the
	// bars alone with no cover). Both wait for the file to say if it has a cover.
	const char* audioDisplayLabel(int d);
	// mpv's lavfi-complex for a visualizer: the first audio track to the speakers and, drawn, to the picture (Cover +
	// bars: over the first video track, the cover); "" for none (the cover, or nothing). Auto draws as Bars.
	std::string audioVisualizerGraph(int d);

	// <stateDir>/settings.conf: what the Settings page changes, for every file
	struct Settings
	{
		SubStyle style;
		// what a file opened again comes back with
		bool rememberPosition = true, rememberAudio = true, rememberSubs = true;
		// the browser opens where it was left, rather than at its first page
		bool rememberFolder = false;
		// the videos played, for Recently watched; off: none kept (and those kept so far gone)
		bool keepHistory = true;
		// the handheld's buttons while a video plays; off: only touch acts there (no press by accident in a pocket
		// or a hand), the menus' and the browser's buttons still work
		bool buttonHotkeys = true;
		int audioDisplay = AUDIO_AUTO;
		// folders with a ".hidden" file in them: listed (and their videos in Recent) only when this is on
		bool showHiddenFolders = false;
	};

	std::string serializeSettings(const Settings& s);
	Settings parseSettings(const std::string& text);
}
