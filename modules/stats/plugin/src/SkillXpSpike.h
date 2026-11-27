#pragma once

// THROWAWAY prototype of spike S3 (docs/stats/PLAN.md, Phase 3). G6 replaces it.

namespace tom::stats
{
// Hooks three candidate points where skill XP can be multiplied, logs the XP
// going in and out of each, and multiplies at the one that
// Data/SKSE/Plugins/trials-of-mastery-stats-s3.json chooses. Call once, at
// kDataLoaded. A candidate whose site is not found is left out, and the log
// says why.
void installSkillXpSpike();
}
