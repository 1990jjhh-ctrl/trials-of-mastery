#pragma once

// THROWAWAY prototype of spike S5 (docs/stats/PLAN.md, Phase 3). G8 replaces it.

namespace tom::stats
{
// Hooks the vanilla physical crit computation and two candidate points for
// spell crits, logs the attacker, target, weapon or spell, crit chance, roll
// and damage at each, and applies the crit chance and multiplier that
// Data/SKSE/Plugins/trials-of-mastery-stats-s5.json sets, for the player as
// attacker. Call once, at kDataLoaded. A site that is not found is left out,
// and the log says why.
void installCritSpike();
}
