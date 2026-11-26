// The events of the StatsService (ST-26).
#include "tom/stats/core/stats_service.h"

#include "stats_service_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <limits>
#include <string>
#include <variant>
#include <vector>

using tom::stats::EventSink;
using tom::stats::LevelUpPointsGrantedEvent;
using tom::stats::PointGainedEvent;
using tom::stats::PointSource;
using tom::stats::StatAdvancedEvent;
using tom::stats::StatAscendedEvent;
using tom::stats::StatsService;
using tom::stats::XpGainedEvent;

using namespace service_test;

namespace
{
using Event = std::variant<XpGainedEvent, PointGainedEvent, LevelUpPointsGrantedEvent, StatAdvancedEvent,
	StatAscendedEvent>;

// Records every event in the order it arrives.
class Recorder : public EventSink
{
public:
	std::vector<Event> events;

	void xpGained(const XpGainedEvent& event) override { events.emplace_back(event); }
	void pointGained(const PointGainedEvent& event) override { events.emplace_back(event); }
	void levelUpPointsGranted(const LevelUpPointsGrantedEvent& event) override { events.emplace_back(event); }
	void statAdvanced(const StatAdvancedEvent& event) override { events.emplace_back(event); }
	void statAscended(const StatAscendedEvent& event) override { events.emplace_back(event); }
};

// Assigns a pending point to CON whenever a level-up grants points.
class Assigner final : public Recorder
{
public:
	StatsService* stats = nullptr;

	void levelUpPointsGranted(const LevelUpPointsGrantedEvent& event) override
	{
		Recorder::levelUpPointsGranted(event);
		(void)stats->assignFreePoint(kCon);
	}
};

StatsService serviceWithPlayer(Recorder& recorder)
{
	StatsService stats = service();
	REQUIRE(stats.addActor(kPlayer, "ImperialRace"));
	REQUIRE(stats.addActor(kNpc, "ImperialRace"));
	stats.setEventSink(&recorder);
	return stats;
}
}

TEST_CASE("XP sends XP gained, then point gained for the points it buys", "[stats_service][events]")
{
	Recorder     recorder;
	StatsService stats = serviceWithPlayer(recorder);

	REQUIRE(stats.addXp(kPlayer, kCon, 350.0));
	REQUIRE(stats.addXp(kPlayer, kCon, 20.0));

	CHECK(recorder.events == std::vector<Event>{
								 XpGainedEvent{ .actor = kPlayer, .stat = kCon, .xp = 350.0 },
								 PointGainedEvent{ .actor = kPlayer, .stat = kCon, .points = 2, .source = PointSource::Xp },
								 XpGainedEvent{ .actor = kPlayer, .stat = kCon, .xp = 20.0 },
							 });
}

TEST_CASE("a level-up sends its points, then each stat it advances", "[stats_service][events]")
{
	Recorder     recorder;
	StatsService stats = serviceWithPlayer(recorder);
	REQUIRE(stats.addXp(kPlayer, kStr, 100.0));
	recorder.events.clear();

	stats.onPlayerLevelUp(4);
	stats.onPlayerLevelUp(5);

	std::vector<Event> expected{
		LevelUpPointsGrantedEvent{ .level = 4, .points = 3, .pending = 3 },
		LevelUpPointsGrantedEvent{ .level = 5, .points = 3, .pending = 6 },
	};
	for (const auto name : kCoreStats) {
		const std::int32_t points = name == "STR" ? 1 : 0;
		expected.emplace_back(StatAdvancedEvent{ .actor = kPlayer, .stat = StatId{ std::string(name) }, .points = points });
	}
	CHECK(recorder.events == expected);
}

TEST_CASE("an assigned free point sends point gained", "[stats_service][events]")
{
	Recorder     recorder;
	StatsService stats = serviceWithPlayer(recorder);
	REQUIRE(stats.claimAdvancing("Mastery"));
	stats.onPlayerLevelUp(5);

	REQUIRE(stats.assignFreePoint(kEnd));

	CHECK(recorder.events == std::vector<Event>{
								 LevelUpPointsGrantedEvent{ .level = 5, .points = 3, .pending = 3 },
								 PointGainedEvent{ .actor = kPlayer, .stat = kEnd, .points = 1, .source = PointSource::FreePoint },
							 });
}

TEST_CASE("advancing and ascending send their events", "[stats_service][events]")
{
	Recorder     recorder;
	StatsService stats = serviceWithPlayer(recorder);
	REQUIRE(stats.setValues(kNpc, kCon, tom::stats::StatValue{ .base = 10, .accumulated = 2, .active = 4 }));

	const auto half = [](std::int32_t points) { return points / 2.0; };

	REQUIRE(stats.advance(kNpc, kCon));
	REQUIRE(stats.ascend(kNpc, kCon, half));

	CHECK(recorder.events == std::vector<Event>{
								 StatAdvancedEvent{ .actor = kNpc, .stat = kCon, .points = 4 },
								 StatAscendedEvent{ .actor = kNpc, .stat = kCon, .points = 6, .baseGained = 3 },
							 });
}

TEST_CASE("rejected changes send no event", "[stats_service][events]")
{
	Recorder     recorder;
	StatsService stats = serviceWithPlayer(recorder);
	const auto   notANumber = [](std::int32_t) { return std::numeric_limits<double>::quiet_NaN(); };

	CHECK_FALSE(stats.addXp(kNpc, kCon, 100.0));
	CHECK_FALSE(stats.assignFreePoint(kCon));
	CHECK_FALSE(stats.advance(kPlayer, kLuck));
	CHECK_FALSE(stats.advanceAll(0x2000));
	CHECK_FALSE(stats.ascend(kPlayer, kCon, notANumber));

	CHECK(recorder.events.empty());
}

TEST_CASE("a receiver may call the service from an event", "[stats_service][events]")
{
	Assigner     assigner;
	StatsService stats = serviceWithPlayer(assigner);
	assigner.stats = &stats;

	stats.onPlayerLevelUp(2);

	CHECK(stats.pendingPoints() == 2);
	CHECK(stats.read(kPlayer, kCon)->accumulated == 1);
	CHECK(assigner.events.size() == 2);
}
