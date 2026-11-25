#include "tom/stats/core/stats_service.h"

#include "saturating.h"

#include <cmath>
#include <format>
#include <string>
#include <utility>

namespace tom::stats
{

namespace
{
[[nodiscard]] std::unexpected<Error> rejected(std::string message)
{
	return std::unexpected(Error{ std::move(message) });
}

[[nodiscard]] std::unexpected<Error> unknownActor(ActorId actor)
{
	return rejected(std::format("unknown actor {:08X}", actor));
}

// The rules of ST-6 for r and N.
[[nodiscard]] bool isValidR(double r)
{
	return std::isfinite(r) && r > 1.0;
}

[[nodiscard]] bool isValidN(std::int32_t n)
{
	return n >= 1;
}

// The settings' value, or the built-in default if it is not valid.
template <typename T, typename Valid>
[[nodiscard]] T validOrDefault(T value, T fallback, Valid valid)
{
	return valid(value) ? value : fallback;
}
}

StatsService::StatsService(const Settings& settings, Tables tables, ActorId player) :
	tables_(std::move(tables)),
	player_(player),
	r_(validOrDefault(settings.costGrowth, Settings{}.costGrowth, isValidR)),
	n_(validOrDefault(settings.advanceInterval, Settings{}.advanceInterval, isValidN)),
	freePointsPerLevelUp_(validOrDefault(settings.freePointsPerLevelUp, Settings{}.freePointsPerLevelUp,
		[](std::int32_t points) { return points >= 0; }))
{
	rebuildActiveTable();
}

std::expected<AvDeltasByActor, Error> StatsService::registerStat(const StatId& id, FreePoints freePoints)
{
	if (!tables_.stats.contains(id)) {
		return rejected(std::format("stat '{}' has no entry in the stats table", id.name));
	}
	if (!registry_.registerStat(id, freePoints)) {
		return rejected(std::format("stat '{}' is already active or has no name", id.name));
	}
	rebuildActiveTable();

	AvDeltasByActor deltas;
	for (auto& [actor, state] : actors_) {
		applyRace(state, id);
		initMissingStats(state);
		auto changes = updateEffects(state);
		if (!changes.empty()) {
			deltas.emplace(actor, std::move(changes));
		}
	}
	return deltas;
}

void StatsService::addDormantStat(const StatId& id)
{
	registry_.addDormant(id);
}

std::expected<std::vector<AvDelta>, Error> StatsService::addActor(ActorId actor, std::string_view race)
{
	if (actors_.contains(actor)) {
		return rejected(std::format("actor {:08X} is already known", actor));
	}
	ActorState& state = actors_[actor];
	state.race = race;
	initMissingStats(state);
	return updateEffects(state);
}

std::vector<AvDelta> StatsService::restoreActor(ActorId actor, std::string_view race, ActorStats stats,
	ContributionLedger ledger)
{
	ActorState& state = actors_[actor];
	state = ActorState{ .stats = std::move(stats), .ledger = std::move(ledger), .race = std::string(race) };
	initMissingStats(state);
	return updateEffects(state);
}

std::expected<std::vector<AvDelta>, Error> StatsService::setRace(ActorId actor, std::string_view race)
{
	return findActor(actor).transform([&](ActorState* state) {
		state->race = race;
		for (const auto& id : registry_.activeStats()) {
			applyRace(*state, id);
		}
		return updateEffects(*state);
	});
}

bool StatsService::hasActor(ActorId actor) const
{
	return actors_.contains(actor);
}

std::expected<StatReading, Error> StatsService::read(ActorId actor, const StatId& stat) const
{
	const auto state = findActor(actor);
	if (!state) {
		return std::unexpected(state.error());
	}
	if (const auto active = checkActive(stat); !active) {
		return std::unexpected(active.error());
	}

	const ActorStats& stats = (*state)->stats;
	StatReading       reading;
	if (const auto it = stats.values.find(stat); it != stats.values.end()) {
		reading.base = it->second.base;
		reading.accumulated = it->second.accumulated;
		reading.active = it->second.active;
		reading.xp = it->second.xp;
	}
	if (const auto it = stats.modifiers.find(stat); it != stats.modifiers.end()) {
		std::int64_t sum = 0;
		for (const auto& [key, amount] : it->second) {
			sum += amount;
		}
		reading.modifier = saturateToInt32(sum);
	}
	reading.effective = effective(stats, stat);
	return reading;
}

std::expected<double, Error> StatsService::learningBonus(ActorId actor) const
{
	return findActor(actor).transform(
		[this](const ActorState* state) { return tom::stats::learningBonus(state->stats, activeTable_); });
}

std::expected<double, Error> StatsService::damageRedirectionShare(ActorId actor) const
{
	return findActor(actor).transform(
		[this](const ActorState* state) { return tom::stats::damageRedirectionShare(state->stats, activeTable_); });
}

std::int32_t StatsService::defaultBase(const StatId& stat) const
{
	const auto it = tables_.stats.find(stat);
	return it == tables_.stats.end() ? 0 : it->second.defaultBase;
}

void StatsService::applyRace(ActorState& state, const StatId& stat) const
{
	const std::string key{ kRaceAdjustmentKey };
	const auto        entry = tables_.stats.find(stat);
	if (entry == tables_.stats.end()) {
		tom::stats::removeBaseAdjustment(state.stats, stat, key);
		return;
	}
	const auto adjustment = entry->second.raceAdjustments.find(state.race);
	if (adjustment == entry->second.raceAdjustments.end()) {
		tom::stats::removeBaseAdjustment(state.stats, stat, key);
		return;
	}
	tom::stats::setBaseAdjustment(state.stats, stat, key, adjustment->second);
}

void StatsService::initMissingStats(ActorState& state) const
{
	for (const auto& id : registry_.activeStats()) {
		if (!state.stats.defaultBases.contains(id)) {
			applyRace(state, id);
			initStat(state.stats, id, defaultBase(id));
		}
	}
}

void StatsService::rebuildActiveTable()
{
	activeTable_.clear();
	for (const auto& id : registry_.activeStats()) {
		if (const auto it = tables_.stats.find(id); it != tables_.stats.end()) {
			activeTable_.emplace(id, it->second);
		}
	}
}

std::vector<AvDelta> StatsService::updateEffects(ActorState& state) const
{
	return updateContributions(state.ledger, state.stats, activeTable_);
}

std::expected<const StatsService::ActorState*, Error> StatsService::findActor(ActorId actor) const
{
	const auto it = actors_.find(actor);
	if (it == actors_.end()) {
		return unknownActor(actor);
	}
	return &it->second;
}

std::expected<StatsService::ActorState*, Error> StatsService::findActor(ActorId actor)
{
	const auto it = actors_.find(actor);
	if (it == actors_.end()) {
		return unknownActor(actor);
	}
	return &it->second;
}

std::expected<void, Error> StatsService::checkActive(const StatId& stat) const
{
	if (!registry_.contains(stat)) {
		return rejected(std::format("unknown stat '{}'", stat.name));
	}
	if (registry_.isDormant(stat)) {
		return rejected(std::format("stat '{}' is dormant: its owner has not registered it", stat.name));
	}
	return {};
}

}
