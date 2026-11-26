#pragma once

// The pages of the stat box and the flow between them (ST-21, ST-22, ST-24,
// Stats plan, C9). It has no UI: the game layer shows each Page as a message
// box and passes the index of the pressed button to press().

#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/error.h"
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

// The stat box of the player. It reads every value from the service when it
// builds a page, so a page always shows the current values.
//
// The first page lists each active stat's effective value and its XP toward
// the next point (ST-24). It closes with "OK", or offers "Level Up" and
// "Cancel" while free points are pending and no module has claimed point
// assignment (ST-21, ST-22). "Cancel" closes the box and leaves the points
// pending. "Level Up" shows the pending points and one button per stat that
// takes free points (ST-2) with its effective value, plus "Back"; each stat
// button assigns one point, until none is pending, which leads back to the
// first page. When those stats and "Back" need more buttons than one box has,
// "Level Up" shows one button per stat group instead, ordered by their first
// stat with Other last, and each group shows its stats (ST-21).
class StatBoxModel
{
public:
	// The service must outlive the model. stats is the stats table the
	// service was built from, for the group of each stat (ST-21); a stat
	// without an entry is in Other. maxButtons is the most buttons one
	// message box can show.
	StatBoxModel(StatsService& service, const StatsTable& stats, std::size_t maxButtons);

	// The page to show now.
	[[nodiscard]] Page page() const;

	// Presses the button with that index on the current page.
	[[nodiscard]] PressResult press(std::size_t button);

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
	// One button per stat, which assigns it a point, then "Back".
	[[nodiscard]] View statsButtons(std::string text, std::vector<StatId> stats, Open back) const;

	// The active stats that take free points (ST-2).
	[[nodiscard]] std::vector<StatId> assignableStats() const;

	// The stat's name and effective value, or its name alone when the player
	// has no value to read.
	[[nodiscard]] std::string statLabel(const StatId& stat) const;

	[[nodiscard]] const std::string& groupOf(const StatId& stat) const;
	// The groups of the stats, each once.
	[[nodiscard]] std::vector<std::string> groupsOf(const std::vector<StatId>& stats) const;

	StatsService&                 service_;
	std::map<StatId, std::string> groups_;
	std::size_t                   maxButtons_;
	Screen                        screen_ = Screen::Stats;
	std::string                   group_;  // the group of Screen::Group
};

}
