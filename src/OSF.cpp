#include "OSF.h"

#include "Settings.h"

#include "vendor/OSFSettings.h"
#include "vendor/OSFSettings_Diagnostics.h"
#include "vendor/OSFSettings_Providers.h"

namespace DROP::OSF
{
	namespace
	{
		using OSFSettings::API::Status;

		struct Issue
		{
			std::string id;
			std::string title;
			std::string impact;
			std::string nextSteps;
			bool        error{ true };
		};

		OSFSettings::API::Client              g_settings;
		OSFSettings::API::Providers::Client   g_providers;
		OSFSettings::API::Diagnostics::Client g_diagnostics;
		std::vector<Issue>                    g_pendingIssues;  // before Connect; load thread only
		std::atomic<bool>                     g_dirty{ false };
		bool                                  g_connected{ false };
		bool                                  g_deliversPresses{ false };

		constexpr auto kPressCount = static_cast<std::size_t>(Press::kCount);
		std::array<std::atomic<bool>, kPressCount> g_pressed{};

		// Key setting behind each press action, in Press order.
		constexpr std::array<const char*, kPressCount> kPressKeys{ "grabKey", "cancelKey", "resetRotationKey", "freezeToggleKey" };

		void Send(const Issue& a_issue)
		{
			const auto status = g_diagnostics.Report({
				.modId = kModID,
				.id = a_issue.id.c_str(),
				.severity = a_issue.error ? OSFSettings::API::Diagnostics::Severity::Error : OSFSettings::API::Diagnostics::Severity::Warning,
				.title = a_issue.title.c_str(),
				.impact = a_issue.impact.empty() ? nullptr : a_issue.impact.c_str(),
				.nextSteps = a_issue.nextSteps.empty() ? nullptr : a_issue.nextSteps.c_str(),
			});
			if (status != Status::Ok) {
				REX::WARN("OSF Settings did not accept issue {} (status {})", a_issue.id, std::to_underlying(status));
			}
		}

		void Warn(const char* a_key, Status a_status)
		{
			REX::WARN("OSF Settings: could not read {} (status {}); keeping the INI value", a_key, std::to_underlying(a_status));
		}

		void ReadBool(const char* a_key, bool& a_value)
		{
			bool value = a_value;
			if (const auto status = g_settings.GetBool(kModID, a_key, &value); status == Status::Ok) {
				a_value = value;
			} else {
				Warn(a_key, status);
			}
		}

		void ReadFloat(const char* a_key, double& a_value)
		{
			double value = a_value;
			if (const auto status = g_settings.GetFloat(kModID, a_key, &value); status == Status::Ok) {
				a_value = value;
			} else {
				Warn(a_key, status);
			}
		}

		void ReadKey(const char* a_key, std::uint32_t& a_value)
		{
			std::uint32_t value = a_value;
			if (const auto status = g_settings.GetKey(kModID, a_key, &value); status == Status::Ok) {
				a_value = value == OSFSettings::API::kUnboundKey ? 0 : value;  // 0 = off for the polling code
			} else {
				Warn(a_key, status);
			}
		}

		void Read()
		{
			auto& s = Settings::Get();
			ReadKey("grabKey", s.grabKey);
			ReadKey("cancelKey", s.cancelKey);
			ReadKey("yawLeftKey", s.yawLeftKey);
			ReadKey("yawRightKey", s.yawRightKey);
			ReadKey("pitchUpKey", s.pitchUpKey);
			ReadKey("pitchDownKey", s.pitchDownKey);
			ReadKey("rollLeftKey", s.rollLeftKey);
			ReadKey("rollRightKey", s.rollRightKey);
			ReadKey("resetRotationKey", s.resetRotationKey);
			ReadKey("pushKey", s.pushKey);
			ReadKey("pullKey", s.pullKey);
			ReadKey("raiseKey", s.raiseKey);
			ReadKey("lowerKey", s.lowerKey);
			ReadKey("fineKey", s.fineKey);
			ReadKey("freezeToggleKey", s.freezeToggleKey);
			ReadBool("dropOnPlace", s.dropOnPlace);
			ReadBool("rotateWithView", s.rotateWithView);
			ReadBool("allowStatic", s.allowStatic);
			ReadFloat("rotateSpeed", s.rotateSpeed);
			ReadFloat("moveSpeed", s.moveSpeed);
			ReadFloat("fineMultiplier", s.fineMultiplier);
			ReadFloat("minDistance", s.minDistance);
			ReadFloat("maxDistance", s.maxDistance);
			ReadBool("notifications", s.notifications);
			ReadBool("debugLog", s.debugLog);
			if (s.minDistance > s.maxDistance) {
				std::swap(s.minDistance, s.maxDistance);
			}
			REX::INFO("Settings from OSF Settings: pick-up key {:#04x}, drop when placed {}, turn with you {}, objects without physics {}",
				s.grabKey, s.dropOnPlace, s.rotateWithView, s.allowStatic);
		}

		// Runs on an SFSE task, without a main-thread guarantee: only flag the change here.
		void OnChanged(const char*, const char*, void*) noexcept
		{
			g_dirty.store(true);
		}

		// Runs inline on OSF's input dispatch, for gameplay key-down edges only.
		void OnKeyPress(const char*, const char*, void* a_context) noexcept
		{
			const auto index = reinterpret_cast<std::uintptr_t>(a_context);
			if (index < kPressCount) {
				g_pressed[index].store(true);
			}
		}

		bool SubscribeKeys()
		{
			if (!g_providers.Init()) {
				REX::INFO("OSF Settings key service not available; polling the keys");
				return false;
			}
			for (std::size_t i = 0; i < kPressCount; ++i) {
				OSFSettings::API::Subscription subscription{};
				const auto status = g_providers.SubscribeKey(kModID, kPressKeys[i], OnKeyPress, reinterpret_cast<void*>(i), &subscription);
				if (status != Status::Ok) {
					REX::WARN("OSF Settings: could not watch {} (status {}); polling the keys", kPressKeys[i], std::to_underlying(status));
					return false;  // the subscriptions made so far fire harmlessly into flags nobody reads
				}
			}
			return true;
		}
	}

	void ReportIssue(const char* a_id, std::string a_title, std::string a_impact, std::string a_nextSteps, bool a_error)
	{
		Issue issue{ a_id, std::move(a_title), std::move(a_impact), std::move(a_nextSteps), a_error };
		if (g_diagnostics) {
			Send(issue);
		} else {
			g_pendingIssues.push_back(std::move(issue));
		}
	}

	void Connect()
	{
		if (g_diagnostics.Init()) {
			for (const auto& issue : g_pendingIssues) {
				Send(issue);
			}
			g_pendingIssues.clear();
		}

		if (!g_settings.Init()) {
			REX::INFO("OSF Settings not found; using the INI and polling the keys");
			return;
		}

		// Subscribe before the first read, so no change falls in between.
		OSFSettings::API::Subscription subscription{};
		if (const auto status = g_settings.Subscribe(kModID, OnChanged, nullptr, &subscription); status != Status::Ok) {
			REX::WARN("OSF Settings: could not subscribe to changes (status {}); values are read once", std::to_underlying(status));
		}
		g_connected = true;
		g_deliversPresses = SubscribeKeys();
		if (g_deliversPresses) {
			REX::INFO("Pick up / place, put back, upright and drops/stays keys come from OSF Settings");
		}

		if (g_settings.IsReady()) {
			Read();
		} else {
			// Its first change notification arrives when it's ready.
			REX::INFO("OSF Settings found but not ready yet; reading values when it is");
		}
	}

	void ApplyChanges()
	{
		if (g_connected && g_dirty.exchange(false)) {
			Read();
		}
	}

	bool DeliversPresses()
	{
		return g_deliversPresses;
	}

	bool TakePress(Press a_press)
	{
		const auto index = static_cast<std::size_t>(a_press);
		return index < kPressCount && g_pressed[index].exchange(false);
	}

	void ClearPresses()
	{
		for (auto& pressed : g_pressed) {
			pressed.store(false);
		}
	}
}
