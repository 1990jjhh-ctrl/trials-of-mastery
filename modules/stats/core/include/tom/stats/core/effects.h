#pragma once

// The effect math of ST-10 to ST-13. The game layer applies the results.

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/curve.h"

#include <map>
#include <string>
#include <vector>

namespace tom::stats
{

// What an actor-value effect adds to its actor value: curve(x) - curve(0) with
// x = effective - start (ST-10), so a new character gets 0. Follows IEEE
// arithmetic like evaluate(); the result can be infinite or NaN.
[[nodiscard]] double avContribution(const Curve& curve, double effective, double start);

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

}
