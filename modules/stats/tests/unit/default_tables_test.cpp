// The shipped default tables (data/<table>/_default.json) and their schemas
// (data/schema/).
#include "json_schema.h"

#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/table_folder.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using Catch::Approx;
using nlohmann::json;
using tom::stats::ActorValueTarget;
using tom::stats::buildTables;
using tom::stats::Diagnostic;
using tom::stats::entryValidator;
using tom::stats::evaluate;
using tom::stats::folderName;
using tom::stats::MergedTables;
using tom::stats::MergeResult;
using tom::stats::PluginInfo;
using tom::stats::PluginQuery;
using tom::stats::StatId;
using tom::stats::Table;
using tom::stats::Tables;
using tom::stats::test::SchemaValidator;

namespace
{

const std::filesystem::path kData{ TOM_STATS_DATA_DIR };

class NoPlugins final : public PluginQuery
{
public:
	[[nodiscard]] std::vector<PluginInfo> activePlugins() const override { return {}; }
};

MergeResult loadShipped(Table table)
{
	return loadTableFolder(kData / folderName(table), NoPlugins{}, json::object(), entryValidator(table));
}

MergedTables shippedTables()
{
	return { loadShipped(Table::Stats), loadShipped(Table::Skills), loadShipped(Table::Actions), loadShipped(Table::Incompatible) };
}

// The names of a schema's "enum", as a set.
std::set<std::string> enumNames(const json& schema, const std::string& pointer)
{
	std::set<std::string> names;
	for (const auto& name : schema.at(json::json_pointer(pointer))) {
		names.insert(name.get<std::string>());
	}
	return names;
}

std::set<std::string> setOf(std::span<const std::string_view> names)
{
	return { names.begin(), names.end() };
}

Tables builtShipped()
{
	auto tables = buildTables(shippedTables());
	REQUIRE(tables);
	return std::move(*tables);
}

json readJson(const std::filesystem::path& path)
{
	std::ifstream stream(path);
	return json::parse(stream);
}

// The default file of `table` with `change` merged in.
json changedDefault(const std::string& table, const json& change)
{
	auto content = readJson(kData / table / "_default.json");
	content.merge_patch(change);
	return content;
}

}

TEST_CASE("each shipped table folder loads only its default file", "[tables][data]")
{
	for (const auto table : { Table::Stats, Table::Skills, Table::Actions, Table::Incompatible }) {
		const auto result = loadShipped(table);

		REQUIRE(result.diagnostics.size() == 1);
		CHECK(result.diagnostics[0].kind == Diagnostic::Kind::Loaded);
		CHECK(result.diagnostics[0].file == "_default.json");
	}
}

TEST_CASE("the shipped tables build without errors", "[tables][data]")
{
	const auto tables = buildTables(shippedTables());

	if (!tables) {
		for (const auto& error : tables.error().errors) {
			UNSCOPED_INFO(error.file << " " << error.key << ": " << error.message);
		}
	}
	REQUIRE(tables);
	CHECK(tables->dropped.empty());
	CHECK(tables->ignoredRaceAdjustments.empty());
}

TEST_CASE("the shipped tables validate against their schemas", "[tables][data][schema]")
{
	SchemaValidator validator(kData / "schema");
	for (const auto* table : { "stats", "skills", "actions", "incompatible" }) {
		const auto errors = validator.validate(readJson(kData / table / "_default.json"), std::string(table) + ".schema.json");

		INFO(table);
		CHECK(errors == std::vector<std::string>{});
	}
}

TEST_CASE("the schemas reject what the entry validators reject", "[tables][data][schema]")
{
	SchemaValidator validator(kData / "schema");
	const auto      rejects = [&](const std::string& table, const json& change) {
		return !validator.validate(changedDefault(table, change), table + ".schema.json").empty();
	};

	CHECK(rejects("stats", { { "LUC", { { "base", 0 } } } }));
	CHECK(rejects("stats", { { "STR", { { "base", 1.5 } } } }));
	CHECK(rejects("stats", { { "STR", { { "bsae", 10 } } } }));
	CHECK(rejects("stats", { { "STR", { { "effects", { { "CarryCapacity", { { "named", "MagicDamage" } } } } } } } }));
	CHECK(rejects("stats", { { "DEX", { { "effects", { { "CritChanceMagic", { { "named", "Flying" } } } } } } } }));
	CHECK(rejects("stats", { { "STR", { { "effects", { { "CarryCapacity", { { "curve", { { "curve", "cubic" } } } } } } } } } }));
	CHECK(rejects("skills", { { "Sneak", { { "primaryShare", 1.5 } } } }));
	CHECK(rejects("skills", { { "Swimming", { { "primary", "STR" }, { "secondary", "DEX" }, { "primaryShare", 1 } } } }));
	CHECK(rejects("actions", { { "Fishing", json::object() } }));
	CHECK(rejects("actions", { { "Sneak", { { "check", { { "result", nullptr } } } } } }));
	CHECK(rejects("incompatible", { { "Requiem", { { "plugins", json::array() } } } }));
	CHECK(rejects("incompatible", { { "Requiem", { { "plugins", { "" } } } } }));
	CHECK(rejects("stats", { { "STR", { { "raceAdjustments", { { "NordRace", 0.5 } } } } } }));
	CHECK(rejects("stats", { { "DEX", { { "effects", { { "CritChanceMagic", { { "input", "total" } } } } } } } }));
	CHECK(rejects("stats", { { "STR", { { "effects", { { "CarryCapacity", { { "input", "absolute" } } } } } } } }));

	CHECK_FALSE(rejects("stats", { { "Luck:LUC", { { "base", 0 } } } }));
	CHECK_FALSE(rejects("skills", { { "CSF:Swimming", { { "primary", "STR" }, { "secondary", "DEX" }, { "primaryShare", 1 } } } }));
	CHECK_FALSE(rejects("actions", { { "Fish:Catch", json::object() } }));
	CHECK_FALSE(rejects("stats", { { "STR", { { "base", 10.0 }, { "raceAdjustments", { { "NordRace", 1 } } } } } }));
	CHECK_FALSE(rejects("stats", { { "DEX", { { "effects", { { "CritChanceMagic", { { "input", "absolute" } } } } } } } }));
}

TEST_CASE("the validator throws on a schema keyword it does not know, also inside anyOf", "[tables][schema]")
{
	SchemaValidator validator(kData / "schema");
	validator.addDocument("top.json", { { "format", "date" } });
	validator.addDocument("nested.json", { { "anyOf", { { { "type", "string" } }, { { "format", "date" } } } } });

	CHECK_THROWS_AS(validator.validate("x", "top.json"), std::logic_error);
	CHECK_THROWS_AS(validator.validate(1, "nested.json"), std::logic_error);
}

TEST_CASE("the schemas and buildTables ship the same names", "[tables][data][schema]")
{
	SchemaValidator validator(kData / "schema");
	const auto&     common = validator.document("common.schema.json");
	const auto&     stats = validator.document("stats.schema.json");
	const auto&     actions = validator.document("actions.schema.json");

	std::set<std::string> coreStats;
	for (const auto& id : tom::stats::StatRegistry{}.activeStats()) {
		coreStats.insert(id.name);
	}
	CHECK(enumNames(common, "/$defs/coreStat/enum") == coreStats);
	CHECK(enumNames(common, "/$defs/vanillaSkill/enum") == setOf(tom::stats::vanillaSkills()));
	CHECK(enumNames(actions, "/propertyNames/anyOf/0/enum") == setOf(tom::stats::builtInActions()));
	CHECK(enumNames(stats, "/$defs/namedEffect/properties/named/enum") == setOf(tom::stats::namedEffectNames()));
}

TEST_CASE("the shipped skills hold the default mapping of ST-27", "[tables][data]")
{
	const std::map<std::string, std::pair<std::string, std::string>> expected{
		{ "OneHanded", { "STR", "DEX" } },
		{ "TwoHanded", { "STR", "END" } },
		{ "Block", { "END", "STR" } },
		{ "HeavyArmor", { "END", "CON" } },
		{ "LightArmor", { "DEX", "END" } },
		{ "Archery", { "DEX", "PER" } },
		{ "Sneak", { "DEX", "PER" } },
		{ "Lockpicking", { "DEX", "PER" } },
		{ "Pickpocket", { "DEX", "PER" } },
		{ "Alchemy", { "INT", "WIS" } },
		{ "Enchanting", { "INT", "WIS" } },
		{ "Destruction", { "WIS", "INT" } },
		{ "Conjuration", { "WIS", "INT" } },
		{ "Alteration", { "INT", "WIS" } },
		{ "Illusion", { "WIS", "PER" } },
		{ "Restoration", { "WIS", "CON" } },
		{ "Speech", { "CHA", "WIS" } },
		{ "Smithing", { "STR", "INT" } },
	};

	const auto tables = builtShipped();

	std::map<std::string, std::pair<std::string, std::string>> actual;
	for (const auto& [name, skill] : tables.skills) {
		actual.emplace(name, std::pair{ skill.primary.name, skill.secondary.name });
	}
	CHECK(actual == expected);
}

TEST_CASE("the shipped stats hold the default groups of ST-27", "[tables][data]")
{
	const std::map<std::string, std::set<std::string>> expected{
		{ "Warrior", { "STR", "CON", "END" } },
		{ "Mage", { "INT", "WIS" } },
		{ "Thief", { "DEX", "PER" } },
		{ "Other", { "CHA" } },
	};

	const auto tables = builtShipped();

	std::map<std::string, std::set<std::string>> actual;
	for (const auto& [id, stat] : tables.stats) {
		actual[stat.group].insert(id.name);
	}
	CHECK(actual == expected);
}

TEST_CASE("the shipped pools come from CON, END and INT (ST-10)", "[tables][data]")
{
	const auto tables = builtShipped();

	const auto drives = [&](const std::string& stat, const std::string& actorValue) {
		return std::ranges::any_of(tables.stats.at(StatId{ stat }).effects, [&](const auto& entry) {
			return entry.second.target == std::variant<ActorValueTarget, tom::stats::NamedEffect>(ActorValueTarget{ actorValue });
		});
	};
	CHECK(drives("CON", "Health"));
	CHECK(drives("END", "Stamina"));
	CHECK(drives("INT", "Magicka"));
}

TEST_CASE("the shipped actions give the ST-15 example XP", "[tables][data]")
{
	const auto tables = builtShipped();

	const auto& swing = tables.actions.at("WeaponSwing").training.at(StatId{ "STR" });
	CHECK(evaluate(swing, 2.0) == Approx(1.0).epsilon(0.01));
	CHECK(evaluate(swing, 100.0) == Approx(10.0).epsilon(0.01));
	const auto& trade = tables.actions.at("Trade").training.at(StatId{ "CHA" });
	CHECK(evaluate(trade, 100.0) == Approx(1.0));
	CHECK(evaluate(trade, 10000.0) == Approx(10.0));
	CHECK(evaluate(tables.actions.at("Interaction").training.at(StatId{ "CHA" }), 0.0) == 1.0);
}

TEST_CASE("every shipped core stat has the default base 10 (ST-3)", "[tables][data]")
{
	const auto tables = builtShipped();

	for (const auto& [id, stat] : tables.stats) {
		INFO(id.name);
		CHECK(stat.defaultBase == 10);
		CHECK(stat.raceAdjustments.empty());
	}
}

TEST_CASE("the shipped incompatible table has one entry per mod PRD 7.9 detects at load", "[tables][data]")
{
	const auto tables = builtShipped();

	std::set<std::string> names;
	for (const auto& [name, entry] : tables.incompatible) {
		names.insert(name);
	}
	CHECK(names == std::set<std::string>{ "Requiem", "Skyrim Attributes Mod.", "CARE", "Alternate Stat System", "SkyRem Grace",
					   "Roleplaying in Skyrim - Stats", "Skyrim Skill Uncapper", "Incremental Injuries",
					   "Exhaustion - Incremental Fatigue", "Valhalla Combat" });
}

TEST_CASE("the shipped placeholders are marked with their open question", "[tables][data]")
{
	const auto tables = builtShipped();

	std::set<std::string> markers;
	for (const auto& placeholder : tables.placeholders) {
		markers.insert(placeholder.note.substr(0, placeholder.note.find(')') + 1));
	}
	CHECK(markers == std::set<std::string>{ "TODO(OQ-2)", "TODO(OQ-3)", "TODO(OQ-4)", "TODO(OQ-5)", "TODO(OQ-6)", "TODO(OQ-7)", "TODO(OQ-9)",
						 "TODO(OQ-10)" });
}
