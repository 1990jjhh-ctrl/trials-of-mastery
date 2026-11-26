#include "tom/stats/core/stat_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using tom::stats::FreePoints;
using tom::stats::StatId;
using tom::stats::StatRegistry;

TEST_CASE("the registry starts with the eight core stats in PRD order", "[registry]")
{
	const StatRegistry registry;

	const std::vector<StatId> expected{
		StatId{ "STR" }, StatId{ "DEX" }, StatId{ "CON" }, StatId{ "INT" },
		StatId{ "WIS" }, StatId{ "PER" }, StatId{ "END" }, StatId{ "CHA" }
	};
	REQUIRE(registry.activeStats() == expected);
	for (const auto& id : expected) {
		CHECK(registry.isCore(id));
		CHECK_FALSE(registry.isDormant(id));
	}
}

TEST_CASE("a registered stat is active after the core stats", "[registry]")
{
	StatRegistry registry;

	REQUIRE(registry.registerStat(StatId{ "LUC" }));

	CHECK(registry.contains(StatId{ "LUC" }));
	CHECK_FALSE(registry.isCore(StatId{ "LUC" }));
	CHECK_FALSE(registry.isDormant(StatId{ "LUC" }));
	REQUIRE(registry.activeStats().size() == 9);
	CHECK(registry.activeStats().back() == StatId{ "LUC" });
}

TEST_CASE("registering a known active stat is rejected", "[registry]")
{
	StatRegistry registry;
	REQUIRE(registry.registerStat(StatId{ "LUC" }));

	CHECK_FALSE(registry.registerStat(StatId{ "LUC" }));
	CHECK_FALSE(registry.registerStat(StatId{ "STR" }));
	CHECK(registry.activeStats().size() == 9);
}

TEST_CASE("a stat with an empty name is rejected", "[registry]")
{
	StatRegistry registry;

	CHECK_FALSE(registry.registerStat(StatId{ "" }));
	CHECK_FALSE(registry.contains(StatId{ "" }));
	CHECK(registry.activeStats().size() == 8);
}

TEST_CASE("a saved stat without its owner stays dormant until registered", "[registry]")
{
	StatRegistry registry;

	registry.addDormant(StatId{ "LUC" });

	CHECK(registry.contains(StatId{ "LUC" }));
	CHECK(registry.isDormant(StatId{ "LUC" }));
	CHECK(registry.activeStats().size() == 8);

	REQUIRE(registry.registerStat(StatId{ "LUC" }));

	CHECK_FALSE(registry.isDormant(StatId{ "LUC" }));
	CHECK(registry.activeStats().back() == StatId{ "LUC" });
}

TEST_CASE("adding a known stat as dormant changes nothing", "[registry]")
{
	StatRegistry registry;
	REQUIRE(registry.registerStat(StatId{ "LUC" }));

	registry.addDormant(StatId{ "LUC" });
	registry.addDormant(StatId{ "STR" });

	CHECK_FALSE(registry.isDormant(StatId{ "LUC" }));
	CHECK_FALSE(registry.isDormant(StatId{ "STR" }));
}

TEST_CASE("an unknown stat is not contained", "[registry]")
{
	const StatRegistry registry;

	CHECK_FALSE(registry.contains(StatId{ "LUC" }));
	CHECK_FALSE(registry.isCore(StatId{ "LUC" }));
	CHECK_FALSE(registry.isDormant(StatId{ "LUC" }));
}

TEST_CASE("core stats and registered stats get free points by default", "[registry]")
{
	StatRegistry registry;
	REQUIRE(registry.registerStat(StatId{ "FTH" }));

	for (const auto& id : registry.activeStats()) {
		CHECK(registry.getsFreePoints(id));
	}
}

TEST_CASE("a registered stat can opt out of free points", "[registry]")
{
	StatRegistry registry;

	REQUIRE(registry.registerStat(StatId{ "LUC" }, FreePoints::OptedOut));

	CHECK_FALSE(registry.getsFreePoints(StatId{ "LUC" }));
	CHECK(registry.getsFreePoints(StatId{ "STR" }));
}

TEST_CASE("a rejected registration keeps the free-point setting", "[registry]")
{
	StatRegistry registry;
	REQUIRE(registry.registerStat(StatId{ "LUC" }, FreePoints::OptedOut));

	CHECK_FALSE(registry.registerStat(StatId{ "LUC" }, FreePoints::Granted));
	CHECK_FALSE(registry.registerStat(StatId{ "STR" }, FreePoints::OptedOut));

	CHECK_FALSE(registry.getsFreePoints(StatId{ "LUC" }));
	CHECK(registry.getsFreePoints(StatId{ "STR" }));
}

TEST_CASE("a dormant stat takes the free-point setting of the registration that brings it back", "[registry]")
{
	StatRegistry registry;
	registry.addDormant(StatId{ "LUC" });

	REQUIRE(registry.registerStat(StatId{ "LUC" }, FreePoints::OptedOut));

	CHECK_FALSE(registry.getsFreePoints(StatId{ "LUC" }));
}

TEST_CASE("an unknown stat gets no free points", "[registry]")
{
	const StatRegistry registry;

	CHECK_FALSE(registry.getsFreePoints(StatId{ "LUC" }));
}

TEST_CASE("the registered stats include dormant ones and leave out the core stats", "[registry]")
{
	StatRegistry registry;
	REQUIRE(registry.registerStat(StatId{ "LUC" }));
	registry.addDormant(StatId{ "FTH" });
	REQUIRE(registry.registerStat(StatId{ "AGI" }, FreePoints::OptedOut));

	CHECK(registry.registeredStats() == std::vector<StatId>{ StatId{ "LUC" }, StatId{ "FTH" }, StatId{ "AGI" } });
}

TEST_CASE("removing the dormant stats keeps the active ones", "[registry]")
{
	StatRegistry registry;
	REQUIRE(registry.registerStat(StatId{ "LUC" }, FreePoints::OptedOut));
	registry.addDormant(StatId{ "FTH" });

	registry.removeDormant();

	CHECK_FALSE(registry.contains(StatId{ "FTH" }));
	CHECK(registry.registeredStats() == std::vector<StatId>{ StatId{ "LUC" } });
	CHECK_FALSE(registry.getsFreePoints(StatId{ "LUC" }));
	CHECK(registry.activeStats().size() == 9);
}
