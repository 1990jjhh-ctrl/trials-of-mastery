#include "tom/stats/core/check.h"

#include "tom/stats/core/curve.h"

namespace tom::stats
{

namespace
{
[[nodiscard]] const Check* findCheck(const ActionsTable& actions, const std::string& action)
{
	const auto it = actions.find(action);
	if (it == actions.end() || !it->second.check) {
		return nullptr;
	}
	return &*it->second.check;
}
}

std::optional<double> checkValue(const ActionsTable& actions, const std::string& action, const ActorStats& actor,
	const SkillLevels& skillLevels)
{
	const Check* check = findCheck(actions, action);
	if (check == nullptr) {
		return std::nullopt;
	}
	double sum = 0.0;
	for (const auto& [stat, curve] : check->stats) {
		sum += evaluate(curve, effective(actor, stat));
	}
	for (const auto& [skill, curve] : check->skills) {
		sum += evaluate(curve, skillLevels(skill));
	}
	return sum;
}

std::optional<double> checkResult(const ActionsTable& actions, const std::string& action, double i)
{
	const Check* check = findCheck(actions, action);
	if (check == nullptr) {
		return std::nullopt;
	}
	return evaluate(check->result, i);
}

}
