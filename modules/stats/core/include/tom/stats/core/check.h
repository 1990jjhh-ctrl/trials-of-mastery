#pragma once

// The check of an action (ST-15): its value I from stats and skills, and the
// game quantity I maps to (Stats plan, assumption 7).

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/config/tables.h"

#include <functional>
#include <optional>
#include <string>

namespace tom::stats
{

// The level of a skill, by its name in the tables. The caller decides what a
// skill the actor does not have gives. Must not be empty: checkValue calls it
// for every skill the check names.
using SkillLevels = std::function<double(const std::string& skill)>;

// I = the sum of the check's stat curves at the actor's effective stats
// (actor_stats.h) and of its skill curves at the skill levels (ST-15).
// nullopt for an unknown action and for an action without a check.
[[nodiscard]] std::optional<double> checkValue(const ActionsTable& actions, const std::string& action,
	const ActorStats& actor, const SkillLevels& skillLevels);

// The check's result curve at i, the game quantity of the action, e.g. the
// lockpicking sweet spot (Stats plan, assumption 7). nullopt for an unknown
// action and for an action without a check.
[[nodiscard]] std::optional<double> checkResult(const ActionsTable& actions, const std::string& action, double i);

}
