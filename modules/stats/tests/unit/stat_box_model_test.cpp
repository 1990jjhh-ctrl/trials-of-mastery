// The stat box pages and the flow between them (ST-2, ST-21, ST-22, ST-24).
#include "tom/stats/core/stat_box_model.h"

#include "stats_service_fixture.h"

#include "tom/stats/core/config/settings.h"
#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/stat_registry.h"
#include "tom/stats/core/stats_service.h"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <initializer_list>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using tom::stats::AvDelta;
using tom::stats::Button;
using tom::stats::Closed;
using tom::stats::FreePoints;
using tom::stats::Page;
using tom::stats::PressResult;
using tom::stats::StatBoxModel;
using tom::stats::StatsService;
using tom::stats::Tables;

using namespace service_test;

namespace
{
// The first page of a player whose stats are all at their default base of 10.
const std::string kDefaultStatsText =
	"STR 10 (XP 0 / 100)\n"
	"DEX 10 (XP 0 / 100)\n"
	"CON 10 (XP 0 / 100)\n"
	"INT 10 (XP 0 / 100)\n"
	"WIS 10 (XP 0 / 100)\n"
	"PER 10 (XP 0 / 100)\n"
	"END 10 (XP 0 / 100)\n"
	"CHA 10 (XP 0 / 100)";

const std::vector<Button> kCoreStatButtons{ { "STR 10" }, { "DEX 10" }, { "CON 10" }, { "INT 10" }, { "WIS 10" },
	{ "PER 10" }, { "END 10" }, { "CHA 10" } };

std::vector<Button> withBack(std::vector<Button> buttons)
{
	buttons.push_back(Button{ "Back" });
	return buttons;
}

// Room for every stat button and "Back", so the box needs no group pages.
constexpr std::size_t kRoomForAll = 10;

StatsService serviceWithPlayer()
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "ImperialRace"));
	return stats;
}

// A box over a service built from the fixture's tables.
StatBoxModel boxFor(StatsService& stats, std::size_t maxButtons = kRoomForAll)
{
	return StatBoxModel(stats, tables().stats, maxButtons);
}

// The page the press leads to; fails the test if the press closed the box.
Page nextPage(const PressResult& result)
{
	REQUIRE(std::holds_alternative<Page>(result.next));
	return std::get<Page>(result.next);
}

bool closes(const PressResult& result)
{
	return std::holds_alternative<Closed>(result.next) && !result.error;
}
}

TEST_CASE("without pending points the box lists the stats and closes with OK", "[stat_box]")
{
	StatsService stats = serviceWithPlayer();
	REQUIRE(stats.addXp(kPlayer, kStr, 140.0));
	REQUIRE(stats.setMultiplier(kPlayer, 1.5));
	StatBoxModel box = boxFor(stats);

	const Page page = box.page();

	CHECK(page.text ==
		  "STR 16.5 (XP 40 / 200)\n"
		  "DEX 15 (XP 0 / 100)\n"
		  "CON 15 (XP 0 / 100)\n"
		  "INT 15 (XP 0 / 100)\n"
		  "WIS 15 (XP 0 / 100)\n"
		  "PER 15 (XP 0 / 100)\n"
		  "END 15 (XP 0 / 100)\n"
		  "CHA 15 (XP 0 / 100)");
	CHECK(page.buttons == std::vector<Button>{ { "OK" } });
	CHECK(closes(box.press(0)));
}

TEST_CASE("pending points offer Level Up and Cancel, and Cancel keeps them", "[stat_box]")
{
	StatsService stats = serviceWithPlayer();
	stats.onPlayerLevelUp(2);
	StatBoxModel box = boxFor(stats);

	CHECK(box.page() == Page{ kDefaultStatsText, { { "Level Up" }, { "Cancel" } } });
	CHECK(closes(box.press(1)));
	CHECK(stats.pendingPoints() == 3);
}

TEST_CASE("Level Up assigns one point per press until none is pending", "[stat_box]")
{
	StatsService stats = serviceWithPlayer();
	stats.onPlayerLevelUp(2);
	StatBoxModel box = boxFor(stats);

	CHECK(nextPage(box.press(0)) == Page{ "Free points: 3", withBack(kCoreStatButtons) });

	const PressResult first = box.press(2);  // CON
	CHECK_FALSE(first.error);
	CHECK(first.deltas == std::vector<AvDelta>{ { .actorValue = "Health", .delta = above(kHealthCurve, 1.0) } });
	auto buttons = kCoreStatButtons;
	buttons[2] = Button{ "CON 11" };
	CHECK(nextPage(first) == Page{ "Free points: 2", withBack(buttons) });

	REQUIRE_FALSE(box.press(0).error);  // STR
	buttons[0] = Button{ "STR 11" };
	CHECK(box.page() == Page{ "Free points: 1", withBack(buttons) });

	// The last point leads back to the first page, which now closes with OK.
	const Page last = nextPage(box.press(0));
	CHECK(last.buttons == std::vector<Button>{ { "OK" } });
	CHECK(last.text.starts_with("STR 12 (XP 0 / 100)\nDEX 10 (XP 0 / 100)\nCON 11 (XP 0 / 100)\n"));
	CHECK(stats.pendingPoints() == 0);
	CHECK(stats.read(kPlayer, kStr)->accumulated == 2);

	// Points granted while the box is still open start on the first page.
	stats.onPlayerLevelUp(3);
	CHECK(box.page().buttons == std::vector<Button>{ { "Level Up" }, { "Cancel" } });
}

TEST_CASE("Back leaves the Level Up page with the points still pending", "[stat_box]")
{
	StatsService stats = serviceWithPlayer();
	stats.onPlayerLevelUp(2);
	StatBoxModel box = boxFor(stats);
	REQUIRE(std::holds_alternative<Page>(box.press(0).next));

	CHECK(nextPage(box.press(kCoreStatButtons.size())) == Page{ kDefaultStatsText, { { "Level Up" }, { "Cancel" } } });
	CHECK(stats.pendingPoints() == 3);
}

TEST_CASE("a point-assignment claim leaves only OK", "[stat_box]")
{
	StatsService stats = serviceWithPlayer();
	stats.onPlayerLevelUp(2);
	REQUIRE(stats.claimPointAssignment("Mod:Professions"));
	StatBoxModel box = boxFor(stats);

	CHECK(box.page() == Page{ kDefaultStatsText, { { "OK" } } });
	CHECK(closes(box.press(0)));
	CHECK(stats.pendingPoints() == 3);
}

TEST_CASE("a registered stat shows like a core stat unless it is opted out of free points", "[stat_box]")
{
	StatsService stats = serviceWithPlayer();
	stats.onPlayerLevelUp(2);

	SECTION("a stat that takes free points is on both pages")
	{
		REQUIRE(stats.registerStat(kLuck));
		StatBoxModel box = boxFor(stats);

		CHECK(box.page().text == kDefaultStatsText + "\nMod:Luck 5 (XP 0 / 100)");
		auto buttons = kCoreStatButtons;
		buttons.push_back(Button{ "Mod:Luck 5" });
		CHECK(nextPage(box.press(0)) == Page{ "Free points: 3", withBack(buttons) });
	}

	SECTION("an opted-out stat is on the first page only")
	{
		REQUIRE(stats.registerStat(kLuck, FreePoints::OptedOut));
		StatBoxModel box = boxFor(stats);

		CHECK(box.page().text == kDefaultStatsText + "\nMod:Luck 5 (XP 0 / 100)");
		CHECK(nextPage(box.press(0)) == Page{ "Free points: 3", withBack(kCoreStatButtons) });
	}
}

TEST_CASE("a button the page does not have changes nothing", "[stat_box]")
{
	StatsService stats = serviceWithPlayer();
	stats.onPlayerLevelUp(2);
	StatBoxModel box = boxFor(stats);
	const Page   before = box.page();

	const PressResult result = box.press(2);

	CHECK(result.error);
	CHECK(nextPage(result) == before);
	CHECK(stats.pendingPoints() == 3);
}

namespace
{
const StatId kMight{ "Mod:Might" };  // a registered stat in the Warrior group
const StatId kFate{ "Mod:Fate" };    // a registered stat in a group of its own

// The fixture's tables with the default groups of ST-27, Mod:Might in
// Warrior, Mod:Fate in Mod:Fortune, and Mod:Luck in none.
Tables groupedTables()
{
	Tables result = tables();
	for (const auto* name : { "STR", "CON", "END" }) {
		result.stats[StatId{ name }].group = "Warrior";
	}
	for (const auto* name : { "INT", "WIS" }) {
		result.stats[StatId{ name }].group = "Mage";
	}
	for (const auto* name : { "DEX", "PER" }) {
		result.stats[StatId{ name }].group = "Thief";
	}
	for (const auto& [stat, group] : { std::pair{ kMight, "Warrior" }, std::pair{ kFate, "Mod:Fortune" } }) {
		auto& entry = result.stats[stat];
		entry.group = group;
		entry.defaultBase = 10;
	}
	return result;
}

// The player with pending points and a box with room for 8 buttons, so the
// 8 core stats and "Back" need group pages.
struct GroupedBox
{
	Tables       grouped = groupedTables();
	StatsService stats{ tom::stats::Settings{}, grouped, kPlayer };

	GroupedBox()
	{
		REQUIRE(stats.addActor(kPlayer, "ImperialRace"));
		stats.onPlayerLevelUp(2);
	}

	[[nodiscard]] StatBoxModel box(std::size_t maxButtons = 8) { return StatBoxModel(stats, grouped.stats, maxButtons); }
};

const Page kGroupsPage{ "Free points: 3", { { "Warrior" }, { "Thief" }, { "Mage" }, { "Other" }, { "Back" } } };
}

TEST_CASE("stats and Back that fit in one box need no group pages", "[stat_box][groups]")
{
	GroupedBox   fixture;
	StatBoxModel box = fixture.box(9);

	CHECK(nextPage(box.press(0)) == Page{ "Free points: 3", withBack(kCoreStatButtons) });
}

TEST_CASE("stats and Back that do not fit show one button per group", "[stat_box][groups]")
{
	GroupedBox   fixture;
	StatBoxModel box = fixture.box();

	CHECK(nextPage(box.press(0)) == kGroupsPage);

	SECTION("a group shows its stats and assigns points until Back")
	{
		const Page warrior{ "Warrior\nFree points: 3", { { "STR 10" }, { "CON 10" }, { "END 10" }, { "Back" } } };
		CHECK(nextPage(box.press(0)) == warrior);

		const PressResult assigned = box.press(1);  // CON
		CHECK(assigned.deltas == std::vector<AvDelta>{ { .actorValue = "Health", .delta = above(kHealthCurve, 1.0) } });
		CHECK(nextPage(assigned) == Page{ "Warrior\nFree points: 2", { { "STR 10" }, { "CON 11" }, { "END 10" }, { "Back" } } });

		// Back goes to the groups, and from there to the first page.
		CHECK(nextPage(box.press(3)) == Page{ "Free points: 2", kGroupsPage.buttons });
		CHECK(nextPage(box.press(4)).buttons == std::vector<Button>{ { "Level Up" }, { "Cancel" } });
		CHECK(fixture.stats.pendingPoints() == 2);
	}

	SECTION("the last point in a group leads back to the first page")
	{
		REQUIRE(std::holds_alternative<Page>(box.press(2).next));  // Mage
		REQUIRE_FALSE(box.press(0).error);
		REQUIRE_FALSE(box.press(0).error);
		const Page last = nextPage(box.press(1));  // WIS

		CHECK(last.buttons == std::vector<Button>{ { "OK" } });
		CHECK(fixture.stats.read(kPlayer, StatId{ "INT" })->accumulated == 2);
		CHECK(fixture.stats.read(kPlayer, StatId{ "WIS" })->accumulated == 1);
	}
}

TEST_CASE("registered stats on the group pages", "[stat_box][groups]")
{
	GroupedBox fixture;

	SECTION("a registered stat that names no group lands in Other")
	{
		REQUIRE(fixture.stats.registerStat(kLuck));
		StatBoxModel box = fixture.box();
		REQUIRE(nextPage(box.press(0)) == kGroupsPage);

		CHECK(nextPage(box.press(3)) == Page{ "Other\nFree points: 3", { { "CHA 10" }, { "Mod:Luck 5" }, { "Back" } } });
	}

	SECTION("a registered stat joins the group it names")
	{
		REQUIRE(fixture.stats.registerStat(kMight));
		StatBoxModel box = fixture.box();
		REQUIRE(nextPage(box.press(0)) == kGroupsPage);

		CHECK(nextPage(box.press(0)).buttons ==
			  std::vector<Button>{ { "STR 10" }, { "CON 10" }, { "END 10" }, { "Mod:Might 10" }, { "Back" } });
	}

	SECTION("a new group comes before Other")
	{
		REQUIRE(fixture.stats.registerStat(kFate));
		StatBoxModel box = fixture.box();

		CHECK(nextPage(box.press(0)).buttons ==
			  std::vector<Button>{ { "Warrior" }, { "Thief" }, { "Mage" }, { "Mod:Fortune" }, { "Other" }, { "Back" } });
	}

	SECTION("a group whose only stat is opted out of free points is not shown")
	{
		REQUIRE(fixture.stats.registerStat(kFate, FreePoints::OptedOut));
		StatBoxModel box = fixture.box();

		CHECK(box.page().text.ends_with("\nMod:Fate 10 (XP 0 / 100)"));
		CHECK(nextPage(box.press(0)) == kGroupsPage);
	}
}
