#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/stat_value.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdint>
#include <limits>

using Catch::Matchers::WithinRel;
using tom::stats::ActorStats;
using tom::stats::applyDefaultBase;
using tom::stats::ascend;
using tom::stats::effective;
using tom::stats::initStat;
using tom::stats::removeBaseAdjustment;
using tom::stats::setBaseAdjustment;
using tom::stats::StatId;
using tom::stats::StatValue;

namespace
{
const StatId kStr{ "STR" };
const StatId kDex{ "DEX" };

// The base and start value of STR, and the pools above the start value.
struct Snapshot
{
	std::int32_t base = 0;
	std::int32_t start = 0;
	double       aboveStart = 0.0;
};

Snapshot snapshotStr(const ActorStats& actor)
{
	const std::int32_t start = actor.startValues.at(kStr);
	return Snapshot{
		.base = actor.values.at(kStr).base,
		.start = start,
		.aboveStart = effective(actor, kStr) - start,
	};
}
}

TEST_CASE("a new actor has multiplier 1 and no values", "[effective]")
{
	const ActorStats actor;

	CHECK(actor.multiplier == 1.0);
	CHECK(effective(actor, kStr) == 0.0);
}

TEST_CASE("the effective stat sums base, accumulated and active points", "[effective]")
{
	ActorStats actor;
	actor.values[kStr] = StatValue{ .base = 10, .accumulated = 3, .active = 4, .xp = 50.0 };

	CHECK(effective(actor, kStr) == 17.0);
}

TEST_CASE("the multiplier scales the points but not the modifier", "[effective]")
{
	ActorStats actor;
	actor.values[kStr] = StatValue{ .base = 10, .accumulated = 3, .active = 4, .xp = 0.0 };
	actor.multiplier = 1.5;
	actor.modifiers[kStr]["gear"] = 5;

	// (10 + 3 + 4) * 1.5 + 5
	CHECK_THAT(effective(actor, kStr), WithinRel(30.5));
}

TEST_CASE("modifiers under several keys add up and one key can be removed", "[effective]")
{
	ActorStats actor;
	actor.values[kStr] = StatValue{ .base = 10, .accumulated = 0, .active = 0, .xp = 0.0 };
	actor.modifiers[kStr]["gear"] = 5;
	actor.modifiers[kStr]["disease"] = -2;
	actor.modifiers[kStr]["otherMod"] = 4;

	CHECK(effective(actor, kStr) == 17.0);

	actor.modifiers[kStr].erase("disease");

	CHECK(effective(actor, kStr) == 19.0);
}

TEST_CASE("a modifier on one stat does not change another", "[effective]")
{
	ActorStats actor;
	actor.values[kStr] = StatValue{ .base = 10, .accumulated = 0, .active = 0, .xp = 0.0 };
	actor.values[kDex] = StatValue{ .base = 8, .accumulated = 0, .active = 0, .xp = 0.0 };
	actor.modifiers[kStr]["gear"] = 5;

	CHECK(effective(actor, kDex) == 8.0);
}

TEST_CASE("the effective stat is not clamped at 100", "[effective]")
{
	ActorStats actor;
	actor.values[kStr] = StatValue{ .base = 150, .accumulated = 30, .active = 7, .xp = 0.0 };
	actor.multiplier = 2.0;
	actor.modifiers[kStr]["gear"] = 20;

	CHECK(effective(actor, kStr) == 394.0);
}

TEST_CASE("the sum of the points does not overflow the integer range", "[effective]")
{
	ActorStats actor;
	actor.values[kStr] = StatValue{ .base = 2'000'000'000, .accumulated = 2'000'000'000, .active = 0, .xp = 0.0 };
	actor.modifiers[kStr]["a"] = 2'000'000'000;
	actor.modifiers[kStr]["b"] = 2'000'000'000;

	CHECK(effective(actor, kStr) == 8e9);
}

TEST_CASE("a new stat starts at its default base", "[base]")
{
	ActorStats actor;

	initStat(actor, kStr, 10);

	CHECK(actor.values.at(kStr).base == 10);
	CHECK(actor.startValues.at(kStr) == 10);
	CHECK(actor.defaultBases.at(kStr) == 10);
}

TEST_CASE("a race +1 and a class -2 on a default base of 10 give a base of 9", "[base]")
{
	SECTION("adjustments set before the stat is initialised")
	{
		ActorStats actor;
		setBaseAdjustment(actor, kStr, "race", 1);
		setBaseAdjustment(actor, kStr, "class", -2);

		initStat(actor, kStr, 10);

		CHECK(actor.values.at(kStr).base == 9);
		CHECK(actor.startValues.at(kStr) == 9);
	}
	SECTION("adjustments set after the stat is initialised")
	{
		ActorStats actor;
		initStat(actor, kStr, 10);

		setBaseAdjustment(actor, kStr, "race", 1);
		setBaseAdjustment(actor, kStr, "class", -2);

		CHECK(actor.values.at(kStr).base == 9);
		CHECK(actor.startValues.at(kStr) == 9);
	}
}

TEST_CASE("adding, changing and removing an adjustment moves base and start value together", "[base]")
{
	ActorStats actor;
	initStat(actor, kStr, 10);
	setBaseAdjustment(actor, kStr, "race", 1);
	actor.values[kStr].accumulated = 4;
	actor.values[kStr].active = 3;
	const Snapshot before = snapshotStr(actor);
	REQUIRE(before.base == 11);
	REQUIRE(before.aboveStart == 7.0);

	setBaseAdjustment(actor, kStr, "class", -2);

	Snapshot now = snapshotStr(actor);
	CHECK(now.base == 9);
	CHECK(now.start == 9);
	CHECK(now.aboveStart == before.aboveStart);

	setBaseAdjustment(actor, kStr, "class", 3);

	now = snapshotStr(actor);
	CHECK(now.base == 14);
	CHECK(now.start == 14);
	CHECK(now.aboveStart == before.aboveStart);

	removeBaseAdjustment(actor, kStr, "class");

	now = snapshotStr(actor);
	CHECK(now.base == 11);
	CHECK(now.start == 11);
	CHECK(now.aboveStart == before.aboveStart);
	CHECK(actor.baseAdjustments.at(kStr).size() == 1);
}

TEST_CASE("removing an adjustment keeps the ascended points in the base", "[base]")
{
	ActorStats actor;
	initStat(actor, kStr, 10);
	setBaseAdjustment(actor, kStr, "race", 1);
	setBaseAdjustment(actor, kStr, "class", -2);
	actor.values[kStr].accumulated = 6;
	const bool ascended = ascend(actor.values[kStr], [](std::int32_t points) { return points / 2.0; });
	REQUIRE(ascended);
	REQUIRE(actor.values.at(kStr).base == 12);

	removeBaseAdjustment(actor, kStr, "class");

	// 3 ascended points stay; the start value is the default base plus race.
	CHECK(actor.values.at(kStr).base == 14);
	CHECK(actor.startValues.at(kStr) == 11);
}

TEST_CASE("removing an adjustment that is not set changes nothing", "[base]")
{
	ActorStats actor;
	initStat(actor, kStr, 10);
	setBaseAdjustment(actor, kStr, "race", 1);

	removeBaseAdjustment(actor, kStr, "class");
	removeBaseAdjustment(actor, kDex, "race");

	CHECK(actor.values.at(kStr).base == 11);
	CHECK(actor.startValues.at(kStr) == 11);
	CHECK_FALSE(actor.values.contains(kDex));
	CHECK_FALSE(actor.baseAdjustments.contains(kDex));
}

TEST_CASE("an adjustment on one stat does not change another", "[base]")
{
	ActorStats actor;
	initStat(actor, kStr, 10);
	initStat(actor, kDex, 10);

	setBaseAdjustment(actor, kStr, "race", 2);

	CHECK(actor.values.at(kDex).base == 10);
	CHECK(actor.startValues.at(kDex) == 10);
}

TEST_CASE("a changed default base moves base and start value by the difference", "[base]")
{
	ActorStats actor;
	initStat(actor, kStr, 10);
	setBaseAdjustment(actor, kStr, "race", 1);
	actor.values[kStr].accumulated = 5;
	const Snapshot before = snapshotStr(actor);

	applyDefaultBase(actor, kStr, 12);

	Snapshot now = snapshotStr(actor);
	CHECK(now.base == before.base + 2);
	CHECK(now.start == before.start + 2);
	CHECK(now.aboveStart == before.aboveStart);
	CHECK(actor.defaultBases.at(kStr) == 12);

	applyDefaultBase(actor, kStr, 12);

	now = snapshotStr(actor);
	CHECK(now.base == before.base + 2);
	CHECK(now.start == before.start + 2);
	CHECK(actor.defaultBases.at(kStr) == 12);
}

TEST_CASE("a default base applied without a previous one moves by the whole value", "[base]")
{
	ActorStats actor;
	setBaseAdjustment(actor, kStr, "race", 1);

	applyDefaultBase(actor, kStr, 10);

	CHECK(actor.values.at(kStr).base == 11);
	CHECK(actor.startValues.at(kStr) == 11);
	CHECK(actor.defaultBases.at(kStr) == 10);
}

TEST_CASE("base adjustments saturate at the integer range", "[base]")
{
	constexpr std::int32_t kMax = std::numeric_limits<std::int32_t>::max();
	constexpr std::int32_t kMin = std::numeric_limits<std::int32_t>::min();
	ActorStats             actor;
	initStat(actor, kStr, 10);

	setBaseAdjustment(actor, kStr, "a", kMax);
	setBaseAdjustment(actor, kStr, "b", kMax);

	CHECK(actor.values.at(kStr).base == kMax);
	CHECK(actor.startValues.at(kStr) == kMax);

	setBaseAdjustment(actor, kStr, "a", kMin);
	setBaseAdjustment(actor, kStr, "b", kMin);

	CHECK(actor.values.at(kStr).base == kMin);
	CHECK(actor.startValues.at(kStr) == kMin);
}
