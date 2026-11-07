#pragma once
#include <SDL.h>
#include <string>

namespace bp
{
	// The handheld's buttons by their printed names (Nintendo layout: A is the east button, B the south one), as
	// banana-ui reads them.
	// L3: the stick pressed in (the RG DS Plus has the left one only)
	enum class Button { None, A, B, X, Y, L, R, Up, Down, Left, Right, Start, Select, L3 };

	// a button pressed (repeats included), from a pad or, on a computer, the keyboard; None for anything else
	Button buttonFromEvent(const SDL_Event& e);

	// "A", "Down", "Select"... (any case) for scripted runs; None for anything else
	Button buttonFromName(const std::string& name);

	// opens pads as they come (the handheld's own buttons are one)
	void handlePadHotplug(const SDL_Event& e);
}
