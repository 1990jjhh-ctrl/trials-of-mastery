#include "tom/stats/core/actor_stats.h"

#include "saturating.h"

namespace tom::stats
{

namespace
{
// Moves base and start value together, so the pools they span stay the same
// (ST-3). The difference is 64-bit, since it can span the whole int32 range
// twice.
void moveBase(ActorStats& actor, const StatId& id, std::int64_t difference)
{
	std::int32_t& base = actor.values[id].base;
	base = saturateToInt32(base + difference);
	std::int32_t& start = actor.startValues[id];
	start = saturateToInt32(start + difference);
}

[[nodiscard]] std::int64_t sumOfAdjustments(const ActorStats& actor, const StatId& id)
{
	const auto it = actor.baseAdjustments.find(id);
	if (it == actor.baseAdjustments.end()) {
		return 0;
	}
	std::int64_t sum = 0;
	for (const auto& [key, amount] : it->second) {
		sum += amount;
	}
	return sum;
}
}

double effective(const ActorStats& actor, const StatId& id)
{
	// Summed as double, so large point counts and modifiers cannot overflow
	// the int32 range.
	double points = 0.0;
	if (const auto it = actor.values.find(id); it != actor.values.end()) {
		const StatValue& value = it->second;
		points = static_cast<double>(value.base) + value.accumulated + value.active;
	}

	double modifier = 0.0;
	if (const auto it = actor.modifiers.find(id); it != actor.modifiers.end()) {
		for (const auto& [key, amount] : it->second) {
			modifier += amount;
		}
	}

	return points * actor.multiplier + modifier;
}

void initStat(ActorStats& actor, const StatId& id, std::int32_t defaultBase)
{
	const std::int32_t base = saturateToInt32(defaultBase + sumOfAdjustments(actor, id));
	actor.values[id].base = base;
	actor.startValues[id] = base;
	actor.defaultBases[id] = defaultBase;
}

void setBaseAdjustment(ActorStats& actor, const StatId& id, const BaseAdjustmentKey& key, std::int32_t amount)
{
	std::int32_t&      stored = actor.baseAdjustments[id][key];
	const std::int64_t difference = std::int64_t{ amount } - stored;
	stored = amount;
	moveBase(actor, id, difference);
}

void removeBaseAdjustment(ActorStats& actor, const StatId& id, const BaseAdjustmentKey& key)
{
	const auto stat = actor.baseAdjustments.find(id);
	if (stat == actor.baseAdjustments.end()) {
		return;
	}
	const auto entry = stat->second.find(key);
	if (entry == stat->second.end()) {
		return;
	}
	const std::int32_t amount = entry->second;
	stat->second.erase(entry);
	if (stat->second.empty()) {
		actor.baseAdjustments.erase(stat);
	}
	moveBase(actor, id, -std::int64_t{ amount });
}

void applyDefaultBase(ActorStats& actor, const StatId& id, std::int32_t defaultBase)
{
	std::int32_t&      applied = actor.defaultBases[id];
	const std::int64_t difference = std::int64_t{ defaultBase } - applied;
	applied = defaultBase;
	moveBase(actor, id, difference);
}

}
