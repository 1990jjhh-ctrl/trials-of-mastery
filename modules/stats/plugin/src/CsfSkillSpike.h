#pragma once

// THROWAWAY prototype of spike S7 (docs/stats/PLAN.md, Phase 3). G4 and G6
// replace it.

namespace tom::stats
{
// Registers for the interface message of Custom Skills Framework (CSF), which
// carries its skill increase event. Call once, at kPostLoad, when every plugin
// is loaded.
void registerCsfSkillSpikeListener();

// Logs every skill level-up that the vanilla events and CSF's event report,
// hooks the candidate points of CSF's skill XP path and the vanilla perk entry
// of spike S3, logs the XP going in and out of each, and multiplies at the
// ones that Data/SKSE/Plugins/trials-of-mastery-stats-s7.json chooses. Call
// once, at kDataLoaded. A candidate whose site is not found is left out, and
// the log says why.
void installCsfSkillSpike();
}
