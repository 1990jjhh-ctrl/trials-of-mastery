#pragma once

// The save format of the stats service (Stats plan, C8): what PRD §8 stores
// per save, as versioned bytes. The game layer (G2) writes them to the SKSE
// co-save and reads them back; nothing here calls the game.
//
// N, the claims, the crit shifts and the XP modifiers are session state and
// are not saved (Stats plan, assumption 16).

#include "tom/stats/core/error.h"
#include "tom/stats/core/stat_registry.h"
#include "tom/stats/core/stats_service.h"

#include <cstdint>
#include <expected>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace tom::stats
{

using Bytes = std::vector<std::uint8_t>;

// The format serialize() writes. The bytes start with it as a 32-bit
// little-endian number; the rest is CBOR. deserialize() reads only this
// version and rejects any other, older or newer, with an error.
inline constexpr std::uint32_t kSaveFormatVersion = 1;

// Maps an actor's saved ID to its ID in the current game, e.g. a FormID
// whose load order index changed.
class ActorIdResolver
{
public:
	virtual ~ActorIdResolver() = default;

	// The current ID, or nullopt to drop the actor.
	[[nodiscard]] virtual std::optional<ActorId> resolve(ActorId saved) = 0;
};

// A saved actor that deserialize() left out, for the log.
struct DroppedActor
{
	ActorId     savedId = 0;
	std::string reason;

	bool operator==(const DroppedActor&) const = default;
};

// What a save holds (PRD §8).
struct SaveState
{
	double       r = 0.0;
	std::int32_t pendingPoints = 0;
	// The registered stats, dormant ones included (Stats plan, assumption 8).
	std::vector<StatId> registeredStats;
	// By the current actor ID.
	std::map<ActorId, ActorSnapshot> actors;
	// For the log.
	std::vector<DroppedActor> droppedActors;
};

// The service's save state in the current format.
[[nodiscard]] Bytes serialize(const StatsService& service);

// Reads bytes that serialize() wrote. Each saved actor ID goes through the
// resolver; an actor it does not resolve, or one whose current ID an earlier
// actor already has, is dropped and listed in droppedActors. Fails for
// another format version and for data that is not well formed, e.g. a
// missing field, a number outside its range, negative points or XP, or
// arrays and objects nested deeper than the format needs. Throws nothing.
[[nodiscard]] std::expected<SaveState, Error> deserialize(std::span<const std::uint8_t> bytes, ActorIdResolver& resolver);

}
