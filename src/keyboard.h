// The on-screen keyboard's layout and the text it edits, with no window: the bottom panel draws the keys and sends
// the presses (Ui). Two pages, letters and symbols; Shift for one letter, twice for all of them. Keys are laid out in
// the panel's 640x480 logical units.
#pragma once
#include <string>
#include <vector>

namespace bp
{
	enum class KeyAction { Text, Shift, Back, Space, Page, Done, Left, Right };

	struct KeyDef
	{
		std::string label;	// as drawn
		std::string text;	// what it types (Text)
		KeyAction action = KeyAction::Text;
		int row = 0, x = 0, y = 0, w = 0, h = 0;
		bool hit(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
	};

	enum class ShiftState { Off, Once, Locked };

	// symbols: the second page; the keys fill {x, y, w, h}
	std::vector<KeyDef> keyboardLayout(bool symbols, ShiftState shift, int x, int y, int w, int h);
	// the key a D-pad press goes to from `from`: along its row (round at the ends), or to the row above or below
	// (round too), on the key nearest under the one it left
	int keyboardMove(const std::vector<KeyDef>& keys, int from, int dx, int dy);
	// the key where a finger is, -1 none
	int keyboardHit(const std::vector<KeyDef>& keys, int x, int y);
	// the first letter (q): where the cursor starts
	int keyboardHome(const std::vector<KeyDef>& keys);

	// a line of text and its caret (a byte offset, always at a UTF-8 character's start)
	struct TextEdit
	{
		std::string text;
		size_t caret = 0;
		void set(const std::string& s) { text = s; caret = s.size(); }
		void insert(const std::string& s);
		void backspace();	// the character before the caret
		void left();
		void right();
	};
}
