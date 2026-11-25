#pragma once

// The effect math of ST-10 to ST-13. The game layer applies the results.

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/curve.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace tom::stats
{

// What an actor-value effect adds to its actor value: curve(x) - curve(0) with
// x = effective - defaultBase (ST-10), so a new character without adjustments
// gets 0. defaultBase is the default base last applied to the stat (Stats
// plan, assumption 12). Follows IEEE arithmetic like evaluate(); the result
// can be infinite or NaN.
[[nodiscard]] double avContribution(const Curve& curve, double effective, double defaultBase);

// The last contribution of one actor to each actor value (ST-10), as stored in
// the save. An actor value without an entry has a contribution of 0.
struct ContributionLedger
{
	std::map<std::string, double> contributions;  // actor value name -> contribution

	bool operator==(const ContributionLedger&) const = default;
};

// The change to add to an actor value's permanent modifier.
struct AvDelta
{
	std::string actorValue;
	double      delta = 0.0;

	bool operator==(const AvDelta&) const = default;
};

// Sums the actor-value effects of every stat in the table per actor value,
// stores the sums in the ledger and returns the difference to the stored ones,
// by actor value name. An actor value whose sum did not change gets no delta;
// one that no effect targets any more gets its stored contribution taken off.
// A sum that is not finite is not applied: its actor value keeps the stored
// contribution and gets no delta.
//
// Every stat in `stats` counts, so the caller (C7) passes the active stats
// only: dormant stats have no effects (Stats plan, assumption 8).
[[nodiscard]] std::vector<AvDelta> updateContributions(ContributionLedger& ledger, const ActorStats& actor,
	const StatsTable& stats);

// Where a hit's damage goes under END's damage redirection (ST-11).
struct RedirectSplit
{
	double fromPool = 0.0;  // taken from Stamina or Magicka
	double toHealth = 0.0;

	bool operator==(const RedirectSplit&) const = default;
};

// Redirects damage * share into the pool; what the pool cannot hold goes to
// Health with the rest. A share outside [0, 1] is clamped to it, a pool that
// is not positive counts as empty, and damage that is not positive is not
// redirected.
[[nodiscard]] RedirectSplit splitDamage(double damage, double share, double poolAvailable);

// The value of a named effect for an actor, summed over all stats in the table
// that list it (ST-9): curve(effective) for an absolute input and, like an
// actor-value effect, avContribution() from the stat's default base for a
// relative one. Follows IEEE arithmetic like evaluate(). As for
// updateContributions, the caller passes the active stats only.
[[nodiscard]] double namedEffectValue(const ActorStats& actor, const StatsTable& stats, NamedEffect effect);

// The share of damage taken that goes into a pool (ST-11), for splitDamage.
[[nodiscard]] double damageRedirectionShare(const ActorStats& actor, const StatsTable& stats);

// The limits of the stat bonus to skill XP (ST-12): -100 % and +200 %.
inline constexpr double kLearningBonusFloor = -1.0;
inline constexpr double kLearningBonusCap = 2.0;

// The stat bonus to skill XP as a fraction (1 is +100 %): the LearningSpeed
// effect, clamped to [kLearningBonusFloor, kLearningBonusCap] (ST-12). A sum
// that is not a number gives 0. Bonuses from outside the module are not
// part of it.
[[nodiscard]] double learningBonus(const ActorStats& actor, const StatsTable& stats);

// The two crit chances of ST-13.
enum class CritKind
{
	Physical,  // NamedEffect::CritChancePhysical
	Magic,     // NamedEffect::CritChanceMagic
};

// The crit effect of that kind, from every stat that lists it (ST-13).
[[nodiscard]] double baseCritChance(const ActorStats& actor, const StatsTable& stats, CritKind kind);

// A shift of the crit chance that another module registers (ST-13), e.g. the
// luck module from LUC (LU-7). It gets the actor, the kind and the current
// base crit chance and returns the amount to add.
using CritShift = std::function<double(const ActorStats& actor, CritKind kind, double baseChance)>;

// The crit shifts, one per owner key (ST-13, ST-26).
class CritShiftRegistry
{
public:
	// Sets or replaces the owner's shift. Returns false, and changes nothing,
	// for an empty owner or an empty function.
	[[nodiscard]] bool set(const std::string& owner, CritShift shift);

	// Does nothing for an owner without a shift.
	void remove(const std::string& owner);

	// The sum of all shifts, each called with the same base chance.
	[[nodiscard]] double sum(const ActorStats& actor, CritKind kind, double baseChance) const;

private:
	std::map<std::string, CritShift, std::less<>> shifts_;
};

// baseCritChance plus the sum of the registered shifts (ST-13).
[[nodiscard]] double critChance(const ActorStats& actor, const StatsTable& stats, const CritShiftRegistry& shifts,
	CritKind kind);

}
