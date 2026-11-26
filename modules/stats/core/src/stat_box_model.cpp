#include "tom/stats/core/stat_box_model.h"

#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/stat_box_format.h"
#include "tom/stats/core/stat_value.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <expected>
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

// The stat box shows whole numbers (ST-24; Stats plan, assumption 17).
[[nodiscard]] std::string valueText(double value)
{
	return formatStatNumber(value, Rounding::Down);
}

// The cost rounded up, but not by more than addXp's tolerance, so the XP
// never shows as having reached a cost it has not paid, and a cost it pays
// shows as the whole number it is.
[[nodiscard]] std::string costText(double cost)
{
	return formatStatNumber(cost * (1.0 - kCostTolerance), Rounding::Up);
}

// The name without its namespace ("LUC" for "Luck:LUC").
[[nodiscard]] std::string_view withoutNamespace(std::string_view name)
{
	const std::string_view space = namespaceOf(name);
	return space.empty() ? name : name.substr(space.size() + 1);
}

void appendOnce(std::vector<std::string>& groups, const std::string& group)
{
	if (std::ranges::find(groups, group) == groups.end()) {
		groups.push_back(group);
	}
}

// Calls the provider of the group's section. It gets a copy, so a provider
// that removes itself from the registry does no harm.
[[nodiscard]] std::expected<std::vector<std::string>, Error> provideLines(SectionProvider provider, std::string_view group)
{
	try {
		return provider();
	} catch (const std::exception& error) {
		return std::unexpected(Error{ std::format("the stat box section provider of {} failed: {}", group, error.what()) });
	} catch (...) {
		return std::unexpected(Error{ std::format("the stat box section provider of {} failed", group) });
	}
}

[[nodiscard]] std::string freePointsText(std::int32_t points)
{
	return std::format("Free points: {}", points);
}
}

StatBoxModel::StatBoxModel(StatsService& service, const Tables& tables, const StatBoxSections& sections,
	StatBoxLayout layout) :
	service_(service),
	tables_(tables),
	layout_(layout)
{
	const std::vector<StatId>      stats = service_.registry().activeStats();
	const std::vector<std::string> groups = groupsOf(stats);
	for (const std::string& group : groups) {
		const SectionProvider* provider = sections.find(group);
		if (provider == nullptr) {
			continue;
		}
		auto lines = provideLines(*provider, group);
		if (!lines) {
			errors_.push_back(std::move(lines.error()));
			continue;
		}
		providedLines_.emplace(group, std::move(*lines));
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
	View result;
	result.page.text = sectionsText();
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
	if (stats.size() + 1 > layout_.maxButtons) {
		return groupsView(text, stats);
	}
	return statsButtons(text, std::move(stats), Open{ .screen = Screen::Stats, .group = {} });
}

StatBoxModel::View StatBoxModel::groupsView(std::string text, const std::vector<StatId>& stats) const
{
	View result;
	result.page.text = std::move(text);
	result.page.buttons.push_back(Button{ std::string(kBack) });
	result.actions.emplace_back(Open{ .screen = Screen::Stats, .group = {} });
	for (std::string& group : groupsOf(stats)) {
		result.page.buttons.push_back(Button{ group });
		result.actions.emplace_back(Open{ .screen = Screen::Group, .group = std::move(group) });
	}
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
	result.page.buttons.push_back(Button{ std::string(kBack) });
	result.actions.emplace_back(std::move(back));
	for (StatId& stat : stats) {
		result.page.buttons.push_back(Button{ statButton(stat) });
		result.actions.emplace_back(Assign{ std::move(stat) });
	}
	return result;
}

std::vector<StatId> StatBoxModel::assignableStats() const
{
	std::vector<StatId> result = service_.registry().activeStats();
	std::erase_if(result, [this](const StatId& stat) { return !service_.registry().getsFreePoints(stat); });
	return result;
}

std::string StatBoxModel::sectionsText() const
{
	const std::vector<StatId> stats = service_.registry().activeStats();
	std::string               text;
	for (const std::string& group : groupsOf(stats)) {
		if (!text.empty()) {
			text += "\n\n";
		}
		text += group;
		for (const std::string& line : sectionLines(group, stats)) {
			text += '\n';
			text += line;
		}
	}
	return text;
}

std::vector<std::string> StatBoxModel::sectionLines(const std::string& group, const std::vector<StatId>& stats) const
{
	if (const auto provided = providedLines_.find(group); provided != providedLines_.end()) {
		return provided->second;
	}
	std::vector<std::string> lines;
	const double             r = service_.r();
	for (const StatId& stat : stats) {
		const auto reading = service_.read(service_.player(), stat);
		if (!reading || groupOf(stat) != group) {
			continue;
		}
		lines.push_back(std::format("{} {} ({} / {} XP)", label(stat), valueText(reading->effective),
			valueText(reading->xp), costText(cost(r, reading->active))));
	}
	return lines;
}

std::string StatBoxModel::label(const StatId& stat) const
{
	std::string fallback(withoutNamespace(stat.name));
	const auto  it = tables_.stats.find(stat);
	if (it == tables_.stats.end()) {
		return fallback;
	}
	const StatEntry& entry = it->second;
	std::string      shortName = entry.shortName.value_or(std::move(fallback));
	if (!entry.displayName) {
		return shortName;
	}
	return std::format("{} ({})", *entry.displayName, shortName);
}

std::string StatBoxModel::statButton(const StatId& stat) const
{
	const auto reading = service_.read(service_.player(), stat);
	if (!reading) {
		return label(stat);
	}
	return std::format("{} {}", label(stat), valueText(reading->effective));
}

std::string StatBoxModel::groupOf(const StatId& stat) const
{
	const auto it = tables_.stats.find(stat);
	if (it != tables_.stats.end()) {
		return it->second.group;
	}
	const std::string_view space = namespaceOf(stat.name);
	return std::string(space.empty() ? kOtherGroup : space);
}

std::vector<std::string> StatBoxModel::groupsOf(const std::vector<StatId>& stats) const
{
	std::vector<std::string> present;
	for (const StatId& stat : stats) {
		appendOnce(present, groupOf(stat));
	}
	// The stats files' groups, then the others by their first active stat, so
	// the namespace groups follow their registration (ST-21).
	std::vector<std::string> order = tables_.statGroups;
	for (const StatId& stat : service_.registry().activeStats()) {
		appendOnce(order, groupOf(stat));
	}
	std::vector<std::string> result;
	for (std::string& group : order) {
		if (group != kOtherGroup && std::ranges::find(present, group) != present.end()) {
			result.push_back(std::move(group));
		}
	}
	// Other gathers what fits nowhere else, so it comes last.
	if (std::ranges::find(present, kOtherGroup) != present.end()) {
		result.emplace_back(kOtherGroup);
	}
	return result;
}

}
