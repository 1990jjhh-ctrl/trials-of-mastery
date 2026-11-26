// The entry validators and buildTables: the typed stats, skills, actions and
// incompatible tables.
#include "tom/stats/core/config/tables.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using nlohmann::json;
using tom::stats::ActorValueTarget;
using tom::stats::buildTables;
using tom::stats::ConfigError;
using tom::stats::CurveInput;
using tom::stats::Diagnostic;
using tom::stats::entryValidator;
using tom::stats::isNamespaced;
using tom::stats::LinearCurve;
using tom::stats::MergedTables;
using tom::stats::MergeResult;
using tom::stats::NamedEffect;
using tom::stats::namespaceOf;
using tom::stats::PluginInfo;
using tom::stats::PluginQuery;
using tom::stats::PowerCurve;
using tom::stats::StatId;
using tom::stats::Table;
using tom::stats::TableSource;

namespace
{

class NoPlugins final : public PluginQuery
{
public:
	[[nodiscard]] std::vector<PluginInfo> activePlugins() const override { return {}; }
};

using Files = std::vector<std::pair<std::string, json>>;

// Merges a default file and plain patches, given as name and JSON, with the
// table's entry validator.
MergeResult merge(Table table, const Files& files, const json& registered = json::object())
{
	std::vector<TableSource> sources;
	for (const auto& [name, content] : files) {
		sources.push_back({ name, content.dump() });
	}
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

const json kLinear = { { "curve", "linear" }, { "a", 2 }, { "b", 0 } };
const json kSkill = { { "primary", "STR" }, { "secondary", "DEX" }, { "primaryShare", 0.75 } };

// Valid tables: the core stats and nothing else.
MergedTables minimal()
{
	MergedTables tables;
	tables.stats = merge(Table::Stats, { { "_default.json", coreStats() } });
	tables.skills = merge(Table::Skills, { { "_default.json", json::object() } });
	tables.actions = merge(Table::Actions, { { "_default.json", json::object() } });
	tables.incompatible = merge(Table::Incompatible, { { "_default.json", json::object() } });
	return tables;
}

// The validator's reason for rejecting the entry, or "" when it accepts it.
std::string rejection(Table table, const std::string& name, const json& entry)
{
	return entryValidator(table)(name, entry).value_or("");
}

std::vector<Diagnostic> invalidEntries(const MergeResult& result)
{
	std::vector<Diagnostic> found;
	for (const auto& diagnostic : result.diagnostics) {
		if (diagnostic.kind == Diagnostic::Kind::InvalidEntry) {
			found.push_back(diagnostic);
		}
	}
	return found;
}

std::vector<std::string> keysOf(const std::vector<ConfigError>& errors)
{
	std::vector<std::string> keys;
	for (const auto& error : errors) {
		keys.push_back(error.key);
	}
	return keys;
}

}

TEST_CASE("tables with the core stats only are valid", "[tables]")
{
	const auto result = buildTables(minimal());

	REQUIRE(result);
	CHECK(result->stats.size() == 8);
	CHECK(result->stats.at(StatId{ "STR" }).defaultBase == 10);
	CHECK(result->stats.at(StatId{ "STR" }).group == "Other");
	CHECK_FALSE(result->stats.at(StatId{ "STR" }).displayName);
	CHECK_FALSE(result->stats.at(StatId{ "STR" }).shortName);
	CHECK(result->stats.at(StatId{ "STR" }).raceAdjustments.empty());
	CHECK(result->stats.at(StatId{ "STR" }).effects.empty());
	CHECK(result->skills.empty());
	CHECK(result->actions.empty());
	CHECK(result->incompatible.empty());
	CHECK(result->dropped.empty());
	CHECK(result->ignoredRaceAdjustments.empty());
	CHECK(result->placeholders.empty());
}

TEST_CASE("a stat entry gives its group, names, base, race adjustments and effects", "[tables]")
{
	auto stats = coreStats();
	stats["CON"] = { { "group", "Warrior" }, { "displayName", "Constitution" }, { "shortName", "CON" }, { "base", 12 }, { "raceAdjustments", { { "NordRace", 1 } } },
		{ "effects", { { "Health", { { "actorValue", "Health" }, { "curve", kLinear } } },
						 { "Crit", { { "named", "CritChancePhysical" }, { "curve", kLinear } } },
						 { "Learning", { { "named", "LearningSpeed" }, { "input", "absolute" }, { "curve", kLinear } } } } } };
	auto tables = minimal();
	tables.stats = merge(Table::Stats, { { "_default.json", stats } });

	const auto result = buildTables(tables, { "NordRace" });

	REQUIRE(result);
	const auto& con = result->stats.at(StatId{ "CON" });
	CHECK(con.group == "Warrior");
	CHECK(con.displayName == "Constitution");
	CHECK(con.shortName == "CON");
	CHECK(con.defaultBase == 12);
	CHECK(con.raceAdjustments == std::map<std::string, std::int32_t>{ { "NordRace", 1 } });
	REQUIRE(con.effects.size() == 3);
	CHECK(std::get<ActorValueTarget>(con.effects.at("Health").target).name == "Health");
	CHECK(std::get<LinearCurve>(con.effects.at("Health").curve).a == 2.0);
	CHECK(std::get<NamedEffect>(con.effects.at("Crit").target) == NamedEffect::CritChancePhysical);
	CHECK(con.effects.at("Health").input == CurveInput::Relative);
	CHECK(con.effects.at("Crit").input == CurveInput::Relative);
	CHECK(con.effects.at("Learning").input == CurveInput::Absolute);
}

TEST_CASE("a stat entry without a display name or short name loads with them empty", "[tables]")
{
	auto stats = coreStats();
	stats["Luck:LUC"] = { { "base", 0 }, { "shortName", "LUC" } };
	stats["Mod:AGI"] = { { "base", 0 }, { "displayName", "Agility" } };
	auto tables = minimal();
	tables.stats = merge(Table::Stats, { { "_default.json", stats } });

	const auto result = buildTables(tables);

	REQUIRE(result);
	CHECK_FALSE(result->stats.at(StatId{ "Luck:LUC" }).displayName);
	CHECK(result->stats.at(StatId{ "Luck:LUC" }).shortName == "LUC");
	CHECK(result->stats.at(StatId{ "Mod:AGI" }).displayName == "Agility");
	CHECK_FALSE(result->stats.at(StatId{ "Mod:AGI" }).shortName);
}

TEST_CASE("a non-text or empty display name or short name skips its file", "[tables][validator]")
{
	CHECK(rejection(Table::Stats, "STR", { { "base", 10 }, { "displayName", 1 } }) == "/STR/displayName: must be a non-empty text");
	CHECK(rejection(Table::Stats, "STR", { { "base", 10 }, { "shortName", "" } }) == "/STR/shortName: must be a non-empty text");

	const auto result = merge(Table::Stats, {
												{ "_default.json", coreStats() },
												{ "names.json", { { "STR", { { "displayName", "Might" } } }, { "DEX", { { "shortName", "" } } } } },
												{ "later.json", { { "CON", { { "shortName", "CON" } } } } },
											});

	const auto invalid = invalidEntries(result);
	REQUIRE(invalid.size() == 1);
	CHECK(invalid[0].file == "names.json");
	CHECK(invalid[0].key == "/DEX");
	CHECK(invalid[0].detail == "/DEX/shortName: must be a non-empty text");
	CHECK_FALSE(result.merged.at("STR").contains("displayName"));
	CHECK(result.merged.at("CON").at("shortName") == "CON");
}

TEST_CASE("a stat without a group is in its namespace's group, or in Other", "[tables]")
{
	auto stats = coreStats();
	stats["STR"]["group"] = "Warrior";
	stats["Mod:Foo"] = { { "base", 0 } };
	stats["Mod:Bar"] = { { "base", 0 }, { "group", "Mage" } };
	auto tables = minimal();
	tables.stats = merge(Table::Stats, { { "_default.json", stats }, { "ungroup.json", { { "STR", { { "group", nullptr } } } } } });

	const auto result = buildTables(tables);

	REQUIRE(result);
	CHECK(result->stats.at(StatId{ "Mod:Foo" }).group == "Mod");
	CHECK(result->stats.at(StatId{ "Mod:Bar" }).group == "Mage");
	CHECK(result->stats.at(StatId{ "STR" }).group == "Other");
}

TEST_CASE("the stat groups follow the order in which the stats files name their stats", "[tables][groups]")
{
	const std::vector<TableSource> sources{
		{ "_default.json", R"({"STR": {"base": 10, "group": "Warrior"}, "DEX": {"base": 10, "group": "Thief"},
			"CON": {"base": 10, "group": "Warrior"}, "INT": {"base": 10, "group": "Mage"}, "WIS": {"base": 10},
			"PER": {"base": 10}, "END": {"base": 10}, "CHA": {"base": 10, "group": "Other"}})" },
		{ "patch.json", R"({"Mod:Foo": {"base": 0}, "Mod:AGI": {"base": 2}, "Mod:Bar": {"base": 0, "group": "Mage"}})" },
	};
	auto tables = minimal();
	tables.stats = mergeTable(sources, NoPlugins{}, { { "Mod:AGI", { { "base", 0 }, { "group", "Agile" } } } }, entryValidator(Table::Stats));

	const auto result = buildTables(tables);

	REQUIRE(result);
	CHECK(result->stats.at(StatId{ "Mod:AGI" }).group == "Agile");
	CHECK(result->statGroups == std::vector<std::string>{ "Warrior", "Thief", "Mage", "Other" });
}

TEST_CASE("a patch that gives a stat a new group puts the group at the stat's position", "[tables][groups]")
{
	const std::vector<TableSource> sources{
		{ "_default.json", R"({"STR": {"base": 10, "group": "Warrior"}, "DEX": {"base": 10, "group": "Thief"},
			"CON": {"base": 10, "group": "Warrior"}, "INT": {"base": 10, "group": "Mage"}, "WIS": {"base": 10, "group": "Mage"},
			"PER": {"base": 10, "group": "Thief"}, "END": {"base": 10}, "CHA": {"base": 10}})" },
		{ "patch.json", R"({"PER": {"group": "Rogue"}, "DEX": {"group": "Rogue"}})" },
	};
	auto tables = minimal();
	tables.stats = mergeTable(sources, NoPlugins{}, json::object(), entryValidator(Table::Stats));

	const auto result = buildTables(tables);

	REQUIRE(result);
	CHECK(result->statGroups == std::vector<std::string>{ "Warrior", "Rogue", "Mage" });
}

TEST_CASE("an integer may be written with a zero fraction", "[tables]")
{
	CHECK(rejection(Table::Stats, "STR", { { "base", 10.0 } }).empty());
	CHECK(rejection(Table::Stats, "STR", { { "base", 10.5 } }) == "/STR/base: must be an integer");
	CHECK(rejection(Table::Stats, "STR", { { "base", 3e9 } }) == "/STR/base: must be an integer from -2147483648 to 2147483647");

	auto stats = coreStats();
	stats["STR"]["base"] = 10.0;
	auto tables = minimal();
	tables.stats = merge(Table::Stats, { { "_default.json", stats } });
	const auto result = buildTables(tables);
	REQUIRE(result);
	CHECK(result->stats.at(StatId{ "STR" }).defaultBase == 10);
}

TEST_CASE("a skill entry gives its stats and split", "[tables]")
{
	auto tables = minimal();
	tables.skills = merge(Table::Skills, { { "_default.json", { { "OneHanded", kSkill } } } });

	const auto result = buildTables(tables);

	REQUIRE(result);
	const auto& skill = result->skills.at("OneHanded");
	CHECK(skill.primary == StatId{ "STR" });
	CHECK(skill.secondary == StatId{ "DEX" });
	CHECK(skill.primaryShare == 0.75);
}

TEST_CASE("an action entry gives its check and training", "[tables]")
{
	const json power = { { "curve", "power" }, { "alpha", 0.1 }, { "lambda", 0.5 } };
	auto       tables = minimal();
	tables.actions = merge(Table::Actions, { { "_default.json",
											   { { "Trade", { { "check", { { "stats", { { "CHA", kLinear } } }, { "skills", { { "Speech", kLinear } } }, { "result", kLinear } } },
																{ "training", { { "CHA", power } } } } },
												   { "Hit", { { "training", { { "DEX", kLinear } } } } } } } });

	const auto result = buildTables(tables);

	REQUIRE(result);
	const auto& trade = result->actions.at("Trade");
	REQUIRE(trade.check);
	CHECK(trade.check->stats.contains(StatId{ "CHA" }));
	CHECK(trade.check->skills.contains("Speech"));
	CHECK(std::get<PowerCurve>(trade.training.at(StatId{ "CHA" })).alpha == 0.1);
	CHECK_FALSE(result->actions.at("Hit").check);
}

TEST_CASE("an incompatible entry gives its plugin names", "[tables]")
{
	auto tables = minimal();
	tables.incompatible = merge(Table::Incompatible, { { "_default.json",
														 { { "Requiem", { { "plugins", { "Requiem.esp" } } } }, { "Other", { { "sksePlugins", { "Other.dll" } } } } } } });

	const auto result = buildTables(tables);

	REQUIRE(result);
	CHECK(result->incompatible.at("Requiem").plugins == std::vector<std::string>{ "Requiem.esp" });
	CHECK(result->incompatible.at("Requiem").sksePlugins.empty());
	CHECK(result->incompatible.at("Other").sksePlugins == std::vector<std::string>{ "Other.dll" });
}

TEST_CASE("every _todo marker is listed as a placeholder", "[tables]")
{
	auto curve = kLinear;
	curve["_todo"] = "TODO(OQ-7): placeholder";
	auto skill = kSkill;
	skill["_todo"] = "TODO(OQ-3): split";
	auto tables = minimal();
	tables.skills = merge(Table::Skills, { { "_default.json", { { "Sneak", skill } } } });
	tables.actions = merge(Table::Actions, { { "_default.json", { { "Sprint", { { "training", { { "END", curve } } } } } } } });

	const auto result = buildTables(tables);

	REQUIRE(result);
	REQUIRE(result->placeholders.size() == 2);
	CHECK(result->placeholders[0].file == "skills/_default.json");
	CHECK(result->placeholders[0].key == "/Sneak");
	CHECK(result->placeholders[0].note == "TODO(OQ-3): split");
	CHECK(result->placeholders[1].file == "actions/_default.json");
	CHECK(result->placeholders[1].key == "/Sprint/training/END");
}

TEST_CASE("the stats validator rejects a wrong type, a missing field, an unknown curve and a plain mod name", "[tables][validator]")
{
	const auto curve = [](const json& value) { return json{ { "base", 10 }, { "effects", { { "Health", { { "actorValue", "Health" }, { "curve", value } } } } } }; };

	CHECK(rejection(Table::Stats, "STR", { { "base", 10 }, { "group", "Warrior" } }).empty());
	CHECK(rejection(Table::Stats, "Luck:LUC", { { "base", 0 } }).empty());
	CHECK(rejection(Table::Stats, "STR", { { "base", "ten" } }) == "/STR/base: must be an integer");
	CHECK(rejection(Table::Stats, "STR", { { "group", "Warrior" } }) == "/STR/base: is missing");
	CHECK(rejection(Table::Stats, "STR", curve({ { "curve", "cubic" } })) == "/STR/effects/Health/curve: unknown curve 'cubic'");
	CHECK(rejection(Table::Stats, "LUC", { { "base", 0 } }).starts_with("/LUC: is not shipped by the module"));
	CHECK(rejection(Table::Stats, "STR", { { "base", 10 }, { "raceAdjustments", { { "NordRace", "one" } } } }) == "/STR/raceAdjustments/NordRace: must be an integer");
	CHECK(rejection(Table::Stats, "STR", json::array()) == "/STR: must be an object");
}

TEST_CASE("an effect needs exactly one target, a known named effect and a curve", "[tables][validator]")
{
	const auto effect = [](const json& value) { return json{ { "base", 10 }, { "effects", { { "E", value } } } }; };

	CHECK(rejection(Table::Stats, "STR", effect({ { "actorValue", "Health" }, { "named", "MagicDamage" }, { "curve", kLinear } })) == "/STR/effects/E: needs exactly one of 'actorValue' and 'named'");
	CHECK(rejection(Table::Stats, "STR", effect({ { "curve", kLinear } })) == "/STR/effects/E: needs exactly one of 'actorValue' and 'named'");
	CHECK(rejection(Table::Stats, "STR", effect({ { "named", "Flying" }, { "curve", kLinear } })) == "/STR/effects/E/named: is not a named effect of the module");
	CHECK(rejection(Table::Stats, "STR", effect({ { "actorValue", "Health" } })) == "/STR/effects/E/curve: is missing");
}

TEST_CASE("only a named effect sets its curve input, as relative or absolute", "[tables][validator]")
{
	const auto effect = [](const json& value) { return json{ { "base", 10 }, { "effects", { { "E", value } } } }; };

	CHECK(rejection(Table::Stats, "STR", effect({ { "named", "MagicDamage" }, { "input", "absolute" }, { "curve", kLinear } })).empty());
	CHECK(rejection(Table::Stats, "STR", effect({ { "named", "MagicDamage" }, { "input", "relative" }, { "curve", kLinear } })).empty());
	CHECK(rejection(Table::Stats, "STR", effect({ { "named", "MagicDamage" }, { "input", "total" }, { "curve", kLinear } })) == "/STR/effects/E/input: must be 'relative' or 'absolute'");
	CHECK(rejection(Table::Stats, "STR", effect({ { "named", "MagicDamage" }, { "input", 1 }, { "curve", kLinear } })) == "/STR/effects/E/input: must be a non-empty text");
	CHECK(rejection(Table::Stats, "STR", effect({ { "actorValue", "Health" }, { "input", "absolute" }, { "curve", kLinear } })) == "/STR/effects/E/input: is only allowed on a named effect");
}

TEST_CASE("unknown keys are rejected; _todo must be text", "[tables][validator]")
{
	auto extra = kLinear;
	extra["alpha"] = 1;

	CHECK(rejection(Table::Stats, "STR", { { "base", 10 }, { "bsae", 10 } }) == "/STR/bsae: is not a known key");
	CHECK(rejection(Table::Stats, "STR", { { "base", 10 }, { "_todo", 1 } }) == "/STR/_todo: must be text");
	CHECK(rejection(Table::Actions, "Hit", { { "training", { { "DEX", extra } } } }) == "/Hit/training/DEX/alpha: is not a known key");
}

TEST_CASE("the skills validator rejects a wrong type, a missing field, a plain mod name and checks stat names only by form", "[tables][validator]")
{
	auto noPrimary = kSkill;
	noPrimary.erase("primary");
	auto shareText = kSkill;
	shareText["primaryShare"] = "most";
	auto modStat = kSkill;
	modStat["secondary"] = "Luck:LUC";
	auto plainStat = kSkill;
	plainStat["secondary"] = "AGI";

	CHECK(rejection(Table::Skills, "Sneak", kSkill).empty());
	CHECK(rejection(Table::Skills, "CSF:Swimming", kSkill).empty());
	CHECK(rejection(Table::Skills, "Sneak", modStat).empty());
	CHECK(rejection(Table::Skills, "Sneak", noPrimary) == "/Sneak/primary: is missing");
	CHECK(rejection(Table::Skills, "Sneak", shareText) == "/Sneak/primaryShare: must be a number");
	CHECK(rejection(Table::Skills, "Swimming", kSkill).starts_with("/Swimming: is not shipped by the module"));
	CHECK(rejection(Table::Skills, "Sneak", plainStat).starts_with("/Sneak/secondary: is not shipped by the module"));
	auto share = kSkill;
	share["primaryShare"] = 1.5;
	CHECK(rejection(Table::Skills, "Sneak", share) == "/Sneak/primaryShare: must be a number from 0 to 1");
}

TEST_CASE("the actions validator rejects a wrong type, a missing field, an unknown curve and a plain mod name", "[tables][validator]")
{
	CHECK(rejection(Table::Actions, "Hit", { { "training", { { "DEX", kLinear } } } }).empty());
	CHECK(rejection(Table::Actions, "Fish:Catch", json::object()).empty());
	CHECK(rejection(Table::Actions, "Hit", { { "training", 1 } }) == "/Hit/training: must be an object");
	CHECK(rejection(Table::Actions, "Sneak", { { "check", { { "stats", { { "DEX", kLinear } } } } } }) == "/Sneak/check/result: is missing");
	CHECK(rejection(Table::Actions, "Hit", { { "training", { { "DEX", { { "curve", "cubic" } } } } } }) == "/Hit/training/DEX: unknown curve 'cubic'");
	CHECK(rejection(Table::Actions, "Fishing", json::object()).starts_with("/Fishing: is not shipped by the module"));
	CHECK(rejection(Table::Actions, "Sneak", { { "check", { { "skills", { { "Thievery", kLinear } } }, { "result", kLinear } } } })
			.starts_with("/Sneak/check/skills/Thievery: is not shipped by the module"));
}

TEST_CASE("the incompatible validator rejects a wrong type, an entry without names and blank names", "[tables][validator]")
{
	CHECK(rejection(Table::Incompatible, "Requiem", { { "plugins", { "Requiem.esp" } } }).empty());
	CHECK(rejection(Table::Incompatible, "Requiem", { { "plugins", "Requiem.esp" } }) == "/Requiem/plugins: must be a list of names");
	CHECK(rejection(Table::Incompatible, "Requiem", { { "plugins", json::array() } }) == "/Requiem: names no plugin to detect");
	CHECK(rejection(Table::Incompatible, "Requiem", { { "plugins", { "" } } }) == "/Requiem/plugins/0: must be a non-empty text");
}

TEST_CASE("a file with an invalid entry is skipped as a whole", "[tables][validator]")
{
	const auto result = merge(Table::Stats, {
												{ "_default.json", coreStats() },
												{ "a.json", { { "STR", { { "base", 12 } } }, { "DEX", { { "base", "high" } } } } },
												{ "b.json", { { "CON", { { "base", 14 } } } } },
											});

	const auto invalid = invalidEntries(result);
	REQUIRE(invalid.size() == 1);
	CHECK(invalid[0].file == "a.json");
	CHECK(invalid[0].key == "/DEX");
	CHECK(invalid[0].detail == "/DEX/base: must be an integer");
	CHECK(result.merged.at("STR").at("base") == 10);
	CHECK(result.merged.at("CON").at("base") == 14);
}

TEST_CASE("a patch may set one field of an entry, but a new entry must be complete", "[tables][validator]")
{
	const auto result = merge(Table::Skills, {
												 { "_default.json", { { "Sneak", kSkill } } },
												 { "field.json", { { "Sneak", { { "primaryShare", 0.5 } } } } },
												 { "partial.json", { { "Mod:Swim", { { "primary", "STR" } } } } },
												 { "removes.json", { { "Sneak", { { "primary", nullptr } } } } },
											 });

	CHECK(result.merged.at("Sneak").at("primaryShare") == 0.5);
	CHECK(result.merged.at("Sneak").contains("primary"));
	CHECK_FALSE(result.merged.contains("Mod:Swim"));
	CHECK(invalidEntries(result).size() == 2);
}

TEST_CASE("skills and actions entries that name an undefined stat are dropped on their own", "[tables][references]")
{
	auto stats = coreStats();
	stats["Mod:AGI"] = { { "base", 0 } };
	auto toMod = kSkill;
	toMod["secondary"] = "Mod:AGI";
	auto toLuck = kSkill;
	toLuck["secondary"] = "Luck:LUC";
	auto tables = minimal();
	tables.stats = merge(Table::Stats, { { "_default.json", stats }, { "drop.json", { { "Mod:AGI", nullptr } } } });
	tables.skills = merge(Table::Skills, { { "_default.json", { { "Sneak", kSkill }, { "Block", toLuck } } }, { "mod.json", { { "Archery", toMod } } } },
		{ { "API:Swim", toLuck } });
	tables.actions = merge(Table::Actions, { { "_default.json",
											   { { "Hit", { { "training", { { "DEX", kLinear } } } } }, { "Sneak", { { "check", { { "stats", { { "Luck:LUC", kLinear } } }, { "result", kLinear } } } } },
												   { "Sprint", { { "training", { { "Mod:AGI", kLinear } } } } } } } });

	const auto result = buildTables(tables);

	REQUIRE(result);
	CHECK(result->skills.size() == 1);
	CHECK(result->skills.contains("Sneak"));
	CHECK(result->actions.size() == 1);
	CHECK(result->actions.contains("Hit"));
	REQUIRE(result->dropped.size() == 5);
	CHECK(keysOf(result->dropped) == std::vector<std::string>{ "/API:Swim/secondary", "/Archery/secondary", "/Block/secondary", "/Sneak/check/stats/Luck:LUC", "/Sprint/training/Mod:AGI" });
	CHECK(result->dropped[0].file.empty());
	CHECK(result->dropped[1].file == "skills/mod.json");
	CHECK(result->dropped[2].file == "skills/_default.json");
	CHECK(result->dropped[2].message.starts_with("names the stat Luck:LUC"));
}

TEST_CASE("an invalid API entry is dropped on its own", "[tables][references]")
{
	auto tables = minimal();
	tables.stats = merge(Table::Stats, { { "_default.json", coreStats() } }, { { "Luck:LUC", { { "base", 1.5 } } }, { "Mod:AGI", { { "base", 1 } } } });

	const auto result = buildTables(tables);

	REQUIRE(result);
	CHECK(result->stats.contains(StatId{ "Mod:AGI" }));
	CHECK_FALSE(result->stats.contains(StatId{ "Luck:LUC" }));
	CHECK(keysOf(result->dropped) == std::vector<std::string>{ "/Luck:LUC/base" });
	CHECK(result->dropped[0].file.empty());
}

TEST_CASE("a core stat without an entry is an error naming the file that removed it", "[tables][errors]")
{
	auto tables = minimal();
	tables.stats = merge(Table::Stats, { { "_default.json", coreStats() }, { "tweaks.json", { { "DEX", nullptr } } } });

	const auto result = buildTables(tables);

	REQUIRE_FALSE(result);
	REQUIRE(result.error().errors.size() == 1);
	CHECK(result.error().errors[0].file == "stats/tweaks.json");
	CHECK(result.error().errors[0].key == "/DEX");
	CHECK(result.error().dropped.empty());
}

TEST_CASE("a core stat error still reports the entries dropped before it", "[tables][errors]")
{
	auto tables = minimal();
	tables.stats = merge(Table::Stats, { { "_default.json", coreStats() }, { "tweaks.json", { { "DEX", nullptr } } } },
		{ { "Luck:LUC", { { "base", 1.5 } } } });

	const auto result = buildTables(tables);

	REQUIRE_FALSE(result);
	CHECK(keysOf(result.error().errors) == std::vector<std::string>{ "/DEX" });
	CHECK(keysOf(result.error().dropped) == std::vector<std::string>{ "/Luck:LUC/base" });
}

TEST_CASE("a race adjustment applies only to its race", "[tables][races]")
{
	auto tables = minimal();
	tables.stats = merge(Table::Stats, { { "_default.json", coreStats() }, { "nord.json", { { "STR", { { "raceAdjustments", { { "NordRace", 1 } } } } } } } });

	const auto result = buildTables(tables, { "NordRace", "BretonRace" });

	REQUIRE(result);
	const auto& adjustments = result->stats.at(StatId{ "STR" }).raceAdjustments;
	CHECK(adjustments == std::map<std::string, std::int32_t>{ { "NordRace", 1 } });
	CHECK(result->stats.at(StatId{ "DEX" }).raceAdjustments.empty());
}

TEST_CASE("a non-integer race adjustment skips its file", "[tables][races]")
{
	const auto result = merge(Table::Stats, { { "_default.json", coreStats() }, { "nord.json", { { "STR", { { "raceAdjustments", { { "NordRace", 0.5 } } } } } } } });

	CHECK(invalidEntries(result).size() == 1);
	CHECK_FALSE(result.merged.at("STR").contains("raceAdjustments"));
}

TEST_CASE("an adjustment for a race no plugin defines is ignored on its own", "[tables][races]")
{
	auto tables = minimal();
	tables.stats = merge(Table::Stats, { { "_default.json", coreStats() },
										   { "races.json", { { "STR", { { "base", 11 }, { "raceAdjustments", { { "NordRace", 1 }, { "Mod:Race", 2 } } } } } } } });

	const auto result = buildTables(tables, { "NordRace" });

	REQUIRE(result);
	CHECK(result->stats.at(StatId{ "STR" }).defaultBase == 11);
	CHECK(result->stats.at(StatId{ "STR" }).raceAdjustments == std::map<std::string, std::int32_t>{ { "NordRace", 1 } });
	CHECK(result->dropped.empty());
	REQUIRE(result->ignoredRaceAdjustments.size() == 1);
	CHECK(result->ignoredRaceAdjustments[0].file == "stats/races.json");
	CHECK(result->ignoredRaceAdjustments[0].key == "/STR/raceAdjustments/Mod:Race");
}

TEST_CASE("the namespacing rule needs one colon between two names", "[tables]")
{
	CHECK(isNamespaced("Luck:LUC"));
	CHECK_FALSE(isNamespaced("LUC"));
	CHECK_FALSE(isNamespaced(":LUC"));
	CHECK_FALSE(isNamespaced("Luck:"));
	CHECK_FALSE(isNamespaced("A:B:C"));
}

TEST_CASE("the namespace of a name is the part before the colon", "[tables]")
{
	CHECK(namespaceOf("Luck:LUC") == "Luck");
	CHECK(namespaceOf("STR").empty());
	CHECK(namespaceOf(":LUC").empty());
	CHECK(namespaceOf("A:B:C").empty());
}
