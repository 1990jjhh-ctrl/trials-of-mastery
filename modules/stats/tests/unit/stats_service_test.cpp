// The StatsService facade: actors, stats, reads and the effects of the active
// stats.
#include "tom/stats/core/stats_service.h"

#include "stats_service_fixture.h"

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/stat_registry.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using tom::stats::ActorStats;
using tom::stats::AvDelta;
using tom::stats::AvDeltasByActor;
using tom::stats::ContributionLedger;
using tom::stats::FreePoints;
using tom::stats::initStat;
using tom::stats::StatReading;
using tom::stats::StatsService;

using namespace service_test;

TEST_CASE("a new actor starts at the default bases with no contribution", "[stats_service]")
{
	StatsService stats = service();

	const auto deltas = stats.addActor(kNpc, "ImperialRace");

	REQUIRE(deltas);
	CHECK(deltas->empty());
	CHECK(stats.hasActor(kNpc));
	CHECK(stats.read(kNpc, kCon) == StatReading{ .base = 10, .effective = 10.0 });
	CHECK(stats.learningBonus(kNpc) == 0.0);
}

TEST_CASE("a race adjustment counts from the actor's creation on", "[stats_service][base]")
{
	StatsService stats = service();

	const auto deltas = stats.addActor(kPlayer, "NordRace");

	REQUIRE(deltas);
	CHECK(*deltas == std::vector<AvDelta>{ { .actorValue = "Health", .delta = above(kHealthCurve, 2.0) } });
	CHECK(stats.read(kPlayer, kCon) == StatReading{ .base = 12, .effective = 12.0 });
	CHECK(stats.read(kPlayer, kEnd)->base == 10);
}

TEST_CASE("an actor is added once", "[stats_service]")
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kNpc, "NordRace"));

	CHECK_FALSE(stats.addActor(kNpc, "NordRace"));
	CHECK(stats.read(kNpc, kCon)->base == 12);
}

TEST_CASE("reads of an unknown actor or stat fail", "[stats_service]")
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kNpc, "NordRace"));

	CHECK_FALSE(stats.read(kPlayer, kCon));
	CHECK_FALSE(stats.read(kNpc, StatId{ "Mod:Unknown" }));
	// Known to the stats table, but not registered.
	CHECK_FALSE(stats.read(kNpc, kLuck));
	CHECK_FALSE(stats.learningBonus(kPlayer));
	CHECK_FALSE(stats.damageRedirectionShare(kPlayer));
}

TEST_CASE("a stat registered mid-game starts at its default base", "[stats_service][registered]")
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "BretonRace"));
	REQUIRE(stats.addActor(kNpc, "ImperialRace"));

	const auto deltas = stats.registerStat(kLuck);

	REQUIRE(deltas);
	// At the default base the relative effects are 0.
	CHECK(deltas->empty());
	CHECK(stats.read(kPlayer, kLuck) == StatReading{ .base = 5, .effective = 5.0 });
	CHECK(stats.read(kNpc, kLuck) == StatReading{ .base = 5, .effective = 5.0 });
	CHECK(stats.learningBonus(kPlayer) == 0.0);
	// An actor added later gets it too.
	REQUIRE(stats.addActor(0x2000, "ImperialRace"));
	CHECK(stats.read(0x2000, kLuck)->base == 5);
}

TEST_CASE("a stat registers only with a stats table entry and only once", "[stats_service][registered]")
{
	StatsService stats = service();

	CHECK_FALSE(stats.registerStat(StatId{ "Mod:NoEntry" }));
	CHECK_FALSE(stats.registry().contains(StatId{ "Mod:NoEntry" }));
	CHECK_FALSE(stats.registerStat(kCon));
	REQUIRE(stats.registerStat(kLuck, FreePoints::OptedOut));
	CHECK_FALSE(stats.registerStat(kLuck));
	CHECK_FALSE(stats.registry().getsFreePoints(kLuck));
}

TEST_CASE("a dormant stat keeps its values but has no effects", "[stats_service][dormant]")
{
	StatsService stats = service();
	stats.addDormantStat(kLuck);
	ActorStats saved;
	initStat(saved, kLuck, 5);
	saved.values[kLuck].accumulated = 4;
	ContributionLedger ledger;
	ledger.contributions["Magicka"] = above(kMagickaCurve, 4.0);

	// The saved Magicka contribution of the dormant stat is taken off.
	CHECK(stats.restoreActor(kPlayer, "ImperialRace", saved, ledger) ==
		  std::vector<AvDelta>{ { .actorValue = "Magicka", .delta = -above(kMagickaCurve, 4.0) } });
	CHECK_FALSE(stats.read(kPlayer, kLuck));
	CHECK(stats.learningBonus(kPlayer) == 0.0);

	// Registering it again brings the values and the effects back.
	const auto deltas = stats.registerStat(kLuck);
	REQUIRE(deltas);
	CHECK(*deltas == AvDeltasByActor{ { kPlayer, { { .actorValue = "Magicka", .delta = above(kMagickaCurve, 4.0) } } } });
	CHECK(stats.read(kPlayer, kLuck)->accumulated == 4);
	CHECK(stats.learningBonus(kPlayer) == above(kLearningCurve, 4.0));
}

TEST_CASE("a restored actor gets the stats registered since the save", "[stats_service][registered]")
{
	StatsService stats = service();
	REQUIRE(stats.registerStat(kLuck));
	ActorStats saved;
	initStat(saved, kCon, 10);
	saved.values[kCon].accumulated = 3;
	ContributionLedger ledger;
	ledger.contributions["Health"] = above(kHealthCurve, 3.0);

	CHECK(stats.restoreActor(kPlayer, "ImperialRace", saved, ledger).empty());
	CHECK(stats.read(kPlayer, kCon) == StatReading{ .base = 10, .accumulated = 3, .effective = 13.0 });
	CHECK(stats.read(kPlayer, kLuck) == StatReading{ .base = 5, .effective = 5.0 });
	CHECK(stats.read(kPlayer, kStr)->base == 10);
}

TEST_CASE("a restored actor's race applies to the stats registered since the save", "[stats_service][registered][race]")
{
	StatsService stats = service();
	REQUIRE(stats.registerStat(kLuck));
	ActorStats saved;
	initStat(saved, kCon, 10);

	// Mod:Luck gets the Nord +1; the saved CON keeps what the save holds
	// until setRace() renews it.
	CHECK(stats.restoreActor(kPlayer, "NordRace", saved, {}) == std::vector<AvDelta>{ { .actorValue = "Magicka", .delta = above(kMagickaCurve, 1.0) } });
	CHECK(stats.read(kPlayer, kLuck) == StatReading{ .base = 6, .effective = 6.0 });
	CHECK(stats.read(kPlayer, kCon)->base == 10);
}

TEST_CASE("a stat registered after an actor of an adjusted race gets the race's adjustment", "[stats_service][registered][race]")
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "NordRace"));
	REQUIRE(stats.addActor(kNpc, "ImperialRace"));

	const auto deltas = stats.registerStat(kLuck);

	REQUIRE(deltas);
	// At multiplier 1 the relative effects are curve(1) - curve(0).
	CHECK(*deltas == AvDeltasByActor{ { kPlayer, { { .actorValue = "Magicka", .delta = above(kMagickaCurve, 1.0) } } } });
	CHECK(stats.read(kPlayer, kLuck) == StatReading{ .base = 6, .effective = 6.0 });
	CHECK(stats.learningBonus(kPlayer) == above(kLearningCurve, 1.0));
	// The Imperial race has no entry, so no adjustment.
	CHECK(stats.read(kNpc, kLuck) == StatReading{ .base = 5, .effective = 5.0 });
}

TEST_CASE("a dormant stat that comes back gets the actor's current race", "[stats_service][dormant][race]")
{
	StatsService stats = service();
	stats.addDormantStat(kLuck);
	ActorStats saved;
	initStat(saved, kLuck, 5);
	(void)stats.restoreActor(kPlayer, "ImperialRace", saved, {});
	REQUIRE(stats.setRace(kPlayer, "NordRace"));

	REQUIRE(stats.registerStat(kLuck));

	CHECK(stats.read(kPlayer, kLuck)->base == 6);
}

TEST_CASE("a new race moves the bases and effects by the difference", "[stats_service][race]")
{
	StatsService stats = service();
	REQUIRE(stats.registerStat(kLuck));
	REQUIRE(stats.addActor(kPlayer, "NordRace"));

	// CON +2 becomes -1, Mod:Luck +1 goes away.
	CHECK(stats.setRace(kPlayer, "BretonRace") == std::vector<AvDelta>{
													  { .actorValue = "Health", .delta = above(kHealthCurve, -1.0) - above(kHealthCurve, 2.0) },
													  { .actorValue = "Magicka", .delta = -above(kMagickaCurve, 1.0) },
												  });
	CHECK(stats.read(kPlayer, kCon)->base == 9);
	CHECK(stats.read(kPlayer, kLuck)->base == 5);

	// A race without an entry takes every adjustment off.
	CHECK(stats.setRace(kPlayer, "ImperialRace") == std::vector<AvDelta>{ { .actorValue = "Health", .delta = -above(kHealthCurve, -1.0) } });
	CHECK(stats.read(kPlayer, kCon)->base == 10);
	// Setting the same race again changes nothing.
	CHECK(stats.setRace(kPlayer, "ImperialRace") == std::vector<AvDelta>{});
	CHECK_FALSE(stats.setRace(kNpc, "NordRace"));
}

TEST_CASE("invalid settings fall back to the built-in defaults", "[stats_service]")
{
	const StatsService         stats = service(tom::stats::Settings{ .costGrowth = 1.0, .advanceInterval = 0, .freePointsPerLevelUp = -2 });
	const tom::stats::Settings defaults;

	CHECK(stats.r() == defaults.costGrowth);
	CHECK(stats.n() == defaults.advanceInterval);
	CHECK(stats.freePointsPerLevelUp() == defaults.freePointsPerLevelUp);
}
