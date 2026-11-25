#include "OSF.h"

#include "Settings.h"

#include "vendor/OSFSettings.h"
#include "vendor/OSFSettings_Diagnostics.h"

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
		OSFSettings::API::Diagnostics::Client g_diagnostics;
		std::vector<Issue>                    g_pendingIssues;  // before Connect; load thread only
		std::atomic<bool>                     g_dirty{ false };
		bool                                  g_connected{ false };

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
			ReadBool("holdMode", s.holdMode);
			ReadFloat("mouseRotateSpeed", s.mouseRotateSpeed);
			ReadFloat("wheelRotateStep", s.wheelRotateStep);
			ReadBool("dropOnPlace", s.dropOnPlace);
			ReadBool("rotateWithView", s.rotateWithView);
			ReadBool("allowStatic", s.allowStatic);
			ReadFloat("rotateSpeed", s.rotateSpeed);
			ReadFloat("moveSpeedMeters", s.moveSpeed);
			ReadFloat("fineMultiplier", s.fineMultiplier);
			ReadFloat("minDistanceMeters", s.minDistance);
			ReadFloat("maxDistanceMeters", s.maxDistance);
			ReadBool("notifications", s.notifications);
			if (s.minDistance > s.maxDistance) {
				std::swap(s.minDistance, s.maxDistance);
			}
			REX::INFO("Settings from OSF Settings: pick-up key {:#04x}, {} mode, drop when placed {}, turn with you {}, objects without physics {}",
				s.grabKey, s.holdMode ? "hold" : "builder", s.dropOnPlace, s.rotateWithView, s.allowStatic);
		}

		// Runs on an SFSE task, without a main-thread guarantee: only flag the change here.
		void OnChanged(const char*, const char*, void*) noexcept
		{
			g_dirty.store(true);
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
			REX::INFO("OSF Settings not found; using the INI");
			return;
		}

		// Subscribe before the first read, so no change falls in between.
		OSFSettings::API::Subscription subscription{};
		if (const auto status = g_settings.Subscribe(kModID, OnChanged, nullptr, &subscription); status != Status::Ok) {
			REX::WARN("OSF Settings: could not subscribe to changes (status {}); values are read once", std::to_underlying(status));
		}
		g_connected = true;

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
}
