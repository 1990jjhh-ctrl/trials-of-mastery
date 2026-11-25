#include "tom/stats/core/training.h"

#include "tom/stats/core/curve.h"

namespace tom::stats
{

std::vector<StatXp> skillLevelUpXp(const SkillsTable& skills, const XpModifierRegistry& modifiers,
	const std::string& skill, std::int32_t levelReached)
{
	const auto it = skills.find(skill);
	if (it == skills.end()) {
		return {};
	}
	const SkillEntry& entry = it->second;
	const double      xp = static_cast<double>(levelReached) * modifiers.factor(XpModifierKind::SkillLevelUpStatXp, skill);
	if (entry.primary == entry.secondary) {
		return { StatXp{ .stat = entry.primary, .xp = xp } };
	}
	return {
		StatXp{ .stat = entry.primary, .xp = xp * entry.primaryShare },
		StatXp{ .stat = entry.secondary, .xp = xp * (1.0 - entry.primaryShare) },
	};
}

std::vector<StatXp> actionTrainingXp(const ActionsTable& actions, const XpModifierRegistry& modifiers,
	const std::string& action, double input)
{
	const auto it = actions.find(action);
	if (it == actions.end()) {
		return {};
	}
	const double        factor = modifiers.factor(XpModifierKind::ActionStatXp, action);
	std::vector<StatXp> result;
	result.reserve(it->second.training.size());
	for (const auto& [stat, curve] : it->second.training) {
		result.push_back(StatXp{ .stat = stat, .xp = evaluate(curve, input) * factor });
	}
	return result;
}

}
