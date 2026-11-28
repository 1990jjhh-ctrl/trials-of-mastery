#pragma once

// The steps of an actor's life in a game that the game layer (Stats plan, G2)
// takes at a new game, after a load and on a race change. The game layer
// reads the races from the game first and passes them in; nothing here calls
// the game.

#include "tom/stats/core/effects.h"
#include "tom/stats/core/stats_service.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace tom::stats
{

// Each actor's current race in the game, by the race's editor ID.
using ActorRaces = std::map<ActorId, std::string>;

// Adds an unknown actor of the race, or sets the race of a known one (ST-3,
// ST-4, Stats plan, assumption 11). An actor added here gets every active
// stat at its default base plus the race's adjustment, the same for the
// player and for an NPC.
//
// TODO(OQ-1): what values an NPC has with only the stats module installed is
// open; until the API sets them, an NPC gets the default base plus its race
// adjustment like the player.
[[nodiscard]] std::vector<AvDelta> ensureActor(StatsService& service, ActorId actor, std::string_view race);

// The rest of a load, after restore() (Stats plan, G2): ensureActor() for
// each actor in races with its current race, so an actor the save does not
// hold is added (the player of a save made without the plugin) and a changed
// race or race adjustment moves the base by the difference; then
// applyDefaultBases(), so a changed default base does the same (ST-3). A
// known actor that races leaves out keeps the race it has. Returns the
// AvDeltas of both steps per actor, in the order they apply.
[[nodiscard]] AvDeltasByActor renewAfterLoad(StatsService& service, const ActorRaces& races);

}
