#pragma once

namespace DROP::Drag
{
	// Runs once per frame, inside the game's own frame update (InputHook): reads the keys, picks up,
	// moves, places or puts back the held object. a_head is the frame's input events, one of the two
	// sources for the keys (with Windows' key state).
	void OnFrame(const RE::InputEvent* a_head);

	// Runs before the game reads the frame's input (InputHook). While an object is held, takes the
	// keys and mouse input that would otherwise make the game act: Activate (E) places the object,
	// the mouse buttons and the mouse movement while one is held turn it, the wheel turns it in steps.
	void FilterInput(RE::InputEvent* a_head);
}
