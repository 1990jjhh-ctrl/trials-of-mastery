#pragma once

namespace tom::stats
{
// Stores the stats service's save state in the SKSE co-save and puts it back
// at a load (docs/stats/PLAN.md, G2, C8); a revert, at a new game or before a
// load, forgets it. Call once, in SKSEPluginLoad.
void registerSerialization();
}
