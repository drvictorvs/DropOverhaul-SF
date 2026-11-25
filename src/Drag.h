#pragma once

namespace DROP::Drag
{
	// Runs once per frame on the game thread (an SFSE permanent task): reads the keys, picks up,
	// moves, places or puts back the held object.
	void OnFrame();
}
