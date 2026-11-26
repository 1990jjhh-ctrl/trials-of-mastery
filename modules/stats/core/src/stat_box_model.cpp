#include "tom/stats/core/stat_box_model.h"

#include "tom/stats/core/stat_value.h"

#include <cmath>
#include <cstdint>
#include <format>
#include <string_view>
#include <utility>
#include <vector>

namespace tom::stats
{

namespace
{
// The button texts that ST-21 and ST-24 name.
constexpr std::string_view kOk = "OK";
constexpr std::string_view kLevelUp = "Level Up";
constexpr std::string_view kCancel = "Cancel";
constexpr std::string_view kBack = "Back";

// A whole number without decimals, any other with one (ST-7: the effective
// stat and the XP are floats).
[[nodiscard]] std::string formatNumber(double value)
{
	if (std::isfinite(value) && value == std::trunc(value)) {
		return std::format("{:.0f}", value);
	}
	return std::format("{:.1f}", value);
}

[[nodiscard]] std::string freePointsText(std::int32_t points)
{
	return std::format("Free points: {}", points);
}
}

StatBoxModel::StatBoxModel(StatsService& service) :
	service_(service)
{}

Page StatBoxModel::page() const
{
	return view().page;
}

PressResult StatBoxModel::press(std::size_t button)
{
	View current = view();
	if (button >= current.actions.size()) {
		return PressResult{ .next = std::move(current.page), .deltas = {}, .error = Error{ std::format("the page has no button {}", button) } };
	}
	const Action action = std::move(current.actions[button]);
	if (std::holds_alternative<Close>(action)) {
		screen_ = Screen::Stats;
		return PressResult{ .next = Closed{}, .deltas = {}, .error = std::nullopt };
	}
	if (const auto* open = std::get_if<Open>(&action)) {
		screen_ = open->screen;
		return PressResult{ .next = page(), .deltas = {}, .error = std::nullopt };
	}
	auto deltas = service_.assignFreePoint(std::get<Assign>(action).stat);
	if (!deltas) {
		return PressResult{ .next = page(), .deltas = {}, .error = std::move(deltas.error()) };
	}
	return PressResult{ .next = page(), .deltas = std::move(*deltas), .error = std::nullopt };
}

bool StatBoxModel::canAssign() const
{
	return service_.pendingPoints() > 0 && !service_.pointAssignmentClaimed();
}

StatBoxModel::Screen StatBoxModel::screen() const
{
	return canAssign() ? screen_ : Screen::Stats;
}

StatBoxModel::View StatBoxModel::view() const
{
	switch (screen()) {
	case Screen::LevelUp:
		return levelUpView();
	case Screen::Stats:
		break;
	}
	return statsView();
}

StatBoxModel::View StatBoxModel::statsView() const
{
	View         result;
	const double r = service_.r();
	for (const StatId& stat : service_.registry().activeStats()) {
		const auto reading = service_.read(service_.player(), stat);
		if (!reading) {
			continue;
		}
		if (!result.page.text.empty()) {
			result.page.text += '\n';
		}
		result.page.text += std::format("{} {} (XP {} / {})", stat.name, formatNumber(reading->effective),
			formatNumber(reading->xp), formatNumber(cost(r, reading->active)));
	}
	if (!canAssign()) {
		result.page.buttons.push_back(Button{ std::string(kOk) });
		result.actions.emplace_back(Close{});
		return result;
	}
	result.page.buttons.push_back(Button{ std::string(kLevelUp) });
	result.actions.emplace_back(Open{ Screen::LevelUp });
	result.page.buttons.push_back(Button{ std::string(kCancel) });
	result.actions.emplace_back(Close{});
	return result;
}

StatBoxModel::View StatBoxModel::levelUpView() const
{
	View result;
	result.page.text = freePointsText(service_.pendingPoints());
	for (StatId& stat : assignableStats()) {
		result.page.buttons.push_back(Button{ statLabel(stat) });
		result.actions.emplace_back(Assign{ std::move(stat) });
	}
	result.page.buttons.push_back(Button{ std::string(kBack) });
	result.actions.emplace_back(Open{ Screen::Stats });
	return result;
}

std::vector<StatId> StatBoxModel::assignableStats() const
{
	std::vector<StatId> result = service_.registry().activeStats();
	std::erase_if(result, [this](const StatId& stat) { return !service_.registry().getsFreePoints(stat); });
	return result;
}

std::string StatBoxModel::statLabel(const StatId& stat) const
{
	const auto reading = service_.read(service_.player(), stat);
	if (!reading) {
		return stat.name;
	}
	return std::format("{} {}", stat.name, formatNumber(reading->effective));
}

}
