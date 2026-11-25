#include "tom/stats/core/effects.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <variant>

namespace tom::stats
{

namespace
{
using Totals = std::map<std::string, double>;

// The reference of relative effects: the default base last applied to the
// stat, 0 if none was (Stats plan, assumption 12).
[[nodiscard]] double appliedDefaultBase(const ActorStats& actor, const StatId& id)
{
	const auto it = actor.defaultBases.find(id);
	return it == actor.defaultBases.end() ? 0.0 : it->second;
}

void addStatContributions(Totals& totals, const ActorStats& actor, const StatId& id, const StatEntry& entry)
{
	const double value = effective(actor, id);
	const double reference = appliedDefaultBase(actor, id);
	for (const auto& [name, effect] : entry.effects) {
		const auto* target = std::get_if<ActorValueTarget>(&effect.target);
		if (target == nullptr) {
			continue;
		}
		totals[target->name] += avContribution(effect.curve, value, reference);
	}
}

// The stat's entries of one named effect, each at its own input (ST-9).
[[nodiscard]] double statNamedEffectValue(const ActorStats& actor, const StatId& id, const StatEntry& entry,
	NamedEffect named)
{
	const double value = effective(actor, id);
	const double reference = appliedDefaultBase(actor, id);
	double       sum = 0.0;
	for (const auto& [name, effect] : entry.effects) {
		const auto* target = std::get_if<NamedEffect>(&effect.target);
		if (target == nullptr || *target != named) {
			continue;
		}
		sum += effect.input == CurveInput::Absolute ? evaluate(effect.curve, value) : avContribution(effect.curve, value, reference);
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

double avContribution(const Curve& curve, double effective, double defaultBase)
{
	return evaluate(curve, effective - defaultBase) - evaluate(curve, 0.0);
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

double baseCritChance(const ActorStats& actor, const StatsTable& stats, CritKind kind)
{
	const NamedEffect effect = kind == CritKind::Physical ? NamedEffect::CritChancePhysical : NamedEffect::CritChanceMagic;
	return namedEffectValue(actor, stats, effect);
}

bool CritShiftRegistry::set(const std::string& owner, CritShift shift)
{
	if (owner.empty() || !shift) {
		return false;
	}
	shifts_.insert_or_assign(owner, std::move(shift));
	return true;
}

void CritShiftRegistry::remove(const std::string& owner)
{
	shifts_.erase(owner);
}

double CritShiftRegistry::sum(const ActorStats& actor, CritKind kind, double baseChance) const
{
	double total = 0.0;
	for (const auto& [owner, shift] : shifts_) {
		total += shift(actor, kind, baseChance);
	}
	return total;
}

double critChance(const ActorStats& actor, const StatsTable& stats, const CritShiftRegistry& shifts, CritKind kind)
{
	const double base = baseCritChance(actor, stats, kind);
	return base + shifts.sum(actor, kind, base);
}

}
