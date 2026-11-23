#pragma once

#include "tom/stats/core/stat_registry.h"
#include "tom/stats/core/stat_value.h"

#include <cstdint>
#include <map>
#include <string>

namespace tom::stats
{

// The key a module or mod sets its modifier under (ST-8).
using ModifierKey = std::string;

// The stats of one actor (ST-4). A stat without an entry in values counts as
// all zero.
struct ActorStats
{
	std::map<StatId, StatValue>                           values;
	std::map<StatId, std::int32_t>                        startValues;
	std::map<StatId, std::map<ModifierKey, std::int32_t>> modifiers;
	double                                                multiplier = 1.0;
};

// (base + accumulated + active) * multiplier + the sum of the stat's
// modifiers (ST-8), without a clamp (ST-7).
[[nodiscard]] double effective(const ActorStats& actor, const StatId& id);

}
