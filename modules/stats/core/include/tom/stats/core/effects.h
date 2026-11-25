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

}
