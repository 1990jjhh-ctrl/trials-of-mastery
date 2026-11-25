#pragma once

// The facade over the stat registry, the actors' stats, the tables and the
// effects (Stats plan, C7). The game layer and the API call only this; it
// calls nothing in the game.

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/config/settings.h"
#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/error.h"
#include "tom/stats/core/stat_registry.h"
#include "tom/stats/core/stat_value.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace tom::stats
{

// An actor as the game layer names it; in game, its FormID.
using ActorId = std::uint32_t;

// The key of the race adjustment (ST-3, Stats plan, assumption 11). The
// module owns it, so the API can neither set nor remove it.
inline constexpr std::string_view kRaceAdjustmentKey = "TrialsOfMasteryStats:Race";

// What the API reads of one stat of one actor (ST-26).
struct StatReading
{
	std::int32_t base = 0;  // with the base adjustments (Stats plan, assumption 11)
	std::int32_t accumulated = 0;
	std::int32_t active = 0;
	std::int32_t modifier = 0;  // the sum over all keys, saturated at the int32 range
	double       xp = 0.0;
	double       effective = 0.0;

	bool operator==(const StatReading&) const = default;
};

// The AvDeltas of each actor whose actor-value contributions changed.
using AvDeltasByActor = std::map<ActorId, std::vector<AvDelta>>;

class StatsService
{
public:
	// Takes r, N and the free points per level-up from the settings. A value
	// that the INI would reject (an r of 1 or less, an N below 1, negative
	// free points; ST-6, ST-20) is replaced by its built-in default, the one
	// Settings starts with. tables holds an entry for every core stat, as
	// buildTables() ensures; a core stat without one has a default base of 0.
	StatsService(const Settings& settings, Tables tables, ActorId player);

	[[nodiscard]] const StatRegistry& registry() const { return registry_; }
	[[nodiscard]] ActorId             player() const { return player_; }

	// r and N of ST-6 and the free points per level-up of ST-20.
	[[nodiscard]] double       r() const { return r_; }
	[[nodiscard]] std::int32_t n() const { return n_; }
	[[nodiscard]] std::int32_t freePointsPerLevelUp() const { return freePointsPerLevelUp_; }

	// Registers a stat, or brings a dormant one back (ST-2, Stats plan,
	// assumption 8). Every known actor gets the adjustment its race has for
	// the stat under kRaceAdjustmentKey, or loses the one it had (ST-3);
	// one without a default base applied to the stat then gets initStat()
	// with the default base of its stats table entry. Fails
	// when the stats table has no entry for it, since the default base comes
	// from there (ST-3), or when StatRegistry::registerStat() refuses it.
	// Returns the AvDeltas of the actors whose contributions changed.
	[[nodiscard]] std::expected<AvDeltasByActor, Error> registerStat(const StatId& id, FreePoints freePoints = FreePoints::Granted);

	// Adds a stat from saved data as dormant (Stats plan, assumption 8): its
	// values are kept, but it has no effects and cannot be read or changed
	// until registerStat() brings it back. Does nothing for a known stat.
	void addDormantStat(const StatId& id);

	// Adds an actor of the race with that editor ID (ST-4) and stores the
	// race. Every active stat gets the race's adjustment from the stats table
	// under kRaceAdjustmentKey, if it has one, then initStat() (ST-3). Returns
	// the AvDeltas the adjustments cause from creation on (ST-10). Fails for
	// a known actor.
	[[nodiscard]] std::expected<std::vector<AvDelta>, Error> addActor(ActorId actor, std::string_view race);

	// Puts an actor's saved stats and contributions back, replacing any the
	// service holds for it (for C8), and stores its race. Active stats
	// without a default base applied get the race's adjustment and
	// initStat() as in addActor(), so a stat registered since the save starts
	// at its default base plus that adjustment. The saved stats keep their
	// race adjustments; setRace() renews them. Returns the AvDeltas to the
	// saved contributions, e.g. those of a stat that is dormant now.
	[[nodiscard]] std::vector<AvDelta> restoreActor(ActorId actor, std::string_view race, ActorStats stats,
		ContributionLedger ledger);

	// Stores the actor's race and sets the race's adjustment from the stats
	// table under kRaceAdjustmentKey on every active stat, or removes the
	// adjustment where the race has none (ST-3, Stats plan, assumption 11).
	// The base and the effects move by the difference; the AvDeltas are
	// returned. For a race change, and after a load when a patch changed the
	// adjustments. Fails for an unknown actor.
	[[nodiscard]] std::expected<std::vector<AvDelta>, Error> setRace(ActorId actor, std::string_view race);

	[[nodiscard]] bool hasActor(ActorId actor) const;

	// Fails for an unknown actor and for a stat that is not active.
	[[nodiscard]] std::expected<StatReading, Error> read(ActorId actor, const StatId& stat) const;

	// The named effects of ST-11 and ST-12 from the active stats. Fail for an
	// unknown actor.
	[[nodiscard]] std::expected<double, Error> learningBonus(ActorId actor) const;
	[[nodiscard]] std::expected<double, Error> damageRedirectionShare(ActorId actor) const;

	// The changes below fail, and change nothing, for an unknown actor and
	// for a stat that is not active. On success they return the AvDeltas of
	// the change (ST-10).

	// Adds XP to a stat of the player and buys the points it pays for at r
	// (ST-5). Only the player trains stats (ST-4), so it fails for any other
	// actor, and for an amount that is not positive and finite.
	[[nodiscard]] std::expected<std::vector<AvDelta>, Error> addXp(ActorId actor, const StatId& stat, double xp);

	// Sets a stat's values, e.g. for an NPC (ST-26). The base is the one
	// read() returns, so it includes the base adjustments; the adjustments
	// and the default base last applied stay as they are. Fails for negative
	// points and for XP that is negative or not finite.
	[[nodiscard]] std::expected<std::vector<AvDelta>, Error> setValues(ActorId actor, const StatId& stat,
		const StatValue& value);

	// Sets or removes the modifier under key (ST-8). Fails for an empty key;
	// removing a key that is not set changes nothing.
	[[nodiscard]] std::expected<std::vector<AvDelta>, Error> setModifier(ActorId actor, const StatId& stat,
		const ModifierKey& key, std::int32_t amount);
	[[nodiscard]] std::expected<std::vector<AvDelta>, Error> removeModifier(ActorId actor, const StatId& stat,
		const ModifierKey& key);

	// Sets the actor's multiplier (ST-8). Fails for a value that is not
	// finite.
	[[nodiscard]] std::expected<std::vector<AvDelta>, Error> setMultiplier(ActorId actor, double multiplier);

	// Sets or removes the base adjustment under key, which moves the base by
	// the difference (ST-3). Fails for an empty key and for
	// kRaceAdjustmentKey; removing a key that is not set changes nothing.
	[[nodiscard]] std::expected<std::vector<AvDelta>, Error> setBaseAdjustment(ActorId actor, const StatId& stat,
		const BaseAdjustmentKey& key, std::int32_t amount);
	[[nodiscard]] std::expected<std::vector<AvDelta>, Error> removeBaseAdjustment(ActorId actor, const StatId& stat,
		const BaseAdjustmentKey& key);

	// Sets r (ST-6). Fails, and keeps the previous value, for a value that is
	// not finite or not greater than 1; the error is for the log.
	[[nodiscard]] std::expected<void, Error> setR(double r);

	// Sets N, the level interval of auto-advancing (ST-6). Fails, and keeps
	// the previous value, for a value below 1.
	[[nodiscard]] std::expected<void, Error> setN(std::int32_t n);

	// Claims advancing for owner (ST-6): from then on the service no longer
	// advances on its own at level-ups. Returns false for an empty owner.
	bool               claimAdvancing(const std::string& owner);
	[[nodiscard]] bool advancingClaimed() const { return !advancingOwners_.empty(); }

	// Advances one stat or every active stat of the actor (ST-6); dormant
	// stats keep their points. Advancing leaves the effective stat as it is,
	// so there are no AvDeltas.
	[[nodiscard]] std::expected<void, Error> advance(ActorId actor, const StatId& stat);
	[[nodiscard]] std::expected<void, Error> advanceAll(ActorId actor);

	// Ascends a stat with the caller's conversion formula (ST-6). Fails, and
	// changes nothing, when the formula's result is not finite.
	[[nodiscard]] std::expected<std::vector<AvDelta>, Error> ascend(ActorId actor, const StatId& stat,
		const std::function<double(std::int32_t)>& convert);

private:
	struct ActorState
	{
		ActorStats         stats;
		ContributionLedger ledger;
		std::string        race;  // the race's editor ID
	};

	[[nodiscard]] std::int32_t defaultBase(const StatId& stat) const;

	// Sets the adjustment the actor's race has for the stat under
	// kRaceAdjustmentKey, or removes it when the race has none.
	void applyRace(ActorState& state, const StatId& stat) const;

	// applyRace() and initStat() for every active stat the actor has no
	// default base for.
	void initMissingStats(ActorState& state) const;

	// The stats table entries of the active stats, which alone have effects
	// (Stats plan, assumption 8).
	void rebuildActiveTable();

	// Stores the actor's contributions from the active stats and returns the
	// change (ST-10).
	[[nodiscard]] std::vector<AvDelta> updateEffects(ActorState& state) const;

	[[nodiscard]] std::expected<const ActorState*, Error> findActor(ActorId actor) const;
	[[nodiscard]] std::expected<ActorState*, Error>       findActor(ActorId actor);
	[[nodiscard]] std::expected<void, Error>              checkActive(const StatId& stat) const;

	// Fails for an empty key and for the module's race key.
	[[nodiscard]] static std::expected<void, Error> checkAdjustmentKey(const BaseAdjustmentKey& key);

	// The actor, if it is known and the stat is active.
	[[nodiscard]] std::expected<ActorState*, Error> findStat(ActorId actor, const StatId& stat);

	Tables                        tables_;
	StatsTable                    activeTable_;
	StatRegistry                  registry_;
	std::map<ActorId, ActorState> actors_;
	ActorId                       player_;
	double                        r_;
	std::int32_t                  n_;
	std::int32_t                  freePointsPerLevelUp_;
	std::set<std::string>         advancingOwners_;
};

}
