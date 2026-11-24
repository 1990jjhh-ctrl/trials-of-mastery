#pragma once

// The single settings of the module's INI (ST-27, and the gamepad button of
// ST-24). The shipped INI, data/trials-of-mastery-stats.ini, documents each key.

#include "tom/stats/core/config/config_error.h"

#include <cstdint>
#include <string_view>
#include <vector>

namespace tom::stats
{

struct Settings
{
	double       costGrowth = 2.0;          // r of ST-6, default from ST-27
	std::int32_t advanceInterval = 5;       // N of ST-6
	std::int32_t freePointsPerLevelUp = 3;  // ST-20
	// SKSE key codes: a DirectX scan code (1 to 255) for the key, a gamepad
	// code (266 to 281) for the button. The defaults are decision D7 of the
	// Stats plan.
	std::uint32_t statBoxKey = 37;                // K
	std::uint32_t statBoxGamepadButton = 279;     // Y
	bool          allowIncompatibleMods = false;  // the override of ST-28

	bool operator==(const Settings&) const = default;
};

struct SettingsResult
{
	Settings                 settings;
	std::vector<ConfigError> errors;  // values that were rejected
};

// Reads the INI text. A missing key keeps its built-in default. A value that
// is not valid, such as an r of 1 or less (ST-6), is rejected and reported,
// and the default stays. Errors name keys as "Section.Key" and leave `file`
// empty; sections and keys are case-insensitive.
[[nodiscard]] SettingsResult loadSettings(std::string_view text);

}
