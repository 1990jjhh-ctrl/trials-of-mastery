#include "tom/stats/core/config/configuration.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using nlohmann::json;
using tom::stats::Configuration;
using tom::stats::describe;
using tom::stats::loadConfiguration;
using tom::stats::LogLevel;
using tom::stats::LogLine;
using tom::stats::Placeholder;
using tom::stats::PluginInfo;
using tom::stats::PluginQuery;
using tom::stats::RegisteredEntries;
using tom::stats::Settings;
using tom::stats::StatId;

namespace
{

const std::filesystem::path kData{ TOM_STATS_DATA_DIR };
const std::string           kIniName = "trials-of-mastery-stats.ini";

class FakePlugins final : public PluginQuery
{
public:
	explicit FakePlugins(std::vector<PluginInfo> active = {}) :
		active_(std::move(active)) {}

	[[nodiscard]] std::vector<PluginInfo> activePlugins() const override { return active_; }

private:
	std::vector<PluginInfo> active_;
};

// A copy of the shipped INI and tables in a fresh folder under the system temp
// directory, removed again at the end: <folder>/<INI> and <folder>/tables/.
class DataCopy
{
public:
	DataCopy() :
		path_(std::filesystem::temp_directory_path() / ("tom-configuration-" + std::to_string(std::random_device{}())))
	{
		std::filesystem::create_directories(tables());
		std::filesystem::copy_file(kData / kIniName, ini());
		for (const auto* table : { "stats", "skills", "actions", "incompatible" }) {
			std::filesystem::copy(kData / table, tables() / table, std::filesystem::copy_options::recursive);
		}
	}
	DataCopy(const DataCopy&) = delete;
	DataCopy& operator=(const DataCopy&) = delete;
	~DataCopy() { std::filesystem::remove_all(path_); }

	[[nodiscard]] std::filesystem::path ini() const { return path_ / kIniName; }
	[[nodiscard]] std::filesystem::path tables() const { return path_ / "tables"; }

	void write(const std::filesystem::path& relative, const std::string& text) const
	{
		std::filesystem::create_directories((path_ / relative).parent_path());
		std::ofstream(path_ / relative) << text;
	}

private:
	std::filesystem::path path_;
};

Configuration load(const DataCopy& data, const PluginQuery& plugins = FakePlugins{},
	const RegisteredEntries& registered = {})
{
	return loadConfiguration(data.ini(), data.tables(), plugins, {}, registered);
}

bool hasLine(const std::vector<LogLine>& lines, LogLevel level, const std::string& text)
{
	return std::ranges::find(lines, LogLine{ level, text }) != lines.end();
}

std::size_t countStartingWith(const std::vector<LogLine>& lines, LogLevel level, std::string_view prefix)
{
	return static_cast<std::size_t>(std::ranges::count_if(lines,
		[&](const LogLine& line) { return line.level == level && line.text.starts_with(prefix); }));
}

bool namesOpenQuestion(const Placeholder& placeholder)
{
	return placeholder.note.starts_with("TODO(OQ-");
}

}

TEST_CASE("the shipped configuration loads without warnings and logs its placeholders", "[configuration]")
{
	const DataCopy data;

	const auto configuration = load(data);
	const auto lines = describe(configuration);

	CHECK(configuration.iniFound);
	CHECK(configuration.settings.errors.empty());
	CHECK(configuration.settings.settings == Settings{});
	REQUIRE(configuration.tables);
	for (const auto* table : { "stats", "skills", "actions", "incompatible" }) {
		CHECK(hasLine(lines, LogLevel::Info, std::string(table) + ": loaded _default.json"));
	}
	CHECK(countStartingWith(lines, LogLevel::Warning, "") == 0);
	CHECK(countStartingWith(lines, LogLevel::Error, "") == 0);
	const auto placeholders = countStartingWith(lines, LogLevel::Info, "Placeholder in use: ");
	CHECK(placeholders == configuration.tables->placeholders.size());
	CHECK(placeholders > 0);
	CHECK(std::ranges::all_of(configuration.tables->placeholders, namesOpenQuestion));
}

TEST_CASE("without an INI file the built-in defaults are in use", "[configuration]")
{
	const DataCopy data;
	std::filesystem::remove(data.ini());

	const auto configuration = load(data);

	CHECK_FALSE(configuration.iniFound);
	CHECK(configuration.settings.errors.empty());
	CHECK(configuration.settings.settings == Settings{});
	CHECK(countStartingWith(describe(configuration), LogLevel::Info, "INI: ") == 1);
}

TEST_CASE("a rejected INI value is logged as a warning", "[configuration]")
{
	const DataCopy data;
	data.write(kIniName, "[Progression]\nCostGrowth = 1\n");

	const auto configuration = load(data);

	CHECK(configuration.iniFound);
	CHECK(configuration.settings.settings.costGrowth == Settings{}.costGrowth);
	CHECK(countStartingWith(describe(configuration), LogLevel::Warning, "INI: Progression.CostGrowth: ") == 1);
}

TEST_CASE("the log lists file order, conflicts, same-name warnings and skipped files", "[configuration]")
{
	const DataCopy data;
	data.write("tables/stats/a.json", R"({"STR": {"base": 11}})");
	data.write("tables/stats/b.json", R"({"STR": {"base": 12}})");
	data.write("tables/stats/broken.json", R"({"STR": )");
	data.write("tables/stats/plugins/Inactive.json", R"({"DEX": {"base": 13}})");
	data.write("tables/stats/plugins/Foo.json", R"({"CON": {"base": 14}})");
	const FakePlugins plugins{ { { "Foo", ".esm" }, { "Foo", ".esp" } } };

	const auto configuration = load(data, plugins);
	const auto lines = describe(configuration);

	const std::vector<std::string> loaded{ "stats: loaded _default.json", "stats: loaded a.json", "stats: loaded b.json",
		"stats: loaded plugins/Foo.json" };
	std::vector<std::string>       loadedInLog;
	for (const auto& line : lines) {
		if (line.text.starts_with("stats: loaded ")) {
			loadedInLog.push_back(line.text);
		}
	}
	CHECK(loadedInLog == loaded);
	CHECK(hasLine(lines, LogLevel::Warning, "stats: conflict at /STR/base: b.json overrides a.json"));
	CHECK(hasLine(lines, LogLevel::Warning,
		"stats: several active plugins share the name of plugins/Foo.json (Foo.esm, Foo.esp); it loads at the last of them"));
	CHECK(hasLine(lines, LogLevel::Info, "stats: skipped plugins/Inactive.json: its plugin is not active"));
	CHECK(countStartingWith(lines, LogLevel::Warning, "stats: skipped broken.json: ") == 1);
	REQUIRE(configuration.tables);
	CHECK(configuration.tables->stats.at(StatId{ "STR" }).defaultBase == 12);
	CHECK(configuration.tables->stats.at(StatId{ "DEX" }).defaultBase == 10);
	CHECK(configuration.tables->stats.at(StatId{ "CON" }).defaultBase == 14);
}

TEST_CASE("a file with an invalid entry is skipped with its entry in the log", "[configuration]")
{
	const DataCopy data;
	data.write("tables/stats/bad.json", R"({"STR": {"base": 0.5}})");

	const auto lines = describe(load(data));

	CHECK(countStartingWith(lines, LogLevel::Warning, "stats: skipped bad.json: entry /STR: ") == 1);
}

TEST_CASE("the registered entries are the lowest layer of their table", "[configuration]")
{
	const DataCopy    data;
	RegisteredEntries registered;
	registered.stats = json::parse(R"({"Luck:LUC": {"base": 7}, "STR": {"base": 99}})");

	const auto configuration = load(data, FakePlugins{}, registered);

	REQUIRE(configuration.tables);
	CHECK(configuration.tables->stats.at(StatId{ "Luck:LUC" }).defaultBase == 7);
	CHECK(configuration.tables->stats.at(StatId{ "STR" }).defaultBase == 10);
}

TEST_CASE("tables that cannot be built are logged as errors", "[configuration]")
{
	const DataCopy data;
	data.write("tables/stats/remove.json", R"({"STR": null})");

	const auto configuration = load(data);

	CHECK_FALSE(configuration.tables);
	CHECK(countStartingWith(describe(configuration), LogLevel::Error, "Tables not built: ") >= 1);
}

TEST_CASE("a race adjustment for a race no plugin defines is logged at debug level", "[configuration]")
{
	const DataCopy data;
	data.write("tables/stats/races.json", R"({"STR": {"raceAdjustments": {"NordRace": 1, "ModRace": 2}}})");

	const auto configuration = loadConfiguration(data.ini(), data.tables(), FakePlugins{}, { "NordRace" });
	const auto lines = describe(configuration);

	REQUIRE(configuration.tables);
	CHECK(configuration.tables->stats.at(StatId{ "STR" }).raceAdjustments.size() == 1);
	CHECK(countStartingWith(lines, LogLevel::Debug, "Ignored ") == 1);
	CHECK(countStartingWith(lines, LogLevel::Warning, "") == 0);
}
