#pragma once

// Address Library safety net (same approach as MoreInformativeConsole-SF).
//
// CommonLibSF resolves an ID the first time it is used. If the ID is missing for the running game
// version, REL::IDDB::offset() calls REX::FAIL, which ends the game. So every ID this plugin uses is
// checked once at load, without going through offset(); if any is missing, the plugin stays inactive.

namespace DROP::Address
{
	// True when the Address Library loads and the ID is non-zero and has an offset in it.
	// Never calls REX::FAIL.
	bool IsPresent(std::uint64_t a_id);

	// Checks every ID the plugin uses (GameIDs.h and the CommonLibSF calls it makes).
	// Logs each missing one and, if a_missing is given, lists them there. Runs the check once;
	// afterwards returns the cached result (and lists nothing).
	bool AllPresent(std::string* a_missing = nullptr);
}
