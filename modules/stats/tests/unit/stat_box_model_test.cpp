// The stat box pages and the flow between them (ST-2, ST-21, ST-22, ST-24).
#include "tom/stats/core/stat_box_model.h"

#include "stats_service_fixture.h"

#include "tom/stats/core/config/settings.h"
#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/stat_registry.h"
#include "tom/stats/core/stat_value.h"
#include "tom/stats/core/stats_service.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using tom::stats::AvDelta;
using tom::stats::Button;
using tom::stats::Closed;
using tom::stats::FreePoints;
using tom::stats::Page;
using tom::stats::PressResult;
using tom::stats::StatBoxLayout;
using tom::stats::StatBoxModel;
using tom::stats::StatsService;
using tom::stats::StatValue;
using tom::stats::Tables;

using namespace service_test;

namespace
{
// The first page of a player whose stats are all at their default base of 10
// (Stats plan, C9b).
const std::string kDefaultStatsText =
	"Warrior\n"
	"Strength (STR) 10 (0 / 100 XP)\n"
	"Constitution (CON) 10 (0 / 100 XP)\n"
	"Endurance (END) 10 (0 / 100 XP)\n"
	"\n"
	"Mage\n"
	"Intelligence (INT) 10 (0 / 100 XP)\n"
	"Wisdom (WIS) 10 (0 / 100 XP)\n"
	"\n"
	"Thief\n"
	"Dexterity (DEX) 10 (0 / 100 XP)\n"
	"Perception (PER) 10 (0 / 100 XP)\n"
	"\n"
	"Other\n"
	"Charisma (CHA) 10 (0 / 100 XP)";

// The default first page with a section before Other.
std::string withSection(const std::string& section)
{
	std::string text = kDefaultStatsText;
	return text.insert(text.find("\n\nOther\n"), "\n\n" + section);
}

// The core stats' buttons in registry order.
const std::vector<Button> kCoreStatButtons{ { "Strength (STR) 10" }, { "Dexterity (DEX) 10" },
	{ "Constitution (CON) 10" }, { "Intelligence (INT) 10" }, { "Wisdom (WIS) 10" }, { "Perception (PER) 10" },
	{ "Endurance (END) 10" }, { "Charisma (CHA) 10" } };

std::vector<Button> withBack(std::vector<Button> buttons)
{
	buttons.push_back(Button{ "Back" });
	return buttons;
}

// Room for every stat button and "Back", so the box needs no group pages.
constexpr std::size_t kRoomForAll = 12;

struct CoreName
{
	std::string_view name;
	std::string_view displayName;
	std::string_view group;
};

constexpr std::array<CoreName, 8> kCoreNames{ { { "STR", "Strength", "Warrior" },
	{ "CON", "Constitution", "Warrior" }, { "END", "Endurance", "Warrior" }, { "INT", "Intelligence", "Mage" },
	{ "WIS", "Wisdom", "Mage" }, { "DEX", "Dexterity", "Thief" }, { "PER", "Perception", "Thief" },
	{ "CHA", "Charisma", "Other" } } };

// The fixture's tables with the display names, short names, groups and group
// order of the shipped default (C3a). Mod:Luck names no group, so it is in
// its namespace's group, as buildTables() puts it.
Tables defaultTables()
{
	Tables result = tables();
	for (const auto& core : kCoreNames) {
		auto& entry = result.stats[StatId{ std::string(core.name) }];
		entry.displayName = std::string(core.displayName);
		entry.shortName = std::string(core.name);
		entry.group = std::string(core.group);
	}
	result.stats[kLuck].group = "Mod";
	result.statGroups = { "Warrior", "Mage", "Thief", "Other" };
	return result;
}

// Adds the entry of a registered stat at a default base of 7.
void addEntry(Tables& tables, const StatId& stat, std::string group, std::optional<std::string> displayName = {},
	std::optional<std::string> shortName = {})
{
	auto& entry = tables.stats[stat];
	entry.group = std::move(group);
	entry.displayName = std::move(displayName);
	entry.shortName = std::move(shortName);
	entry.defaultBase = 7;
}

// The player of a service over the tables, and boxes over both.
struct Fixture
{
	Tables       boxTables;
	StatsService stats;

	explicit Fixture(Tables tables = defaultTables()) :
		boxTables(std::move(tables)),
		stats(tom::stats::Settings{}, boxTables, kPlayer)
	{
		REQUIRE(stats.addActor(kPlayer, "ImperialRace"));
	}

	[[nodiscard]] StatBoxModel box(std::size_t maxButtons = kRoomForAll)
	{
		return StatBoxModel(stats, boxTables, StatBoxLayout{ .maxButtons = maxButtons });
	}

	// The line of the stat with that label on the first page.
	[[nodiscard]] std::string lineOf(std::string_view label)
	{
		const std::string text = box().page().text;
		const auto        start = text.find(std::string(label) + ' ');
		REQUIRE(start != std::string::npos);
		return text.substr(start, text.find('\n', start) - start);
	}
};

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

TEST_CASE("without pending points the box lists the stats in sections and closes with OK", "[stat_box]")
{
	Fixture      fixture;
	StatBoxModel box = fixture.box();

	CHECK(box.page() == Page{ kDefaultStatsText, { { "OK" } } });
	CHECK(closes(box.press(0)));
}

TEST_CASE("the effective value shows rounded down", "[stat_box]")
{
	Fixture fixture;
	REQUIRE(fixture.stats.addXp(kPlayer, kStr, 140.0));
	REQUIRE(fixture.stats.setMultiplier(kPlayer, 1.5));

	// (10 + 1) * 1.5 = 16.5
	CHECK(fixture.box().page().text.starts_with(
		"Warrior\n"
		"Strength (STR) 16 (40 / 200 XP)\n"
		"Constitution (CON) 15 (0 / 100 XP)\n"));
}

TEST_CASE("XP rounds down and the cost rounds up without floating-point noise", "[stat_box]")
{
	Fixture fixture;

	SECTION("XP just below the cost shows below it")
	{
		REQUIRE(fixture.stats.addXp(kPlayer, kStr, 99.99));
		CHECK(fixture.lineOf("Strength") == "Strength (STR) 10 (99 / 100 XP)");
	}

	SECTION("a cost above a whole number only by rounding shows as that number")
	{
		REQUIRE(fixture.stats.setR(1.1));
		REQUIRE(fixture.stats.addXp(kPlayer, kStr, 100.0));
		REQUIRE(tom::stats::cost(1.1, 1) > 110.0);
		CHECK(fixture.lineOf("Strength") == "Strength (STR) 11 (0 / 110 XP)");
	}

	SECTION("an infinite cost shows as infinity")
	{
		REQUIRE(fixture.stats.setValues(kPlayer, kStr, StatValue{ .base = 10, .accumulated = 0, .active = 2000, .xp = 0.0 }));
		CHECK(fixture.lineOf("Strength") == "Strength (STR) 2010 (0 / ∞ XP)");
	}

	SECTION("large numbers take a suffix")
	{
		REQUIRE(fixture.stats.setValues(kPlayer, kStr, StatValue{ .base = 10, .accumulated = 0, .active = 10, .xp = 12345.0 }));
		// The cost is 102 400.
		CHECK(fixture.lineOf("Strength") == "Strength (STR) 20 (12.3K / 103K XP)");
	}
}

TEST_CASE("labels of registered stats", "[stat_box][labels]")
{
	const StatId kLuc{ "Luck:LUC" };
	Tables       tables = defaultTables();

	SECTION("without display name and short name the label is the name without the namespace")
	{
		addEntry(tables, kLuc, "Luck");
		Fixture fixture(tables);
		REQUIRE(fixture.stats.registerStat(kLuc, FreePoints::OptedOut));

		CHECK(fixture.box().page().text == withSection("Luck\nLUC 7 (0 / 100 XP)"));
	}

	SECTION("with a display name only the short name is the name without the namespace")
	{
		addEntry(tables, kLuc, "Luck", "Luck");
		Fixture fixture(tables);
		REQUIRE(fixture.stats.registerStat(kLuc, FreePoints::OptedOut));

		CHECK(fixture.box().page().text == withSection("Luck\nLuck (LUC) 7 (0 / 100 XP)"));
	}

	SECTION("with a short name only the label is the short name")
	{
		addEntry(tables, kLuc, "Luck", std::nullopt, "Lk");
		Fixture fixture(tables);
		REQUIRE(fixture.stats.registerStat(kLuc, FreePoints::OptedOut));

		CHECK(fixture.box().page().text == withSection("Luck\nLk 7 (0 / 100 XP)"));
	}

	SECTION("a stat without a stats-table entry shows by its name without the namespace in its namespace's group")
	{
		addEntry(tables, kLuc, "Elsewhere", "Luck", "LUC");
		Fixture fixture(tables);
		REQUIRE(fixture.stats.registerStat(kLuc, FreePoints::OptedOut));
		// Only a test can build this: the box gets tables without the entry
		// the service registered the stat with (Stats plan, assumption 14).
		Tables withoutEntry = fixture.boxTables;
		withoutEntry.stats.erase(kLuc);

		const StatBoxModel box(fixture.stats, withoutEntry, StatBoxLayout{ .maxButtons = kRoomForAll });

		CHECK(box.page().text == withSection("Luck\nLUC 7 (0 / 100 XP)"));
	}
}

TEST_CASE("groups come in the stats files' order, then by registration, with Other last", "[stat_box][groups]")
{
	Tables tables = defaultTables();

	SECTION("namespace groups follow the stats files' groups in registration order")
	{
		addEntry(tables, StatId{ "B:Y" }, "B");
		addEntry(tables, StatId{ "A:X" }, "A");
		Fixture fixture(tables);
		REQUIRE(fixture.stats.registerStat(StatId{ "B:Y" }));
		REQUIRE(fixture.stats.registerStat(StatId{ "A:X" }));

		CHECK(fixture.box().page().text ==
			  "Warrior\n"
			  "Strength (STR) 10 (0 / 100 XP)\n"
			  "Constitution (CON) 10 (0 / 100 XP)\n"
			  "Endurance (END) 10 (0 / 100 XP)\n"
			  "\n"
			  "Mage\n"
			  "Intelligence (INT) 10 (0 / 100 XP)\n"
			  "Wisdom (WIS) 10 (0 / 100 XP)\n"
			  "\n"
			  "Thief\n"
			  "Dexterity (DEX) 10 (0 / 100 XP)\n"
			  "Perception (PER) 10 (0 / 100 XP)\n"
			  "\n"
			  "B\n"
			  "Y 7 (0 / 100 XP)\n"
			  "\n"
			  "A\n"
			  "X 7 (0 / 100 XP)\n"
			  "\n"
			  "Other\n"
			  "Charisma (CHA) 10 (0 / 100 XP)");
	}

	SECTION("a registered stat that names a stats-file group is in its section")
	{
		addEntry(tables, StatId{ "Mod:Arcana" }, "Mage", "Arcana", "ARC");
		Fixture fixture(tables);
		REQUIRE(fixture.stats.registerStat(StatId{ "Mod:Arcana" }));

		CHECK(fixture.box().page().text.contains(
			"Mage\n"
			"Intelligence (INT) 10 (0 / 100 XP)\n"
			"Wisdom (WIS) 10 (0 / 100 XP)\n"
			"Arcana (ARC) 7 (0 / 100 XP)\n"
			"\n"
			"Thief\n"));
	}

	SECTION("Other is last even when the stats files name it first")
	{
		tables.statGroups = { "Other", "Thief", "Mage", "Warrior" };
		Fixture fixture(tables);

		CHECK(fixture.box().page().text ==
			  "Thief\n"
			  "Dexterity (DEX) 10 (0 / 100 XP)\n"
			  "Perception (PER) 10 (0 / 100 XP)\n"
			  "\n"
			  "Mage\n"
			  "Intelligence (INT) 10 (0 / 100 XP)\n"
			  "Wisdom (WIS) 10 (0 / 100 XP)\n"
			  "\n"
			  "Warrior\n"
			  "Strength (STR) 10 (0 / 100 XP)\n"
			  "Constitution (CON) 10 (0 / 100 XP)\n"
			  "Endurance (END) 10 (0 / 100 XP)\n"
			  "\n"
			  "Other\n"
			  "Charisma (CHA) 10 (0 / 100 XP)");
	}

	SECTION("a group without an active stat has no section")
	{
		for (const auto* name : { "DEX", "PER" }) {
			tables.stats[StatId{ name }].group = "Warrior";
		}
		Fixture fixture(tables);

		CHECK_FALSE(fixture.box().page().text.contains("Thief"));
	}
}

TEST_CASE("pending points offer Level Up and Cancel, and Cancel keeps them", "[stat_box]")
{
	Fixture fixture;
	fixture.stats.onPlayerLevelUp(2);
	StatBoxModel box = fixture.box();

	CHECK(box.page() == Page{ kDefaultStatsText, { { "Level Up" }, { "Cancel" } } });
	CHECK(closes(box.press(1)));
	CHECK(fixture.stats.pendingPoints() == 3);
}

TEST_CASE("Level Up assigns one point per press until none is pending", "[stat_box]")
{
	Fixture fixture;
	fixture.stats.onPlayerLevelUp(2);
	StatBoxModel box = fixture.box();

	CHECK(nextPage(box.press(0)) == Page{ "Free points: 3", withBack(kCoreStatButtons) });

	const PressResult first = box.press(2);  // CON
	CHECK_FALSE(first.error);
	CHECK(first.deltas == std::vector<AvDelta>{ { .actorValue = "Health", .delta = above(kHealthCurve, 1.0) } });
	auto buttons = kCoreStatButtons;
	buttons[2] = Button{ "Constitution (CON) 11" };
	CHECK(nextPage(first) == Page{ "Free points: 2", withBack(buttons) });

	REQUIRE_FALSE(box.press(0).error);  // STR
	buttons[0] = Button{ "Strength (STR) 11" };
	CHECK(box.page() == Page{ "Free points: 1", withBack(buttons) });

	// The last point leads back to the first page, which now closes with OK.
	const Page last = nextPage(box.press(0));
	CHECK(last.buttons == std::vector<Button>{ { "OK" } });
	CHECK(last.text.starts_with("Warrior\nStrength (STR) 12 (0 / 100 XP)\nConstitution (CON) 11 (0 / 100 XP)\n"));
	CHECK(fixture.stats.pendingPoints() == 0);
	CHECK(fixture.stats.read(kPlayer, kStr)->accumulated == 2);

	// Points granted while the box is still open start on the first page.
	fixture.stats.onPlayerLevelUp(3);
	CHECK(box.page().buttons == std::vector<Button>{ { "Level Up" }, { "Cancel" } });
}

TEST_CASE("Back leaves the Level Up page with the points still pending", "[stat_box]")
{
	Fixture fixture;
	fixture.stats.onPlayerLevelUp(2);
	StatBoxModel box = fixture.box();
	REQUIRE(std::holds_alternative<Page>(box.press(0).next));

	CHECK(nextPage(box.press(kCoreStatButtons.size())) == Page{ kDefaultStatsText, { { "Level Up" }, { "Cancel" } } });
	CHECK(fixture.stats.pendingPoints() == 3);
}

TEST_CASE("a point-assignment claim leaves only OK", "[stat_box]")
{
	Fixture fixture;
	fixture.stats.onPlayerLevelUp(2);
	REQUIRE(fixture.stats.claimPointAssignment("Mod:Professions"));
	StatBoxModel box = fixture.box();

	CHECK(box.page() == Page{ kDefaultStatsText, { { "OK" } } });
	CHECK(closes(box.press(0)));
	CHECK(fixture.stats.pendingPoints() == 3);
}

TEST_CASE("a registered stat shows like a core stat unless it is opted out of free points", "[stat_box]")
{
	Fixture fixture;
	fixture.stats.onPlayerLevelUp(2);
	const std::string text = withSection("Mod\nLuck 5 (0 / 100 XP)");

	SECTION("a stat that takes free points is on both pages")
	{
		REQUIRE(fixture.stats.registerStat(kLuck));
		StatBoxModel box = fixture.box();

		CHECK(box.page().text == text);
		auto buttons = kCoreStatButtons;
		buttons.push_back(Button{ "Luck 5" });
		CHECK(nextPage(box.press(0)) == Page{ "Free points: 3", withBack(buttons) });
	}

	SECTION("an opted-out stat is on the first page only")
	{
		REQUIRE(fixture.stats.registerStat(kLuck, FreePoints::OptedOut));
		StatBoxModel box = fixture.box();

		CHECK(box.page().text == text);
		CHECK(nextPage(box.press(0)) == Page{ "Free points: 3", withBack(kCoreStatButtons) });
	}
}

TEST_CASE("a button the page does not have changes nothing", "[stat_box]")
{
	Fixture fixture;
	fixture.stats.onPlayerLevelUp(2);
	StatBoxModel box = fixture.box();
	const Page   before = box.page();

	const PressResult result = box.press(2);

	CHECK(result.error);
	CHECK(nextPage(result) == before);
	CHECK(fixture.stats.pendingPoints() == 3);
}

namespace
{
const StatId kMight{ "Mod:Might" };    // a registered stat in the Warrior group
const StatId kFate{ "Fortune:Fate" };  // a registered stat in its namespace's group
const StatId kY{ "B:Y" };              // registered before A:X
const StatId kX{ "A:X" };

// The default tables with Mod:Might in Warrior and the others in their
// namespaces' groups.
Tables groupedTables()
{
	Tables result = defaultTables();
	addEntry(result, kMight, "Warrior", "Might", "MGT");
	addEntry(result, kFate, "Fortune", "Fate", "FAT");
	addEntry(result, kY, "B");
	addEntry(result, kX, "A");
	return result;
}

// The player with pending points; groupedBox() has room for 8 buttons, so
// the 8 core stats and "Back" need group pages.
struct GroupedBox : Fixture
{
	GroupedBox() :
		Fixture(groupedTables())
	{
		stats.onPlayerLevelUp(2);
	}

	[[nodiscard]] StatBoxModel groupedBox() { return box(8); }
};

const Page kGroupsPage{ "Free points: 3", { { "Warrior" }, { "Mage" }, { "Thief" }, { "Other" }, { "Back" } } };
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
	StatBoxModel box = fixture.groupedBox();

	CHECK(nextPage(box.press(0)) == kGroupsPage);

	SECTION("a group shows its stats and assigns points until Back")
	{
		const Page warrior{ "Warrior\nFree points: 3",
			{ { "Strength (STR) 10" }, { "Constitution (CON) 10" }, { "Endurance (END) 10" }, { "Back" } } };
		CHECK(nextPage(box.press(0)) == warrior);

		const PressResult assigned = box.press(1);  // CON
		CHECK(assigned.deltas == std::vector<AvDelta>{ { .actorValue = "Health", .delta = above(kHealthCurve, 1.0) } });
		CHECK(nextPage(assigned) == Page{ "Warrior\nFree points: 2",
										{ { "Strength (STR) 10" }, { "Constitution (CON) 11" }, { "Endurance (END) 10" },
											{ "Back" } } });

		// Back goes to the groups, and from there to the first page.
		CHECK(nextPage(box.press(3)) == Page{ "Free points: 2", kGroupsPage.buttons });
		CHECK(nextPage(box.press(4)).buttons == std::vector<Button>{ { "Level Up" }, { "Cancel" } });
		CHECK(fixture.stats.pendingPoints() == 2);
	}

	SECTION("the last point in a group leads back to the first page")
	{
		REQUIRE(std::holds_alternative<Page>(box.press(1).next));  // Mage
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

	SECTION("a registered stat that names no group is in its namespace's group")
	{
		REQUIRE(fixture.stats.registerStat(kLuck));
		StatBoxModel box = fixture.groupedBox();

		CHECK(nextPage(box.press(0)).buttons ==
			  std::vector<Button>{ { "Warrior" }, { "Mage" }, { "Thief" }, { "Mod" }, { "Other" }, { "Back" } });
		CHECK(nextPage(box.press(3)) == Page{ "Mod\nFree points: 3", { { "Luck 5" }, { "Back" } } });
	}

	SECTION("a registered stat joins the group it names")
	{
		REQUIRE(fixture.stats.registerStat(kMight));
		StatBoxModel box = fixture.groupedBox();
		REQUIRE(nextPage(box.press(0)) == kGroupsPage);

		CHECK(nextPage(box.press(0)).buttons == std::vector<Button>{ { "Strength (STR) 10" }, { "Constitution (CON) 10" },
													{ "Endurance (END) 10" }, { "Might (MGT) 7" }, { "Back" } });
	}

	SECTION("the group buttons follow the section order")
	{
		REQUIRE(fixture.stats.registerStat(kY));
		REQUIRE(fixture.stats.registerStat(kX));
		StatBoxModel box = fixture.groupedBox();

		CHECK(nextPage(box.press(0)).buttons ==
			  std::vector<Button>{ { "Warrior" }, { "Mage" }, { "Thief" }, { "B" }, { "A" }, { "Other" }, { "Back" } });
	}

	SECTION("a group whose only stat is opted out of free points is not shown")
	{
		REQUIRE(fixture.stats.registerStat(kFate, FreePoints::OptedOut));
		StatBoxModel box = fixture.groupedBox();

		CHECK(box.page().text.ends_with("\n\nFortune\nFate (FAT) 7 (0 / 100 XP)\n\nOther\nCharisma (CHA) 10 (0 / 100 XP)"));
		CHECK(nextPage(box.press(0)) == kGroupsPage);
	}
}
