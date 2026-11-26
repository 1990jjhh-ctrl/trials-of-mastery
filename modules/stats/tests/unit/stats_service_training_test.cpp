// Training, checks and XP modifiers through the StatsService (Stats plan,
// C7a).
#include "tom/stats/core/stats_service.h"

#include "stats_service_fixture.h"

#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/curve.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/stat_value.h"
#include "tom/stats/core/xp_modifiers.h"

#include <catch2/catch_test_macros.hpp>

#include <variant>
#include <vector>

using tom::stats::ActionEntry;
using tom::stats::AvDelta;
using tom::stats::EventSink;
using tom::stats::LevelUpPointsGrantedEvent;
using tom::stats::LinearCurve;
using tom::stats::PointGainedEvent;
using tom::stats::PointSource;
using tom::stats::SkillEntry;
using tom::stats::StatAdvancedEvent;
using tom::stats::StatAscendedEvent;
using tom::stats::StatReading;
using tom::stats::StatsService;
using tom::stats::XpGainedEvent;
using tom::stats::XpModifierKind;

using namespace service_test;

namespace
{
using Event = std::variant<XpGainedEvent, PointGainedEvent>;

// Records the training events in the order they arrive.
class Recorder final : public EventSink
{
public:
	std::vector<Event> events;

	void xpGained(const XpGainedEvent& event) override { events.emplace_back(event); }
	void pointGained(const PointGainedEvent& event) override { events.emplace_back(event); }
	void levelUpPointsGranted(const LevelUpPointsGrantedEvent& /*event*/) override {}
	void statAdvanced(const StatAdvancedEvent& /*event*/) override {}
	void statAscended(const StatAscendedEvent& /*event*/) override {}
};

// The fixture's tables plus skills and actions. OneHanded trains CON and
// END 3 : 1, Block trains only CON, and Fortune trains CON and the
// unregistered Mod:Luck. The action Swing trains STR by 0.5 per unit of
// input and CON by a fixed 2, Gamble trains CON by 3 and Mod:Luck by 4,
// and Look trains nothing.
tom::stats::Tables trainingTables()
{
	tom::stats::Tables result = tables();
	result.skills["OneHanded"] = SkillEntry{ .primary = kCon, .secondary = kEnd, .primaryShare = 0.75 };
	result.skills["Block"] = SkillEntry{ .primary = kCon, .secondary = kCon, .primaryShare = 1.0 };
	result.skills["Fortune"] = SkillEntry{ .primary = kCon, .secondary = kLuck, .primaryShare = 0.5 };
	result.actions["Swing"].training = { { kStr, LinearCurve{ .a = 0.5, .b = 0.0 } }, { kCon, LinearCurve{ .a = 0.0, .b = 2.0 } } };
	result.actions["Gamble"].training = { { kCon, LinearCurve{ .a = 0.0, .b = 3.0 } }, { kLuck, LinearCurve{ .a = 0.0, .b = 4.0 } } };
	result.actions["Look"] = ActionEntry{};
	return result;
}

// A service over trainingTables() with the player and an NPC, both
// Imperials, so every stat is at its default base.
StatsService trainingService()
{
	StatsService stats({}, trainingTables(), kPlayer);
	REQUIRE(stats.addActor(kPlayer, "ImperialRace"));
	REQUIRE(stats.addActor(kNpc, "ImperialRace"));
	return stats;
}
}

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

TEST_CASE("a skill level-up through the service trains the player's stats by the split", "[stats_service][training]")
{
	StatsService stats = trainingService();
	Recorder     recorder;
	stats.setEventSink(&recorder);

	// Level 200: 150 XP to CON buys one point at r = 2, 50 XP to END none.
	const auto deltas = stats.trainSkillLevelUp("OneHanded", 200);

	REQUIRE(deltas);
	CHECK(*deltas == std::vector<AvDelta>{ { .actorValue = "Health", .delta = above(kHealthCurve, 1.0) } });
	CHECK(stats.read(kPlayer, kCon) == StatReading{ .base = 10, .active = 1, .xp = 50.0, .effective = 11.0 });
	CHECK(stats.read(kPlayer, kEnd) == StatReading{ .base = 10, .xp = 50.0, .effective = 10.0 });
	CHECK(stats.read(kNpc, kCon) == StatReading{ .base = 10, .effective = 10.0 });
	CHECK(recorder.events == std::vector<Event>{
								 XpGainedEvent{ .actor = kPlayer, .stat = kCon, .xp = 150.0 },
								 PointGainedEvent{ .actor = kPlayer, .stat = kCon, .points = 1, .source = PointSource::Xp },
								 XpGainedEvent{ .actor = kPlayer, .stat = kEnd, .xp = 50.0 },
							 });
}

TEST_CASE("a skill that trains one stat gives it the whole level", "[stats_service][training]")
{
	StatsService stats = trainingService();

	REQUIRE(stats.trainSkillLevelUp("Block", 30));

	CHECK(stats.read(kPlayer, kCon)->xp == 30.0);
}

TEST_CASE("an unmapped skill trains nothing and is no error", "[stats_service][training]")
{
	StatsService stats = trainingService();
	Recorder     recorder;
	stats.setEventSink(&recorder);

	const auto deltas = stats.trainSkillLevelUp("Smithing", 50);

	REQUIRE(deltas);
	CHECK(deltas->empty());
	CHECK(stats.read(kPlayer, kCon) == StatReading{ .base = 10, .effective = 10.0 });
	CHECK(recorder.events.empty());
}

TEST_CASE("a skill level-up leaves out stats that are not active and amounts that are not positive", "[stats_service][training]")
{
	StatsService stats = trainingService();
	Recorder     recorder;
	stats.setEventSink(&recorder);

	REQUIRE(stats.trainSkillLevelUp("Fortune", 40));
	REQUIRE(stats.trainSkillLevelUp("Block", 0));
	REQUIRE(stats.trainSkillLevelUp("Block", -5));

	CHECK(stats.read(kPlayer, kCon)->xp == 20.0);
	CHECK(recorder.events == std::vector<Event>{ XpGainedEvent{ .actor = kPlayer, .stat = kCon, .xp = 20.0 } });
}

TEST_CASE("a skill level-up without the player fails", "[stats_service][training]")
{
	StatsService stats({}, trainingTables(), kPlayer);

	CHECK_FALSE(stats.trainSkillLevelUp("OneHanded", 20));
}

TEST_CASE("XP modifiers change the stat XP of the next skill level-up until removed", "[stats_service][training][xp_modifier]")
{
	StatsService stats = trainingService();

	REQUIRE(stats.setXpModifier("Professions", XpModifierKind::SkillLevelUpStatXp, "Block", 1.0));
	REQUIRE(stats.setXpModifier("Mastery", XpModifierKind::SkillLevelUpStatXp, "Block", -0.5));
	REQUIRE(stats.trainSkillLevelUp("Block", 30));
	CHECK(stats.read(kPlayer, kCon)->xp == 30.0 * 2.0 * 0.5);

	stats.removeXpModifier("Mastery", XpModifierKind::SkillLevelUpStatXp, "Block");
	REQUIRE(stats.trainSkillLevelUp("Block", 30));
	CHECK(stats.read(kPlayer, kCon)->xp == 30.0 + 60.0);

	stats.removeXpModifier("Professions", XpModifierKind::SkillLevelUpStatXp, "Block");
	REQUIRE(stats.trainSkillLevelUp("Block", 5));
	CHECK(stats.read(kPlayer, kCon)->xp == 90.0 + 5.0);
}

TEST_CASE("an action through the service trains the player's stats by its curves", "[stats_service][training]")
{
	StatsService stats = trainingService();
	Recorder     recorder;
	stats.setEventSink(&recorder);

	// 0.5 * 220 = 110 XP buy STR one point at r = 2; CON gets 2.
	const auto deltas = stats.applyTraining("Swing", 220.0);

	REQUIRE(deltas);
	CHECK(deltas->empty());  // STR has no actor-value effect here
	CHECK(stats.read(kPlayer, kStr) == StatReading{ .base = 10, .active = 1, .xp = 10.0, .effective = 11.0 });
	CHECK(stats.read(kPlayer, kCon)->xp == 2.0);
	CHECK(stats.read(kNpc, kStr) == StatReading{ .base = 10, .effective = 10.0 });
	CHECK(recorder.events == std::vector<Event>{
								 XpGainedEvent{ .actor = kPlayer, .stat = kCon, .xp = 2.0 },
								 XpGainedEvent{ .actor = kPlayer, .stat = kStr, .xp = 110.0 },
								 PointGainedEvent{ .actor = kPlayer, .stat = kStr, .points = 1, .source = PointSource::Xp },
							 });
}

TEST_CASE("an action leaves out stats that are not active and amounts that are not positive", "[stats_service][training]")
{
	StatsService stats = trainingService();
	Recorder     recorder;
	stats.setEventSink(&recorder);

	REQUIRE(stats.applyTraining("Gamble", 0.0));
	// At input 0 Swing gives STR no XP, only CON its fixed 2.
	REQUIRE(stats.applyTraining("Swing", 0.0));
	REQUIRE(stats.applyTraining("Look", 1.0));

	CHECK(stats.read(kPlayer, kStr)->xp == 0.0);
	CHECK(stats.read(kPlayer, kCon)->xp == 3.0 + 2.0);
	CHECK(recorder.events == std::vector<Event>{
								 XpGainedEvent{ .actor = kPlayer, .stat = kCon, .xp = 3.0 },
								 XpGainedEvent{ .actor = kPlayer, .stat = kCon, .xp = 2.0 },
							 });
}

TEST_CASE("an unknown action fails and changes nothing", "[stats_service][training]")
{
	StatsService stats = trainingService();
	Recorder     recorder;
	stats.setEventSink(&recorder);

	CHECK_FALSE(stats.applyTraining("Juggle", 10.0));
	CHECK(stats.read(kPlayer, kCon) == StatReading{ .base = 10, .effective = 10.0 });
	CHECK(recorder.events.empty());
}

TEST_CASE("an action without the player fails", "[stats_service][training]")
{
	StatsService stats({}, trainingTables(), kPlayer);

	CHECK_FALSE(stats.applyTraining("Swing", 10.0));
}

TEST_CASE("XP modifiers change the stat XP of the next action until removed", "[stats_service][training][xp_modifier]")
{
	StatsService stats = trainingService();

	REQUIRE(stats.setXpModifier("Professions", XpModifierKind::ActionStatXp, "Swing", 0.5));
	REQUIRE(stats.setXpModifier("Mastery", XpModifierKind::ActionStatXp, "Swing", 1.0));
	CHECK_FALSE(stats.setXpModifier("Luck", XpModifierKind::ActionStatXp, "Swing", -1.01));
	REQUIRE(stats.applyTraining("Swing", 0.0));
	CHECK(stats.read(kPlayer, kCon)->xp == 2.0 * 1.5 * 2.0);

	stats.removeXpModifier("Professions", XpModifierKind::ActionStatXp, "Swing");
	stats.removeXpModifier("Mastery", XpModifierKind::ActionStatXp, "Swing");
	REQUIRE(stats.applyTraining("Swing", 0.0));
	CHECK(stats.read(kPlayer, kCon)->xp == 6.0 + 2.0);
}
