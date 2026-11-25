// The StatsService level-up: free points, auto-advancing and the claims.
#include "tom/stats/core/stats_service.h"

#include "stats_service_fixture.h"

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/stat_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using tom::stats::ActorStats;
using tom::stats::AvDelta;
using tom::stats::ContributionLedger;
using tom::stats::FreePoints;
using tom::stats::initStat;
using tom::stats::StatReading;
using tom::stats::StatsService;

using namespace service_test;

namespace
{
// The player with 1 active point in STR, which advancing would move.
StatsService serviceWithTrainedPlayer()
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "ImperialRace"));
	REQUIRE(stats.addXp(kPlayer, kStr, 100.0));
	return stats;
}

bool strAdvanced(const StatsService& stats)
{
	return stats.read(kPlayer, kStr)->accumulated == 1;
}
}

TEST_CASE("free points go to accumulated and do not change the cost", "[stats_service][level_up]")
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "ImperialRace"));

	stats.onPlayerLevelUp(2);
	REQUIRE(stats.pendingPoints() == 3);
	CHECK(stats.assignFreePoint(kCon) == std::vector<AvDelta>{ { .actorValue = "Health", .delta = above(kHealthCurve, 1.0) } });
	REQUIRE(stats.assignFreePoint(kCon));
	REQUIRE(stats.assignFreePoint(kCon));

	CHECK(stats.pendingPoints() == 0);
	CHECK(stats.read(kPlayer, kCon) == StatReading{ .base = 10, .accumulated = 3, .effective = 13.0 });
	// The next trained point still costs 100.
	REQUIRE(stats.addXp(kPlayer, kCon, 100.0));
	CHECK(stats.read(kPlayer, kCon)->active == 1);
}

TEST_CASE("each real level-up grants the points of one level", "[stats_service][level_up]")
{
	StatsService stats = serviceWithTrainedPlayer();

	stats.onPlayerLevelUp(2);
	stats.onPlayerLevelUp(3);
	stats.onPlayerLevelUp(4);

	CHECK(stats.pendingPoints() == 9);
	CHECK_FALSE(strAdvanced(stats));
}

TEST_CASE("a bypassed level change counts neither for points nor for advancing", "[stats_service][level_up]")
{
	StatsService stats = serviceWithTrainedPlayer();
	stats.onPlayerLevelUp(4);

	SECTION("a jump from 4 to 10 grants one level's points and advances because 10 is a multiple of N")
	{
		stats.onPlayerLevelUp(10);

		CHECK(stats.pendingPoints() == 6);
		CHECK(strAdvanced(stats));
	}

	SECTION("a bypassed change to 10 without a call advances nothing")
	{
		// setlevel 10 calls nothing; the next real level-up reaches 11.
		CHECK_FALSE(strAdvanced(stats));
		stats.onPlayerLevelUp(11);

		CHECK(stats.pendingPoints() == 6);
		CHECK_FALSE(strAdvanced(stats));
	}
}

TEST_CASE("the service advances at multiples of N until advancing is claimed", "[stats_service][level_up]")
{
	StatsService stats = serviceWithTrainedPlayer();

	stats.onPlayerLevelUp(5);
	REQUIRE(strAdvanced(stats));

	REQUIRE(stats.addXp(kPlayer, kStr, 100.0));
	CHECK_FALSE(stats.claimAdvancing(""));
	CHECK_FALSE(stats.advancingClaimed());
	REQUIRE(stats.claimAdvancing("Mastery"));
	CHECK(stats.advancingClaimed());
	stats.onPlayerLevelUp(10);

	CHECK(stats.read(kPlayer, kStr) == StatReading{ .base = 10, .accumulated = 1, .active = 1, .effective = 12.0 });
	CHECK(stats.pendingPoints() == 6);
	// The claiming module advances itself.
	REQUIRE(stats.advanceAll(kPlayer));
	CHECK(stats.read(kPlayer, kStr)->accumulated == 2);
}

TEST_CASE("a new N changes the advance interval", "[stats_service][level_up]")
{
	StatsService stats = serviceWithTrainedPlayer();
	REQUIRE(stats.setN(3));

	stats.onPlayerLevelUp(5);
	CHECK_FALSE(strAdvanced(stats));

	stats.onPlayerLevelUp(6);
	CHECK(strAdvanced(stats));
}

TEST_CASE("unspent points stay pending", "[stats_service][level_up]")
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "ImperialRace"));
	CHECK_FALSE(stats.assignFreePoint(kCon));

	stats.onPlayerLevelUp(2);
	REQUIRE(stats.assignFreePoint(kStr));
	stats.onPlayerLevelUp(3);

	CHECK(stats.pendingPoints() == 5);
}

TEST_CASE("a level-up before the player exists still grants its points", "[stats_service][level_up]")
{
	StatsService stats = service();

	stats.onPlayerLevelUp(5);

	CHECK(stats.pendingPoints() == 3);
	CHECK_FALSE(stats.assignFreePoint(kCon));
	CHECK(stats.pendingPoints() == 3);
}

TEST_CASE("a stat opted out of free points gets none but still trains", "[stats_service][level_up][registered]")
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "ImperialRace"));
	REQUIRE(stats.registerStat(kLuck, FreePoints::OptedOut));
	stats.onPlayerLevelUp(2);

	CHECK_FALSE(stats.assignFreePoint(kLuck));
	REQUIRE(stats.claimPointAssignment("Professions"));
	CHECK_FALSE(stats.assignFreePoint(kLuck));

	CHECK(stats.pendingPoints() == 3);
	CHECK(stats.read(kPlayer, kLuck)->accumulated == 0);
	REQUIRE(stats.addXp(kPlayer, kLuck, 100.0));
	CHECK(stats.read(kPlayer, kLuck)->active == 1);
}

TEST_CASE("a dormant stat gets no free points", "[stats_service][level_up][dormant]")
{
	StatsService stats = service();
	stats.addDormantStat(kLuck);
	ActorStats saved;
	initStat(saved, kLuck, 5);
	(void)stats.restoreActor(kPlayer, "ImperialRace", saved, ContributionLedger{});
	stats.onPlayerLevelUp(2);

	// The registry counts it as getting free points; the service checks
	// that it is dormant.
	REQUIRE(stats.registry().getsFreePoints(kLuck));
	CHECK_FALSE(stats.assignFreePoint(kLuck));
	CHECK(stats.pendingPoints() == 3);
}

TEST_CASE("point assignment can be claimed", "[stats_service][level_up]")
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "ImperialRace"));
	stats.onPlayerLevelUp(2);

	CHECK_FALSE(stats.claimPointAssignment(""));
	CHECK_FALSE(stats.pointAssignmentClaimed());
	REQUIRE(stats.claimPointAssignment("Professions"));
	CHECK(stats.pointAssignmentClaimed());

	// The claiming module assigns the points through the same call.
	REQUIRE(stats.assignFreePoint(kStr));
	CHECK(stats.pendingPoints() == 2);
}
