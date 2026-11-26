// The StatsService accessors for a save (Stats plan, C8): actor snapshots,
// pending points and the reset of the save state.
#include "tom/stats/core/stats_service.h"

#include "stats_service_fixture.h"

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/stat_value.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

using tom::stats::ActorSnapshot;
using tom::stats::ActorStats;
using tom::stats::ContributionLedger;
using tom::stats::CritKind;
using tom::stats::FreePoints;
using tom::stats::initStat;
using tom::stats::kRaceAdjustmentKey;
using tom::stats::Settings;
using tom::stats::StatsService;
using tom::stats::StatValue;

using namespace service_test;

TEST_CASE("the actor IDs are the known actors in ascending order", "[stats_service][save]")
{
	StatsService stats = service();
	CHECK(stats.actorIds().empty());
	REQUIRE(stats.addActor(kNpc, "ImperialRace"));
	REQUIRE(stats.addActor(kPlayer, "NordRace"));

	CHECK(stats.actorIds() == std::vector<ActorId>{ kPlayer, kNpc });
}

TEST_CASE("an actor snapshot holds the race, the stats and the contributions", "[stats_service][save]")
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "NordRace"));
	REQUIRE(stats.setBaseAdjustment(kPlayer, kCon, "Mod:Class", -1));
	REQUIRE(stats.setModifier(kPlayer, kEnd, "Mod:Buff", 3));

	const auto snapshot = stats.actorSnapshot(kPlayer);

	REQUIRE(snapshot);
	CHECK(snapshot->race == "NordRace");
	CHECK(snapshot->stats.values.at(kCon) == StatValue{ .base = 11 });
	CHECK(snapshot->stats.baseAdjustments.at(kCon) ==
		  std::map<std::string, std::int32_t>{ { std::string(kRaceAdjustmentKey), 2 }, { "Mod:Class", -1 } });
	CHECK(snapshot->stats.defaultBases.at(kCon) == 10);
	CHECK(snapshot->stats.modifiers.at(kEnd).at("Mod:Buff") == 3);
	CHECK(snapshot->ledger.contributions.at("Health") == above(kHealthCurve, 1.0));
	CHECK(snapshot->ledger.contributions.at("Stamina") == above(kStaminaCurve, 3.0));
}

TEST_CASE("an actor snapshot keeps the values of a dormant stat", "[stats_service][save][dormant]")
{
	StatsService stats = service();
	stats.addDormantStat(kLuck);
	ActorStats saved;
	initStat(saved, kLuck, 5);
	saved.values[kLuck].accumulated = 4;
	(void)stats.restoreActor(kPlayer, "ImperialRace", saved, {});

	const auto snapshot = stats.actorSnapshot(kPlayer);

	REQUIRE(snapshot);
	CHECK(snapshot->stats.values.at(kLuck) == StatValue{ .base = 5, .accumulated = 4 });
	CHECK(snapshot->stats.defaultBases.at(kLuck) == 5);
}

TEST_CASE("an actor snapshot is taken back by restoreActor unchanged", "[stats_service][save]")
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "NordRace"));
	REQUIRE(stats.setMultiplier(kPlayer, 1.5));
	const ActorSnapshot snapshot = *stats.actorSnapshot(kPlayer);

	StatsService other = service();
	CHECK(other.restoreActor(kPlayer, snapshot.race, snapshot.stats, snapshot.ledger).empty());
	CHECK(other.actorSnapshot(kPlayer) == snapshot);
}

TEST_CASE("the snapshot of an unknown actor fails", "[stats_service][save]")
{
	const StatsService stats = service();

	CHECK_FALSE(stats.actorSnapshot(kPlayer));
}

TEST_CASE("saved pending points are put back", "[stats_service][save]")
{
	StatsService stats = service();

	REQUIRE(stats.restorePendingPoints(7));
	CHECK(stats.pendingPoints() == 7);
	REQUIRE(stats.restorePendingPoints(0));
	CHECK(stats.pendingPoints() == 0);
}

TEST_CASE("a negative count of pending points is rejected", "[stats_service][save]")
{
	StatsService stats = service();
	REQUIRE(stats.restorePendingPoints(2));

	CHECK_FALSE(stats.restorePendingPoints(-1));
	CHECK(stats.pendingPoints() == 2);
}

TEST_CASE("resetting the save state forgets actors, pending points and dormant stats", "[stats_service][save]")
{
	StatsService stats = service(Settings{ .costGrowth = 3.0 });
	REQUIRE(stats.addActor(kPlayer, "NordRace"));
	REQUIRE(stats.addActor(kNpc, "ImperialRace"));
	stats.onPlayerLevelUp(2);
	stats.addDormantStat(StatId{ "Mod:Dormant" });
	REQUIRE(stats.setR(1.5));

	stats.resetSaveState();

	CHECK(stats.actorIds().empty());
	CHECK_FALSE(stats.hasActor(kPlayer));
	CHECK(stats.pendingPoints() == 0);
	CHECK_FALSE(stats.registry().contains(StatId{ "Mod:Dormant" }));
	CHECK(stats.r() == 3.0);
}

TEST_CASE("resetting the save state keeps the session state", "[stats_service][save]")
{
	StatsService stats = service();
	REQUIRE(stats.registerStat(kLuck, FreePoints::OptedOut));
	REQUIRE(stats.setN(3));
	REQUIRE(stats.claimAdvancing("Mastery"));
	REQUIRE(stats.claimPointAssignment("Mastery"));
	const auto shift = [](const ActorStats&, CritKind, double) { return 2.0; };
	REQUIRE(stats.setCritShift("Luck", shift));

	stats.resetSaveState();

	CHECK(stats.n() == 3);
	CHECK(stats.advancingClaimed());
	CHECK(stats.pointAssignmentClaimed());
	CHECK(stats.registry().registeredStats() == std::vector<StatId>{ kLuck });
	CHECK_FALSE(stats.registry().isDormant(kLuck));
	CHECK_FALSE(stats.registry().getsFreePoints(kLuck));
	// An actor added after the reset gets the active registered stat and the
	// crit shift.
	REQUIRE(stats.addActor(kPlayer, "ImperialRace"));
	CHECK(stats.read(kPlayer, kLuck)->base == 5);
	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 5.0 + 2.0);
}
