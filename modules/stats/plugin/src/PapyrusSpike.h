#pragma once

namespace tom::stats
{
// Spike S8 (docs/stats/PLAN.md), throwaway: binds the natives of the
// TrialsOfMasteryStatsSpike script (modules/stats/papyrus), which log when
// they are called. Call once, in SKSEPluginLoad after SKSE::Init.
[[nodiscard]] bool registerPapyrusSpike();

// Calls the script's SpikeAnswer and SpikeLog through the Papyrus VM and logs
// the result, which shows that the .pex loaded and the natives are bound.
// Call at kPostLoadGame.
void callPapyrusSpike();
}
