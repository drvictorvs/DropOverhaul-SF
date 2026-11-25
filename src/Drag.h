#pragma once

namespace DROP::Drag
{
	// Runs once per frame on the game thread (InputHook): reads the keys, picks up, moves, places or
	// puts back the held object. a_head is the frame's input events, read only for the debug log.
	void OnFrame(const RE::InputEvent* a_head);
}
