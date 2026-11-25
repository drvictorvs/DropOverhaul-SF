#pragma once

// Optional OSF Settings integration (https://github.com/ozooma10/osf-settings-slim).
//
// With OSFSettings.dll loaded:
// - the settings page from Data/SFSE/Plugins/OSF/Settings/schemas/dropoverhaul-sf.json supplies the
//   values (the INI is then only a fallback), and changes apply while the game runs;
// - problems are listed in its Mod Issues tab.
// Without it, the INI is used. Either way the plugin reads the keys itself, once per frame.
namespace DROP::OSF
{
	inline constexpr const char* kModID = "dropoverhaul-sf";

	// Records a problem for the Mod Issues tab. Safe to call before Connect: reports are queued
	// and sent when OSF Settings is reached.
	void ReportIssue(const char* a_id, std::string a_title, std::string a_impact, std::string a_nextSteps, bool a_error = true);

	// At SFSE kPostLoad: attaches to OSF Settings if it's loaded, subscribes to value changes, reads
	// the values, and sends queued issues.
	void Connect();

	// On the game thread, once per frame: applies values changed in OSF Settings since last time.
	void ApplyChanges();
}
