#include "AddressCheck.h"

#include "GameIDs.h"

namespace DROP::Address
{
	namespace
	{
		// Reads IDDB's lookup tables without calling offset(). get_id2offset() is protected; a pointer to
		// it may be formed through a derived class, then called on the real IDDB object.
		struct IDDBPeek : REL::IDDB
		{
			static std::span<std::uint32_t> V5(const REL::IDDB& a_db)
			{
				constexpr auto fn = &IDDBPeek::get_id2offset<std::uint32_t>;
				return (a_db.*fn)();
			}

			static std::span<REL::IDDB::MAPPING> V0(const REL::IDDB& a_db)
			{
				constexpr auto fn = &IDDBPeek::get_id2offset<REL::IDDB::MAPPING>;
				return (a_db.*fn)();
			}
		};

		// The same checks IDDB's constructor makes before it would call REX::FAIL: the plugin sits in
		// Data/SFSE/Plugins, and versionlib-<game version>.bin is there with a supported format.
		bool DatabaseLoadable()
		{
			wchar_t buffer[REX::W32::MAX_PATH]{};
			REX::W32::GetModuleFileNameW(REX::W32::GetCurrentModule(), buffer, REX::W32::MAX_PATH);
			const std::filesystem::path plugin(buffer);

			auto loader = plugin.parent_path().parent_path().filename().wstring();
			std::ranges::transform(loader, loader.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
			if (loader != L"SFSE") {
				REX::ERROR("Plugin is not inside an SFSE folder ({}); Address Library cannot be located",
					plugin.parent_path().string());
				return false;
			}

			const auto version = REX::FModule::GetExecutingModule().GetFileVersion();
			const auto path = plugin.parent_path() / std::format(L"versionlib-{}.bin", version.wstring(L"-"));

			std::error_code ec;
			if (!std::filesystem::exists(path, ec)) {
				REX::ERROR("Address Library for game version {} not found ({})", version.string(), path.string());
				return false;
			}

			std::ifstream file(path, std::ios::binary);
			std::uint32_t format = 0;
			if (!file.read(reinterpret_cast<char*>(&format), sizeof(format))) {
				REX::ERROR("Could not read the Address Library file {}", path.string());
				return false;
			}
			if (format != 1 && format != 2 && format != 5) {
				REX::ERROR("Unsupported Address Library format {} in {}", format, path.string());
				return false;
			}
			return true;
		}

		bool DatabaseOK()
		{
			static const bool ok = DatabaseLoadable();
			return ok;
		}
	}

	bool IsPresent(std::uint64_t a_id)
	{
		if (a_id == 0 || !DatabaseOK()) {
			return false;
		}
		const auto* db = REL::IDDB::GetSingleton();
		if (!db) {
			return false;
		}

		if (const auto v5 = IDDBPeek::V5(*db); !v5.empty()) {
			return a_id < v5.size() && v5[a_id] != 0;
		}

		const auto v0 = IDDBPeek::V0(*db);
		const auto it = std::ranges::lower_bound(v0, a_id, {}, &REL::IDDB::MAPPING::id);
		return it != v0.end() && it->id == a_id && it->offset != 0;
	}

	bool AllPresent(std::string* a_missing)
	{
		static std::string missing;
		static const bool  ok = []() {
			struct Need
			{
				std::uint64_t id;
				const char*   name;
			};
			// GameIDs.h, plus what CommonLibSF reaches for notifications, UI::IsMenuOpen and BSFixedString.
			const Need needs[]{
				{ GameID::PlayerSingleton.id(), "PlayerCharacter singleton" },
				{ GameID::PlayerCameraSingleton.id(), "PlayerCamera singleton" },
				{ GameID::ViewCasterSingleton.id(), "ViewCaster singleton" },
				{ GameID::InvalidRefHandle.id(), "invalid reference handle" },
				{ GameID::ViewCaster_GetActivatePickRef.id(), "ViewCaster::GetActivatePickRef" },
				{ GameID::GetSmartPointer.id(), "GetSmartPointer" },
				{ GameID::TESForm_DecRefCount.id(), "TESForm::DecRefCount" },
				{ GameID::MoveTo.id(), "TESObjectREFR::MoveTo" },
				{ GameID::LoadedDataHasFlag.id(), "LOADED_REF_DATA flag test" },
				{ GameID::SetMotionType3D.id(), "SetMotionType (3D)" },
				{ GameID::WakePhysics.id(), "wake physics" },
				{ GameID::HUDNotificationSource.id(), "HUD notification event source" },
				{ GameID::PerformInputProcessing.id(), "PlayerControls input pass" },
				{ GameID::PlayerControlsVtable.id(), "PlayerControls vtable" },
				{ RE::ID::BSTEventSource::Notify.id(), "BSTEventSource::Notify" },
				{ RE::ID::UI::Singleton.id(), "UI::Singleton" },
				{ RE::ID::UI::IsMenuOpen.id(), "UI::IsMenuOpen" },
				{ RE::ID::BSStringPool::GetEntry.id(), "BSStringPool::GetEntry" },
				{ RE::ID::BSStringPool::Entry::Release.id(), "BSStringPool::Entry::Release" },
				{ RE::ID::BSStringPool::BucketTable::GetSingleton.id(), "BSStringPool::BucketTable::GetSingleton" },
			};
			bool all = true;
			for (const auto& need : needs) {
				if (!IsPresent(need.id)) {
					REX::ERROR("Address Library ID {} ({}) is not available for this game version", need.id, need.name);
					missing += std::format("{}ID {} ({})", missing.empty() ? "" : ", ", need.id, need.name);
					all = false;
				}
			}
			return all;
		}();
		if (a_missing) {
			*a_missing = missing;
		}
		return ok;
	}
}
