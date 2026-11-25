#pragma once

// Thin wrappers around the engine functions in GameIDs.h. Game thread only.
namespace DROP::Engine
{
	struct Vec3
	{
		float x{ 0.0F };
		float y{ 0.0F };
		float z{ 0.0F };

		Vec3 operator+(const Vec3& a_rhs) const { return { x + a_rhs.x, y + a_rhs.y, z + a_rhs.z }; }
		Vec3 operator-(const Vec3& a_rhs) const { return { x - a_rhs.x, y - a_rhs.y, z - a_rhs.z }; }
		Vec3 operator*(float a_s) const { return { x * a_s, y * a_s, z * a_s }; }
		float Dot(const Vec3& a_rhs) const { return x * a_rhs.x + y * a_rhs.y + z * a_rhs.z; }
		float Length() const { return std::sqrt(Dot(*this)); }
		Vec3  Normalized() const
		{
			const float len = Length();
			return len > 1e-6F ? *this * (1.0F / len) : Vec3{ 0.0F, 1.0F, 0.0F };
		}
	};
	static_assert(sizeof(Vec3) == 12);

	// Owning pointer to a reference: holds one reference count, released with TESForm::DecRefCount.
	class RefPtr
	{
	public:
		RefPtr() = default;
		explicit RefPtr(RE::TESObjectREFR* a_ref) noexcept :
			_ref(a_ref) {}
		RefPtr(const RefPtr&) = delete;
		RefPtr& operator=(const RefPtr&) = delete;
		RefPtr(RefPtr&& a_rhs) noexcept :
			_ref(std::exchange(a_rhs._ref, nullptr)) {}
		RefPtr& operator=(RefPtr&& a_rhs) noexcept
		{
			if (this != &a_rhs) {
				reset();
				_ref = std::exchange(a_rhs._ref, nullptr);
			}
			return *this;
		}
		~RefPtr() { reset(); }

		void                             reset() noexcept;
		[[nodiscard]] RE::TESObjectREFR* get() const noexcept { return _ref; }
		[[nodiscard]] explicit           operator bool() const noexcept { return _ref != nullptr; }

	private:
		RE::TESObjectREFR* _ref{ nullptr };
	};

	// The reference the player is looking at (the one the HUD's activation prompt is for), if any.
	RefPtr GetCrosshairRef();

	RE::TESObjectREFR* GetPlayer();

	std::uint32_t   FormID(const RE::TESObjectREFR* a_ref);
	bool            IsActor(const RE::TESObjectREFR* a_ref);
	Vec3            Position(const RE::TESObjectREFR* a_ref);
	Vec3            Angle(const RE::TESObjectREFR* a_ref);
	void*           ParentCell(const RE::TESObjectREFR* a_ref);
	RE::NiAVObject* Loaded3D(const RE::TESObjectREFR* a_ref);  // borrowed; null if not loaded

	// Moves the reference within its own cell (what Papyrus SetPosition/SetAngle do).
	void MoveTo(RE::TESObjectREFR* a_ref, const Vec3& a_position, const Vec3& a_angle);

	// What Papyrus SetMotionType(a_type, true) does. False if the reference has no physics to switch.
	bool SetMotionType(RE::TESObjectREFR* a_ref, std::uint32_t a_type);

	// Shows a HUD notification, as Papyrus Debug.Notification does.
	void Notify(const char* a_text);

	// Camera position (in reference coordinates) and view direction.
	struct View
	{
		Vec3 origin;
		Vec3 forward;
		Vec3 shift;  // scene-graph (and physics) coordinates minus reference coordinates
		bool firstPerson{ false };
	};
	std::optional<View> GetView();

	// The reference's bounds, relative to its position (unrotated). Null if it has none.
	struct Bounds
	{
		Vec3 min;
		Vec3 max;
	};
	std::optional<Bounds> GetBounds(RE::TESObjectREFR* a_ref);

	// The first surface along the view direction, up to a_length meters away, skipping the given
	// references (and see-through layers, as the activation pick does). Also skips hits before the
	// ray leaves a_self's bounds (as placed now), in case a hit on it isn't recognized as its own.
	// Position in reference coordinates. Uses the physics world of the player's cell.
	struct Hit
	{
		Vec3               position;
		Vec3               normal;
		float              distance{ 0.0F };
		RE::TESObjectREFR* ref{ nullptr };  // borrowed; for comparison and logging only
		std::uint32_t      layer{ 0 };
	};
	std::optional<Hit> CastRay(const View& a_view, float a_length, std::initializer_list<const RE::TESObjectREFR*> a_ignore,
		const RE::TESObjectREFR* a_self = nullptr, const std::optional<Bounds>& a_selfBounds = std::nullopt);

	// Euler angles (radians) helpers, Bethesda convention: yaw 0 = +Y, positive towards +X.
	inline float YawOf(const Vec3& a_dir) { return std::atan2(a_dir.x, a_dir.y); }
	inline float PitchOf(const Vec3& a_dir) { return -std::asin(std::clamp(a_dir.z, -1.0F, 1.0F)); }
}
