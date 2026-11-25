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

// Removes the modifier under key, and the stat's map once it is empty.
void eraseModifier(ActorStats& stats, const StatId& stat, const ModifierKey& key)
{
	const auto it = stats.modifiers.find(stat);
	if (it == stats.modifiers.end()) {
		return;
	}
	it->second.erase(key);
	if (it->second.empty()) {
		stats.modifiers.erase(it);
	}
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

std::expected<std::vector<AvDelta>, Error> StatsService::addXp(ActorId actor, const StatId& stat, double xp)
{
	if (actor != player_) {
		return rejected(std::format("actor {:08X} is not the player; only the player trains stats", actor));
	}
	if (!std::isfinite(xp) || xp <= 0.0) {
		return rejected(std::format("XP {} for stat '{}' is not positive and finite", xp, stat.name));
	}
	return findStat(actor, stat).transform([&](ActorState* state) {
		(void)tom::stats::addXp(state->stats.values[stat], xp, r_);
		return updateEffects(*state);
	});
}

std::expected<std::vector<AvDelta>, Error> StatsService::setValues(ActorId actor, const StatId& stat, const StatValue& value)
{
	if (value.accumulated < 0 || value.active < 0) {
		return rejected(std::format("negative points for stat '{}'", stat.name));
	}
	if (!std::isfinite(value.xp) || value.xp < 0.0) {
		return rejected(std::format("XP {} for stat '{}' is negative or not finite", value.xp, stat.name));
	}
	return findStat(actor, stat).transform([&](ActorState* state) {
		state->stats.values[stat] = value;
		return updateEffects(*state);
	});
}

std::expected<std::vector<AvDelta>, Error> StatsService::setModifier(ActorId actor, const StatId& stat, const ModifierKey& key, std::int32_t amount)
{
	if (key.empty()) {
		return rejected(std::format("empty modifier key for stat '{}'", stat.name));
	}
	return findStat(actor, stat).transform([&](ActorState* state) {
		state->stats.modifiers[stat][key] = amount;
		return updateEffects(*state);
	});
}

std::expected<std::vector<AvDelta>, Error> StatsService::removeModifier(ActorId actor, const StatId& stat, const ModifierKey& key)
{
	return findStat(actor, stat).transform([&](ActorState* state) {
		eraseModifier(state->stats, stat, key);
		return updateEffects(*state);
	});
}

std::expected<std::vector<AvDelta>, Error> StatsService::setMultiplier(ActorId actor, double multiplier)
{
	if (!std::isfinite(multiplier)) {
		return rejected(std::format("multiplier {} is not finite", multiplier));
	}
	return findActor(actor).transform([&](ActorState* state) {
		state->stats.multiplier = multiplier;
		return updateEffects(*state);
	});
}

std::expected<std::vector<AvDelta>, Error> StatsService::setBaseAdjustment(ActorId actor, const StatId& stat, const BaseAdjustmentKey& key, std::int32_t amount)
{
	if (const auto valid = checkAdjustmentKey(key); !valid) {
		return std::unexpected(valid.error());
	}
	return findStat(actor, stat).transform([&](ActorState* state) {
		tom::stats::setBaseAdjustment(state->stats, stat, key, amount);
		return updateEffects(*state);
	});
}

std::expected<std::vector<AvDelta>, Error> StatsService::removeBaseAdjustment(ActorId actor, const StatId& stat, const BaseAdjustmentKey& key)
{
	if (const auto valid = checkAdjustmentKey(key); !valid) {
		return std::unexpected(valid.error());
	}
	return findStat(actor, stat).transform([&](ActorState* state) {
		tom::stats::removeBaseAdjustment(state->stats, stat, key);
		return updateEffects(*state);
	});
}

std::expected<void, Error> StatsService::setR(double r)
{
	if (!isValidR(r)) {
		return rejected(std::format("r {} is not a finite number greater than 1; keeping {}", r, r_));
	}
	r_ = r;
	return {};
}

std::expected<void, Error> StatsService::setN(std::int32_t n)
{
	if (!isValidN(n)) {
		return rejected(std::format("N {} is below 1; keeping {}", n, n_));
	}
	n_ = n;
	return {};
}

bool StatsService::claimAdvancing(const std::string& owner)
{
	if (owner.empty()) {
		return false;
	}
	advancingOwners_.insert(owner);
	return true;
}

std::expected<void, Error> StatsService::advance(ActorId actor, const StatId& stat)
{
	return findStat(actor, stat).transform([&](ActorState* state) { tom::stats::advance(state->stats.values[stat]); });
}

std::expected<void, Error> StatsService::advanceAll(ActorId actor)
{
	return findActor(actor).transform([this](ActorState* state) {
		for (const auto& id : registry_.activeStats()) {
			tom::stats::advance(state->stats.values[id]);
		}
	});
}

std::expected<std::vector<AvDelta>, Error> StatsService::ascend(ActorId actor, const StatId& stat, const std::function<double(std::int32_t)>& convert)
{
	return findStat(actor, stat).and_then([&](ActorState* state) -> std::expected<std::vector<AvDelta>, Error> {
		if (!tom::stats::ascend(state->stats.values[stat], convert)) {
			return rejected(std::format("the ascend formula for stat '{}' gave a result that is not finite", stat.name));
		}
		return updateEffects(*state);
	});
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

std::expected<StatsService::ActorState*, Error> StatsService::findStat(ActorId actor, const StatId& stat)
{
	if (const auto active = checkActive(stat); !active) {
		return std::unexpected(active.error());
	}
	return findActor(actor);
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

std::expected<void, Error> StatsService::checkAdjustmentKey(const BaseAdjustmentKey& key)
{
	if (key.empty()) {
		return rejected("empty base adjustment key");
	}
	if (key == kRaceAdjustmentKey) {
		return rejected(std::format("the base adjustment key '{}' belongs to the module", key));
	}
	return {};
}

}
