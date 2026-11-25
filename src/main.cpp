#include "AddressCheck.h"
#include "Drag.h"
#include "Settings.h"

namespace
{
	void OnMessage(SFSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg || a_msg->type != SFSE::MessagingInterface::kPostDataLoad) {
			return;
		}
		const auto* tasks = SFSE::GetTaskInterface();
		if (!tasks) {
			REX::ERROR("SFSE task interface unavailable; the plugin stays inactive");
			return;
		}
		// Runs once per frame on the game thread (SFSE's command-processing hook).
		tasks->AddPermanentTask([]() { DROP::Drag::OnFrame(); });
		REX::INFO("Ready");
	}
}

SFSE_PLUGIN_LOAD(const SFSE::LoadInterface* a_sfse)
{
	SFSE::Init(a_sfse);

	REX::INFO("{} {} loading", SFSE::GetPluginName(), SFSE::GetPluginVersion().string());
	DROP::Settings::Get().Load();

	// Check every Address Library ID up front, so a missing one leaves the game untouched
	// instead of ending it.
	if (!DROP::Address::AllPresent()) {
		REX::ERROR("The plugin stays inactive");
		return true;
	}

	const auto* messaging = SFSE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnMessage)) {
		REX::ERROR("Could not register for SFSE messages");
		return false;
	}
	return true;
}
