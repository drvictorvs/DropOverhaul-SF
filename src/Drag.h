#pragma once

namespace DROP::Drag
{
	// Runs once per frame, inside the game's own frame update (InputHook): reads the keys, picks up,
	// moves, places or puts back the held object. a_head is the frame's input events, one of the two
	// sources for the keys (with Windows' key state).
	void OnFrame(const RE::InputEvent* a_head);
}
