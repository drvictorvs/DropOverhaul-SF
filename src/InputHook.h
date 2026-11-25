#pragma once

// The plugin's frame: a hook on the player-controls input pass (PerformInputProcessing, ID 82442),
// which the game runs once per frame as part of its frame update (one call at a time, though not
// always on the same thread). The plugin's work runs right after it.
//
// The hook is a jump written at the start of the function, so it doesn't take the vtable slot that
// other plugins (Dialogue Movement Enabler, for one) hook and check. If the function's start isn't
// what this version expects, it falls back to the vtable slot, calling whatever was there.
namespace DROP::InputHook
{
	using FrameFn = void (*)(const RE::InputEvent* a_head);
	using FilterFn = void (*)(RE::InputEvent* a_head);

	// At plugin load. Needs the SFSE trampoline. Returns false if nothing could be installed.
	bool Install();

	// Starts (or, with nullptr, stops) calling a_fn after each input pass.
	void SetFrameCallback(FrameFn a_fn);

	// Starts (or stops) calling a_fn before each input pass, with the events the game is about to
	// act on: it may change them (rename, disable, zero mouse movement) so the game ignores them.
	void SetInputFilter(FilterFn a_fn);
}
