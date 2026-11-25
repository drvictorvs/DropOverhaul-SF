#include "Settings.h"

namespace DROP
{
	namespace
	{
		enum class Kind
		{
			Bool,
			Key,
			Double
		};

		struct Option
		{
			const char* section;
			const char* key;
			Kind        kind;
			void*       target;
			double      min{ 0.0 };
			double      max{ 0.0 };
		};

		std::string Trim(std::string_view a_text)
		{
			const auto first = a_text.find_first_not_of(" \t\r\n");
			if (first == std::string_view::npos) {
				return {};
			}
			const auto last = a_text.find_last_not_of(" \t\r\n");
			return std::string(a_text.substr(first, last - first + 1));
		}

		std::string Lower(std::string a_text)
		{
			std::ranges::transform(a_text, a_text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return a_text;
		}

		std::optional<double> ParseNumber(const std::string& a_value)
		{
			try {
				std::size_t used = 0;
				const bool  hex = a_value.size() > 2 && a_value[0] == '0' && (a_value[1] == 'x' || a_value[1] == 'X');
				const double v = hex ? static_cast<double>(std::stoul(a_value, &used, 16)) : std::stod(a_value, &used);
				return used == a_value.size() ? std::optional(v) : std::nullopt;
			} catch (...) {
				return std::nullopt;
			}
		}

		void Set(const Option& a_opt, const std::string& a_value)
		{
			const auto v = ParseNumber(a_value);
			if (!v) {
				REX::WARN("Ignoring {} = {} in [{}]: not a number", a_opt.key, a_value, a_opt.section);
				return;
			}
			const double clamped = a_opt.max > a_opt.min ? std::clamp(*v, a_opt.min, a_opt.max) : *v;
			switch (a_opt.kind) {
			case Kind::Bool:
				*static_cast<bool*>(a_opt.target) = clamped != 0.0;
				break;
			case Kind::Key:
				*static_cast<std::uint32_t*>(a_opt.target) = static_cast<std::uint32_t>(std::clamp(clamped, 0.0, 255.0));
				break;
			case Kind::Double:
				*static_cast<double*>(a_opt.target) = clamped;
				break;
			}
		}
	}

	void Settings::Load()
	{
		const Option options[]{
			{ "Keys", "iGrabKey", Kind::Key, &grabKey },
			{ "Keys", "iCancelKey", Kind::Key, &cancelKey },
			{ "Keys", "iYawLeftKey", Kind::Key, &yawLeftKey },
			{ "Keys", "iYawRightKey", Kind::Key, &yawRightKey },
			{ "Keys", "iPitchUpKey", Kind::Key, &pitchUpKey },
			{ "Keys", "iPitchDownKey", Kind::Key, &pitchDownKey },
			{ "Keys", "iRollLeftKey", Kind::Key, &rollLeftKey },
			{ "Keys", "iRollRightKey", Kind::Key, &rollRightKey },
			{ "Keys", "iResetRotationKey", Kind::Key, &resetRotationKey },
			{ "Keys", "iPushKey", Kind::Key, &pushKey },
			{ "Keys", "iPullKey", Kind::Key, &pullKey },
			{ "Keys", "iRaiseKey", Kind::Key, &raiseKey },
			{ "Keys", "iLowerKey", Kind::Key, &lowerKey },
			{ "Keys", "iFineKey", Kind::Key, &fineKey },
			{ "Keys", "iFreezeToggleKey", Kind::Key, &freezeToggleKey },
			{ "Placement", "bDropOnPlace", Kind::Bool, &dropOnPlace },
			{ "Placement", "bRotateWithView", Kind::Bool, &rotateWithView },
			{ "Placement", "bAllowStatic", Kind::Bool, &allowStatic },
			{ "Placement", "fRotateSpeed", Kind::Double, &rotateSpeed, 1.0, 720.0 },
			{ "Placement", "fMoveSpeed", Kind::Double, &moveSpeed, 1.0, 2000.0 },
			{ "Placement", "fFineMultiplier", Kind::Double, &fineMultiplier, 0.01, 1.0 },
			{ "Placement", "fMinDistance", Kind::Double, &minDistance, 0.0, 1000.0 },
			{ "Placement", "fMaxDistance", Kind::Double, &maxDistance, 10.0, 10000.0 },
			{ "General", "bNotifications", Kind::Bool, &notifications },
			{ "General", "bDebugLog", Kind::Bool, &debugLog },
		};

		const auto    path = std::filesystem::path("Data/SFSE/Plugins/DropOverhaulSF.ini");
		std::ifstream file(path);
		if (!file) {
			REX::INFO("No {} found; using defaults", path.string());
			return;
		}

		std::string section;
		std::string line;
		while (std::getline(file, line)) {
			if (const auto comment = line.find_first_of(";#"); comment != std::string::npos) {
				line.erase(comment);
			}
			line = Trim(line);
			if (line.empty()) {
				continue;
			}
			if (line.front() == '[' && line.back() == ']') {
				section = Lower(Trim(std::string_view(line).substr(1, line.size() - 2)));
				continue;
			}
			const auto eq = line.find('=');
			if (eq == std::string::npos) {
				continue;
			}
			const auto key = Lower(Trim(std::string_view(line).substr(0, eq)));
			const auto value = Trim(std::string_view(line).substr(eq + 1));
			for (const auto& opt : options) {
				if (Lower(opt.section) == section && Lower(opt.key) == key) {
					Set(opt, value);
					break;
				}
			}
		}
		if (minDistance > maxDistance) {
			std::swap(minDistance, maxDistance);
		}
	}
}
