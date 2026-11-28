#pragma once

// THROWAWAY prototype of spike S6a (docs/stats/PLAN.md, Phase 3). G9a to G9c
// replace it.

namespace tom::stats
{
// Registers a candidate hook or event sink for each default training action
// of ST-15 and logs, per action, the data the hook sees, the thread it runs on
// and the stat XP the default actions table would give. It only observes; no
// stat or actor value changes. Data/SKSE/Plugins/trials-of-mastery-stats-s6a.json
// switches the combat, body and social groups and a verbose mode on and off,
// and is read again when it changes. Call once, at kDataLoaded.
void installTrainingSpike();
}
