#include "tom/stats/core/actor_stats.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using Catch::Matchers::WithinRel;
using tom::stats::ActorStats;
using tom::stats::effective;
using tom::stats::StatId;
using tom::stats::StatValue;

namespace
{
const StatId kStr{ "STR" };
const StatId kDex{ "DEX" };
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
