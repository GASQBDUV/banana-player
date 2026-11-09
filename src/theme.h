#pragma once
// The player's look in one place: its colours by role and its corners (docs/design/midnight.md). The bottom panel
// (ui.cpp), the top panel's ASS cards (player.cpp) and the web dashboard's web/style.css (by hand) follow it.
#include <cstdio>
#include <string>

namespace bp
{
	namespace theme
	{
		struct Rgb
		{
			unsigned char r, g, b;
		};

		// Midnight: true black, so the bottom panel matches the black around a video on the top one
		constexpr Rgb BG{0x00, 0x00, 0x00};
		constexpr Rgb SURFACE{0x12, 0x11, 0x16};	// tiles, Recent cards, dialogs' bodies
		constexpr Rgb BUTTON{0x26, 0x24, 0x2E};	// buttons, unselected tabs, the focused row's fill
		constexpr Rgb PRESSED{0x3E, 0x3B, 0x4A};	// a button while held
		constexpr Rgb ACCENT{0xFF, 0xD2, 0x3F};	// key hints, Playing, the bar's fill, the selected tab, the cursor
		constexpr Rgb ON_ACCENT{0x00, 0x00, 0x00};	// ink on an accent (or danger) fill
		constexpr Rgb TEXT{0xE8, 0xE2, 0xD3};
		constexpr Rgb DIM{0x8F, 0x8A, 0x80};	// secondary text on BG / SURFACE, disabled ink
		constexpr Rgb DIM_ON{0xAB, 0xA5, 0x97};	// secondary text on a button or the focused row
		constexpr Rgb DANGER{0xF0, 0x64, 0x5A};
		constexpr Rgb SCREEN{0x26, 0x24, 0x2E};	// the subtitle preview's box, a photo's pan area
		constexpr Rgb TRACK{0x1E, 0x1D, 0x24};	// a bar's empty part, keyboard special keys, dimmed chips
		constexpr Rgb KNOB = TEXT;
		constexpr Rgb RULE{0x2A, 0x28, 0x33};	// thin lines between rows

		// the top panel (mpv's OSD)
		constexpr Rgb TOP_TITLE = ACCENT;	// the idle title and logo, a card's level, "Playing in your browser"
		constexpr Rgb TOP_SUB = DIM;
		constexpr Rgb TOP_CARD{0x0B, 0x0A, 0x0D};	// the volume / chapter / sound cards' body
		constexpr Rgb TOP_TRACK = BUTTON;
		constexpr Rgb TOP_SQUARE = SURFACE;	// the logo's dark square

		// corners, in the bottom panel's 640x480 units (the top panel's ASS cards in its 960x720)
		constexpr int R_BUTTON = 10;	// buttons, tabs, the focused row
		constexpr int R_TILE = 14;	// Home's cards and tiles, Settings' group tiles
		constexpr int R_DIALOG = 18;	// dialogs, pop-ups
		constexpr int R_CHIP = 6;
		constexpr int R_FIELD = 4;	// text fields, keyboard keys
		constexpr int R_BAR = 6;	// the progress bar's track
		constexpr int R_KNOB = 2;
		constexpr int R_TOP_CARD = 18;
		constexpr int R_TOP_BAR = 4;
		constexpr int EDGE = 2;	// a box's edge, inside its outline
		constexpr int LIP = 3;	// the darker band above its bottom edge

		// a box's edge: its fill 30% towards the text; its lip: the fill 30% darker (both rounded, as the mock's)
		constexpr unsigned char towards(int a, int b)
		{
			return (unsigned char)(a + ((b - a) * 3 + ((b - a) >= 0 ? 5 : -5)) / 10);
		}
		constexpr Rgb edgeOf(Rgb c)
		{
			return {towards(c.r, TEXT.r), towards(c.g, TEXT.g), towards(c.b, TEXT.b)};
		}
		constexpr Rgb lipOf(Rgb c)
		{
			return {(unsigned char)((c.r * 7 + 5) / 10), (unsigned char)((c.g * 7 + 5) / 10), (unsigned char)((c.b * 7 + 5) / 10)};
		}

		// "&HBBGGRR&", as ASS writes a colour
		inline std::string ass(Rgb c)
		{
			char s[12];
			snprintf(s, sizeof s, "&H%02X%02X%02X&", c.b, c.g, c.r);
			return s;
		}
	}
}
