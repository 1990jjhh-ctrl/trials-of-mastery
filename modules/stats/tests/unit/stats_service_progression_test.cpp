// The StatsService progression: r, N, advancing and ascending.
#include "tom/stats/core/stats_service.h"

#include "stats_service_fixture.h"

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/stat_value.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <vector>

using tom::stats::ActorStats;
using tom::stats::AvDelta;
using tom::stats::ContributionLedger;
using tom::stats::initStat;
using tom::stats::StatReading;
using tom::stats::StatsService;

using namespace service_test;

namespace
{
StatsService serviceWithPlayer()
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "ImperialRace"));
	return stats;
}
}

TEST_CASE("r and N start at the settings' values", "[stats_service][progression]")
{
	const StatsService stats = service(tom::stats::Settings{ .costGrowth = 1.5, .advanceInterval = 4, .freePointsPerLevelUp = 2 });

	CHECK(stats.r() == 1.5);
	CHECK(stats.n() == 4);
	CHECK(stats.freePointsPerLevelUp() == 2);
}

TEST_CASE("a new r sets the cost of the next points", "[stats_service][progression]")
{
	StatsService stats = serviceWithPlayer();

	REQUIRE(stats.setR(1.5));

	CHECK(stats.r() == 1.5);
	// 100 + 150 buys two points at r = 1.5.
	REQUIRE(stats.addXp(kPlayer, kStr, 250.0));
	CHECK(stats.read(kPlayer, kStr)->active == 2);
}

TEST_CASE("an r that ST-6 rejects keeps the previous value and reports why", "[stats_service][progression]")
{
	StatsService stats = serviceWithPlayer();
	REQUIRE(stats.setR(1.25));

	for (const double r : { 1.0, 0.5, -2.0, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN() }) {
		const auto result = stats.setR(r);
		REQUIRE_FALSE(result);
		CHECK_FALSE(result.error().message.empty());
		CHECK(stats.r() == 1.25);
	}
}

TEST_CASE("N must be at least 1", "[stats_service][progression]")
{
	StatsService stats = service();

	CHECK_FALSE(stats.setN(0));
	CHECK_FALSE(stats.setN(-5));
	CHECK(stats.n() == 5);
	REQUIRE(stats.setN(1));
	CHECK(stats.n() == 1);
}

TEST_CASE("advancing moves the active points and resets the cost", "[stats_service][progression]")
{
	StatsService stats = serviceWithPlayer();
	REQUIRE(stats.addXp(kPlayer, kCon, 350.0));

	REQUIRE(stats.advance(kPlayer, kCon));

	CHECK(stats.read(kPlayer, kCon) == StatReading{ .base = 10, .accumulated = 2, .effective = 12.0 });
	REQUIRE(stats.addXp(kPlayer, kCon, 100.0));
	CHECK(stats.read(kPlayer, kCon)->active == 1);
	CHECK_FALSE(stats.advance(kNpc, kCon));
	CHECK_FALSE(stats.advance(kPlayer, kLuck));
}

TEST_CASE("advancing all stats leaves dormant stats alone", "[stats_service][progression][dormant]")
{
	StatsService stats = service();
	stats.addDormantStat(kLuck);
	ActorStats saved;
	initStat(saved, kLuck, 5);
	saved.values[kLuck].active = 3;
	(void)stats.restoreActor(kPlayer, "ImperialRace", saved, ContributionLedger{});
	REQUIRE(stats.addXp(kPlayer, kStr, 100.0));
	REQUIRE(stats.addXp(kPlayer, kEnd, 300.0));

	REQUIRE(stats.advanceAll(kPlayer));

	CHECK(stats.read(kPlayer, kStr)->accumulated == 1);
	CHECK(stats.read(kPlayer, kEnd)->accumulated == 2);
	REQUIRE(stats.registerStat(kLuck));
	CHECK(stats.read(kPlayer, kLuck)->active == 3);
	CHECK_FALSE(stats.advanceAll(kNpc));
}

TEST_CASE("ascending converts the points into base, rounded down", "[stats_service][progression]")
{
	StatsService stats = serviceWithPlayer();
	REQUIRE(stats.setValues(kPlayer, kCon, tom::stats::StatValue{ .base = 10, .accumulated = 2, .active = 1, .xp = 40.0 }));

	// 3 points convert to 2.91, which gives 2.
	const auto deltas = stats.ascend(kPlayer, kCon, [](std::int32_t points) { return points * 0.97; });

	REQUIRE(deltas);
	CHECK(*deltas == std::vector<AvDelta>{ { .actorValue = "Health", .delta = above(kHealthCurve, 2.0) - above(kHealthCurve, 3.0) } });
	CHECK(stats.read(kPlayer, kCon) == StatReading{ .base = 12, .effective = 12.0 });
}

TEST_CASE("an ascend formula without a finite result changes nothing", "[stats_service][progression]")
{
	StatsService stats = serviceWithPlayer();
	REQUIRE(stats.setValues(kPlayer, kCon, tom::stats::StatValue{ .base = 10, .accumulated = 2 }));

	const auto infinite = [](std::int32_t) { return std::numeric_limits<double>::infinity(); };

	CHECK_FALSE(stats.ascend(kPlayer, kCon, infinite));
	CHECK(stats.read(kPlayer, kCon) == StatReading{ .base = 10, .accumulated = 2, .effective = 12.0 });
}
