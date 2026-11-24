#pragma once

namespace tom::stats
{
// Replaces the vanilla level-up menu (docs/stats/PLAN.md, spike S1): where the
// skills menu would open the LevelUp menu, the level-up is finished at once,
// without the +10 Health/Magicka/Stamina reward, and the "Leveling Up"
// tutorial that asks for that choice is not shown. Call once, at kDataLoaded.
// Returns false, and leaves the game unpatched, if a patch site is not found.
[[nodiscard]] bool installLevelUpHook();

// Logs the player's level, perk points, XP and Health/Magicka/Stamina when the
// skills menu or the LevelUp menu opens or closes, and on every LevelIncrease
// event, at debug level. Spike S1 evidence; call once, at kDataLoaded.
void registerLevelUpDiagnostics();
}
