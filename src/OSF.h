#pragma once

// Optional OSF Settings integration (https://github.com/ozooma10/osf-settings-slim).
//
// With OSFSettings.dll loaded:
// - the settings page from Data/SFSE/Plugins/OSF/Settings/schemas/dropoverhaul-sf.json supplies the
//   values (the INI is then only a fallback), and changes apply while the game runs;
// - the four press actions (pick up / place, put back, upright, drops / stays) come from OSF's own
//   gameplay input path instead of polling the keyboard, including mouse buttons;
// - problems are listed in its Mod Issues tab.
// Without it, the INI is used and keys are polled each frame, as before.
namespace DROP::OSF
{
	inline constexpr const char* kModID = "dropoverhaul-sf";

	enum class Press : std::uint32_t
	{
		Grab,
		Cancel,
		ResetRotation,
		FreezeToggle,
		kCount
	};

	// Records a problem for the Mod Issues tab. Safe to call before Connect: reports are queued
	// and sent when OSF Settings is reached.
	void ReportIssue(const char* a_id, std::string a_title, std::string a_impact, std::string a_nextSteps, bool a_error = true);

	// At SFSE kPostLoad: attaches to OSF Settings if it's loaded, subscribes to value changes and
	// key presses, reads the values, and sends queued issues.
	void Connect();

	// On the game thread, once per frame: applies values changed in OSF Settings since last time.
	void ApplyChanges();

	// True when OSF delivers the press actions; the plugin then doesn't poll those keys itself.
	bool DeliversPresses();

	// Takes (and clears) a press OSF delivered since the last call.
	bool TakePress(Press a_press);

	// Drops presses that arrived while the plugin couldn't act on them.
	void ClearPresses();
}
