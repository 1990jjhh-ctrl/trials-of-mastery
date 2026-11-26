// Training, checks and XP modifiers through the StatsService (Stats plan,
// C7a).
#include "tom/stats/core/stats_service.h"

#include "stats_service_fixture.h"

#include "tom/stats/core/xp_modifiers.h"

#include <catch2/catch_test_macros.hpp>

using tom::stats::StatsService;
using tom::stats::XpModifierKind;

using namespace service_test;

TEST_CASE("the skill XP factor is 1 without XP modifiers", "[stats_service][xp_modifier]")
{
	const StatsService stats = service();

	CHECK(stats.skillXpFactor("Smithing") == 1.0);
}

TEST_CASE("the skill XP factor multiplies 1 + m over the owners", "[stats_service][xp_modifier]")
{
	StatsService stats = service();

	REQUIRE(stats.setXpModifier("Professions", XpModifierKind::SkillXp, "Smithing", 0.5));
	REQUIRE(stats.setXpModifier("Mastery", XpModifierKind::SkillXp, "Smithing", -0.25));

	CHECK(stats.skillXpFactor("Smithing") == 1.5 * 0.75);
	CHECK(stats.skillXpFactor("Alchemy") == 1.0);

	stats.removeXpModifier("Professions", XpModifierKind::SkillXp, "Smithing");
	CHECK(stats.skillXpFactor("Smithing") == 0.75);
}

TEST_CASE("an XP modifier below -100 % is rejected", "[stats_service][xp_modifier]")
{
	StatsService stats = service();
	REQUIRE(stats.setXpModifier("Professions", XpModifierKind::SkillXp, "Smithing", 0.5));

	CHECK_FALSE(stats.setXpModifier("Professions", XpModifierKind::SkillXp, "Smithing", -1.5));
	CHECK_FALSE(stats.setXpModifier("", XpModifierKind::SkillXp, "Smithing", 0.5));
	CHECK(stats.skillXpFactor("Smithing") == 1.5);
	// -100 % takes the XP to 0 and is valid.
	REQUIRE(stats.setXpModifier("Professions", XpModifierKind::SkillXp, "Smithing", -1.0));
	CHECK(stats.skillXpFactor("Smithing") == 0.0);
}
