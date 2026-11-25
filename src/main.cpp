#include "AddressCheck.h"
#include "Drag.h"
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
			if (const auto* tasks = SFSE::GetTaskInterface()) {
				// Runs once per frame on the game thread (SFSE's command-processing hook).
				tasks->AddPermanentTask([]() {
					DROP::OSF::ApplyChanges();
					DROP::Drag::OnFrame();
				});
				REX::INFO("Ready");
			} else {
				REX::ERROR("SFSE task interface unavailable; the plugin stays inactive");
			}
			break;
		default:
			break;
		}
	}
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
	SFSE::Init(a_sfse);

	REX::INFO("{} {} loading", SFSE::GetPluginName(), SFSE::GetPluginVersion().string());
	DROP::Settings::Get().Load();

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

	const auto* messaging = SFSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		REX::ERROR("Could not register for SFSE messages; the plugin stays inactive");
	}
	return true;
}
