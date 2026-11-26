#include "tom/stats/core/stat_box_model.h"

#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/stat_box_format.h"
#include "tom/stats/core/stat_value.h"

#include <algorithm>
#include <cstddef>
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
constexpr std::string_view kNext = "Next";

// A group name and one line, so a page shows at least one line of a section.
constexpr std::size_t kMinLinesPerPage = 2;

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

// The number of parts of at most perPart items each that n items need.
[[nodiscard]] std::size_t partCount(std::size_t n, std::size_t perPart)
{
	return (n + perPart - 1) / perPart;
}

// The items of one part when n items are spread evenly over parts parts: the
// first n mod parts of them get one item more (Stats plan, assumption 17).
struct Part
{
	std::size_t first = 0;
	std::size_t size = 0;
};

[[nodiscard]] Part part(std::size_t n, std::size_t parts, std::size_t index)
{
	const std::size_t size = n / parts;
	const std::size_t longer = n % parts;
	return Part{ .first = index * size + std::min(index, longer), .size = size + (index < longer ? 1U : 0U) };
}

// "<title> (<i> / <k>)" for the part with that index of count parts.
[[nodiscard]] std::string partTitle(std::string_view title, std::size_t index, std::size_t count)
{
	return std::format("{} ({} / {})", title, index + 1, count);
}

// Fills the overview pages with the sections in order (ST-24; Stats plan,
// assumption 17). Every line counts toward the lines per page, the group
// names and the empty lines between sections included.
class OverviewPages
{
public:
	explicit OverviewPages(std::size_t linesPerPage) :
		linesPerPage_(linesPerPage)
	{}

	// Adds the section to the current page, or starts a new page when it does
	// not fit there. A section longer than a page gets pages of its own.
	void add(std::string_view group, const std::vector<std::string>& lines)
	{
		if (lines.size() + 1 > linesPerPage_) {
			endPage();
			split(group, lines);
			return;
		}
		if (!current_.empty() && current_.size() + 1 + lines.size() + 1 > linesPerPage_) {
			endPage();
		}
		if (!current_.empty()) {
			current_.emplace_back();
		}
		current_.emplace_back(group);
		current_.insert(current_.end(), lines.begin(), lines.end());
	}

	// The text of each page, once the last section is added; one empty page
	// without sections.
	[[nodiscard]] std::vector<std::string> texts()
	{
		endPage();
		if (texts_.empty()) {
			texts_.emplace_back();
		}
		return std::move(texts_);
	}

private:
	// The section's lines spread evenly over as few pages as hold them under
	// the group name and its part.
	void split(std::string_view group, const std::vector<std::string>& lines)
	{
		const std::size_t count = partCount(lines.size(), linesPerPage_ - 1);
		for (std::size_t index = 0; index < count; ++index) {
			const Part shown = part(lines.size(), count, index);
			current_.push_back(partTitle(group, index, count));
			const auto first = lines.begin() + static_cast<std::ptrdiff_t>(shown.first);
			current_.insert(current_.end(), first, first + static_cast<std::ptrdiff_t>(shown.size));
			endPage();
		}
	}

	void endPage()
	{
		if (current_.empty()) {
			return;
		}
		std::string text;
		for (const std::string& line : current_) {
			text += line;
			text += '\n';
		}
		text.pop_back();
		texts_.push_back(std::move(text));
		current_.clear();
	}

	std::size_t              linesPerPage_;
	std::vector<std::string> current_;  // the lines of the page being filled
	std::vector<std::string> texts_;
};
}

StatBoxModel::StatBoxModel(StatsService& service, const Tables& tables, const StatBoxSections& sections,
	StatBoxLayout layout) :
	service_(service),
	tables_(tables),
	layout_{ .maxButtons = layout.maxButtons, .linesPerPage = std::max(layout.linesPerPage, kMinLinesPerPage) }
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
		startOver();
		return PressResult{ .next = Closed{}, .deltas = {}, .error = std::nullopt };
	}
	if (const auto* open = std::get_if<Open>(&action)) {
		screen_ = open->screen;
		group_ = open->group;
		if (open->screen == Screen::Stats) {
			overviewPage_ = open->page;
		}
		return PressResult{ .next = page(), .deltas = {}, .error = std::nullopt };
	}
	auto deltas = service_.assignFreePoint(std::get<Assign>(action).stat);
	if (!deltas) {
		return PressResult{ .next = page(), .deltas = {}, .error = std::move(deltas.error()) };
	}
	if (!canAssign()) {
		// After the last point the box starts over on its first page (ST-21),
		// also when points become pending again while it is open.
		startOver();
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
	std::vector<std::string> texts = overviewTexts();
	const std::size_t        index = std::min(overviewPage_, texts.size() - 1);
	View                     result;
	result.page.text = std::move(texts[index]);
	if (index > 0) {
		result.add(kBack, Open{ .screen = Screen::Stats, .group = {}, .page = index - 1 });
	}
	if (canAssign()) {
		result.add(kLevelUp, Open{ .screen = Screen::LevelUp, .group = {}, .page = 0 });
		result.add(kCancel, Close{});
	} else {
		result.add(kOk, Close{});
	}
	if (index + 1 < texts.size()) {
		result.add(kNext, Open{ .screen = Screen::Stats, .group = {}, .page = index + 1 });
	}
	return result;
}

StatBoxModel::View StatBoxModel::levelUpView() const
{
	std::vector<StatId> stats = assignableStats();
	const std::string   text = freePointsText(service_.pendingPoints());
	if (stats.size() + 1 > layout_.maxButtons) {
		return groupsView(text, stats);
	}
	return statsButtons(text, std::move(stats), Open{ .screen = Screen::Stats, .group = {}, .page = overviewPage_ });
}

StatBoxModel::View StatBoxModel::groupsView(std::string text, const std::vector<StatId>& stats) const
{
	View result;
	result.page.text = std::move(text);
	result.page.buttons.push_back(Button{ std::string(kBack) });
	result.actions.emplace_back(Open{ .screen = Screen::Stats, .group = {}, .page = overviewPage_ });
	for (std::string& group : groupsOf(stats)) {
		result.page.buttons.push_back(Button{ group });
		result.actions.emplace_back(Open{ .screen = Screen::Group, .group = std::move(group), .page = 0 });
	}
	return result;
}

StatBoxModel::View StatBoxModel::groupView() const
{
	std::vector<StatId> stats = assignableStats();
	std::erase_if(stats, [this](const StatId& stat) { return groupOf(stat) != group_; });
	return statsButtons(std::format("{}\n{}", group_, freePointsText(service_.pendingPoints())), std::move(stats),
		Open{ .screen = Screen::LevelUp, .group = {}, .page = 0 });
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

std::vector<std::string> StatBoxModel::overviewTexts() const
{
	const std::vector<StatId> stats = service_.registry().activeStats();
	OverviewPages             pages(layout_.linesPerPage);
	for (const std::string& group : groupsOf(stats)) {
		pages.add(group, sectionLines(group, stats));
	}
	return pages.texts();
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

void StatBoxModel::startOver()
{
	screen_ = Screen::Stats;
	overviewPage_ = 0;
}

}
