#include "tom/stats/core/xp_modifiers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <limits>

using Catch::Matchers::WithinRel;
using tom::stats::XpModifierKind;
using tom::stats::XpModifierRegistry;

TEST_CASE("without modifiers the factor is 1", "[xp_modifiers]")
{
	const XpModifierRegistry registry;

	CHECK(registry.factor(XpModifierKind::SkillXp, "OneHanded") == 1.0);
	CHECK(registry.factor(XpModifierKind::ActionStatXp, "WeaponSwing") == 1.0);
}

TEST_CASE("a modifier m gives the factor 1 + m", "[xp_modifiers]")
{
	XpModifierRegistry registry;

	REQUIRE(registry.set("Professions", XpModifierKind::SkillXp, "OneHanded", 0.25));

	CHECK_THAT(registry.factor(XpModifierKind::SkillXp, "OneHanded"), WithinRel(1.25));
}

TEST_CASE("modifiers from two owners multiply", "[xp_modifiers]")
{
	XpModifierRegistry registry;

	REQUIRE(registry.set("Professions", XpModifierKind::SkillLevelUpStatXp, "OneHanded", 0.5));
	REQUIRE(registry.set("OtherMod", XpModifierKind::SkillLevelUpStatXp, "OneHanded", -0.2));

	// (1 + 0.5) * (1 - 0.2)
	CHECK_THAT(registry.factor(XpModifierKind::SkillLevelUpStatXp, "OneHanded"), WithinRel(1.2));
}

TEST_CASE("setting an owner's modifier again replaces it", "[xp_modifiers]")
{
	XpModifierRegistry registry;
	REQUIRE(registry.set("Professions", XpModifierKind::ActionStatXp, "WeaponSwing", 0.5));

	REQUIRE(registry.set("Professions", XpModifierKind::ActionStatXp, "WeaponSwing", 1.0));

	CHECK_THAT(registry.factor(XpModifierKind::ActionStatXp, "WeaponSwing"), WithinRel(2.0));
}

TEST_CASE("removing one owner's modifier keeps the others", "[xp_modifiers]")
{
	XpModifierRegistry registry;
	REQUIRE(registry.set("Professions", XpModifierKind::ActionStatXp, "WeaponSwing", 0.5));
	REQUIRE(registry.set("OtherMod", XpModifierKind::ActionStatXp, "WeaponSwing", 1.0));

	registry.remove("Professions", XpModifierKind::ActionStatXp, "WeaponSwing");

	CHECK_THAT(registry.factor(XpModifierKind::ActionStatXp, "WeaponSwing"), WithinRel(2.0));

	registry.remove("OtherMod", XpModifierKind::ActionStatXp, "WeaponSwing");

	CHECK(registry.factor(XpModifierKind::ActionStatXp, "WeaponSwing") == 1.0);
}

TEST_CASE("removing a modifier that is not set changes nothing", "[xp_modifiers]")
{
	XpModifierRegistry registry;
	REQUIRE(registry.set("Professions", XpModifierKind::SkillXp, "OneHanded", 0.5));

	registry.remove("OtherMod", XpModifierKind::SkillXp, "OneHanded");
	registry.remove("Professions", XpModifierKind::SkillXp, "TwoHanded");
	registry.remove("Professions", XpModifierKind::ActionStatXp, "OneHanded");

	CHECK_THAT(registry.factor(XpModifierKind::SkillXp, "OneHanded"), WithinRel(1.5));
}

TEST_CASE("the kinds and names keep their modifiers apart", "[xp_modifiers]")
{
	XpModifierRegistry registry;

	REQUIRE(registry.set("Professions", XpModifierKind::SkillXp, "OneHanded", 0.5));

	CHECK(registry.factor(XpModifierKind::SkillLevelUpStatXp, "OneHanded") == 1.0);
	CHECK(registry.factor(XpModifierKind::ActionStatXp, "OneHanded") == 1.0);
	CHECK(registry.factor(XpModifierKind::SkillXp, "TwoHanded") == 1.0);
}

TEST_CASE("an empty owner or name and a modifier that is not finite are rejected", "[xp_modifiers]")
{
	XpModifierRegistry registry;
	REQUIRE(registry.set("Professions", XpModifierKind::SkillXp, "OneHanded", 0.5));

	CHECK_FALSE(registry.set("", XpModifierKind::SkillXp, "OneHanded", 1.0));
	CHECK_FALSE(registry.set("Professions", XpModifierKind::SkillXp, "", 1.0));
	CHECK_FALSE(registry.set("Professions", XpModifierKind::SkillXp, "OneHanded", std::numeric_limits<double>::infinity()));
	CHECK_FALSE(registry.set("Professions", XpModifierKind::SkillXp, "OneHanded", std::numeric_limits<double>::quiet_NaN()));

	CHECK_THAT(registry.factor(XpModifierKind::SkillXp, "OneHanded"), WithinRel(1.5));
}

TEST_CASE("a modifier below -1 is rejected, so no factor is negative", "[xp_modifiers]")
{
	XpModifierRegistry registry;

	CHECK_FALSE(registry.set("Professions", XpModifierKind::SkillXp, "OneHanded", -2.0));
	CHECK_FALSE(registry.set("OtherMod", XpModifierKind::SkillXp, "OneHanded", -1.000001));

	CHECK(registry.factor(XpModifierKind::SkillXp, "OneHanded") == 1.0);
}

TEST_CASE("a modifier of -1 is valid and gives the factor 0", "[xp_modifiers]")
{
	XpModifierRegistry registry;
	REQUIRE(registry.set("OtherMod", XpModifierKind::SkillXp, "OneHanded", 0.5));

	REQUIRE(registry.set("Professions", XpModifierKind::SkillXp, "OneHanded", -1.0));

	CHECK(registry.factor(XpModifierKind::SkillXp, "OneHanded") == 0.0);
}
