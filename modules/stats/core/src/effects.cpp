#include "tom/stats/core/effects.h"

#include <cmath>
#include <variant>

namespace tom::stats
{

namespace
{
using Totals = std::map<std::string, double>;

[[nodiscard]] double startValue(const ActorStats& actor, const StatId& id)
{
	const auto it = actor.startValues.find(id);
	return it == actor.startValues.end() ? 0.0 : it->second;
}

void addStatContributions(Totals& totals, const ActorStats& actor, const StatId& id, const StatEntry& entry)
{
	const double value = effective(actor, id);
	const double start = startValue(actor, id);
	for (const auto& [name, effect] : entry.effects) {
		const auto* target = std::get_if<ActorValueTarget>(&effect.target);
		if (target == nullptr) {
			continue;
		}
		totals[target->name] += avContribution(effect.curve, value, start);
	}
}

// Sets the ledger entry to total and returns the change, keeping the ledger
// free of zero entries.
[[nodiscard]] double store(ContributionLedger& ledger, const std::string& actorValue, double total)
{
	double&      stored = ledger.contributions[actorValue];
	const double delta = total - stored;
	stored = total;
	if (total == 0.0) {
		ledger.contributions.erase(actorValue);
	}
	return delta;
}
}

double avContribution(const Curve& curve, double effective, double start)
{
	return evaluate(curve, effective - start) - evaluate(curve, 0.0);
}

std::vector<AvDelta> updateContributions(ContributionLedger& ledger, const ActorStats& actor, const StatsTable& stats)
{
	Totals totals;
	for (const auto& [id, entry] : stats) {
		addStatContributions(totals, actor, id, entry);
	}
	// Actor values that no effect targets any more go back to 0.
	for (const auto& [actorValue, contribution] : ledger.contributions) {
		totals.try_emplace(actorValue, 0.0);
	}

	std::vector<AvDelta> deltas;
	for (const auto& [actorValue, total] : totals) {
		if (!std::isfinite(total)) {
			continue;
		}
		const double delta = store(ledger, actorValue, total);
		if (delta != 0.0) {
			deltas.push_back(AvDelta{ .actorValue = actorValue, .delta = delta });
		}
	}
	return deltas;
}

}
