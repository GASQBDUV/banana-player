#pragma once
#include "substyle.h"
#include <string>

namespace bp
{
	// Music on the top panel: its album art (embedded, or cover.jpg beside it), a visualizer drawn from the sound by
	// FFmpeg's filters, or the art with the visualizer over its lower part; a song with no art shows the visualizer
	// whatever this says. The title, artist and album over either.
	// None: neither, the title alone on black (the lightest)
	enum MusicShow { SHOW_ART, SHOW_BOTH, SHOW_VIS, SHOW_NONE, SHOWS };
	// Circle: the bars round a ring (cyan to purple to pink), drawn by looking each pixel up in two tables (remap)
	enum Visualizer { VIS_BARS, VIS_WAVES, VIS_SPECTRUM, VIS_CIRCLE, VISUALIZERS };
	// music only (a video never loops): all = back to the folder's first file after its last
	enum Loop { LOOP_OFF, LOOP_ALL, LOOP_ONE, LOOPS };
	const char* musicShowLabel(int v);
	const char* visualizerLabel(int v);
	const char* loopLabel(int v);
	// mpv's lavfi-complex: the first audio track to the speakers and, drawn, to the picture; overCover: over the first
	// video track (the art), on its lower part
	// mapsDir: where writeCircleMaps put Circle's tables; without them Circle is drawn as Bars
	std::string visualizerGraph(int vis, bool overCover, const std::string& mapsDir = "");
	// Circle's tables, remap's xmap and ymap as 16-bit PGMs, in dir (made once, kept); false if they can't be written
	bool writeCircleMaps(const std::string& dir);
	// a folder a filter graph can name (FFmpeg's option parser splits at ':' and '=' even inside quotes)
	bool lavfiSafePath(const std::string& dir);
	// Circle's picture, the panel's shape: the circle a little low, under the song's title
	constexpr int CIRCLE_W = 640, CIRCLE_H = 480;
	// None's picture: black, a tiny one at 5 frames a second drawn from the sound (so it ends with it): the last
	// visualizer's frame would stay otherwise
	std::string blankGraph();

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
		int musicShow = SHOW_ART, visualizer = VIS_BARS, loop = LOOP_OFF;
		// folders with a ".hidden" file in them: listed (and their videos in Recent) only when this is on
		bool showHiddenFolders = false;
		// the battery's percent beside its icon on the bottom panel (the icon is always there)
		bool batteryPercent = false;
		// photos: the next one opens at the zoom (and the place in it) the last was left at; off: each at its size
		bool keepPhotoZoom = true;
		// the photo's pan area on the bottom panel: the left half (the default) or the right
		bool panRight = false;
		// a drag in the pan area moves the photo (as on a phone: it follows the finger on the top panel) or the
		// box over the small photo (the top panel follows the box)
		bool dragBox = false;
	};

	std::string serializeSettings(const Settings& s);
	Settings parseSettings(const std::string& text);
}
