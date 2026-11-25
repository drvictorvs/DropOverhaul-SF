#pragma once

// Address Library IDs and engine layouts for Starfield 1.16.244, found for this mod.
// Each ID is checked by AddressCheck before the plugin does anything. Layout offsets marked
// "game code" are read straight from the instructions that use them (CommonLibSF's headers are out
// of date for some of these types, e.g. TESObjectCELL's flags are 8 bytes later than it says).
namespace DROP::GameID
{
	// ---- Globals -------------------------------------------------------------------------------

	// PlayerCharacter* (global pointer).
	inline constexpr REL::ID PlayerSingleton{ 922868 };

	// PlayerCamera* (global pointer). Written right after the PlayerCamera constructor (ID 113379).
	inline constexpr REL::ID PlayerCameraSingleton{ 937788 };

	// ViewCaster* (global pointer to a static ViewCaster, ID 950058). The ViewCaster keeps what the
	// player is looking at: the activation target the HUD prompt shows.
	inline constexpr REL::ID ViewCasterSingleton{ 938422 };

	// std::uint32_t: the game's "no reference" handle value. Set at startup; read it at run time.
	inline constexpr REL::ID InvalidRefHandle{ 944496 };

	// ---- Functions -----------------------------------------------------------------------------

	// ObjectRefHandle* (ViewCaster*, ObjectRefHandle* out). The activation target (ViewCaster + 0x10),
	// read under the ViewCaster's lock (+0x50). Used by 18 engine systems.
	inline constexpr REL::ID ViewCaster_GetActivatePickRef{ 82616 };

	// NiPointer<TESObjectREFR>& (NiPointer<TESObjectREFR>& out, const ObjectRefHandle& in).
	// Same ID as CommonLibSF's BSPointerHandleManagerInterface::GetSmartPointer.
	inline constexpr REL::ID GetSmartPointer{ 35638 };

	// std::uint64_t TESForm::DecRefCount() const. Same ID as CommonLibSF's.
	inline constexpr REL::ID TESForm_DecRefCount{ 38742 };

	// void (TESObjectREFR*, const ObjectRefHandle& target, TESObjectCELL*, TESWorldSpace*,
	//       const NiPoint3& position, const NiPoint3& angle).
	// The engine's MoveTo. Papyrus ObjectReference.SetPosition (ID 118468) and SetAngle (118440)
	// queue functors (SetPositionFunctor / SetAngleFunctor) whose apply function (118197 / 118200)
	// calls this with the reference's own cell and worldspace, the invalid handle, and the new
	// position (keeping the angle) or the new angle (keeping the position).
	inline constexpr REL::ID MoveTo{ 118176 };

	// bool (TESObjectREFR*, std::uint32_t mask). Tests the reference's LOADED_REF_DATA flags
	// (ref + 0xB8, flags at + 0x20) under its lock. Papyrus SetMotionType requires mask 1.
	inline constexpr REL::ID LoadedDataHasFlag{ 38044 };

	// bool (NiAVObject* root, std::uint32_t motionType, bool, bool, bool).
	// Sets the motion type of the 3D's physics bodies. Papyrus SetMotionType(type, abAllowActivate)
	// runs it as (root, type, true, false, !abAllowActivate) from SetMotionTypeFunctor (ID 118460).
	inline constexpr REL::ID SetMotionType3D{ 125023 };

	// void (PhysicsOwner*, bool). Called by SetMotionTypeFunctor after switching to dynamic:
	// wakes the bodies so they fall and settle.
	inline constexpr REL::ID WakePhysics{ 147423 };

	// BSTEventSource<ShowHUDMessageEvent>* (). What Papyrus Debug.Notification (ID 117311) sends its
	// message through: { BSFixedString text; BSFixedString sound (empty); std::uint16_t 1 }.
	// Same ID as CommonLibSF's ShowHUDMessageEvent::GetEventSource, which has no event layout.
	inline constexpr REL::ID HUDNotificationSource{ 86800 };

	// ---- Per-frame hook --------------------------------------------------------------------------

	// void (PlayerControls::Manager*, InputEvent* head). The player-controls input pass: runs on the
	// game thread every frame with that frame's input events (it resets the movement input and adds
	// the frame time to a timer before reading them). Slot 1 of the PlayerControls::Manager vtable.
	inline constexpr REL::ID PerformInputProcessing{ 82442 };

	// PlayerControls::Manager vtable (RTTI .?AVManager@PlayerControls@@); slot 1 is the function above.
	inline constexpr REL::ID PlayerControlsVtable{ 433847 };
	inline constexpr std::size_t kInputProcessingSlot = 1;

	// First 22 bytes of PerformInputProcessing in 1.16.244. The jump to the hook is written only if
	// they match. The first 5 (mov [rsp+18h], rbx) have no RIP-relative operand, so the stub that
	// runs the original function can copy them.
	inline constexpr std::array<std::uint8_t, 22> kInputProcessingBytes{
		0x48, 0x89, 0x5C, 0x24, 0x18,  // mov [rsp+18h], rbx
		0x55,                          // push rbp
		0x56,                          // push rsi
		0x57,                          // push rdi
		0x41, 0x56,                    // push r14
		0x41, 0x57,                    // push r15
		0x48, 0x83, 0xEC, 0x20,        // sub rsp, 20h
		0x48, 0x8B, 0xFA,              // mov rdi, rdx
		0x48, 0x8B, 0xF1               // mov rsi, rcx
	};
	inline constexpr std::size_t kInputProcessingPrologue = 5;

	// ---- Motion types (Papyrus SetMotionType accepts only these two) -----------------------------

	inline constexpr std::uint32_t kMotionDynamic = 1;
	inline constexpr std::uint32_t kMotionKeyframed = 2;

	// ---- Layouts -------------------------------------------------------------------------------

	namespace Offset
	{
		// TESForm
		inline constexpr std::size_t FormID = 0x28;    // std::uint32_t (game code: MoveTo reads [ref+0x28])
		inline constexpr std::size_t FormType = 0x2E;  // std::uint8_t; 0x4B = Actor (game code)

		// TESObjectREFR (game code: the SetPosition/SetAngle apply functions)
		inline constexpr std::size_t RefAngle = 0x80;       // NiPoint3, radians
		inline constexpr std::size_t RefPosition = 0x8C;    // NiPoint3
		inline constexpr std::size_t RefParentCell = 0xB0;  // TESObjectCELL*
		inline constexpr std::size_t RefLoadedData = 0xB8;  // LOADED_REF_DATA* (BSGuarded; lock at +0xC0)

		// LOADED_REF_DATA
		inline constexpr std::size_t LoadedData3D = 0x08;  // NiAVObject*

		// TESObjectREFR vtable slots (game code: SetMotionTypeFunctor)
		inline constexpr std::size_t VSlotGet3D = 0xAC;      // NiPointer<NiAVObject> Get3D() (returned through a hidden pointer)
		inline constexpr std::size_t VSlotAddChange = 0x17;  // bool AddChange(std::uint32_t)
		inline constexpr std::uint32_t ChangeHavokMoved = 4;   // what SetMotionTypeFunctor passes

		// TESObjectCELL (game code: SetPosition apply function)
		inline constexpr std::size_t CellFlags = 0x48;      // bit 0 = interior
		inline constexpr std::size_t CellWorldSpace = 0x110;  // TESWorldSpace*, for exterior cells

		// NiAVObject (game code: SetMotionTypeFunctor's physics walk, camera code)
		inline constexpr std::size_t NodeParent = 0x38;          // NiNode*
		inline constexpr std::size_t NodeWorldRotate = 0x80;     // 3 rows of 4 floats
		inline constexpr std::size_t NodeWorldTranslate = 0xB0;  // NiPoint3
		inline constexpr std::size_t NodeFlags = 0x118;          // bit 14 = owns the physics data
		inline constexpr std::size_t NodeVSlotGetPhysicsHolder = 7;  // returns an object with the owner at +0x180
		inline constexpr std::size_t NodeVSlotStopWalk = 14;         // non-null: stop looking further up
		inline constexpr std::size_t PhysicsHolderOwner = 0x180;     // ref-counted (count at +8)
		inline constexpr std::size_t PhysicsOwnerKeyframed = 0x70;   // object whose vtable slot 8 runs after keyframing

		// TESCamera / PlayerCamera (game code: StartDialogueCamera, ID 113405)
		inline constexpr std::size_t CameraRoot = 0x08;          // NiNode*
		inline constexpr std::size_t CameraCurrentState = 0x10;  // TESCameraState*
		inline constexpr std::size_t CameraStateID = 0x50;       // std::uint32_t; 0 = first person
	}
}
