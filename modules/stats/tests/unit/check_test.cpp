// Check values and check results of actions.
#include "tom/stats/core/check.h"

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/curve.h"
#include "tom/stats/core/stat_value.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using Catch::Matchers::WithinRel;
using tom::stats::ActionsTable;
using tom::stats::ActorStats;
using tom::stats::Check;
using tom::stats::checkResult;
using tom::stats::checkValue;
using tom::stats::LinearCurve;
using tom::stats::PowerCurve;
using tom::stats::SkillLevels;
using tom::stats::StatId;
using tom::stats::StatValue;

namespace
{

const StatId kDex{ "DEX" };
const StatId kCha{ "CHA" };
const StatId kStr{ "STR" };

ActionsTable actions()
{
	ActionsTable table;
	table["Intimidate"].check = Check{
		.stats = { { kCha, LinearCurve{ .a = 0.5, .b = 0.0 } }, { kStr, PowerCurve{ .alpha = 2.0, .lambda = 0.5 } } },
		.skills = { { "Speech", LinearCurve{ .a = 1.0, .b = 1.0 } } },
		.result = LinearCurve{ .a = 0.01, .b = 0.5 },
	};
	table["Lockpicking"].check = Check{
		.stats = { { kDex, LinearCurve{ .a = 0.25, .b = 0.0 } } },
		.skills = { { "Lockpicking", LinearCurve{ .a = 1.0, .b = 0.0 } } },
		.result = LinearCurve{ .a = 1.0, .b = 0.0 },
	};
	table["Hit"].training = { { kDex, LinearCurve{ .a = 0.0, .b = 0.5 } } };
	return table;
}

ActorStats actor()
{
	ActorStats stats;
	stats.values[kCha] = StatValue{ .base = 10, .accumulated = 2, .active = 0, .xp = 0.0 };
	stats.values[kStr] = StatValue{ .base = 16, .accumulated = 0, .active = 0, .xp = 0.0 };
	stats.values[kDex] = StatValue{ .base = 40, .accumulated = 0, .active = 0, .xp = 0.0 };
	return stats;
}

SkillLevels levels(std::map<std::string, double> known)
{
	return [known = std::move(known)](const std::string& skill) {
		const auto it = known.find(skill);
		return it == known.end() ? 0.0 : it->second;
	};
}

}

TEST_CASE("a check sums the stat terms and the skill terms", "[check]")
{
	const auto i = checkValue(actions(), "Intimidate", actor(), levels({ { "Speech", 30.0 } }));

	// 0.5 * 12 + 2 * sqrt(16) + (30 + 1)
	REQUIRE(i);
	CHECK_THAT(*i, WithinRel(45.0));
}

TEST_CASE("a check reads the effective stat, with multiplier and modifiers", "[check]")
{
	ActorStats stats = actor();
	stats.multiplier = 2.0;
	stats.modifiers[kDex]["gear"] = 5;

	const auto i = checkValue(actions(), "Lockpicking", stats, levels({ { "Lockpicking", 25.0 } }));

	// 0.25 * (40 * 2 + 5) + 25
	REQUIRE(i);
	CHECK_THAT(*i, WithinRel(46.25));
}

TEST_CASE("a check asks only for the skills it names", "[check]")
{
	std::vector<std::string> asked;
	const SkillLevels        recording = [&asked](const std::string& skill) {
		asked.push_back(skill);
		return 10.0;
	};

	const auto i = checkValue(actions(), "Lockpicking", actor(), recording);

	REQUIRE(i);
	CHECK(asked == std::vector<std::string>{ "Lockpicking" });
}

TEST_CASE("an unknown action and an action without a check have no check value", "[check]")
{
	const SkillLevels none = levels({});

	CHECK_FALSE(checkValue(actions(), "Other:Climb", actor(), none));
	CHECK_FALSE(checkValue(actions(), "Hit", actor(), none));
}

TEST_CASE("the check result maps I with the result curve", "[check]")
{
	const auto result = checkResult(actions(), "Intimidate", 45.0);

	// 0.01 * 45 + 0.5
	REQUIRE(result);
	CHECK_THAT(*result, WithinRel(0.95));
}

TEST_CASE("an unknown action and an action without a check have no check result", "[check]")
{
	CHECK_FALSE(checkResult(actions(), "Other:Climb", 1.0));
	CHECK_FALSE(checkResult(actions(), "Hit", 1.0));
}
