#pragma once

namespace tom::stats
{
// The actors' side of a new game and a load (docs/stats/PLAN.md, G2). The
// co-save part is in Serialization.h.

// Adds the player with its current race at kNewGame.
void onNewGame();

// After a load, at kPostLoadGame: sets each known actor's race again from the
// game and applies the default bases, so a patch that changed a race
// adjustment or a default base, or an actor that changed race, moves the base
// by the difference (ST-3). Adds the player if the save held no stats for it,
// e.g. a save made without the plugin.
void onPostLoadGame();
}
