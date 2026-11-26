#pragma once

namespace tom::stats
{
// THROWAWAY prototype of spike S2 (docs/stats/PLAN.md, Phase 3). U1 replaces
// it; nothing else may depend on it.
//
// While the skills menu is open, K or the gamepad's Y opens a native message
// box whose body and buttons come from
// Data/SKSE/Plugins/trials-of-mastery-stats-s2.json, read again on every press.
// Logs every button event seen while the skills menu is open, the text and
// buttons shown, and the button chosen. Call once, at kDataLoaded.
void registerStatBoxSpike();
}
