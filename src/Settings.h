#pragma once

namespace DROP
{
	// Values come from Data/SFSE/Plugins/DropOverhaulSF.ini. Keys are Windows virtual-key codes.
	struct Settings
	{
		// [Keys]
		std::uint32_t grabKey{ 0x04 };         // middle mouse: pick up what you're looking at / place it
		std::uint32_t cancelKey{ 0x2E };       // Delete: put it back where it was
		std::uint32_t yawLeftKey{ 0x64 };      // numpad 4
		std::uint32_t yawRightKey{ 0x66 };     // numpad 6
		std::uint32_t pitchUpKey{ 0x68 };      // numpad 8
		std::uint32_t pitchDownKey{ 0x62 };    // numpad 2
		std::uint32_t rollLeftKey{ 0x67 };     // numpad 7
		std::uint32_t rollRightKey{ 0x69 };    // numpad 9
		std::uint32_t resetRotationKey{ 0x65 };  // numpad 5
		std::uint32_t pushKey{ 0x6B };         // numpad +: farther
		std::uint32_t pullKey{ 0x6D };         // numpad -: closer
		std::uint32_t raiseKey{ 0x21 };        // PageUp
		std::uint32_t lowerKey{ 0x22 };        // PageDown
		std::uint32_t fineKey{ 0x12 };         // Alt: slow, fine adjustments while held
		std::uint32_t freezeToggleKey{ 0x6E };  // numpad .: switch between dropping and staying in place

		// [Placement]
		bool   dropOnPlace{ true };      // true: physics takes over when placed (it settles); false: stays exactly where placed
		bool   rotateWithView{ true };   // the object turns with you, keeping the same side towards you
		bool   allowStatic{ false };     // also move objects without physics (furniture, clutter statics)
		double rotateSpeed{ 90.0 };      // degrees per second
		double moveSpeed{ 150.0 };       // units per second (distance and height)
		double fineMultiplier{ 0.2 };
		double minDistance{ 40.0 };
		double maxDistance{ 1000.0 };

		// [General]
		bool notifications{ true };  // HUD messages: picked up, placed, can't move that
		bool debugLog{ false };

		static Settings& Get()
		{
			static Settings settings;
			return settings;
		}

		void Load();
	};
}
