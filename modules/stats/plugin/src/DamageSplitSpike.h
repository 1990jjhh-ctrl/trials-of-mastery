#pragma once

// THROWAWAY prototype of spike S4 (docs/stats/PLAN.md, Phase 3). G7 replaces it.

namespace tom::stats
{
// Hooks three candidate points where incoming Health damage can be split
// before it reaches Health, logs the damage, attacker, target and kind
// (physical or magic) at each, and splits the player's damage into Stamina or
// Magicka at the one that Data/SKSE/Plugins/trials-of-mastery-stats-s4.json
// chooses. Also logs which plugins already patch the damage functions. Call
// once, at kDataLoaded. A candidate whose site is not found is left out, and
// the log says why.
void installDamageSplitSpike();
}
