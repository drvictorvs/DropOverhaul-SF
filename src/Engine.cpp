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
		view.shift = shift;

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
	std::optional<Bounds> GetBounds(RE::TESObjectREFR* a_ref)
	{
		using fn_t = const Vec3* (*)(RE::TESObjectREFR*, Vec3*);
		Vec3        minBuf{};
		Vec3        maxBuf{};
		const auto* lo = VFunc<fn_t>(a_ref, VSlotBoundMin)(a_ref, &minBuf);
		const auto* hi = VFunc<fn_t>(a_ref, VSlotBoundMax)(a_ref, &maxBuf);
		if (!lo || !hi) {
			return std::nullopt;
		}
		Bounds b{ *lo, *hi };
		if (b.max.x < b.min.x || b.max.y < b.min.y || b.max.z < b.min.z) {
			return std::nullopt;
		}
		return b;
	}

	namespace
	{
		void* PhysicsWorld()
		{
			const auto* player = GetPlayer();
			void* const cell = player ? ParentCell(player) : nullptr;
			if (!cell) {
				return nullptr;
			}
			if ((At<std::uint32_t>(cell, CellFlags) & 1) != 0) {
				void* const extra = At<void*>(cell, CellExtraData);
				if (!extra) {
					return nullptr;
				}
				static REL::Relocation<void* (*)(void*)> fromExtra{ bhkWorld_FromExtraData };
				return fromExtra(extra);
			}
			static REL::Relocation<void***> exterior{ ExteriorPhysicsWorld };
			void** const slot = *exterior;
			return slot ? *slot : nullptr;
		}

		// Same steps as the ViewCaster's hit loop.
		RE::TESObjectREFR* RefForBody(void* a_world, std::uint32_t a_body)
		{
			if (!a_world || a_body == 0xFFFFFF) {
				return nullptr;
			}
			void* const bodies = reinterpret_cast<std::byte*>(a_world) + WorldBodies;
			if (VFunc<int (*)(void*, std::uint32_t)>(bodies, BodiesVSlotIsValid)(bodies, a_body) == 0) {
				return nullptr;
			}
			auto* const body = VFunc<std::byte* (*)(void*, std::uint32_t)>(bodies, BodiesVSlotGet)(bodies, a_body);
			void* const collision = body ? At<void*>(body, BodyCollisionObject) : nullptr;
			if (!collision) {
				return nullptr;
			}
			auto* const owner = VFunc<std::byte* (*)(void*)>(collision, CollisionVSlotOwner)(collision);
			auto* const node = owner ? At<RE::NiAVObject*>(owner, OwnerNode) : nullptr;
			if (!node) {
				return nullptr;
			}
			static REL::Relocation<RE::TESObjectREFR* (*)(RE::NiAVObject*, bool)> refFor3D{ RefFor3D };
			return refFor3D(node, true);
		}

		struct alignas(16) Vec4
		{
			float x, y, z, w;
		};

		// Where a ray (physics / scene-graph coordinates, a_dir of length 1) passes through a
		// reference's bounds as its 3D is placed now: distances along the ray where it enters and
		// leaves them. Null if it misses them.
		std::optional<std::pair<float, float>> RayThroughBounds(const RE::TESObjectREFR* a_ref, const Bounds& a_bounds,
			const Vec3& a_start, const Vec3& a_dir)
		{
			const auto* root = Loaded3D(a_ref);
			if (!root) {
				return std::nullopt;
			}
			float r[3][4];
			std::memcpy(r, reinterpret_cast<const std::byte*>(root) + NodeWorldRotate, sizeof(r));
			// world = R * local + t, so local = R^T * (world - t).
			auto toLocal = [&](const Vec3& a_v) {
				return Vec3{ r[0][0] * a_v.x + r[1][0] * a_v.y + r[2][0] * a_v.z,
					r[0][1] * a_v.x + r[1][1] * a_v.y + r[2][1] * a_v.z,
					r[0][2] * a_v.x + r[1][2] * a_v.y + r[2][2] * a_v.z };
			};
			const Vec3  o = toLocal(a_start - At<Vec3>(root, NodeWorldTranslate));
			const Vec3  v = toLocal(a_dir);
			const float lo[3]{ a_bounds.min.x, a_bounds.min.y, a_bounds.min.z };
			const float hi[3]{ a_bounds.max.x, a_bounds.max.y, a_bounds.max.z };
			const float oo[3]{ o.x, o.y, o.z };
			const float vv[3]{ v.x, v.y, v.z };
			float       enter = -std::numeric_limits<float>::max();
			float       leave = std::numeric_limits<float>::max();
			for (int k = 0; k < 3; ++k) {
				if (std::abs(vv[k]) < 1e-6F) {
					if (oo[k] < lo[k] || oo[k] > hi[k]) {
						return std::nullopt;
					}
					continue;
				}
				float t1 = (lo[k] - oo[k]) / vv[k];
				float t2 = (hi[k] - oo[k]) / vv[k];
				if (t1 > t2) {
					std::swap(t1, t2);
				}
				enter = std::max(enter, t1);
				leave = std::min(leave, t2);
			}
			if (leave < std::max(enter, 0.0F)) {
				return std::nullopt;
			}
			return std::pair{ enter, leave };
		}

		// One cast from a_from to a_to (physics coordinates). False: nothing hit (or skipped).
		bool PickOnce(void* a_world, const Vec3& a_from, const Vec3& a_to, std::byte* a_pick)
		{
			std::memset(a_pick, 0, PickSize);
			At<std::uint16_t>(a_pick, PickFilterHint) = 0xFFFF;
			At<std::uint32_t>(a_pick, PickFilterInfo) = 0;
			At<std::uint16_t>(a_pick, PickQueryFlags) = 1;
			At<std::uint8_t>(a_pick, PickQueryFlags + 2) = 0xFB;

			const Vec3 d = a_to - a_from;
			At<Vec4>(a_pick, PickFrom) = { a_from.x, a_from.y, a_from.z, 0.0F };
			At<Vec4>(a_pick, PickDirection) = { d.x, d.y, d.z, 1.0F };
			constexpr float kHuge = 3.40282002e+38F;  // the game's value for a zero component (0x7F7FFFEE)
			auto inv = [](float a_v) { return a_v == 0.0F ? kHuge : 1.0F / a_v; };
			At<Vec4>(a_pick, PickInvDirection) = { inv(d.x), inv(d.y), inv(d.z), 1.0F };

			static REL::Relocation<void* (*)(std::byte*)> initResult{ CollisionResult_Init };
			initResult(a_pick + PickResult);

			At<std::uint64_t>(a_pick, PickLayerMask) = kPickLayerMask;
			At<void*>(a_pick, PickCollector) = nullptr;
			At<std::uint32_t>(a_pick, PickFilterInfo2) = 0;
			At<std::uint8_t>(a_pick, PickFlags) = 1;
			At<std::uint8_t>(a_pick, PickFlags + 1) = 1;

			static REL::Relocation<bool (*)(void*, std::byte*)> pick{ bhkWorld_Pick };
			const bool hit = pick(a_world, a_pick);
			return hit && At<std::uint8_t>(a_pick, PickSkipped) == 0;
		}

		// Pick keeps a reference on the world it stores at + 0xE0; the game's pick data releases it.
		void ReleasePick(std::byte* a_pick)
		{
			if (void* const world = At<void*>(a_pick, PickWorld)) {
				static REL::Relocation<void (*)(void*)> release{ ReferencedObject_Release };
				release(world);
				At<void*>(a_pick, PickWorld) = nullptr;
			}
		}
	}

	std::optional<Hit> CastRay(const View& a_view, float a_length, std::initializer_list<const RE::TESObjectREFR*> a_ignore,
		const RE::TESObjectREFR* a_self, const std::optional<Bounds>& a_selfBounds)
	{
		void* const world = PhysicsWorld();
		if (!world || a_length <= 0.0F) {
			return std::nullopt;
		}

		// In physics (scene-graph) coordinates.
		const Vec3 start = a_view.origin + a_view.shift;
		const Vec3 end = start + a_view.forward * a_length;
		Vec3       from = start;

		// Hits on the held object, if a hit on it isn't recognized as its own: anything before the ray
		// leaves its bounds. A surface right where the ray leaves them (what it rests on) counts.
		const auto self = a_self && a_selfBounds ? RayThroughBounds(a_self, *a_selfBounds, start, a_view.forward) : std::nullopt;

		alignas(16) std::byte pick[PickSize];
		// A hit on a skipped reference: cast again from just past it, a few times at most.
		for (int i = 0; i < 6; ++i) {
			if ((end - from).Dot(a_view.forward) <= 0.01F) {
				break;
			}
			const bool hitSomething = PickOnce(world, from, end, pick);
			// Read what's needed before letting go of the world reference.
			if (!hitSomething) {
				ReleasePick(pick);
				return std::nullopt;
			}
			const auto&   result = *reinterpret_cast<const Vec4*>(pick + PickResult + ResultPosition);
			const auto&   normal = *reinterpret_cast<const Vec4*>(pick + PickResult + ResultNormal);
			const Vec3    position{ result.x, result.y, result.z };
			const auto    layer = At<std::uint32_t>(pick, PickResult + ResultHitFilter) & 0x7F;
			auto* const   ref = RefForBody(At<void*>(pick, PickWorld), At<std::uint32_t>(pick, PickResult + ResultHitBody));
			ReleasePick(pick);
			const bool    seeThrough = layer == 3 || (layer >= 26 && layer <= 28);
			const float   along = (position - start).Dot(a_view.forward);
			const bool    insideSelf = self && along > self->first - 0.03F && along < self->second - 0.01F;
			const bool    ignored = (ref && std::ranges::find(a_ignore, ref) != a_ignore.end()) || insideSelf;
			if (seeThrough || ignored) {
				from = position + a_view.forward * 0.02F;
				continue;
			}
			Hit hit;
			hit.position = position - a_view.shift;
			hit.normal = Vec3{ normal.x, normal.y, normal.z }.Normalized();
			hit.distance = (position - start).Length();
			hit.ref = ref;
			hit.layer = layer;
			return hit;
		}
		return std::nullopt;
	}
}
