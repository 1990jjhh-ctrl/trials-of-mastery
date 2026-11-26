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
#include <memory>
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

// What the service holds for one actor, as restoreActor() takes it back
// (for C8).
struct ActorSnapshot
{
	std::string        race;  // the race's editor ID
	ActorStats         stats;
	ContributionLedger ledger;

	bool operator==(const ActorSnapshot&) const = default;
};

// The AvDeltas of each actor whose actor-value contributions changed.
using AvDeltasByActor = std::map<ActorId, std::vector<AvDelta>>;

// The events of ST-26.

// Where a stat point came from.
enum class PointSource
{
	Xp,         // bought with XP (ST-5)
	FreePoint,  // a free level-up point assigned to the stat (ST-20)
};

struct XpGainedEvent
{
	ActorId actor = 0;
	StatId  stat;
	double  xp = 0.0;  // the amount added

	bool operator==(const XpGainedEvent&) const = default;
};

struct PointGainedEvent
{
	ActorId      actor = 0;
	StatId       stat;
	std::int32_t points = 0;
	PointSource  source = PointSource::Xp;

	bool operator==(const PointGainedEvent&) const = default;
};

// The free points of one player level-up (ST-20).
struct LevelUpPointsGrantedEvent
{
	std::int32_t level = 0;    // the level reached
	std::int32_t points = 0;   // granted by this level-up
	std::int32_t pending = 0;  // pending in all, these included

	bool operator==(const LevelUpPointsGrantedEvent&) const = default;
};

struct StatAdvancedEvent
{
	ActorId      actor = 0;
	StatId       stat;
	std::int32_t points = 0;  // the active points moved into accumulated

	bool operator==(const StatAdvancedEvent&) const = default;
};

struct StatAscendedEvent
{
	ActorId      actor = 0;
	StatId       stat;
	std::int32_t points = 0;      // the accumulated and active points converted
	std::int32_t baseGained = 0;  // what the conversion added to the base

	bool operator==(const StatAscendedEvent&) const = default;
};

// Receives the events of ST-26. Each is sent after its change is complete,
// so a receiver may call the service.
class EventSink
{
public:
	virtual ~EventSink() = default;

	virtual void xpGained(const XpGainedEvent& event) = 0;
	virtual void pointGained(const PointGainedEvent& event) = 0;
	virtual void levelUpPointsGranted(const LevelUpPointsGrantedEvent& event) = 0;
	virtual void statAdvanced(const StatAdvancedEvent& event) = 0;
	virtual void statAscended(const StatAscendedEvent& event) = 0;
};

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

	// Sets the receiver of the events, or none for nullptr. The service does
	// not own it.
	void setEventSink(EventSink* sink) { events_ = sink; }

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

	// Applies the stats table's default base to every active stat of every
	// actor (ST-3, Stats plan, assumption 11): a default base that a patch
	// changed since the save moves the base by the difference. For after a
	// load. Returns the AvDeltas of the actors whose contributions changed;
	// at multiplier 1 there are none (Stats plan, assumption 13).
	[[nodiscard]] AvDeltasByActor applyDefaultBases();

	// Forgets what a save holds, for a revert on a new game or a load: the
	// actors, the pending points and the dormant stats; r goes back to the
	// value the service was built with. The session state stays (Stats plan,
	// assumption 16): N, the claims, the active stats, the crit shifts and
	// the event sink.
	void resetSaveState();

	[[nodiscard]] bool hasActor(ActorId actor) const;

	// The known actors in ascending order.
	[[nodiscard]] std::vector<ActorId> actorIds() const;

	// The actor's race, stats and contributions, dormant stats included, for
	// a save. Fails for an unknown actor.
	[[nodiscard]] std::expected<ActorSnapshot, Error> actorSnapshot(ActorId actor) const;

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

	// One real level-up of the player, from the game's level-up flow, which
	// is the only caller (ST-18). Grants the free points of one level, which
	// stay pending until they are assigned (ST-20, ST-21). Unless advancing
	// is claimed, it advances every active stat of the player when newLevel
	// is a positive multiple of N (ST-6). It takes no previous level, so a
	// level change that bypassed the level-up never counts, neither for
	// points nor for advancing.
	void onPlayerLevelUp(std::int32_t newLevel);

	// Claims point assignment for owner (ST-22): the stat box then shows only
	// "OK", and the owner assigns the points. Returns false for an empty
	// owner.
	bool               claimPointAssignment(const std::string& owner);
	[[nodiscard]] bool pointAssignmentClaimed() const { return !pointAssignmentOwners_.empty(); }

	// The free points granted and not yet assigned (ST-21).
	[[nodiscard]] std::int32_t pendingPoints() const { return pendingPoints_; }

	// Puts the saved pending points back (for C8). Fails, and changes
	// nothing, for a negative count.
	[[nodiscard]] std::expected<void, Error> restorePendingPoints(std::int32_t points);

	// Moves one pending point into the player's accumulated points of the
	// stat, so it does not raise the cost (ST-20). Fails, and keeps the point
	// pending, when none is pending, for an unknown player, for a stat that
	// is not active (dormant ones included, Stats plan, assumption 8) and for
	// a stat opted out of free points (ST-2), whoever calls it.
	[[nodiscard]] std::expected<std::vector<AvDelta>, Error> assignFreePoint(const StatId& stat);

	// Sets or replaces, or removes, the owner's crit shift (ST-13, ST-26).
	// setCritShift returns false, and changes nothing, for an empty owner or
	// an empty function. Both may be called from inside a shift.
	bool setCritShift(const std::string& owner, CritShift shift);
	void removeCritShift(const std::string& owner);

	// The crit chance of that kind from the active stats plus the sum of the
	// crit shifts (ST-13). The shifts are the ones set when the call starts:
	// a shift that sets or removes shifts changes only later calls. Fails for
	// an unknown actor.
	[[nodiscard]] std::expected<double, Error> critChance(ActorId actor, CritKind kind) const;

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

	// Advances the stat and returns the event to send.
	[[nodiscard]] static StatAdvancedEvent advanceStat(ActorId actor, ActorState& state, const StatId& stat);

	// Sends the event, if there is a receiver.
	template <typename Event>
	void notify(void (EventSink::*handler)(const Event&), const Event& event) const;

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
	double                        initialR_;  // r_ as the constructor set it
	std::int32_t                  n_;
	std::int32_t                  freePointsPerLevelUp_;
	std::set<std::string>         advancingOwners_;
	std::set<std::string>         pointAssignmentOwners_;
	std::int32_t                  pendingPoints_ = 0;
	EventSink*                    events_ = nullptr;
	// Replaced, never changed, so a running critChance() keeps its own.
	std::shared_ptr<const CritShiftRegistry> critShifts_ = std::make_shared<const CritShiftRegistry>();
};

}
