#include "keyboard.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace bp
{
	namespace
	{
		struct Spec
		{
			const char* label;
			int units;	// width, in elevenths of the keyboard
			KeyAction action;
		};

		// five rows of 11 units. Letters: what host names, shares, users and most passwords need without a page
		// change ('.', '-', '_', '@', '\' for DOMAIN\user)
		const std::vector<std::vector<Spec>>& letterRows()
		{
			static const std::vector<std::vector<Spec>> rows = {
				{{"1", 1, KeyAction::Text}, {"2", 1, KeyAction::Text}, {"3", 1, KeyAction::Text}, {"4", 1, KeyAction::Text},
					{"5", 1, KeyAction::Text}, {"6", 1, KeyAction::Text}, {"7", 1, KeyAction::Text}, {"8", 1, KeyAction::Text},
					{"9", 1, KeyAction::Text}, {"0", 1, KeyAction::Text}, {"Del", 1, KeyAction::Back}},
				{{"q", 1, KeyAction::Text}, {"w", 1, KeyAction::Text}, {"e", 1, KeyAction::Text}, {"r", 1, KeyAction::Text},
					{"t", 1, KeyAction::Text}, {"y", 1, KeyAction::Text}, {"u", 1, KeyAction::Text}, {"i", 1, KeyAction::Text},
					{"o", 1, KeyAction::Text}, {"p", 1, KeyAction::Text}, {"-", 1, KeyAction::Text}},
				{{"a", 1, KeyAction::Text}, {"s", 1, KeyAction::Text}, {"d", 1, KeyAction::Text}, {"f", 1, KeyAction::Text},
					{"g", 1, KeyAction::Text}, {"h", 1, KeyAction::Text}, {"j", 1, KeyAction::Text}, {"k", 1, KeyAction::Text},
					{"l", 1, KeyAction::Text}, {".", 1, KeyAction::Text}, {"_", 1, KeyAction::Text}},
				{{"Shift", 2, KeyAction::Shift}, {"z", 1, KeyAction::Text}, {"x", 1, KeyAction::Text}, {"c", 1, KeyAction::Text},
					{"v", 1, KeyAction::Text}, {"b", 1, KeyAction::Text}, {"n", 1, KeyAction::Text}, {"m", 1, KeyAction::Text},
					{"@", 1, KeyAction::Text}, {"\\", 1, KeyAction::Text}},
				{{"#+=", 2, KeyAction::Page}, {"<", 1, KeyAction::Left}, {"Space", 4, KeyAction::Space}, {">", 1, KeyAction::Right},
					{"Done", 3, KeyAction::Done}},
			};
			return rows;
		}

		const std::vector<std::vector<Spec>>& symbolRows()
		{
			static const std::vector<std::vector<Spec>> rows = {
				{{"1", 1, KeyAction::Text}, {"2", 1, KeyAction::Text}, {"3", 1, KeyAction::Text}, {"4", 1, KeyAction::Text},
					{"5", 1, KeyAction::Text}, {"6", 1, KeyAction::Text}, {"7", 1, KeyAction::Text}, {"8", 1, KeyAction::Text},
					{"9", 1, KeyAction::Text}, {"0", 1, KeyAction::Text}, {"Del", 1, KeyAction::Back}},
				{{"!", 1, KeyAction::Text}, {"@", 1, KeyAction::Text}, {"#", 1, KeyAction::Text}, {"$", 1, KeyAction::Text},
					{"%", 1, KeyAction::Text}, {"^", 1, KeyAction::Text}, {"&", 1, KeyAction::Text}, {"*", 1, KeyAction::Text},
					{"(", 1, KeyAction::Text}, {")", 1, KeyAction::Text}, {"~", 1, KeyAction::Text}},
				{{"-", 1, KeyAction::Text}, {"_", 1, KeyAction::Text}, {"=", 1, KeyAction::Text}, {"+", 1, KeyAction::Text},
					{"[", 1, KeyAction::Text}, {"]", 1, KeyAction::Text}, {"{", 1, KeyAction::Text}, {"}", 1, KeyAction::Text},
					{";", 1, KeyAction::Text}, {":", 1, KeyAction::Text}, {"'", 1, KeyAction::Text}},
				{{"\"", 1, KeyAction::Text}, {"`", 1, KeyAction::Text}, {"|", 1, KeyAction::Text}, {"\\", 1, KeyAction::Text},
					{"/", 1, KeyAction::Text}, {"<", 1, KeyAction::Text}, {">", 1, KeyAction::Text}, {"?", 1, KeyAction::Text},
					{",", 1, KeyAction::Text}, {".", 2, KeyAction::Text}},
				{{"abc", 2, KeyAction::Page}, {"<", 1, KeyAction::Left}, {"Space", 4, KeyAction::Space}, {">", 1, KeyAction::Right},
					{"Done", 3, KeyAction::Done}},
			};
			return rows;
		}

		int centre(const KeyDef& k) { return k.x + k.w / 2; }
	}

	std::vector<KeyDef> keyboardLayout(bool symbols, ShiftState shift, int x, int y, int w, int h)
	{
		const auto& rows = symbols ? symbolRows() : letterRows();
		std::vector<KeyDef> out;
		int nRows = (int)rows.size();
		for (int r = 0; r < nRows; r++)
		{
			int units = 0;
			for (auto& s : rows[r]) units += s.units;
			int top = y + r * h / nRows, bottom = y + (r + 1) * h / nRows, at = 0;
			for (auto& s : rows[r])
			{
				KeyDef k;
				k.label = s.label;
				k.action = s.action;
				if (s.action == KeyAction::Text)
				{
					k.text = s.label;
					// a letter shifted: its capital, drawn as it types
					if (!symbols && shift != ShiftState::Off && k.text.size() == 1 && islower((unsigned char)k.text[0]))
						k.text = k.label = std::string(1, (char)toupper((unsigned char)k.text[0]));
				}
				if (s.action == KeyAction::Shift && shift == ShiftState::Locked) k.label = "SHIFT";
				k.row = r;
				// edges by the running total: the keys tile the row exactly, whatever w divides into
				k.x = x + at * w / units;
				at += s.units;
				k.w = x + at * w / units - k.x;
				k.y = top;
				k.h = bottom - top;
				out.push_back(k);
			}
		}
		return out;
	}

	int keyboardMove(const std::vector<KeyDef>& keys, int from, int dx, int dy)
	{
		if (keys.empty()) return -1;
		if (from < 0 || from >= (int)keys.size()) return 0;
		int row = keys[from].row, rows = keys.back().row + 1;
		if (dx)
		{
			// along the row, round at its ends
			std::vector<int> in;
			for (int i = 0; i < (int)keys.size(); i++)
				if (keys[i].row == row) in.push_back(i);
			int at = 0;
			for (int i = 0; i < (int)in.size(); i++)
				if (in[i] == from) at = i;
			int n = (int)in.size();
			return in[((at + dx) % n + n) % n];
		}
		if (!dy) return from;
		int target = ((row + dy) % rows + rows) % rows, best = from, bestDist = 1 << 30;
		for (int i = 0; i < (int)keys.size(); i++)
			if (keys[i].row == target)
			{
				// the key under it (wide ones, Space, take what's above them), else the nearest
				int c = centre(keys[from]);
				int d = c >= keys[i].x && c < keys[i].x + keys[i].w ? -1 : std::abs(centre(keys[i]) - c);
				if (d < bestDist) { bestDist = d; best = i; }
			}
		return best;
	}

	int keyboardHit(const std::vector<KeyDef>& keys, int x, int y)
	{
		for (int i = 0; i < (int)keys.size(); i++)
			if (keys[i].hit(x, y)) return i;
		return -1;
	}

	int keyboardHome(const std::vector<KeyDef>& keys)
	{
		for (int i = 0; i < (int)keys.size(); i++)
			if (keys[i].row == 1) return i;
		return 0;
	}

	// a UTF-8 continuation byte (10xxxxxx): not a place for the caret
	static bool cont(char c) { return ((unsigned char)c & 0xC0) == 0x80; }

	void TextEdit::insert(const std::string& s)
	{
		caret = std::min(caret, text.size());
		text.insert(caret, s);
		caret += s.size();
	}

	void TextEdit::backspace()
	{
		caret = std::min(caret, text.size());
		if (!caret) return;
		size_t from = caret - 1;
		while (from > 0 && cont(text[from])) from--;
		text.erase(from, caret - from);
		caret = from;
	}

	void TextEdit::left()
	{
		caret = std::min(caret, text.size());
		if (!caret) return;
		caret--;
		while (caret > 0 && cont(text[caret])) caret--;
	}

	void TextEdit::right()
	{
		if (caret >= text.size()) { caret = text.size(); return; }
		caret++;
		while (caret < text.size() && cont(text[caret])) caret++;
	}
}
