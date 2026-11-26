#pragma once

// The pages of the stat box and the flow between them (ST-21, ST-22, ST-24,
// Stats plan, C9). It has no UI: the game layer shows each Page as a message
// box and passes the index of the pressed button to press().

#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/error.h"
#include "tom/stats/core/stat_box_sections.h"
#include "tom/stats/core/stat_registry.h"
#include "tom/stats/core/stats_service.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace tom::stats
{

struct Button
{
	std::string text;

	bool operator==(const Button&) const = default;
};

struct Page
{
	std::string         text;
	std::vector<Button> buttons;

	bool operator==(const Page&) const = default;
};

// The box is closed; the next opening starts a new StatBoxModel.
struct Closed
{
	bool operator==(const Closed&) const = default;
};

struct PressResult
{
	std::variant<Page, Closed> next;
	// The AvDeltas of the free point the press assigned, if it assigned one
	// (ST-10); the game layer applies them to the player.
	std::vector<AvDelta> deltas;
	// Why the press changed nothing, for the log: a button the page does not
	// have, or a free point the service did not assign.
	std::optional<Error> error;
};

// The size of the message boxes that show the pages.
struct StatBoxLayout
{
	std::size_t maxButtons = 0;  // the most buttons one message box can show
};

// The stat box of the player. It reads every value from the service when it
// builds a page, so a page always shows the current values.
//
// The first page lists the active stats in sections, one per group with an
// active stat, each under the group's name and separated by an empty line
// (ST-24; Stats plan, assumption 17). A stat's line holds its label, its
// effective value and its XP toward the next point, e.g.
// "Intelligence (INT) 12 (40 / 100 XP)". A section whose group is the
// namespace of a provider in StatBoxSections shows the provider's lines in
// place of the stat lines (ST-26); the Level Up pages still show its stats.
// The page closes with "OK", or
// offers "Level Up" and "Cancel" while free points are pending and no module
// has claimed point assignment (ST-21, ST-22). "Cancel" closes the box and
// leaves the points pending. "Level Up" shows the pending points and one
// button per stat that takes free points (ST-2) with its label and effective
// value, after "Back" (Stats plan, assumption 17); each stat button assigns
// one point, and the last point leads back to the first page. When those
// stats and "Back" need more buttons than one box has, "Level Up" shows one
// button per group instead, and each group shows its stats (ST-21).
//
// Groups come in the order of ST-21: the groups the stats files name except
// Other, then the other groups in the order of their first active stat, so
// the namespace groups follow their registration, then Other. Within a group
// the stats keep the registry order.
class StatBoxModel
{
public:
	// The service and the tables must outlive the model. tables are the ones
	// the service was built from, for each stat's group, display name and
	// short name and for the order of the groups (ST-21, ST-24). A stat
	// without an entry is in its namespace's group, or in Other.
	//
	// Calls the provider of each section the first page shows, once, and
	// keeps its lines while the model lives, so a module's changes show at
	// the next opening (Stats plan, assumption 17). A provider that throws
	// leaves its section with the stat lines and adds to errors().
	StatBoxModel(StatsService& service, const Tables& tables, const StatBoxSections& sections, StatBoxLayout layout);

	// The page to show now.
	[[nodiscard]] Page page() const;

	// Presses the button with that index on the current page.
	[[nodiscard]] PressResult press(std::size_t button);

	// The section providers that failed in the constructor, for the log.
	[[nodiscard]] const std::vector<Error>& errors() const { return errors_; }

private:
	enum class Screen
	{
		Stats,    // the first page
		LevelUp,  // the stats to assign a free point to, or their groups
		Group,    // the stats of one group
	};

	struct Close
	{};
	struct Open
	{
		Screen      screen;
		std::string group;  // for Screen::Group
	};
	struct Assign
	{
		StatId stat;
	};
	using Action = std::variant<Close, Open, Assign>;

	// A page with the action of each of its buttons.
	struct View
	{
		Page                page;
		std::vector<Action> actions;
	};

	// The screen to show: the first page once no point is left to assign.
	[[nodiscard]] Screen screen() const;
	[[nodiscard]] bool   canAssign() const;

	[[nodiscard]] View view() const;
	[[nodiscard]] View statsView() const;
	[[nodiscard]] View levelUpView() const;
	[[nodiscard]] View groupsView(std::string text, const std::vector<StatId>& stats) const;
	[[nodiscard]] View groupView() const;
	// "Back", then one button per stat, which assigns it a point.
	[[nodiscard]] View statsButtons(std::string text, std::vector<StatId> stats, Open back) const;

	// The active stats that take free points (ST-2).
	[[nodiscard]] std::vector<StatId> assignableStats() const;

	// The first page's text: the sections of the active stats.
	[[nodiscard]] std::string sectionsText() const;
	// The provider's lines of the group, or the lines of its stats among
	// stats.
	[[nodiscard]] std::vector<std::string> sectionLines(const std::string& group, const std::vector<StatId>& stats) const;

	// "<display name> (<short name>)", or the short name alone (ST-24).
	[[nodiscard]] std::string label(const StatId& stat) const;
	// The label and the effective value, or the label alone when the player
	// has no value to read.
	[[nodiscard]] std::string statButton(const StatId& stat) const;

	[[nodiscard]] std::string groupOf(const StatId& stat) const;
	// The groups of the stats, each once, in the order of ST-21.
	[[nodiscard]] std::vector<std::string> groupsOf(const std::vector<StatId>& stats) const;

	StatsService& service_;
	const Tables& tables_;
	StatBoxLayout layout_;
	Screen        screen_ = Screen::Stats;
	std::string   group_;  // the group of Screen::Group
	// The lines of the sections that a provider filled, by group.
	std::map<std::string, std::vector<std::string>> providedLines_;
	std::vector<Error>                              errors_;
};

}
