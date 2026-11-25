// The stat XP from skill level-ups and from actions, with XP modifiers.
#include "tom/stats/core/training.h"

#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/curve.h"
#include "tom/stats/core/table_folder.h"
#include "tom/stats/core/table_merge.h"
#include "tom/stats/core/xp_modifiers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

using Catch::Matchers::WithinRel;
using nlohmann::json;
using tom::stats::ActionsTable;
using tom::stats::actionTrainingXp;
using tom::stats::buildTables;
using tom::stats::entryValidator;
using tom::stats::folderName;
using tom::stats::LinearCurve;
using tom::stats::MergedTables;
using tom::stats::MergeResult;
using tom::stats::PluginInfo;
using tom::stats::PluginQuery;
using tom::stats::SkillEntry;
using tom::stats::skillLevelUpXp;
using tom::stats::SkillsTable;
using tom::stats::StatId;
using tom::stats::StatXp;
using tom::stats::Table;
using tom::stats::Tables;
using tom::stats::TableSource;
using tom::stats::XpModifierKind;
using tom::stats::XpModifierRegistry;

namespace
{

const StatId kStr{ "STR" };
const StatId kDex{ "DEX" };
const StatId kCon{ "CON" };
const StatId kEnd{ "END" };
const StatId kCha{ "CHA" };

const std::filesystem::path kData{ TOM_STATS_DATA_DIR };

class NoPlugins final : public PluginQuery
{
public:
	[[nodiscard]] std::vector<PluginInfo> activePlugins() const override { return {}; }
};

const SkillsTable kSkills{
	{ "OneHanded", SkillEntry{ .primary = kStr, .secondary = kDex, .primaryShare = 0.75 } },
	{ "MyMod:Lifting", SkillEntry{ .primary = kStr, .secondary = kStr, .primaryShare = 0.75 } },
};

ActionsTable actions()
{
	ActionsTable table;
	table["DamageTaken"].training = { { kCon, LinearCurve{ .a = 0.5, .b = 0.0 } }, { kEnd, LinearCurve{ .a = 0.0, .b = 2.0 } } };
	table["Lockpicking"].training = {};
	return table;
}

// The default file of a table merged on top of the registered entries, with
// the table's entry validator.
MergeResult merge(Table table, const json& file, const json& registered = json::object())
{
	const std::vector<TableSource> sources{ { "_default.json", file.dump() } };
	return mergeTable(sources, NoPlugins{}, registered, entryValidator(table));
}

json coreStats()
{
	json stats = json::object();
	for (const char* name : { "STR", "DEX", "CON", "INT", "WIS", "PER", "END", "CHA" }) {
		stats[name] = { { "base", 10 } };
	}
	return stats;
}

// Tables from default files for skills and actions above their registered
// entries.
Tables layered(const json& skillsFile, const json& skillsRegistered, const json& actionsFile, const json& actionsRegistered)
{
	MergedTables merged;
	merged.stats = merge(Table::Stats, coreStats());
	merged.skills = merge(Table::Skills, skillsFile, skillsRegistered);
	merged.actions = merge(Table::Actions, actionsFile, actionsRegistered);
	merged.incompatible = merge(Table::Incompatible, json::object());
	auto tables = buildTables(merged);
	REQUIRE(tables);
	return std::move(*tables);
}

MergeResult loadShipped(Table table)
{
	return loadTableFolder(kData / folderName(table), NoPlugins{}, json::object(), entryValidator(table));
}

Tables shipped()
{
	const MergedTables merged{ loadShipped(Table::Stats), loadShipped(Table::Skills), loadShipped(Table::Actions),
		loadShipped(Table::Incompatible) };
	auto               tables = buildTables(merged);
	REQUIRE(tables);
	return std::move(*tables);
}

}

TEST_CASE("a skill level-up splits the level reached between primary and secondary stat", "[training][skills]")
{
	const auto xp = skillLevelUpXp(kSkills, XpModifierRegistry{}, "OneHanded", 20);

	REQUIRE(xp.size() == 2);
	CHECK(xp[0].stat == kStr);
	CHECK_THAT(xp[0].xp, WithinRel(15.0));
	CHECK(xp[1].stat == kDex);
	CHECK_THAT(xp[1].xp, WithinRel(5.0));
}

TEST_CASE("an unmapped skill gives no XP", "[training][skills]")
{
	CHECK(skillLevelUpXp(kSkills, XpModifierRegistry{}, "TwoHanded", 20).empty());
	CHECK(skillLevelUpXp(kSkills, XpModifierRegistry{}, "Other:Swimming", 20).empty());
}

TEST_CASE("a skill at level 150 trains with the same rule", "[training][skills]")
{
	const auto xp = skillLevelUpXp(kSkills, XpModifierRegistry{}, "OneHanded", 150);

	REQUIRE(xp.size() == 2);
	CHECK_THAT(xp[0].xp, WithinRel(112.5));
	CHECK_THAT(xp[1].xp, WithinRel(37.5));
}

TEST_CASE("a skill with the same primary and secondary stat gives that stat the whole level", "[training][skills]")
{
	const auto xp = skillLevelUpXp(kSkills, XpModifierRegistry{}, "MyMod:Lifting", 30);

	CHECK(xp == std::vector<StatXp>{ { .stat = kStr, .xp = 30.0 } });
}

TEST_CASE("level-up stat XP modifiers from two owners combine", "[training][skills][xp_modifiers]")
{
	XpModifierRegistry modifiers;
	REQUIRE(modifiers.set("Professions", XpModifierKind::SkillLevelUpStatXp, "OneHanded", 0.5));
	REQUIRE(modifiers.set("OtherMod", XpModifierKind::SkillLevelUpStatXp, "OneHanded", 1.0));

	const auto xp = skillLevelUpXp(kSkills, modifiers, "OneHanded", 20);

	// 20 * (1 + 0.5) * (1 + 1), split 3:1
	REQUIRE(xp.size() == 2);
	CHECK_THAT(xp[0].xp, WithinRel(45.0));
	CHECK_THAT(xp[1].xp, WithinRel(15.0));
}

TEST_CASE("level-up stat XP ignores the other kinds of modifier", "[training][skills][xp_modifiers]")
{
	XpModifierRegistry modifiers;
	REQUIRE(modifiers.set("Professions", XpModifierKind::SkillXp, "OneHanded", 1.0));
	REQUIRE(modifiers.set("Professions", XpModifierKind::ActionStatXp, "OneHanded", 1.0));

	const auto xp = skillLevelUpXp(kSkills, modifiers, "OneHanded", 20);

	REQUIRE(xp.size() == 2);
	CHECK_THAT(xp[0].xp, WithinRel(15.0));
}

TEST_CASE("an action gives each stat it trains the XP of its curve at the input", "[training][actions]")
{
	const auto xp = actionTrainingXp(actions(), XpModifierRegistry{}, "DamageTaken", 30.0);

	REQUIRE(xp.size() == 2);
	CHECK(xp[0].stat == kCon);
	CHECK_THAT(xp[0].xp, WithinRel(15.0));
	CHECK(xp[1].stat == kEnd);
	CHECK_THAT(xp[1].xp, WithinRel(2.0));
}

TEST_CASE("an unknown action and an action without training give no XP", "[training][actions]")
{
	CHECK(actionTrainingXp(actions(), XpModifierRegistry{}, "Other:Climb", 10.0).empty());
	CHECK(actionTrainingXp(actions(), XpModifierRegistry{}, "Lockpicking", 10.0).empty());
}

TEST_CASE("action stat XP modifiers from two owners combine", "[training][actions][xp_modifiers]")
{
	XpModifierRegistry modifiers;
	REQUIRE(modifiers.set("Professions", XpModifierKind::ActionStatXp, "DamageTaken", 0.5));
	REQUIRE(modifiers.set("OtherMod", XpModifierKind::ActionStatXp, "DamageTaken", -0.5));
	REQUIRE(modifiers.set("Professions", XpModifierKind::ActionStatXp, "Hit", 3.0));

	const auto xp = actionTrainingXp(actions(), modifiers, "DamageTaken", 30.0);

	// 15 * (1 + 0.5) * (1 - 0.5) and 2 * (1 + 0.5) * (1 - 0.5)
	REQUIRE(xp.size() == 2);
	CHECK_THAT(xp[0].xp, WithinRel(11.25));
	CHECK_THAT(xp[1].xp, WithinRel(1.5));
}

TEST_CASE("the shipped weapon and trade training give the ST-15 example values", "[training][actions][data]")
{
	const Tables             tables = shipped();
	const XpModifierRegistry none;

	// Iron dagger (W = 2), Stendarr's Hammer (W = 100), 100 septims.
	const auto dagger = actionTrainingXp(tables.actions, none, "WeaponSwing", 2.0);
	const auto hammer = actionTrainingXp(tables.actions, none, "WeaponSwing", 100.0);
	const auto trade = actionTrainingXp(tables.actions, none, "Trade", 100.0);

	REQUIRE(dagger.size() == 1);
	CHECK(dagger[0].stat == kStr);
	CHECK_THAT(dagger[0].xp, WithinRel(1.0, 0.01));
	REQUIRE(hammer.size() == 1);
	CHECK_THAT(hammer[0].xp, WithinRel(10.0, 0.01));
	REQUIRE(trade.size() == 1);
	CHECK(trade[0].stat == kCha);
	CHECK_THAT(trade[0].xp, WithinRel(1.0));
}

TEST_CASE("the shipped mapping splits a skill level-up by the skill's share", "[training][skills][data]")
{
	const Tables      tables = shipped();
	const SkillEntry& oneHanded = tables.skills.at("OneHanded");

	const auto xp = skillLevelUpXp(tables.skills, XpModifierRegistry{}, "OneHanded", 40);

	REQUIRE(xp.size() == 2);
	CHECK(xp[0].stat == kStr);
	CHECK_THAT(xp[0].xp, WithinRel(40.0 * oneHanded.primaryShare));
	CHECK(xp[1].stat == kDex);
	CHECK_THAT(xp[1].xp, WithinRel(40.0 * (1.0 - oneHanded.primaryShare)));
}

TEST_CASE("files override registered mappings and actions, which still apply where no file sets them", "[training][layers]")
{
	const json registeredSkills = {
		{ "OneHanded", { { "primary", "END" }, { "secondary", "CON" }, { "primaryShare", 0.5 } } },
		{ "MyMod:Climbing", { { "primary", "STR" }, { "secondary", "DEX" }, { "primaryShare", 0.5 } } },
		{ "MyMod:Rowing", { { "primary", "END" }, { "secondary", "STR" }, { "primaryShare", 0.5 } } },
	};
	const json skillsFile = {
		{ "OneHanded", { { "primary", "STR" }, { "secondary", "DEX" }, { "primaryShare", 0.75 } } },
		{ "MyMod:Climbing", { { "primaryShare", 0.75 } } },
	};
	const json linear = { { "curve", "linear" }, { "a", 1 }, { "b", 0 } };
	const json registeredActions = {
		{ "MyMod:Climb", { { "training", { { "STR", linear } } } } },
		{ "MyMod:Row", { { "training", { { "END", linear } } } } },
	};
	const json actionsFile = {
		{ "MyMod:Climb", { { "training", { { "STR", { { "a", 3 } } } } } } },
	};
	const Tables             tables = layered(skillsFile, registeredSkills, actionsFile, registeredActions);
	const XpModifierRegistry none;

	// The file replaces the registered mapping, and sets one field of another.
	CHECK(skillLevelUpXp(tables.skills, none, "OneHanded", 20) == std::vector<StatXp>{ { .stat = kStr, .xp = 15.0 }, { .stat = kDex, .xp = 5.0 } });
	CHECK(skillLevelUpXp(tables.skills, none, "MyMod:Climbing", 20) == std::vector<StatXp>{ { .stat = kStr, .xp = 15.0 }, { .stat = kDex, .xp = 5.0 } });
	// A registered mapping no file sets applies as registered.
	CHECK(skillLevelUpXp(tables.skills, none, "MyMod:Rowing", 20) == std::vector<StatXp>{ { .stat = kEnd, .xp = 10.0 }, { .stat = kStr, .xp = 10.0 } });
	CHECK(actionTrainingXp(tables.actions, none, "MyMod:Climb", 2.0) == std::vector<StatXp>{ { .stat = kStr, .xp = 6.0 } });
	CHECK(actionTrainingXp(tables.actions, none, "MyMod:Row", 2.0) == std::vector<StatXp>{ { .stat = kEnd, .xp = 2.0 } });
}
