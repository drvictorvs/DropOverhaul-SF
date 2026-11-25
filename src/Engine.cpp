#include "Engine.h"

#include "GameIDs.h"

namespace DROP::Engine
{
	namespace
	{
		using namespace GameID;
		using namespace GameID::Offset;

		template <class T>
		T& At(const void* a_base, std::size_t a_offset)
		{
			return *reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(a_base) + a_offset);
		}

		template <class F>
		F VFunc(const void* a_object, std::size_t a_slot)
		{
			const auto* vtbl = *reinterpret_cast<void* const* const*>(a_object);
			return reinterpret_cast<F>(vtbl[a_slot]);
		}

		std::uint32_t InvalidHandle()
		{
			static REL::Relocation<std::uint32_t*> value{ InvalidRefHandle };
			return *value;
		}

		// ---- Camera direction ----------------------------------------------------------------------
		//
		// The camera root's world rotation gives the view direction in first and third person, but which
		// of its axes is "forward" is not confirmed for Starfield. In first person the view direction is
		// also the player's own pitch and yaw, so the plugin compares the 12 candidate axes (3 rows,
		// 3 columns, each signed) against that, and locks in the one that agrees for 30 frames in a row.
		// Until then (or if it never agrees), the player's pitch and yaw are used.

		struct Calibration
		{
			int  candidate{ -1 };
			int  lastBest{ -1 };
			int  streak{ 0 };
			bool locked{ false };
		};
		Calibration g_calibration;

		Vec3 Candidate(const float (&a_m)[3][4], int a_index)
		{
			const int  k = a_index % 6;
			const bool negate = a_index >= 6;
			const Vec3 v = k < 3 ? Vec3{ a_m[k][0], a_m[k][1], a_m[k][2] } : Vec3{ a_m[0][k - 3], a_m[1][k - 3], a_m[2][k - 3] };
			return (negate ? v * -1.0F : v).Normalized();
		}

		void Calibrate(const float (&a_m)[3][4], const Vec3& a_lookForward)
		{
			int   best = -1;
			float bestDot = -2.0F;
			for (int i = 0; i < 12; ++i) {
				const float d = Candidate(a_m, i).Dot(a_lookForward);
				if (d > bestDot) {
					bestDot = d;
					best = i;
				}
			}
			if (bestDot > 0.995F && best == g_calibration.lastBest) {
				if (++g_calibration.streak >= 30) {
					g_calibration.candidate = best;
					g_calibration.locked = true;
					REX::INFO("Camera forward axis: {} {} {}", best >= 6 ? "minus" : "plus", (best % 6) < 3 ? "row" : "column", best % 3);
				}
			} else {
				g_calibration.streak = 0;
			}
			g_calibration.lastBest = best;
		}
	}

	void RefPtr::reset() noexcept
	{
		if (_ref) {
			static REL::Relocation<std::uint64_t (*)(const RE::TESObjectREFR*)> decRef{ TESForm_DecRefCount };
			decRef(_ref);
			_ref = nullptr;
		}
	}

	RefPtr GetCrosshairRef()
	{
		static REL::Relocation<void**> viewCaster{ ViewCasterSingleton };
		void* const                    caster = *viewCaster;
		if (!caster) {
			return {};
		}

		static REL::Relocation<std::uint32_t* (*)(void*, std::uint32_t*)> getActivatePickRef{ ViewCaster_GetActivatePickRef };
		std::uint32_t handle = 0;
		getActivatePickRef(caster, &handle);
		if (handle == 0 || handle == InvalidHandle()) {
			return {};
		}

		static REL::Relocation<void* (*)(RE::TESObjectREFR**, const std::uint32_t*)> getSmartPointer{ GetSmartPointer };
		RE::TESObjectREFR* ref = nullptr;
		getSmartPointer(&ref, &handle);
		return RefPtr(ref);
	}

	RE::TESObjectREFR* GetPlayer()
	{
		static REL::Relocation<RE::TESObjectREFR**> player{ PlayerSingleton };
		return *player;
	}

	std::uint32_t FormID(const RE::TESObjectREFR* a_ref) { return At<std::uint32_t>(a_ref, Offset::FormID); }
	bool          IsActor(const RE::TESObjectREFR* a_ref) { return At<std::uint8_t>(a_ref, FormType) == 0x4B; }
	Vec3          Position(const RE::TESObjectREFR* a_ref) { return At<Vec3>(a_ref, RefPosition); }
	Vec3          Angle(const RE::TESObjectREFR* a_ref) { return At<Vec3>(a_ref, RefAngle); }
	void*         ParentCell(const RE::TESObjectREFR* a_ref) { return At<void*>(a_ref, RefParentCell); }

	RE::NiAVObject* Loaded3D(const RE::TESObjectREFR* a_ref)
	{
		const auto* loaded = At<std::byte*>(a_ref, RefLoadedData);
		return loaded ? At<RE::NiAVObject*>(loaded, LoadedData3D) : nullptr;
	}

	void MoveTo(RE::TESObjectREFR* a_ref, const Vec3& a_position, const Vec3& a_angle)
	{
		void* const cell = ParentCell(a_ref);
		if (!cell) {
			return;
		}
		// As the SetPosition/SetAngle functors do: exterior cells pass their worldspace.
		void* const worldSpace = (At<std::uint32_t>(cell, CellFlags) & 1) != 0 ? nullptr : At<void*>(cell, CellWorldSpace);
		const auto  handle = InvalidHandle();

		using func_t = void (*)(RE::TESObjectREFR*, const std::uint32_t*, void*, void*, const Vec3*, const Vec3*);
		static REL::Relocation<func_t> moveTo{ GameID::MoveTo };
		moveTo(a_ref, &handle, cell, worldSpace, &a_position, &a_angle);
	}

	bool SetMotionType(RE::TESObjectREFR* a_ref, std::uint32_t a_type)
	{
		// The same steps as SetMotionTypeFunctor (ID 118460), Papyrus' SetMotionType(a_type, true).
		RE::NiPointer<RE::NiAVObject> root;
		VFunc<void (*)(RE::TESObjectREFR*, RE::NiPointer<RE::NiAVObject>*)>(a_ref, VSlotGet3D)(a_ref, &root);
		if (!root) {
			return false;
		}

		static REL::Relocation<bool (*)(RE::TESObjectREFR*, std::uint32_t)> loadedDataHasFlag{ LoadedDataHasFlag };
		if (!loadedDataHasFlag(a_ref, 1)) {
			return false;
		}

		static REL::Relocation<bool (*)(RE::NiAVObject*, std::uint32_t, bool, bool, bool)> setMotionType{ SetMotionType3D };
		const bool switched = setMotionType(root.get(), a_type, true, false, false);

		VFunc<bool (*)(RE::TESObjectREFR*, std::uint32_t)>(a_ref, VSlotAddChange)(a_ref, ChangeHavokMoved);

		// Find the node that owns the physics data, walking up from the root.
		void* owner = nullptr;
		for (auto* node = root.get(); node; node = At<RE::NiAVObject*>(node, NodeParent)) {
			if (((At<std::uint32_t>(node, NodeFlags) >> 14) & 1) != 0) {
				if (void* holder = VFunc<void* (*)(RE::NiAVObject*)>(node, NodeVSlotGetPhysicsHolder)(node)) {
					owner = At<void*>(holder, PhysicsHolderOwner);
					if (owner) {
						_InterlockedExchangeAdd(&At<long>(owner, 8), 1);
					}
				}
				break;
			}
			if (VFunc<void* (*)(RE::NiAVObject*)>(node, NodeVSlotStopWalk)(node)) {
				break;
			}
		}

		if (owner) {
			if (a_type == kMotionDynamic) {
				static REL::Relocation<void (*)(void*, bool)> wake{ WakePhysics };
				wake(owner, true);
			} else if (a_type == kMotionKeyframed) {
				if (void* keyframed = At<void*>(owner, PhysicsOwnerKeyframed)) {
					VFunc<void (*)(void*)>(keyframed, 8)(keyframed);
				}
			}
			if (_InterlockedExchangeAdd(&At<long>(owner, 8), -1) == 1) {
				VFunc<void (*)(void*, std::uint32_t)>(owner, 0)(owner, 1);
			}
		}
		return switched;
	}

	void Notify(const char* a_text)
	{
		// The event Debug.Notification (ID 117311) sends: { text, sound (empty), 1 }.
		struct NotificationData
		{
			RE::BSFixedString text;
			RE::BSFixedString sound;
			std::uint16_t     flags{ 1 };
		};
		static REL::Relocation<void* (*)()> getSource{ HUDNotificationSource };
		auto* source = static_cast<RE::BSTEventSource<NotificationData>*>(getSource());
		if (source) {
			source->Notify(NotificationData{ RE::BSFixedString(a_text), RE::BSFixedString(), 1 });
		}
	}

	std::optional<View> GetView()
	{
		auto* const player = GetPlayer();
		static REL::Relocation<std::byte**> cameraPtr{ PlayerCameraSingleton };
		const auto* camera = *cameraPtr;
		if (!player || !camera) {
			return std::nullopt;
		}
		const auto* root = At<std::byte*>(camera, CameraRoot);
		if (!root) {
			return std::nullopt;
		}

		View view;
		const auto* state = At<std::byte*>(camera, CameraCurrentState);
		view.firstPerson = state && At<std::uint32_t>(state, CameraStateID) == 0;

		// The scene graph may be shifted from reference coordinates (Starfield moves the world origin),
		// so convert through the player: its 3D root sits at its reference position.
		Vec3 shift{};
		if (const auto* player3D = Loaded3D(player)) {
			shift = At<Vec3>(player3D, NodeWorldTranslate) - Position(player);
		}
		view.origin = At<Vec3>(root, NodeWorldTranslate) - shift;

		const Vec3 a = Angle(player);
		const Vec3 lookForward{ std::sin(a.z) * std::cos(a.x), std::cos(a.z) * std::cos(a.x), -std::sin(a.x) };

		float m[3][4];
		std::memcpy(m, root + NodeWorldRotate, sizeof(m));
		if (view.firstPerson && !g_calibration.locked) {
			Calibrate(m, lookForward);
		}
		view.forward = g_calibration.locked ? Candidate(m, g_calibration.candidate) : lookForward;
		return view;
	}
}
