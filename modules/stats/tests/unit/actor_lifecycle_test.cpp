// The actor steps of a new game, a load and a race change (Stats plan, G2).
#include "tom/stats/core/actor_lifecycle.h"

#include "stats_service_fixture.h"

#include "tom/stats/core/effects.h"
#include "tom/stats/core/persistence.h"
#include "tom/stats/core/stats_service.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <vector>

using tom::stats::ActorIdResolver;
using tom::stats::AvDelta;
using tom::stats::AvDeltasByActor;
using tom::stats::deserialize;
using tom::stats::ensureActor;
using tom::stats::renewAfterLoad;
using tom::stats::restore;
using tom::stats::serialize;
using tom::stats::Settings;
using tom::stats::StatsService;
using tom::stats::Tables;

using namespace service_test;

namespace
{
// Keeps every saved actor ID, as a load with an unchanged load order does.
class SameIds final : public ActorIdResolver
{
public:
	std::optional<ActorId> resolve(ActorId saved) override { return saved; }
};

// What a load of `saved` into a service built from `tables` restores.
StatsService load(const StatsService& saved, const Tables& tables)
{
	StatsService loaded(Settings{}, tables, kPlayer);
	SameIds      resolver;
	auto         state = deserialize(serialize(saved), resolver);
	REQUIRE(state);
	REQUIRE(restore(loaded, std::move(*state)).deltas.empty());
	return loaded;
}
}

TEST_CASE("ensureActor adds an unknown actor with its race's adjustments", "[lifecycle]")
{
	StatsService stats = service();

	CHECK(ensureActor(stats, kPlayer, "NordRace") == std::vector<AvDelta>{ { .actorValue = "Health", .delta = above(kHealthCurve, 2.0) } });
	CHECK(stats.actorSnapshot(kPlayer)->race == "NordRace");
	CHECK(stats.read(kPlayer, kCon)->base == 12);
	CHECK(stats.read(kPlayer, kStr)->base == 10);
}

TEST_CASE("ensureActor sets the race of a known actor", "[lifecycle][race]")
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "NordRace"));

	CHECK(ensureActor(stats, kPlayer, "BretonRace") == std::vector<AvDelta>{
														   { .actorValue = "Health", .delta = above(kHealthCurve, -1.0) - above(kHealthCurve, 2.0) },
													   });
	CHECK(stats.actorSnapshot(kPlayer)->race == "BretonRace");
	CHECK(stats.read(kPlayer, kCon)->base == 9);
	// The same race again changes nothing.
	CHECK(ensureActor(stats, kPlayer, "BretonRace").empty());
}

TEST_CASE("after a load, a patched race adjustment moves the base by the difference", "[lifecycle][race]")
{
	StatsService saved = service();
	REQUIRE(saved.addActor(kPlayer, "NordRace"));
	REQUIRE(saved.setBaseAdjustment(kPlayer, kCon, "Mod:Class", -1));
	// A patch raised the Nord adjustment of CON from +2 to +3.
	Tables patched = tables();
	patched.stats[kCon].raceAdjustments["NordRace"] = 3;
	StatsService stats = load(saved, patched);
	REQUIRE(stats.read(kPlayer, kCon)->base == 11);

	CHECK(renewAfterLoad(stats, { { kPlayer, "NordRace" } }) ==
		  AvDeltasByActor{ { kPlayer, { { .actorValue = "Health", .delta = above(kHealthCurve, 2.0) - above(kHealthCurve, 1.0) } } } });
	CHECK(stats.read(kPlayer, kCon)->base == 12);
	CHECK(stats.actorSnapshot(kPlayer)->stats.baseAdjustments.at(kCon).at("Mod:Class") == -1);
}

TEST_CASE("after a load, an actor that changed race gets the new race's adjustments", "[lifecycle][race]")
{
	StatsService saved = service();
	REQUIRE(saved.addActor(kPlayer, "NordRace"));
	StatsService stats = load(saved, tables());

	CHECK(renewAfterLoad(stats, { { kPlayer, "BretonRace" } }) ==
		  AvDeltasByActor{ { kPlayer, { { .actorValue = "Health", .delta = above(kHealthCurve, -1.0) - above(kHealthCurve, 2.0) } } } });
	CHECK(stats.actorSnapshot(kPlayer)->race == "BretonRace");
	CHECK(stats.read(kPlayer, kCon)->base == 9);
}

TEST_CASE("after a load of a save without the player, the player is added", "[lifecycle]")
{
	// A save made without the plugin holds no stats, so nothing is restored.
	StatsService stats = service();

	CHECK(renewAfterLoad(stats, { { kPlayer, "NordRace" } }) ==
		  AvDeltasByActor{ { kPlayer, { { .actorValue = "Health", .delta = above(kHealthCurve, 2.0) } } } });
	CHECK(stats.actorIds() == std::vector<ActorId>{ kPlayer });
	CHECK(stats.read(kPlayer, kCon)->base == 12);
}

TEST_CASE("after a load, a known actor without a current race keeps its own", "[lifecycle][race]")
{
	StatsService saved = service();
	REQUIRE(saved.addActor(kPlayer, "ImperialRace"));
	REQUIRE(saved.addActor(kNpc, "NordRace"));
	Tables patched = tables();
	patched.stats[kCon].raceAdjustments["NordRace"] = 3;
	StatsService stats = load(saved, patched);

	// The game reported no race for the NPC, e.g. one whose form is gone.
	CHECK(renewAfterLoad(stats, { { kPlayer, "ImperialRace" } }).empty());
	CHECK(stats.actorSnapshot(kNpc)->race == "NordRace");
	CHECK(stats.read(kNpc, kCon)->base == 12);
}

TEST_CASE("after a load, a changed default base applies after the race", "[lifecycle][base]")
{
	StatsService saved = service();
	REQUIRE(saved.addActor(kPlayer, "NordRace"));
	// Away from multiplier 1, a default base change moves the effects
	// (assumption 13), so both steps give deltas.
	REQUIRE(saved.setMultiplier(kPlayer, 2.0));
	Tables patched = tables();
	patched.stats[kCon].raceAdjustments["NordRace"] = 3;
	patched.stats[kCon].defaultBase = 12;
	StatsService stats = load(saved, patched);

	// CON is (12 + 0 + 0) * 2 - 10 = 14 above its default base when saved,
	// 16 with the race's +3, and 18 with base and default base both 2 higher.
	CHECK(renewAfterLoad(stats, { { kPlayer, "NordRace" } }) ==
		  AvDeltasByActor{ { kPlayer, {
										  { .actorValue = "Health", .delta = above(kHealthCurve, 16.0) - above(kHealthCurve, 14.0) },
										  { .actorValue = "Health", .delta = above(kHealthCurve, 18.0) - above(kHealthCurve, 16.0) },
									  } } });
	CHECK(stats.read(kPlayer, kCon)->base == 15);
	CHECK(stats.actorSnapshot(kPlayer)->stats.defaultBases.at(kCon) == 12);
}

TEST_CASE("after a load at multiplier 1, a changed default base leaves the effects", "[lifecycle][base]")
{
	StatsService saved = service();
	REQUIRE(saved.addActor(kPlayer, "ImperialRace"));
	Tables patched = tables();
	patched.stats[kStr].defaultBase = 12;
	StatsService stats = load(saved, patched);
	const auto   ledgerBefore = stats.actorSnapshot(kPlayer)->ledger;

	CHECK(renewAfterLoad(stats, { { kPlayer, "ImperialRace" } }).empty());
	CHECK(stats.read(kPlayer, kStr)->base == 12);
	CHECK(stats.actorSnapshot(kPlayer)->ledger == ledgerBefore);
}
