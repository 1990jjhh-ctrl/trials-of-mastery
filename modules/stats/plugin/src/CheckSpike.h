#pragma once

// THROWAWAY prototype of spike S6b (docs/stats/PLAN.md, Phase 3). G10a to G10c
// replace it.

namespace tom::stats
{
// Hooks the game's computation of each default check of ST-15 (lockpicking,
// pickpocket, sneak and detection, persuasion, intimidation, barter), logs
// its inputs, the value the game computes and the thread, and adjusts the
// result by the values in Data/SKSE/Plugins/trials-of-mastery-stats-s6b.json
// (the defaults only observe). Also logs which plugins already patch these
// functions. Call once, at kDataLoaded. A site that is not found is left out,
// and the log says why.
void installCheckSpike();
}
