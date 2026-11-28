// The test hooks of TestHooks.h, built only with TOM_STATS_TEST_HOOKS.

#include "TestHooks.h"

#include "DeltaLog.h"
#include "Service.h"

#include "tom/stats/core/stats_service.h"

#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace tom::stats
{
namespace
{
// While this file exists, every load sets the test change below.
const std::filesystem::path kTestChangeFile{ "Data/SKSE/Plugins/trials-of-mastery-stats-test-change.txt" };

// The key of the test's base adjustment and modifier, as another module's
// key would be.
const std::string kTestKey{ "TrialsOfMasteryStatsTest:G2" };

const StatId kStr{ "STR" };
const StatId kDex{ "DEX" };
const StatId kCon{ "CON" };

// What the test change did, for the log.
struct TestChange
{
	AvDeltasByActor          deltas;
	std::vector<std::string> errors;
};

void record(TestChange& change, ActorId actor, const std::expected<std::vector<AvDelta>, Error>& result,
	std::string_view step)
{
	if (!result) {
		change.errors.push_back(std::format("{}: {}", step, result.error().message));
		return;
	}
	auto& deltas = change.deltas[actor];
	deltas.insert(deltas.end(), result->begin(), result->end());
}

void record(TestChange& change, const std::expected<void, Error>& result, std::string_view step)
{
	if (!result) {
		change.errors.push_back(std::format("{}: {}", step, result.error().message));
	}
}

// Each step sets a value, so applying the change again at the next load
// changes nothing.
TestChange applyTestChange(StatsService& service)
{
	TestChange    change;
	const ActorId player = service.player();
	record(change, player, service.setBaseAdjustment(player, kStr, kTestKey, 3), "STR base adjustment");
	const auto dex = service.read(player, kDex);
	const auto base = dex ? dex->base : 0;
	record(change, player,
		service.setValues(player, kDex, StatValue{ .base = base, .accumulated = 3, .active = 2, .xp = 42.5 }),
		"DEX values");
	record(change, player, service.setModifier(player, kCon, kTestKey, 4), "CON modifier");
	record(change, service.restorePendingPoints(2), "pending points");
	record(change, service.setR(1.5), "r");
	return change;
}

// Sets the test change while kTestChangeFile exists.
void applyTestChangeIfAsked()
{
	std::error_code missing;
	if (!std::filesystem::exists(kTestChangeFile, missing)) {
		return;
	}
	const auto change = withService([](StatsService& service) { return applyTestChange(service); });
	if (!change) {
		return;
	}
	SKSE::log::info("Test change applied, {} errors", change->errors.size());
	for (const auto& error : change->errors) {
		SKSE::log::error("Test change: {}", error);
	}
	logDeltas("test change", change->deltas);
}

std::string keyed(const std::map<std::string, std::int32_t>& amounts)
{
	std::string text;
	for (const auto& [key, amount] : amounts) {
		text += std::format("{}{} {:+}", text.empty() ? "" : ", ", key, amount);
	}
	return text;
}

// One line per stat, then the contributions.
std::vector<std::string> describePlayer(const StatsService& service)
{
	const auto snapshot = service.actorSnapshot(service.player());
	if (!snapshot) {
		return { std::format("no player: {}", snapshot.error().message) };
	}
	const ActorStats&        stats = snapshot->stats;
	std::vector<std::string> lines{ std::format("player {:08X}, race {}, multiplier {}, r {}, N {}, pending points {}",
		service.player(), snapshot->race, stats.multiplier, service.r(), service.n(), service.pendingPoints()) };
	for (const auto& [id, value] : stats.values) {
		const auto adjustments = stats.baseAdjustments.find(id);
		const auto modifiers = stats.modifiers.find(id);
		const auto defaultBase = stats.defaultBases.find(id);
		lines.push_back(std::format("{} base {} [{}], default base {}, accumulated {}, active {}, xp {}, modifiers [{}]",
			id.name, value.base, adjustments != stats.baseAdjustments.end() ? keyed(adjustments->second) : "",
			defaultBase != stats.defaultBases.end() ? std::to_string(defaultBase->second) : "none", value.accumulated,
			value.active, value.xp, modifiers != stats.modifiers.end() ? keyed(modifiers->second) : ""));
	}
	std::string contributions;
	for (const auto& [actorValue, amount] : snapshot->ledger.contributions) {
		contributions += std::format("{}{} {}", contributions.empty() ? "" : ", ", actorValue, amount);
	}
	lines.push_back(std::format("contributions [{}]", contributions));
	return lines;
}
}

void testLogPlayer(std::string_view when)
{
	const auto lines = withService([](const StatsService& service) { return describePlayer(service); });
	if (!lines) {
		SKSE::log::info("Test {}: no stats service", when);
		return;
	}
	for (const auto& line : *lines) {
		SKSE::log::info("Test {}: {}", when, line);
	}
}

void testAfterLoad()
{
	applyTestChangeIfAsked();
	testLogPlayer("after the load");
}
}
