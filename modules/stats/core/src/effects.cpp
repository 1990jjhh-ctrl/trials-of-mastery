#include "tom/stats/core/effects.h"

#include <algorithm>
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

// The stat's entries of one named effect, each at its own input (ST-9).
[[nodiscard]] double statNamedEffectValue(const ActorStats& actor, const StatId& id, const StatEntry& entry,
	NamedEffect named)
{
	const double value = effective(actor, id);
	const double start = startValue(actor, id);
	double       sum = 0.0;
	for (const auto& [name, effect] : entry.effects) {
		const auto* target = std::get_if<NamedEffect>(&effect.target);
		if (target == nullptr || *target != named) {
			continue;
		}
		const double x = effect.input == CurveInput::Absolute ? value : value - start;
		sum += evaluate(effect.curve, x);
	}
	return sum;
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

RedirectSplit splitDamage(double damage, double share, double poolAvailable)
{
	// The negated comparisons also catch NaN.
	if (!(damage > 0.0) || !(share > 0.0) || !(poolAvailable > 0.0)) {
		return RedirectSplit{ .fromPool = 0.0, .toHealth = damage };
	}
	const double redirected = damage * std::min(share, 1.0);
	const double fromPool = std::min(redirected, poolAvailable);
	return RedirectSplit{ .fromPool = fromPool, .toHealth = damage - fromPool };
}

double namedEffectValue(const ActorStats& actor, const StatsTable& stats, NamedEffect effect)
{
	double sum = 0.0;
	for (const auto& [id, entry] : stats) {
		sum += statNamedEffectValue(actor, id, entry, effect);
	}
	return sum;
}

double damageRedirectionShare(const ActorStats& actor, const StatsTable& stats)
{
	return namedEffectValue(actor, stats, NamedEffect::DamageRedirection);
}

double learningBonus(const ActorStats& actor, const StatsTable& stats)
{
	const double bonus = namedEffectValue(actor, stats, NamedEffect::LearningSpeed);
	if (std::isnan(bonus)) {
		return 0.0;
	}
	return std::clamp(bonus, kLearningBonusFloor, kLearningBonusCap);
}

}
