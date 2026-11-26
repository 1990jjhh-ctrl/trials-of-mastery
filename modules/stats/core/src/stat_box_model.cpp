#include "tom/stats/core/stat_box_model.h"

#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/stat_value.h"

#include <algorithm>
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

StatBoxModel::StatBoxModel(StatsService& service, const StatsTable& stats, std::size_t maxButtons) :
	service_(service),
	maxButtons_(maxButtons)
{
	for (const auto& [stat, entry] : stats) {
		groups_.emplace(stat, entry.group);
	}
}

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
		group_ = open->group;
		return PressResult{ .next = page(), .deltas = {}, .error = std::nullopt };
	}
	auto deltas = service_.assignFreePoint(std::get<Assign>(action).stat);
	if (!deltas) {
		return PressResult{ .next = page(), .deltas = {}, .error = std::move(deltas.error()) };
	}
	if (!canAssign()) {
		// After the last point the box starts over on its first page (ST-21),
		// also when points become pending again while it is open.
		screen_ = Screen::Stats;
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
	case Screen::Group:
		return groupView();
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
	result.actions.emplace_back(Open{ .screen = Screen::LevelUp, .group = {} });
	result.page.buttons.push_back(Button{ std::string(kCancel) });
	result.actions.emplace_back(Close{});
	return result;
}

StatBoxModel::View StatBoxModel::levelUpView() const
{
	std::vector<StatId> stats = assignableStats();
	const std::string   text = freePointsText(service_.pendingPoints());
	if (stats.size() + 1 > maxButtons_) {
		return groupsView(text, stats);
	}
	return statsButtons(text, std::move(stats), Open{ .screen = Screen::Stats, .group = {} });
}

StatBoxModel::View StatBoxModel::groupsView(std::string text, const std::vector<StatId>& stats) const
{
	View result;
	result.page.text = std::move(text);
	for (std::string& group : groupsOf(stats)) {
		result.page.buttons.push_back(Button{ group });
		result.actions.emplace_back(Open{ .screen = Screen::Group, .group = std::move(group) });
	}
	result.page.buttons.push_back(Button{ std::string(kBack) });
	result.actions.emplace_back(Open{ .screen = Screen::Stats, .group = {} });
	return result;
}

StatBoxModel::View StatBoxModel::groupView() const
{
	std::vector<StatId> stats = assignableStats();
	std::erase_if(stats, [this](const StatId& stat) { return groupOf(stat) != group_; });
	return statsButtons(std::format("{}\n{}", group_, freePointsText(service_.pendingPoints())), std::move(stats),
		Open{ .screen = Screen::LevelUp, .group = {} });
}

StatBoxModel::View StatBoxModel::statsButtons(std::string text, std::vector<StatId> stats, Open back) const
{
	View result;
	result.page.text = std::move(text);
	for (StatId& stat : stats) {
		result.page.buttons.push_back(Button{ statLabel(stat) });
		result.actions.emplace_back(Assign{ std::move(stat) });
	}
	result.page.buttons.push_back(Button{ std::string(kBack) });
	result.actions.emplace_back(std::move(back));
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

const std::string& StatBoxModel::groupOf(const StatId& stat) const
{
	static const std::string other(kOtherGroup);
	const auto               it = groups_.find(stat);
	return it == groups_.end() ? other : it->second;
}

std::vector<std::string> StatBoxModel::groupsOf(const std::vector<StatId>& stats) const
{
	std::vector<std::string> result;
	for (const StatId& stat : stats) {
		const std::string& group = groupOf(stat);
		if (std::ranges::find(result, group) == result.end()) {
			result.push_back(group);
		}
	}
	// Other gathers what fits nowhere else, so it comes last.
	std::ranges::stable_partition(result, [](const std::string& group) { return group != kOtherGroup; });
	return result;
}

}
