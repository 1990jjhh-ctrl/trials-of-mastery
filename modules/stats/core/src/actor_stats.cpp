#include "tom/stats/core/actor_stats.h"

namespace tom::stats
{

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

}
