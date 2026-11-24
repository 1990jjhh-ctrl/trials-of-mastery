// loadSettings: the INI settings of ST-27 and the gamepad button of ST-24.
#include "tom/stats/core/config/settings.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using tom::stats::ConfigError;
using tom::stats::loadSettings;
using tom::stats::Settings;

namespace
{

std::vector<std::string> keysOf(const std::vector<ConfigError>& errors)
{
	std::vector<std::string> keys;
	for (const auto& error : errors) {
		keys.push_back(error.key);
	}
	return keys;
}

std::string readFile(const std::filesystem::path& path)
{
	std::ifstream stream(path, std::ios::binary);
	return { std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
}

}

TEST_CASE("an empty INI gives the built-in defaults", "[settings]")
{
	const auto result = loadSettings("");

	CHECK(result.errors.empty());
	CHECK(result.settings == Settings{});
	CHECK(result.settings.costGrowth == 2.0);
	CHECK(result.settings.advanceInterval == 5);
	CHECK(result.settings.freePointsPerLevelUp == 3);
	CHECK_FALSE(result.settings.allowIncompatibleMods);
}

TEST_CASE("the INI values replace the defaults", "[settings]")
{
	const auto result = loadSettings(R"(
[Progression]
CostGrowth = 1.25
AdvanceInterval = 7
FreePointsPerLevelUp = 4

[StatBox]
Key = 48
GamepadButton = 276

[Compatibility]
AllowIncompatibleMods = true
)");

	CHECK(result.errors.empty());
	CHECK(result.settings.costGrowth == 1.25);
	CHECK(result.settings.advanceInterval == 7);
	CHECK(result.settings.freePointsPerLevelUp == 4);
	CHECK(result.settings.statBoxKey == 48);
	CHECK(result.settings.statBoxGamepadButton == 276);
	CHECK(result.settings.allowIncompatibleMods);
}

TEST_CASE("missing keys keep their defaults", "[settings]")
{
	const auto result = loadSettings("[Progression]\nAdvanceInterval = 2\n");

	CHECK(result.errors.empty());
	auto expected = Settings{};
	expected.advanceInterval = 2;
	CHECK(result.settings == expected);
}

TEST_CASE("an r of 1 or less is rejected and the default stays", "[settings]")
{
	for (const char* text : { "1", "1.0", "0.5", "-3" }) {
		const auto result = loadSettings(std::string("[Progression]\nCostGrowth = ") + text + "\n");

		CHECK(result.settings.costGrowth == Settings{}.costGrowth);
		CHECK(keysOf(result.errors) == std::vector<std::string>{ "Progression.CostGrowth" });
	}
}

TEST_CASE("values that are not valid are rejected and the defaults stay", "[settings]")
{
	const auto result = loadSettings(R"(
[Progression]
CostGrowth = fast
AdvanceInterval = 0
FreePointsPerLevelUp = -1

[StatBox]
Key = 300
GamepadButton = 48

[Compatibility]
AllowIncompatibleMods = maybe
)");

	CHECK(result.settings == Settings{});
	CHECK(keysOf(result.errors) == std::vector<std::string>{ "Progression.CostGrowth", "Progression.AdvanceInterval",
									   "Progression.FreePointsPerLevelUp", "StatBox.Key", "StatBox.GamepadButton",
									   "Compatibility.AllowIncompatibleMods" });
}

TEST_CASE("numbers must fill the whole value", "[settings]")
{
	const auto result = loadSettings("[Progression]\nAdvanceInterval = 5 levels\nCostGrowth = 1.5x\nFreePointsPerLevelUp = 3.0\n");

	CHECK(result.settings == Settings{});
	CHECK(result.errors.size() == 3);
}

TEST_CASE("r must be finite", "[settings]")
{
	const auto result = loadSettings("[Progression]\nCostGrowth = inf\n");

	CHECK(result.settings.costGrowth == Settings{}.costGrowth);
	CHECK(result.errors.size() == 1);
}

TEST_CASE("an error names the value it rejected and the value kept", "[settings]")
{
	const auto result = loadSettings("[Progression]\nAdvanceInterval = 0\n");

	REQUIRE(result.errors.size() == 1);
	CHECK(result.errors[0].file.empty());
	CHECK(result.errors[0].message == "'0' must be an integer from 1 to 2147483647; keeping 5");
}

TEST_CASE("sections, keys and booleans are case-insensitive", "[settings]")
{
	const auto result = loadSettings("[compatibility]\nallowincompatiblemods = TRUE\n");

	CHECK(result.errors.empty());
	CHECK(result.settings.allowIncompatibleMods);
}

TEST_CASE("the shipped INI loads without errors and matches the built-in defaults", "[settings][data]")
{
	const auto text = readFile(std::filesystem::path(TOM_STATS_DATA_DIR) / "trials-of-mastery-stats.ini");
	REQUIRE_FALSE(text.empty());

	const auto result = loadSettings(text);

	CHECK(result.errors.empty());
	CHECK(result.settings == Settings{});
}
