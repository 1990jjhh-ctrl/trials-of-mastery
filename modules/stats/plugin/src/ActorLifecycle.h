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

// Registers the sinks that follow race changes (ST-3, Stats plan, assumption
// 11): the game's race switch event, for the player and every known actor,
// and the closing of the race menu, for the player. Call once, at
// kDataLoaded.
void registerRaceChangeSinks();
}
