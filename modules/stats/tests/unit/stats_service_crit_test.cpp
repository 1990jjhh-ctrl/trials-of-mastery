// The StatsService crit chance and crit shifts.
#include "tom/stats/core/stats_service.h"

#include "stats_service_fixture.h"

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/effects.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using tom::stats::ActorStats;
using tom::stats::CritKind;
using tom::stats::initStat;
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

double shiftByOne(const ActorStats&, CritKind, double)
{
	return 1.0;
}
}

TEST_CASE("the base crit chance comes from the active stats", "[stats_service][crit]")
{
	StatsService stats = serviceWithPlayer();

	// The shipped crit effects are absolute: 0.5 per DEX point.
	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 5.0);
	CHECK(stats.critChance(kPlayer, CritKind::Magic) == 0.0);
	REQUIRE(stats.setModifier(kPlayer, kDex, "Mod:Ring", 2));
	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 6.0);
	CHECK_FALSE(stats.critChance(kNpc, CritKind::Physical));
}

TEST_CASE("crit shifts from two owners add up and see the base chance and kind", "[stats_service][crit]")
{
	StatsService        stats = serviceWithPlayer();
	std::vector<double> seenBase;
	CritKind            seenKind = CritKind::Magic;

	const auto luck = [&](const ActorStats&, CritKind kind, double base) {
		seenBase.push_back(base);
		seenKind = kind;
		return 2.0;
	};
	const auto other = [](const ActorStats&, CritKind, double base) { return base * 0.1; };

	REQUIRE(stats.setCritShift("Luck", luck));
	REQUIRE(stats.setCritShift("Mod:Other", other));

	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 5.0 + 2.0 + 0.5);
	CHECK(seenBase == std::vector<double>{ 5.0 });
	CHECK(seenKind == CritKind::Physical);

	stats.removeCritShift("Mod:Other");
	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 7.0);
	stats.removeCritShift("Mod:Unknown");
	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 7.0);
}

TEST_CASE("a crit shift needs an owner and a function", "[stats_service][crit]")
{
	StatsService stats = serviceWithPlayer();

	CHECK_FALSE(stats.setCritShift("", shiftByOne));
	CHECK_FALSE(stats.setCritShift("Luck", tom::stats::CritShift{}));
	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 5.0);
}

TEST_CASE("a crit shift that removes itself changes only later calls", "[stats_service][crit]")
{
	StatsService stats = serviceWithPlayer();
	const auto   removeBoth = [&stats](const ActorStats&, CritKind, double) {
		stats.removeCritShift("Luck");
		stats.removeCritShift("Mod:Other");
		return 2.0;
	};
	REQUIRE(stats.setCritShift("Mod:Other", shiftByOne));
	REQUIRE(stats.setCritShift("Luck", removeBoth));

	// "Luck" runs first and removes both; this call still has both.
	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 5.0 + 2.0 + 1.0);
	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 5.0);
}

TEST_CASE("a crit shift that sets another shift changes only later calls", "[stats_service][crit]")
{
	StatsService stats = serviceWithPlayer();
	const auto   addOther = [&stats](const ActorStats&, CritKind, double) {
		(void)stats.setCritShift("Mod:Added", shiftByOne);
		return 2.0;
	};
	REQUIRE(stats.setCritShift("Luck", addOther));

	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 5.0 + 2.0);
	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 5.0 + 2.0 + 1.0);
}

TEST_CASE("a dormant stat adds no crit chance", "[stats_service][crit][dormant]")
{
	tom::stats::Tables table = tables();
	table.stats[kLuck].effects["CritChancePhysical"] = tom::stats::Effect{ .target = tom::stats::NamedEffect::CritChancePhysical,
		.curve = kCritCurve,
		.input = tom::stats::CurveInput::Absolute };
	StatsService stats({}, table, kPlayer);
	stats.addDormantStat(kLuck);
	ActorStats saved;
	initStat(saved, kLuck, 5);
	(void)stats.restoreActor(kPlayer, "ImperialRace", saved, {});

	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 5.0);
	REQUIRE(stats.registerStat(kLuck));
	CHECK(stats.critChance(kPlayer, CritKind::Physical) == 7.5);
}
