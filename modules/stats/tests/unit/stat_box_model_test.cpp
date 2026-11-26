// The stat box pages and the flow between them (ST-2, ST-21, ST-22, ST-24).
#include "tom/stats/core/stat_box_model.h"

#include "stats_service_fixture.h"

#include "tom/stats/core/effects.h"
#include "tom/stats/core/stat_registry.h"
#include "tom/stats/core/stats_service.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
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

StatsService serviceWithPlayer()
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "ImperialRace"));
	return stats;
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
	StatBoxModel box(stats);

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
	StatBoxModel box(stats);

	CHECK(box.page() == Page{ kDefaultStatsText, { { "Level Up" }, { "Cancel" } } });
	CHECK(closes(box.press(1)));
	CHECK(stats.pendingPoints() == 3);
}

TEST_CASE("Level Up assigns one point per press until none is pending", "[stat_box]")
{
	StatsService stats = serviceWithPlayer();
	stats.onPlayerLevelUp(2);
	StatBoxModel box(stats);

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
}

TEST_CASE("Back leaves the Level Up page with the points still pending", "[stat_box]")
{
	StatsService stats = serviceWithPlayer();
	stats.onPlayerLevelUp(2);
	StatBoxModel box(stats);
	REQUIRE(std::holds_alternative<Page>(box.press(0).next));

	CHECK(nextPage(box.press(kCoreStatButtons.size())) == Page{ kDefaultStatsText, { { "Level Up" }, { "Cancel" } } });
	CHECK(stats.pendingPoints() == 3);
}

TEST_CASE("a point-assignment claim leaves only OK", "[stat_box]")
{
	StatsService stats = serviceWithPlayer();
	stats.onPlayerLevelUp(2);
	REQUIRE(stats.claimPointAssignment("Mod:Professions"));
	StatBoxModel box(stats);

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
		StatBoxModel box(stats);

		CHECK(box.page().text == kDefaultStatsText + "\nMod:Luck 5 (XP 0 / 100)");
		auto buttons = kCoreStatButtons;
		buttons.push_back(Button{ "Mod:Luck 5" });
		CHECK(nextPage(box.press(0)) == Page{ "Free points: 3", withBack(buttons) });
	}

	SECTION("an opted-out stat is on the first page only")
	{
		REQUIRE(stats.registerStat(kLuck, FreePoints::OptedOut));
		StatBoxModel box(stats);

		CHECK(box.page().text == kDefaultStatsText + "\nMod:Luck 5 (XP 0 / 100)");
		CHECK(nextPage(box.press(0)) == Page{ "Free points: 3", withBack(kCoreStatButtons) });
	}
}

TEST_CASE("a button the page does not have changes nothing", "[stat_box]")
{
	StatsService stats = serviceWithPlayer();
	stats.onPlayerLevelUp(2);
	StatBoxModel box(stats);
	const Page   before = box.page();

	const PressResult result = box.press(2);

	CHECK(result.error);
	CHECK(nextPage(result) == before);
	CHECK(stats.pendingPoints() == 3);
}
