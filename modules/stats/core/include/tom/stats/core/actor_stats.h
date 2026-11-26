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

// The key a base adjustment is stored under (ST-3): the module's race key, or
// the key of a source registered through the API.
using BaseAdjustmentKey = std::string;

// The stats of one actor (ST-4). A stat without an entry in values counts as
// all zero, and so does a missing default base.
//
// The base in values already contains the stat's base adjustments (Stats plan,
// assumption 11); baseAdjustments records them per key so they can be changed
// and removed later. defaultBases is the default base last applied per stat,
// the reference of the relative effects (Stats plan, assumption 12).
struct ActorStats
{
	std::map<StatId, StatValue>                                 values;
	std::map<StatId, std::map<ModifierKey, std::int32_t>>       modifiers;
	std::map<StatId, std::map<BaseAdjustmentKey, std::int32_t>> baseAdjustments;
	std::map<StatId, std::int32_t>                              defaultBases;
	double                                                      multiplier = 1.0;

	bool operator==(const ActorStats&) const = default;
};

// Sets the base to the default base plus the stat's base adjustments and
// stores the default base as last applied (ST-3). Meant for a new actor or a
// new stat: points ascended into the base are replaced.
// Leaves the accumulated and active points and the XP alone.
void initStat(ActorStats& actor, const StatId& id, std::int32_t defaultBase);

// Sets the adjustment under key and moves the base by the difference to the
// key's old value, or by the whole value for a new key (ST-3).
void setBaseAdjustment(ActorStats& actor, const StatId& id, const BaseAdjustmentKey& key, std::int32_t amount);

// Removes the adjustment under key and takes its value off the base (ST-3).
// Does nothing for a key that is not set.
void removeBaseAdjustment(ActorStats& actor, const StatId& id, const BaseAdjustmentKey& key);

// Moves the base by the difference between defaultBase and the default base
// last applied, then stores defaultBase as last applied (ST-3).
// With none applied yet, the difference is the whole of defaultBase.
void applyDefaultBase(ActorStats& actor, const StatId& id, std::int32_t defaultBase);

// (base + accumulated + active) * multiplier + the sum of the stat's
// modifiers (ST-8), without a clamp (ST-7).
[[nodiscard]] double effective(const ActorStats& actor, const StatId& id);

}
