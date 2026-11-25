#include "tom/stats/core/effects.h"

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/curve.h"
#include "tom/stats/core/stat_value.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using Catch::Matchers::WithinRel;
using tom::stats::ActorStats;
using tom::stats::ActorValueTarget;
using tom::stats::ascend;
using tom::stats::AsymptoticCurve;
using tom::stats::avContribution;
using tom::stats::AvDelta;
using tom::stats::ContributionLedger;
using tom::stats::CurveInput;
using tom::stats::damageRedirectionShare;
using tom::stats::Effect;
using tom::stats::initStat;
using tom::stats::learningBonus;
using tom::stats::LinearCurve;
using tom::stats::NamedEffect;
using tom::stats::namedEffectValue;
using tom::stats::PowerCurve;
using tom::stats::RedirectSplit;
using tom::stats::setBaseAdjustment;
using tom::stats::splitDamage;
using tom::stats::StatEntry;
using tom::stats::StatId;
using tom::stats::StatsTable;
using tom::stats::updateContributions;

namespace
{
const StatId kCon{ "CON" };
const StatId kEnd{ "END" };

Effect actorValueEffect(const char* actorValue, double a)
{
	return Effect{ .target = ActorValueTarget{ actorValue }, .curve = LinearCurve{ .a = a, .b = 0.0 } };
}

// CON gives 5 Health and 0.02 HealRate per point, END 0.01 HealRate per point
// and a named effect, which is no actor value.
StatsTable poolTable()
{
	StatsTable table;
	table[kCon].effects["Health"] = actorValueEffect("Health", 5.0);
	table[kCon].effects["HealthRecovery"] = actorValueEffect("HealRate", 0.02);
	table[kEnd].effects["HealthRecovery"] = actorValueEffect("HealRate", 0.01);
	table[kEnd].effects["DamageRedirection"] = Effect{ .target = NamedEffect::DamageRedirection,
		.curve = AsymptoticCurve{ .start = 0.0, .end = 0.25, .c = 0.01 } };
	return table;
}

ActorStats newActor()
{
	ActorStats actor;
	initStat(actor, kCon, 10);
	initStat(actor, kEnd, 10);
	return actor;
}
}

TEST_CASE("a new character gets a zero contribution", "[effects][contribution]")
{
	// b and S give a curve that is not 0 at x = 0; only the difference counts.
	CHECK(avContribution(LinearCurve{ .a = 5.0, .b = 3.0 }, 10.0, 10.0) == 0.0);
	CHECK(avContribution(AsymptoticCurve{ .start = 2.0, .end = 6.0, .c = 0.1 }, 12.0, 12.0) == 0.0);

	ContributionLedger ledger;
	CHECK(updateContributions(ledger, newActor(), poolTable()).empty());
	CHECK(ledger.contributions.empty());
}

TEST_CASE("the contribution is curve(x) - curve(0) of the stat above its start value", "[effects][contribution]")
{
	CHECK(avContribution(LinearCurve{ .a = 5.0, .b = 3.0 }, 14.0, 10.0) == 20.0);
	CHECK(avContribution(PowerCurve{ .alpha = 2.0, .lambda = 0.5 }, 19.0, 10.0) == 6.0);
	// Below the start value the contribution is negative.
	CHECK(avContribution(LinearCurve{ .a = 5.0, .b = 3.0 }, 8.0, 10.0) == -10.0);
}

TEST_CASE("a stat change yields only the difference", "[effects][contribution]")
{
	const StatsTable   table = poolTable();
	ActorStats         actor = newActor();
	ContributionLedger ledger;

	actor.values[kCon].accumulated = 4;
	CHECK(updateContributions(ledger, actor, table) == std::vector<AvDelta>{ { .actorValue = "HealRate", .delta = 0.08 }, { .actorValue = "Health", .delta = 20.0 } });

	actor.values[kCon].active = 2;
	const auto deltas = updateContributions(ledger, actor, table);
	REQUIRE(deltas.size() == 2);
	CHECK(deltas[0].actorValue == "HealRate");
	CHECK_THAT(deltas[0].delta, WithinRel(0.04));
	CHECK(deltas[1] == AvDelta{ .actorValue = "Health", .delta = 10.0 });
	CHECK(ledger.contributions.at("Health") == 30.0);

	// Nothing changed, so nothing is applied.
	CHECK(updateContributions(ledger, actor, table).empty());
}

TEST_CASE("effects of several stats on one actor value add up", "[effects][contribution]")
{
	const StatsTable   table = poolTable();
	ActorStats         actor = newActor();
	ContributionLedger ledger;

	actor.values[kCon].accumulated = 10;
	actor.values[kEnd].accumulated = 10;
	(void)updateContributions(ledger, actor, table);

	// 10 * 0.02 + 10 * 0.01
	CHECK_THAT(ledger.contributions.at("HealRate"), WithinRel(0.3));
	CHECK(ledger.contributions.size() == 2);
}

TEST_CASE("modifier and multiplier move the contribution, base adjustments don't", "[effects][contribution]")
{
	const StatsTable   table = poolTable();
	ActorStats         actor = newActor();
	ContributionLedger ledger;

	actor.modifiers[kCon]["gear"] = 2;
	(void)updateContributions(ledger, actor, table);
	CHECK(ledger.contributions.at("Health") == 10.0);

	setBaseAdjustment(actor, kCon, "class", -3);
	CHECK(updateContributions(ledger, actor, table).empty());

	// (7 * 2 + 2) - 7
	actor.multiplier = 2.0;
	(void)updateContributions(ledger, actor, table);
	CHECK(ledger.contributions.at("Health") == 45.0);
}

TEST_CASE("ascending shrinks the contributions", "[effects][contribution]")
{
	const StatsTable   table = poolTable();
	ActorStats         actor = newActor();
	ContributionLedger ledger;

	actor.values[kCon].accumulated = 8;
	actor.values[kCon].active = 2;
	(void)updateContributions(ledger, actor, table);
	REQUIRE(ledger.contributions.at("Health") == 50.0);

	// 10 points convert to 4 base points.
	const bool ascended = ascend(actor.values[kCon], [](std::int32_t points) { return points * 0.4; });
	REQUIRE(ascended);
	const auto deltas = updateContributions(ledger, actor, table);

	REQUIRE(deltas.size() == 2);
	CHECK(deltas[0].actorValue == "HealRate");
	CHECK(deltas[0].delta < 0.0);
	CHECK(deltas[1] == AvDelta{ .actorValue = "Health", .delta = -30.0 });
	CHECK(ledger.contributions.at("Health") == 20.0);
}

TEST_CASE("an actor value no effect targets any more gets its contribution taken off", "[effects][contribution]")
{
	StatsTable         table = poolTable();
	ActorStats         actor = newActor();
	ContributionLedger ledger;

	actor.values[kCon].accumulated = 4;
	(void)updateContributions(ledger, actor, table);

	table[kCon].effects.erase("Health");
	CHECK(updateContributions(ledger, actor, table) == std::vector<AvDelta>{ { .actorValue = "Health", .delta = -20.0 } });
	CHECK_FALSE(ledger.contributions.contains("Health"));
}

TEST_CASE("an infinite contribution is not applied", "[effects][contribution]")
{
	StatsTable table;
	table[kCon].effects["Health"] = Effect{ .target = ActorValueTarget{ "Health" },
		.curve = PowerCurve{ .alpha = 1.0, .lambda = 400.0 } };
	ActorStats         actor = newActor();
	ContributionLedger ledger;

	actor.values[kCon].accumulated = 1;
	(void)updateContributions(ledger, actor, table);
	REQUIRE(ledger.contributions.at("Health") == 1.0);

	actor.values[kCon].accumulated = 100;
	CHECK(updateContributions(ledger, actor, table).empty());
	CHECK(ledger.contributions.at("Health") == 1.0);
}

TEST_CASE("an empty pool sends all damage to Health", "[effects][redirection]")
{
	CHECK(splitDamage(40.0, 0.25, 0.0) == RedirectSplit{ .fromPool = 0.0, .toHealth = 40.0 });
	CHECK(splitDamage(40.0, 0.25, -5.0) == RedirectSplit{ .fromPool = 0.0, .toHealth = 40.0 });
}

TEST_CASE("a short pool gives what it holds and Health takes the rest", "[effects][redirection]")
{
	// 10 of the 40 are redirected, the pool holds 4.
	CHECK(splitDamage(40.0, 0.25, 4.0) == RedirectSplit{ .fromPool = 4.0, .toHealth = 36.0 });
}

TEST_CASE("a full pool takes the redirected share", "[effects][redirection]")
{
	CHECK(splitDamage(40.0, 0.25, 100.0) == RedirectSplit{ .fromPool = 10.0, .toHealth = 30.0 });
	CHECK(splitDamage(40.0, 0.25, 10.0) == RedirectSplit{ .fromPool = 10.0, .toHealth = 30.0 });
}

TEST_CASE("a share outside 0 to 1 is clamped, and damage that is not positive stays whole", "[effects][redirection]")
{
	CHECK(splitDamage(40.0, 1.5, 100.0) == RedirectSplit{ .fromPool = 40.0, .toHealth = 0.0 });
	CHECK(splitDamage(40.0, -0.5, 100.0) == RedirectSplit{ .fromPool = 0.0, .toHealth = 40.0 });
	CHECK(splitDamage(40.0, std::numeric_limits<double>::quiet_NaN(), 100.0) == RedirectSplit{ .fromPool = 0.0, .toHealth = 40.0 });
	CHECK(splitDamage(0.0, 0.25, 100.0) == RedirectSplit{ .fromPool = 0.0, .toHealth = 0.0 });
	CHECK(splitDamage(-10.0, 0.25, 100.0) == RedirectSplit{ .fromPool = 0.0, .toHealth = -10.0 });
}

namespace
{
const StatId kInt{ "INT" };
const StatId kPer{ "PER" };

Effect namedEffect(NamedEffect effect, double a, CurveInput input = CurveInput::Relative)
{
	return Effect{ .target = effect, .curve = LinearCurve{ .a = a, .b = 0.0 }, .input = input };
}

// INT and PER at a start value of 10, each with points above it.
ActorStats learner(std::int32_t intAbove, std::int32_t perAbove)
{
	ActorStats actor;
	initStat(actor, kInt, 10);
	initStat(actor, kPer, 10);
	actor.values[kInt].accumulated = intAbove;
	actor.values[kPer].accumulated = perAbove;
	return actor;
}
}

TEST_CASE("a named effect takes effective - start, or the effective stat when absolute", "[effects][named]")
{
	const ActorStats actor = learner(4, 0);
	StatsTable       table;

	table[kInt].effects["MagicDamage"] = namedEffect(NamedEffect::MagicDamage, 0.01);
	CHECK_THAT(namedEffectValue(actor, table, NamedEffect::MagicDamage), WithinRel(0.04));

	table[kInt].effects["MagicDamage"] = namedEffect(NamedEffect::MagicDamage, 0.01, CurveInput::Absolute);
	CHECK_THAT(namedEffectValue(actor, table, NamedEffect::MagicDamage), WithinRel(0.14));
}

TEST_CASE("a named effect adds up over every stat that lists it, registered ones included", "[effects][named]")
{
	const StatId luck{ "Luck:LUC" };
	ActorStats   actor = learner(10, 20);
	initStat(actor, luck, 0);
	actor.values[luck].accumulated = 5;
	StatsTable table;
	table[kInt].effects["LearningSpeed"] = namedEffect(NamedEffect::LearningSpeed, 0.01);
	table[kPer].effects["LearningSpeed"] = namedEffect(NamedEffect::LearningSpeed, 0.01);
	table[luck].effects["Learning"] = namedEffect(NamedEffect::LearningSpeed, 0.02);
	// Another named effect and an actor-value effect don't count.
	table[kInt].effects["MagicDamage"] = namedEffect(NamedEffect::MagicDamage, 1.0);
	table[kInt].effects["Magicka"] = actorValueEffect("Magicka", 5.0);

	// 0.1 + 0.2 + 0.1
	CHECK_THAT(learningBonus(actor, table), WithinRel(0.4));
}

TEST_CASE("the damage redirection share adds up the stats that list it", "[effects][named]")
{
	StatsTable table = poolTable();
	ActorStats actor = newActor();
	CHECK(damageRedirectionShare(actor, table) == 0.0);

	actor.values[kEnd].accumulated = 50;
	const double fromEnd = 0.25 * (1.0 - std::exp(-0.5));
	CHECK_THAT(damageRedirectionShare(actor, table), WithinRel(fromEnd));

	table[kCon].effects["DamageRedirection"] = namedEffect(NamedEffect::DamageRedirection, 0.01, CurveInput::Absolute);
	CHECK_THAT(damageRedirectionShare(actor, table), WithinRel(fromEnd + 0.1));
}

TEST_CASE("a learning curve above the cap still yields at most +200 %", "[effects][learning]")
{
	StatsTable table;
	table[kInt].effects["LearningSpeed"] = namedEffect(NamedEffect::LearningSpeed, 1.0);
	table[kPer].effects["LearningSpeed"] = namedEffect(NamedEffect::LearningSpeed, 1.0);

	CHECK(learningBonus(learner(1, 0), table) == 1.0);
	CHECK(learningBonus(learner(1, 1), table) == 2.0);
	CHECK(learningBonus(learner(50, 50), table) == 2.0);
}

TEST_CASE("a learning malus stops at -100 %", "[effects][learning]")
{
	StatsTable table;
	table[kInt].effects["LearningSpeed"] = namedEffect(NamedEffect::LearningSpeed, 0.1);
	ActorStats actor = learner(0, 0);

	actor.modifiers[kInt]["curse"] = -5;
	CHECK_THAT(learningBonus(actor, table), WithinRel(-0.5));

	actor.modifiers[kInt]["curse"] = -50;
	CHECK(learningBonus(actor, table) == -1.0);
}

TEST_CASE("a learning bonus that is not a number gives none", "[effects][learning]")
{
	const Effect steep{ .target = NamedEffect::LearningSpeed, .curve = PowerCurve{ .alpha = 1.0, .lambda = 400.0 } };
	StatsTable   table;
	table[kInt].effects["LearningSpeed"] = steep;
	table[kPer].effects["LearningSpeed"] = steep;
	ActorStats actor = learner(100, 0);
	actor.modifiers[kPer]["curse"] = -100;

	// +inf from INT and -inf from PER
	CHECK(learningBonus(actor, table) == 0.0);
}
