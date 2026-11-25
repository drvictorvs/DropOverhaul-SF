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

		// ---- Keys --------------------------------------------------------------------------------
		//
		// A key counts as down if either source says so: the game's own input events for this frame
		// (what the game itself acts on), or Windows' key state. The game's keyboard and mouse devices
		// send a button event every frame for each key that is down (value 1), and one more when it's
		// released (value 0) (keyboard poll ID 124263; mouse code at 1422fad30). Keyboard codes are
		// virtual-key codes; mouse codes are 0 left, 1 right, 2 middle, 3 and 4 the side buttons.

		std::uint64_t g_frame = 0;

		constexpr auto kKeyboard = RE::InputEvent::DeviceType::kKeyboard;
		constexpr auto kMouse = RE::InputEvent::DeviceType::kMouse;

		std::optional<std::uint32_t> MouseButton(std::uint32_t a_vk)
		{
			switch (a_vk) {
			case 0x01: return 0;  // VK_LBUTTON
			case 0x02: return 1;  // VK_RBUTTON
			case 0x04: return 2;  // VK_MBUTTON
			case 0x05: return 3;  // VK_XBUTTON1
			case 0x06: return 4;  // VK_XBUTTON2
			default: return std::nullopt;
			}
		}

		// The game's keyboard codes that stand for a Windows key code. Shift, Ctrl and Alt come as either
		// the generic code or the left/right one (the game reports Shift as left or right Shift; OSF
		// Settings stores "L Ctrl" as the left code), so each matches the other: the generic code
		// matches both sides, a left or right code matches itself and the generic one.
		std::array<std::uint32_t, 3> KeyboardCodes(std::uint32_t a_vk)
		{
			switch (a_vk) {
			case 0x10: return { 0x10, 0xA0, 0xA1 };
			case 0x11: return { 0x11, 0xA2, 0xA3 };
			case 0x12: return { 0x12, 0xA4, 0xA5 };
			case 0xA0:
			case 0xA1: return { a_vk, 0x10, 0x10 };
			case 0xA2:
			case 0xA3: return { a_vk, 0x11, 0x11 };
			case 0xA4:
			case 0xA5: return { a_vk, 0x12, 0x12 };
			default: return { a_vk, a_vk, a_vk };
			}
		}

		// Whether a game button event is the key a setting names (a Windows key code).
		bool Matches(const RE::ButtonEvent& a_button, std::uint32_t a_vk)
		{
			if (a_vk == 0 || a_button.idCode < 0) {
				return false;
			}
			const auto code = static_cast<std::uint32_t>(a_button.idCode);
			if (const auto button = MouseButton(a_vk)) {
				return a_button.deviceType == kMouse && code == *button;
			}
			if (a_button.deviceType != kKeyboard) {
				return false;
			}
			const auto codes = KeyboardCodes(a_vk);
			return std::ranges::find(codes, code) != codes.end();
		}

		class GameKeys
		{
		public:
			void Update(const RE::InputEvent* a_head)
			{
				for (const auto* event = a_head; event; event = event->next) {
					if (event->eventType != RE::InputEvent::EventType::kButton) {
						continue;
					}
					const auto* button = static_cast<const RE::ButtonEvent*>(event);
					Key* key = Find(button->deviceType, button->idCode);
					if (!key) {
						continue;
					}
					const bool down = button->value != 0.0F;
					if (down && !IsDown(*key) && Settings::Get().debugLog && _logged < 300) {
						++_logged;
						const char* action = button->strUserEvent.c_str();
						REX::INFO("Game input: {} {:#x} down (action '{}')",
							button->deviceType == RE::InputEvent::DeviceType::kMouse ? "mouse" : "key",
							static_cast<std::uint32_t>(button->idCode), action ? action : "");
					}
					key->down = down;
					key->frame = g_frame;
				}
			}

			// a_vk: a Windows virtual-key code, as the settings use.
			bool Down(std::uint32_t a_vk) const
			{
				if (const auto button = MouseButton(a_vk)) {
					return IsDown(_mouse[*button]);
				}
				for (const auto code : KeyboardCodes(a_vk)) {
					if (code < _keyboard.size() && IsDown(_keyboard[code])) {
						return true;
					}
				}
				return false;
			}

		private:
			struct Key
			{
				bool          down{ false };
				std::uint64_t frame{ 0 };
			};

			// A key that sent no event for this many frames is up (a release that wasn't seen).
			static constexpr std::uint64_t kStaleFrames = 10;

			static bool IsDown(const Key& a_key) { return a_key.down && g_frame - a_key.frame <= kStaleFrames; }

			Key* Find(RE::InputEvent::DeviceType a_device, std::int32_t a_code)
			{
				if (a_code < 0) {
					return nullptr;
				}
				const auto code = static_cast<std::size_t>(a_code);
				if (a_device == kKeyboard && code < _keyboard.size()) {
					return &_keyboard[code];
				}
				if (a_device == kMouse && code < _mouse.size()) {
					return &_mouse[code];
				}
				return nullptr;
			}

			std::array<Key, 256> _keyboard{};
			std::array<Key, 5>   _mouse{};
			std::uint32_t        _logged{ 0 };
		};

		GameKeys g_gameKeys;

		bool WindowsDown(std::uint32_t a_key)
		{
			return (GetAsyncKeyState(static_cast<int>(a_key)) & 0x8000) != 0;
		}

		bool Held(std::uint32_t a_key)
		{
			return a_key != 0 && (g_gameKeys.Down(a_key) || WindowsDown(a_key));
		}

		bool IsActivate(const RE::ButtonEvent& a_button)
		{
			const char* name = a_button.strUserEvent.c_str();
			if (!name) {
				return false;
			}
			constexpr std::string_view kActivate = "activate";
			std::size_t                i = 0;
			for (; name[i] && i < kActivate.size(); ++i) {
				if (std::tolower(static_cast<unsigned char>(name[i])) != kActivate[i]) {
					return false;
				}
			}
			return i == kActivate.size() && name[i] == '\0';
		}

		// Makes the game ignore a button event: no action name, and marked disabled (as Dialogue
		// Movement Enabler marks the movement keys it doesn't allow).
		void Take(RE::ButtonEvent& a_button)
		{
			// Never freed: avoids releasing strings after the game's string pool is gone at exit.
			static const auto* none = new RE::BSFixedString("");
			a_button.strUserEvent = *none;
			reinterpret_cast<std::uint8_t&>(a_button.disabled) = 4;
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
			Engine::RefPtr                ref;
			Vec3                          originalPosition;
			Vec3                          originalAngle;
			std::optional<Engine::Bounds> bounds;
			bool                          builder{ true };  // sits on what you aim at; else held in front of you
			bool                          advanced{ false };  // the mouse also tilts (rotate modifier) and moves sideways (move modifier)
			bool                          keyframedByUs{ false };
			bool                          dropOnPlace{ true };
			bool                          loggedAim{ false };
			bool                          loggedNoSurface{ false };
			float                         distance{ 1.5F };  // meters
			float                         height{ 0.0F };    // meters, added to the target height
			float                         side{ 0.0F };      // meters, to the right of the view
			float                         yaw{ 0.0F };       // with bRotateWithView: relative to the view's yaw; else absolute
			float                         pitch{ 0.0F };
			float                         roll{ 0.0F };
		};

		// ---- Input taken from the game while holding (FilterInput) ------------------------------

		struct Captured
		{
			bool         place{ false };  // an Activate press
			bool         wheelUpLast{ false };
			bool         wheelDownLast{ false };
			int          wheel{ 0 };      // notches since the frame update, + = up
			std::int32_t dx{ 0 };         // mouse movement since the frame update, with a modifier held
			std::int32_t dy{ 0 };
		};
		Captured g_captured;
		bool     g_capturing = false;  // set by the frame update: an object is held and keys are active

		// Buttons taken while down: their later events are taken too, until released, so the game
		// never sees a release without its press.
		std::array<bool, 256> g_takenKeys{};
		std::array<bool, 5>   g_takenMouse{};
		bool                  g_takenActivate{ false };

		std::optional<HeldObject>             g_held;
		Edge                                  g_grab;
		Edge                                  g_cancel;
		Edge                                  g_reset;
		Edge                                  g_freeze;
		Edge                                  g_place;
		Edge                                  g_advanced;
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
			const bool  withView = Settings::Get().rotateWithView;
			const float yaw = withView ? Engine::YawOf(a_view.forward) + a_held.yaw : a_held.yaw;
			return { WrapAngle(a_held.pitch), WrapAngle(a_held.roll), WrapAngle(yaw) };
		}

		// The view's horizontal right-hand direction.
		Vec3 RightOf(const Engine::View& a_view)
		{
			return Vec3{ a_view.forward.y, -a_view.forward.x, 0.0F }.Normalized();
		}

		// Height and sideways offsets.
		Vec3 Offset(const HeldObject& a_held, const Engine::View& a_view)
		{
			return RightOf(a_view) * a_held.side + Vec3{ 0.0F, 0.0F, a_held.height };
		}

		// Hold mode: at the held distance along the view, plus the offsets.
		Vec3 TargetPosition(const HeldObject& a_held, const Engine::View& a_view)
		{
			return a_view.origin + a_view.forward * a_held.distance + Offset(a_held, a_view);
		}

		// Builder mode: on the surface hit, moved out by the object's bounds so it rests against it
		// instead of sinking into it (bounds are unrotated, so this is exact for an upright object).
		Vec3 OnSurface(const HeldObject& a_held, const Engine::Hit& a_hit)
		{
			Vec3 p = a_hit.position;
			if (!a_held.bounds) {
				return p + a_hit.normal * 0.02F;
			}
			const auto& b = *a_held.bounds;
			if (a_hit.normal.z >= 0.7F) {
				p.z -= b.min.z;  // floor, table: bottom on the surface
			} else if (a_hit.normal.z <= -0.7F) {
				p.z -= b.max.z;  // ceiling: top against it
			} else {
				// Wall: centered on the aim point, its widest horizontal extent out from the wall.
				const float extent = std::max({ std::abs(b.min.x), std::abs(b.max.x), std::abs(b.min.y), std::abs(b.max.y) });
				const Vec3  out = Vec3{ a_hit.normal.x, a_hit.normal.y, 0.0F }.Normalized();
				p = p + out * extent;
				p.z -= (b.min.z + b.max.z) * 0.5F;
			}
			return p;
		}

		Vec3 BuilderPosition(HeldObject& a_held, const Engine::View& a_view)
		{
			const auto& settings = Settings::Get();
			auto* const object = a_held.ref.get();
			const auto  hit = Engine::CastRay(a_view, static_cast<float>(settings.maxDistance), { object, Engine::GetPlayer() },
				object, a_held.bounds);
			if (!hit) {
				if (!a_held.loggedNoSurface) {
					a_held.loggedNoSurface = true;
					REX::INFO("  no surface within {:.1f} m: holding it at {:.2f} m", settings.maxDistance, a_held.distance);
				}
				return a_view.origin + a_view.forward * a_held.distance + Offset(a_held, a_view);
			}
			a_held.distance = std::clamp(hit->distance, static_cast<float>(settings.minDistance), static_cast<float>(settings.maxDistance));
			const Vec3 p = OnSurface(a_held, *hit) + Offset(a_held, a_view);
			if (!a_held.loggedAim || settings.debugLog) {
				static std::uint32_t count = 0;
				if (!a_held.loggedAim || ++count % 60 == 0) {
					REX::INFO("  aiming at {} (layer {}), {:.2f} m away, normal ({:.2f}, {:.2f}, {:.2f}); object to ({:.2f}, {:.2f}, {:.2f})",
						hit->ref ? std::format("{:08X}", Engine::FormID(hit->ref)) : std::string("a surface"), hit->layer, hit->distance,
						hit->normal.x, hit->normal.y, hit->normal.z, p.x, p.y, p.z);
				}
				a_held.loggedAim = true;
			}
			return p;
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

			held.builder = !settings.holdMode;
			held.bounds = Engine::GetBounds(object);

			// Start where the object is: at its distance along the view, and (hold mode) at its height
			// relative to that line, so it doesn't jump up to eye level.
			const Vec3  rel = held.originalPosition - view->origin;
			const float away = rel.Length();
			held.distance = std::clamp(rel.Dot(view->forward), static_cast<float>(settings.minDistance), static_cast<float>(settings.maxDistance));
			held.height = held.builder ? 0.0F : rel.z - view->forward.z * held.distance;
			held.pitch = held.originalAngle.x;
			held.roll = held.originalAngle.y;
			held.yaw = settings.rotateWithView ? WrapAngle(held.originalAngle.z - Engine::YawOf(view->forward)) : held.originalAngle.z;

			REX::INFO("Picked up {:08X}{}, {:.2f} m away ({} mode)", Engine::FormID(object),
				held.keyframedByUs ? "" : " (no physics)", away, held.builder ? "builder" : "hold");
			if (held.bounds) {
				REX::INFO("  bounds ({:.2f}, {:.2f}, {:.2f}) to ({:.2f}, {:.2f}, {:.2f})", held.bounds->min.x, held.bounds->min.y,
					held.bounds->min.z, held.bounds->max.x, held.bounds->max.y, held.bounds->max.z);
			}
			if (settings.debugLog) {
				REX::INFO("  view origin ({:.1f}, {:.1f}, {:.1f}) forward ({:.3f}, {:.3f}, {:.3f}) first person {}; object at ({:.1f}, {:.1f}, {:.1f})",
					view->origin.x, view->origin.y, view->origin.z, view->forward.x, view->forward.y, view->forward.z, view->firstPerson,
					held.originalPosition.x, held.originalPosition.y, held.originalPosition.z);
			}

			if (held.builder) {
				Tell(held.dropOnPlace ? "Placing: click to set down, right-click to put back" :
				                        "Placing (stays in place): click to set down, right-click to put back");
			} else {
				Tell(held.dropOnPlace ? "Holding: drops when placed" : "Holding: stays where placed");
			}
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
			g_capturing = false;
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
			g_capturing = false;
		}

		void UpdateHeld(float a_seconds, const Captured& a_captured)
		{
			const auto& settings = Settings::Get();
			auto&       held = *g_held;
			auto* const object = held.ref.get();

			if (!Engine::Loaded3D(object) || !Engine::ParentCell(object)) {
				REX::INFO("{:08X} unloaded while held; let go", Engine::FormID(object));
				g_held.reset();
			g_capturing = false;
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
			if (!held.builder) {
				if (Held(settings.pushKey)) held.distance += move;
				if (Held(settings.pullKey)) held.distance -= move;
			}
			if (Held(settings.raiseKey)) held.height += move;
			if (Held(settings.lowerKey)) held.height -= move;
			held.distance = std::clamp(held.distance, static_cast<float>(settings.minDistance), static_cast<float>(settings.maxDistance));

			// Mouse, as in Object Manipulation Overhaul: with the rotate modifier held it turns the object
			// (and, in advanced mode, tilts it with up/down); with the move modifier held it raises and
			// lowers it (and, in advanced mode, moves it sideways). The wheel turns it in steps.
			const bool rotating = Held(settings.rotateModifierKey);
			const bool moving = !rotating && Held(settings.moveModifierKey);
			if (rotating) {
				const float perCount = static_cast<float>(settings.mouseRotateSpeed) * kDegToRad * scale;
				held.yaw += static_cast<float>(a_captured.dx) * perCount;
				if (held.advanced) {
					held.pitch += static_cast<float>(a_captured.dy) * perCount;
				}
			} else if (moving) {
				const float perCount = static_cast<float>(settings.mouseMoveSpeed) * scale;
				held.height -= static_cast<float>(a_captured.dy) * perCount;  // mouse up raises it
				if (held.advanced) {
					held.side += static_cast<float>(a_captured.dx) * perCount;
				}
			}
			held.yaw -= static_cast<float>(a_captured.wheel) * static_cast<float>(settings.wheelRotateStep) * kDegToRad;

			if (g_advanced.Pressed(settings.advancedModeKey)) {
				held.advanced = !held.advanced;
				REX::INFO("{:08X}: advanced mode {}", Engine::FormID(object), held.advanced ? "on" : "off");
				Tell(held.advanced ? "Advanced: Ctrl tilts too, Shift moves sideways too" : "Advanced off");
			}
			if (g_reset.Pressed(settings.resetRotationKey)) {
				// With a modifier held, only what it changes; otherwise both.
				if (!moving) {
					held.pitch = 0.0F;
					held.roll = 0.0F;
					held.yaw = 0.0F;  // facing the same way as you (or north, without bRotateWithView)
				}
				if (!rotating) {
					held.height = 0.0F;
					held.side = 0.0F;
				}
			}
			if (g_freeze.Pressed(settings.freezeToggleKey)) {
				held.dropOnPlace = !held.dropOnPlace;
				REX::INFO("{:08X}: {} when placed", Engine::FormID(object), held.dropOnPlace ? "drops" : "stays in place");
				Tell(held.dropOnPlace ? "Drops when placed" : "Stays where placed");
			}

			if (const auto view = Engine::GetView()) {
				const Vec3 position = held.builder ? BuilderPosition(held, *view) : TargetPosition(held, *view);
				Engine::MoveTo(object, position, TargetAngle(held, *view));
			}
		}
	}

	void OnFrame(const RE::InputEvent* a_head)
	{
		++g_frame;
		g_gameKeys.Update(a_head);

		const auto now = std::chrono::steady_clock::now();
		const float seconds = g_lastFrame.time_since_epoch().count() == 0 ? 0.0F :
		                                                                     std::clamp(std::chrono::duration<float>(now - g_lastFrame).count(), 0.0F, 0.1F);
		g_lastFrame = now;

		OSF::ApplyChanges();
		const auto& settings = Settings::Get();

		const auto* ui = RE::UI::GetSingleton();
		if (g_held && ui && ui->IsMenuOpen(LoadingMenu())) {
			REX::INFO("Loading: let go of {:08X}", Engine::FormID(g_held->ref.get()));
			g_held.reset();
			g_capturing = false;
			return;
		}

		// Log why keys are ignored whenever that changes, so a menu that never closes shows up.
		static bool        first = true;
		static bool        lastFocus = true;
		static const char* lastBlocker = nullptr;
		const bool         focus = GameHasFocus();
		const char*        blocker = BlockingMenu();
		if (focus != lastFocus) {
			REX::INFO(focus ? "Game window has focus again" : "Game window lost focus: keys ignored");
			lastFocus = focus;
		}
		if (blocker != lastBlocker || first) {
			first = false;
			if (blocker) {
				REX::INFO("{} is open: keys ignored", blocker);
			} else {
				REX::INFO("No menu open: keys active");
			}
			lastBlocker = blocker;
		}
		g_capturing = g_held && focus && !blocker;
		const Captured captured = g_captured;
		g_captured.place = false;
		g_captured.wheel = 0;
		g_captured.dx = 0;
		g_captured.dy = 0;

		// Keep every press action's key state current, so a press made while it couldn't act (in a
		// menu, or while not holding) doesn't act later.
		auto syncEdges = [&]() {
			g_cancel.Pressed(settings.cancelKey);
			g_place.Pressed(settings.placeKey);
			g_reset.Pressed(settings.resetRotationKey);
			g_freeze.Pressed(settings.freezeToggleKey);
			g_advanced.Pressed(settings.advancedModeKey);
		};
		if (!focus || blocker) {
			g_grab.Pressed(settings.grabKey);
			syncEdges();
			return;
		}

		if (g_grab.Pressed(settings.grabKey)) {
			REX::INFO("Pick-up key ({:#04x}) pressed (game input: {}, Windows: {}): {}", settings.grabKey,
				g_gameKeys.Down(settings.grabKey) ? "down" : "up", WindowsDown(settings.grabKey) ? "down" : "up",
				g_held ? "placing" : "picking up");
			if (g_held) {
				Place();
			} else {
				TryPickUp();
			}
			return;
		}
		if (!g_held) {
			syncEdges();
			return;
		}
		if (g_cancel.Pressed(settings.cancelKey)) {
			PutBack();
			return;
		}
		if (g_place.Pressed(settings.placeKey) || captured.place) {
			REX::INFO("{} pressed: placing", captured.place ? "Activate" : "Place key");
			Place();
			return;
		}
		UpdateHeld(seconds, captured);
	}

	void FilterInput(RE::InputEvent* a_head)
	{
		const auto& settings = Settings::Get();
		const bool  active = g_capturing;
		auto&       c = g_captured;

		// While holding: the keys this plugin uses while holding, Activate, the wheel, and the mouse
		// movement while a modifier is held. Once let go, only buttons still down from then.
		const bool modifier = active && (Held(settings.rotateModifierKey) || Held(settings.moveModifierKey));
		const bool wheelOn = active && settings.wheelRotateStep > 0.0;
		bool       wheelUp = false;
		bool       wheelDown = false;

		for (auto* event = a_head; event; event = event->next) {
			if (event->eventType == RE::InputEvent::EventType::kMouseMove) {
				if (modifier) {
					// MouseMoveEvent: IDEvent, then the movement in mouse counts (game code: ID 124081).
					auto* const raw = reinterpret_cast<std::byte*>(event);
					auto&       x = *reinterpret_cast<std::int32_t*>(raw + 0x38);
					auto&       y = *reinterpret_cast<std::int32_t*>(raw + 0x3C);
					c.dx += x;
					c.dy += y;
					x = 0;  // the camera stays still meanwhile
					y = 0;
				}
				continue;
			}
			if (event->eventType != RE::InputEvent::EventType::kButton) {
				continue;
			}
			auto& button = *static_cast<RE::ButtonEvent*>(event);
			if (button.idCode < 0) {
				continue;
			}
			const bool down = button.value != 0.0F;
			const auto code = static_cast<std::uint32_t>(button.idCode);

			bool* taken = nullptr;  // "taken while down" flag for this button, if it has one
			if (button.deviceType == kKeyboard && code < g_takenKeys.size()) {
				taken = &g_takenKeys[code];
			} else if (button.deviceType == kMouse && code < g_takenMouse.size()) {
				taken = &g_takenMouse[code];
			}

			if (IsActivate(button)) {
				if (active || g_takenActivate) {
					if (active && down && !g_takenActivate) {
						c.place = true;
					}
					g_takenActivate = down;
					Take(button);
				}
				continue;
			}
			if (button.deviceType == kMouse && (code == 0x800 || code == 0x900)) {
				if (wheelOn) {
					(code == 0x800 ? wheelUp : wheelDown) = true;
					Take(button);
				}
				continue;
			}

			const bool ours = active && std::ranges::any_of(
				std::array{ settings.placeKey, settings.cancelKey, settings.rotateModifierKey, settings.moveModifierKey,
					settings.advancedModeKey, settings.resetRotationKey },
				[&](std::uint32_t a_key) { return Matches(button, a_key); });
			if (ours || (taken && *taken)) {
				if (taken) {
					*taken = down;
				}
				Take(button);
			}
		}

		// One notch per scroll, even if the game sends it over two frames.
		if (wheelUp && !c.wheelUpLast) {
			++c.wheel;
		}
		if (wheelDown && !c.wheelDownLast) {
			--c.wheel;
		}
		c.wheelUpLast = wheelUp;
		c.wheelDownLast = wheelDown;
	}
}
