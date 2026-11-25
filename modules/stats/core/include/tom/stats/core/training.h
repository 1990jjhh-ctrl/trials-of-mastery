#pragma once

// The stat XP from skill level-ups (ST-14, ST-16) and from actions (ST-15,
// ST-16), with the XP modifiers of ST-17 applied. Mappings and actions that
// mods register through the API reach the tables as their lowest layer
// (Stats plan, assumption 3), so these functions treat them like any other.

#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/stat_registry.h"
#include "tom/stats/core/xp_modifiers.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tom::stats
{

// XP for one stat.
struct StatXp
{
	StatId stat;
	double xp = 0.0;

	bool operator==(const StatXp&) const = default;
};

// The stat XP for a skill reaching levelReached: the level, split between the
// skill's primary and secondary stat by its primary share (ST-14), times the
// skill's SkillLevelUpStatXp factor (ST-17). Gives the primary stat first; a
// skill whose primary and secondary stat are the same gives one entry with
// the whole amount. Levels above 100 follow the same rule (ST-16). Empty for
// a skill without a mapping (ST-16).
//
// The amounts are not clamped; addXp (stat_value.h) ignores those that are
// not positive and finite.
[[nodiscard]] std::vector<StatXp> skillLevelUpXp(const SkillsTable& skills, const XpModifierRegistry& modifiers,
	const std::string& skill, std::int32_t levelReached);

// The stat XP an action gives for its input, e.g. the weapon weight or the
// money value (ST-15): each training curve of the action evaluated at input,
// times the action's ActionStatXp factor (ST-17), in stat order. Empty for an
// unknown action and for an action that trains no stat.
//
// The amounts are not clamped; addXp (stat_value.h) ignores those that are
// not positive and finite.
[[nodiscard]] std::vector<StatXp> actionTrainingXp(const ActionsTable& actions, const XpModifierRegistry& modifiers,
	const std::string& action, double input);

}
