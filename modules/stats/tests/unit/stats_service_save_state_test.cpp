// The StatsService accessors for a save (Stats plan, C8): actor snapshots.
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
using tom::stats::initStat;
using tom::stats::kRaceAdjustmentKey;
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
