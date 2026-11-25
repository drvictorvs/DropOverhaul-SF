#include "Drag.h"

#include "Engine.h"
#include "GameIDs.h"
#include "OSF.h"
#include "Settings.h"

// user32 / kernel32. Declared here because the shared Windows wrappers don't have them.
extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int a_key);
extern "C" __declspec(dllimport) void* __stdcall GetForegroundWindow();
extern "C" __declspec(dllimport) unsigned long __stdcall GetWindowThreadProcessId(void* a_window, unsigned long* a_processId);
extern "C" __declspec(dllimport) unsigned long __stdcall GetCurrentProcessId();

namespace DROP::Drag
{
	namespace
	{
		using Engine::Vec3;

		constexpr float kDegToRad = 0.0174532925F;
		constexpr float kPi = 3.14159265F;

		float WrapAngle(float a_angle)
		{
			a_angle = std::fmod(a_angle + kPi, 2.0F * kPi);
			if (a_angle < 0.0F) {
				a_angle += 2.0F * kPi;
			}
			return a_angle - kPi;
		}

		bool Held(std::uint32_t a_key)
		{
			return a_key != 0 && (GetAsyncKeyState(static_cast<int>(a_key)) & 0x8000) != 0;
		}

		// Acts on the press, not while held.
		struct Edge
		{
			bool held{ false };
			bool Pressed(std::uint32_t a_key)
			{
				const bool now = Held(a_key);
				const bool pressed = now && !held;
				held = now;
				return pressed;
			}
		};

		struct HeldObject
		{
			Engine::RefPtr ref;
			Vec3           originalPosition;
			Vec3           originalAngle;
			bool           keyframedByUs{ false };
			bool           dropOnPlace{ true };
			float          distance{ 150.0F };
			float          height{ 0.0F };
			float          yaw{ 0.0F };  // relative to the view's yaw when bRotateWithView, else absolute
			float          pitch{ 0.0F };
			float          roll{ 0.0F };
		};

		std::optional<HeldObject>             g_held;
		Edge                                  g_grab;
		Edge                                  g_cancel;
		Edge                                  g_reset;
		Edge                                  g_freeze;
		std::chrono::steady_clock::time_point g_lastFrame{};

		bool GameHasFocus()
		{
			void* const window = GetForegroundWindow();
			unsigned long processId = 0;
			if (window) {
				GetWindowThreadProcessId(window, &processId);
			}
			return processId == GetCurrentProcessId();
		}

		const RE::BSFixedString& LoadingMenu()
		{
			// Never freed: avoids releasing strings after the game's string pool is gone at exit.
			static const auto* name = new RE::BSFixedString("LoadingMenu");
			return *name;
		}

		// Menus during which keys are ignored and the held object waits. Only menus that open and close
		// with the player's actions: a menu that stays open the whole session (in Fallout 4, FaderMenu
		// did) would switch the plugin off for good. Returns the open one, or null.
		const char* BlockingMenu()
		{
			struct Menu
			{
				const char*       name;
				RE::BSFixedString fixed;
			};
			// Never freed: avoids releasing strings after the game's string pool is gone at exit.
			static const auto* menus = []() {
				auto* list = new std::vector<Menu>;
				for (const char* name : { "Console", "PauseMenu", "InventoryMenu", "ContainerMenu", "BarterMenu",
						 "DataMenu", "DialogueMenu", "SleepWaitMenu", "PhotoModeMenu", "WorkshopMenu",
						 "SpaceshipEditorMenu", "MainMenu", "PickpocketMenu", "LevelUpMenu", "SkillsMenu",
						 "GalaxyStarMapMenu", "TerminalMenu", "BookMenu", "LockpickingMenu" }) {
					list->push_back({ name, RE::BSFixedString(name) });
				}
				return list;
			}();
			const auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return "UI (not created yet)";
			}
			for (const auto& menu : *menus) {
				if (ui->IsMenuOpen(menu.fixed)) {
					return menu.name;
				}
			}
			return nullptr;
		}

		void Tell(const char* a_text)
		{
			if (Settings::Get().notifications) {
				Engine::Notify(a_text);
			}
		}

		Vec3 TargetAngle(const HeldObject& a_held, const Engine::View& a_view)
		{
			const float yaw = Settings::Get().rotateWithView ? Engine::YawOf(a_view.forward) + a_held.yaw : a_held.yaw;
			return { WrapAngle(a_held.pitch), WrapAngle(a_held.roll), WrapAngle(yaw) };
		}

		Vec3 TargetPosition(const HeldObject& a_held, const Engine::View& a_view)
		{
			return a_view.origin + a_view.forward * a_held.distance + Vec3{ 0.0F, 0.0F, a_held.height };
		}

		// Debug log: the button presses the game itself received this frame (device, code, action name),
		// to compare with the keys set for this plugin.
		void LogButtonPresses(const RE::InputEvent* a_head)
		{
			static std::uint32_t logged = 0;
			for (const auto* event = a_head; event && logged < 500; event = event->next) {
				if (event->eventType != RE::InputEvent::EventType::kButton) {
					continue;
				}
				const auto* button = static_cast<const RE::ButtonEvent*>(event);
				if (button->value == 0.0F || button->heldDownSecs > 0.0F) {
					continue;  // released, or still held from an earlier frame
				}
				++logged;
				const char* action = button->strUserEvent.c_str();
				REX::INFO("Game input: device {} code {:#x} action '{}'", std::to_underlying(button->deviceType),
					static_cast<std::uint32_t>(button->idCode), action ? action : "");
			}
		}

		void TryPickUp()
		{
			const auto& settings = Settings::Get();

			auto ref = Engine::GetCrosshairRef();
			if (!ref) {
				REX::INFO("Nothing to pick up: no activation target under the crosshair");
				Tell("Nothing to pick up there");
				return;
			}
			auto* const object = ref.get();
			if (object == Engine::GetPlayer() || Engine::IsActor(object)) {
				REX::INFO("{:08X} is an actor; actors are not moved", Engine::FormID(object));
				Tell("Can't move characters");
				return;
			}
			if (!Engine::ParentCell(object) || !Engine::Loaded3D(object)) {
				REX::INFO("{:08X} has no cell or no 3D loaded; not picked up", Engine::FormID(object));
				Tell("Can't move that");
				return;
			}
			const auto view = Engine::GetView();
			if (!view) {
				REX::INFO("No player camera; not picked up");
				return;
			}

			HeldObject held;
			held.originalPosition = Engine::Position(object);
			held.originalAngle = Engine::Angle(object);
			held.dropOnPlace = settings.dropOnPlace;

			held.keyframedByUs = Engine::SetMotionType(object, GameID::kMotionKeyframed);
			if (!held.keyframedByUs && !settings.allowStatic) {
				REX::INFO("{:08X} has no physics to hold it by; not picked up (bAllowStatic = 1 moves it anyway)", Engine::FormID(object));
				Tell("Can't move that");
				return;
			}

			// Start at the object's current distance, so it doesn't jump towards or away from you.
			held.distance = std::clamp((held.originalPosition - view->origin).Length(),
				static_cast<float>(settings.minDistance), static_cast<float>(settings.maxDistance));
			held.pitch = held.originalAngle.x;
			held.roll = held.originalAngle.y;
			held.yaw = settings.rotateWithView ? WrapAngle(held.originalAngle.z - Engine::YawOf(view->forward)) : held.originalAngle.z;

			REX::INFO("Picked up {:08X}{} at distance {:.0f}", Engine::FormID(object), held.keyframedByUs ? "" : " (no physics)", held.distance);
			if (settings.debugLog) {
				REX::INFO("  view origin ({:.1f}, {:.1f}, {:.1f}) forward ({:.3f}, {:.3f}, {:.3f}) first person {}; object at ({:.1f}, {:.1f}, {:.1f})",
					view->origin.x, view->origin.y, view->origin.z, view->forward.x, view->forward.y, view->forward.z, view->firstPerson,
					held.originalPosition.x, held.originalPosition.y, held.originalPosition.z);
			}

			Tell(held.dropOnPlace ? "Holding: drops when placed" : "Holding: stays where placed");
			held.ref = std::move(ref);
			g_held = std::move(held);
		}

		void Place()
		{
			auto* const object = g_held->ref.get();
			const bool  drop = g_held->dropOnPlace && g_held->keyframedByUs;
			if (drop) {
				Engine::SetMotionType(object, GameID::kMotionDynamic);
			}
			REX::INFO("Placed {:08X}{}", Engine::FormID(object), drop ? "" : " (stays in place)");
			g_held.reset();
		}

		void PutBack()
		{
			auto* const object = g_held->ref.get();
			Engine::MoveTo(object, g_held->originalPosition, g_held->originalAngle);
			if (g_held->keyframedByUs) {
				Engine::SetMotionType(object, GameID::kMotionDynamic);
			}
			REX::INFO("Put back {:08X}", Engine::FormID(object));
			Tell("Put back");
			g_held.reset();
		}

		void UpdateHeld(float a_seconds)
		{
			const auto& settings = Settings::Get();
			auto&       held = *g_held;
			auto* const object = held.ref.get();

			if (!Engine::Loaded3D(object) || !Engine::ParentCell(object)) {
				REX::INFO("{:08X} unloaded while held; let go", Engine::FormID(object));
				g_held.reset();
				return;
			}

			const float scale = Held(settings.fineKey) ? static_cast<float>(settings.fineMultiplier) : 1.0F;
			const float turn = static_cast<float>(settings.rotateSpeed) * kDegToRad * a_seconds * scale;
			const float move = static_cast<float>(settings.moveSpeed) * a_seconds * scale;

			if (Held(settings.yawLeftKey)) held.yaw -= turn;
			if (Held(settings.yawRightKey)) held.yaw += turn;
			if (Held(settings.pitchUpKey)) held.pitch -= turn;
			if (Held(settings.pitchDownKey)) held.pitch += turn;
			if (Held(settings.rollLeftKey)) held.roll -= turn;
			if (Held(settings.rollRightKey)) held.roll += turn;
			if (Held(settings.pushKey)) held.distance += move;
			if (Held(settings.pullKey)) held.distance -= move;
			if (Held(settings.raiseKey)) held.height += move;
			if (Held(settings.lowerKey)) held.height -= move;
			held.distance = std::clamp(held.distance, static_cast<float>(settings.minDistance), static_cast<float>(settings.maxDistance));

			if (g_reset.Pressed(settings.resetRotationKey)) {
				held.pitch = 0.0F;
				held.roll = 0.0F;
				held.yaw = 0.0F;  // upright, facing the same way as you (or north, without bRotateWithView)
			}
			if (g_freeze.Pressed(settings.freezeToggleKey)) {
				held.dropOnPlace = !held.dropOnPlace;
				REX::INFO("{:08X}: {} when placed", Engine::FormID(object), held.dropOnPlace ? "drops" : "stays in place");
				Tell(held.dropOnPlace ? "Drops when placed" : "Stays where placed");
			}

			if (const auto view = Engine::GetView()) {
				Engine::MoveTo(object, TargetPosition(held, *view), TargetAngle(held, *view));
			}
		}
	}

	void OnFrame(const RE::InputEvent* a_head)
	{
		const auto now = std::chrono::steady_clock::now();
		const float seconds = g_lastFrame.time_since_epoch().count() == 0 ? 0.0F :
		                                                                     std::clamp(std::chrono::duration<float>(now - g_lastFrame).count(), 0.0F, 0.1F);
		g_lastFrame = now;

		OSF::ApplyChanges();
		const auto& settings = Settings::Get();
		if (settings.debugLog) {
			LogButtonPresses(a_head);
		}

		const auto* ui = RE::UI::GetSingleton();
		if (g_held && ui && ui->IsMenuOpen(LoadingMenu())) {
			REX::INFO("Loading: let go of {:08X}", Engine::FormID(g_held->ref.get()));
			g_held.reset();
			return;
		}

		// Log why keys are ignored whenever that changes, so a menu that never closes shows up.
		static bool        lastFocus = true;
		static const char* lastBlocker = nullptr;
		const bool         focus = GameHasFocus();
		const char*        blocker = BlockingMenu();
		if (focus != lastFocus) {
			REX::INFO(focus ? "Game window has focus again" : "Game window lost focus: keys ignored");
			lastFocus = focus;
		}
		if (blocker != lastBlocker) {
			if (blocker) {
				REX::INFO("{} is open: keys ignored", blocker);
			} else {
				REX::INFO("No menu open: keys active");
			}
			lastBlocker = blocker;
		}
		if (!focus || blocker) {
			// Forget key states, so a key pressed in a menu doesn't act when it closes.
			g_grab.Pressed(settings.grabKey);
			g_cancel.Pressed(settings.cancelKey);
			g_reset.Pressed(settings.resetRotationKey);
			g_freeze.Pressed(settings.freezeToggleKey);
			return;
		}

		if (g_grab.Pressed(settings.grabKey)) {
			REX::INFO("Pick-up key ({:#04x}) pressed: {}", settings.grabKey, g_held ? "placing" : "picking up");
			if (g_held) {
				Place();
			} else {
				TryPickUp();
			}
			return;
		}
		if (!g_held) {
			// Keep the edge states current, so a press made while not holding doesn't act later.
			g_cancel.Pressed(settings.cancelKey);
			g_reset.Pressed(settings.resetRotationKey);
			g_freeze.Pressed(settings.freezeToggleKey);
			return;
		}
		if (g_cancel.Pressed(settings.cancelKey)) {
			PutBack();
			return;
		}
		UpdateHeld(seconds);
	}
}
