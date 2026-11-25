#include "AddressCheck.h"
#include "Drag.h"
#include "InputHook.h"
#include "OSF.h"
#include "Settings.h"

namespace
{
	bool g_active = false;

	void OnMessage(SFSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}
		switch (a_msg->type) {
		case SFSE::MessagingInterface::kPostLoad:
			// OSF Settings starts its services during its own plugin load; kPostLoad is the earliest
			// point that doesn't depend on load order. Also delivers any queued Mod Issues.
			DROP::OSF::Connect();
			break;
		case SFSE::MessagingInterface::kPostDataLoad:
			if (!g_active) {
				break;
			}
			// The frame hook is in place since plugin load; the work starts now that the game data is loaded.
			DROP::InputHook::SetFrameCallback(&DROP::Drag::OnFrame);
			REX::INFO("Ready");
			break;
		default:
			break;
		}
	}
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
	SFSE::Init(a_sfse, { .trampoline = true, .trampolineSize = 64 });

	REX::INFO("{} {} loading", SFSE::GetPluginName(), SFSE::GetPluginVersion().string());
	DROP::Settings::Get().Load();
	if (DROP::Settings::Get().debugLog) {
		REX::INFO("Detailed log on (bDebugLog = 1)");
	}

	// Check every Address Library ID up front, so a missing one leaves the game untouched
	// instead of ending it.
	std::string missing;
	g_active = DROP::Address::AllPresent(&missing);
	if (!g_active) {
		REX::ERROR("The plugin stays inactive");
		DROP::OSF::ReportIssue("address-ids", "Drop Overhaul is inactive",
			"Objects can't be picked up. Missing from the Address Library: " + missing + ".",
			"Install the Address Library for SFSE Plugins that matches your game version, or update Drop Overhaul.");
	}

	// Not an SFSE task: those run several times a frame, in parallel, where moving objects isn't safe.
	if (g_active && !DROP::InputHook::Install()) {
		g_active = false;
		DROP::OSF::ReportIssue("frame-hook", "Drop Overhaul is inactive",
			"Objects can't be picked up: the game's input code isn't what this version expects.",
			"Check for an update of Drop Overhaul.");
	}

	const auto* messaging = SFSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		REX::ERROR("Could not register for SFSE messages; the plugin stays inactive");
	}
	return true;
}
