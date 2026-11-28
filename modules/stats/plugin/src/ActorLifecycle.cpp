#include "ActorLifecycle.h"

#include "DeltaLog.h"
#include "Service.h"

#include "tom/stats/core/actor_lifecycle.h"
#include "tom/stats/core/stats_service.h"

#include <optional>
#include <string>
#include <vector>

namespace tom::stats
{
namespace
{
// The editor ID of the actor's race, which the stats table names races by.
std::optional<std::string> raceOf(const RE::Actor& actor)
{
	const auto* race = actor.GetRace();
	const char* editorId = race != nullptr ? race->GetFormEditorID() : nullptr;
	if (editorId == nullptr || *editorId == '\0') {
		return std::nullopt;
	}
	return std::string{ editorId };
}

// The current race of each known actor and of the player, read from the game
// before the service is called again. An actor whose form is not loaded, or
// whose race has no editor ID, is left out and keeps its saved race.
ActorRaces currentRaces(const std::vector<ActorId>& known, const RE::PlayerCharacter& player)
{
	ActorRaces races;
	for (const ActorId id : known) {
		const auto* actor = RE::TESForm::LookupByID<RE::Actor>(id);
		const auto  race = actor != nullptr ? raceOf(*actor) : std::nullopt;
		if (!race) {
			SKSE::log::warn("Actor {:08X} has no race in this game; it keeps its saved race", id);
			continue;
		}
		races.emplace(id, *race);
	}
	if (const auto race = raceOf(player)) {
		races.insert_or_assign(player.GetFormID(), *race);
	} else {
		SKSE::log::error("The player's race has no editor ID; the player keeps its saved stats");
	}
	return races;
}
}

void onNewGame()
{
	SKSE::log::info("New game on thread {}", REX::W32::GetCurrentThreadId());
	const auto* player = RE::PlayerCharacter::GetSingleton();
	const auto  race = player != nullptr ? raceOf(*player) : std::nullopt;
	if (!race) {
		SKSE::log::error("No player race at the new game; the player has no stats until its race is known");
		return;
	}
	const ActorId id = player->GetFormID();
	const auto    deltas = withService([&](StatsService& service) { return ensureActor(service, id, *race); });
	if (!deltas) {
		return;
	}
	SKSE::log::info("New game: player {:08X} of race {}", id, *race);
	logDeltas("new game", AvDeltasByActor{ { id, *deltas } });
}

void onPostLoadGame()
{
	SKSE::log::info("After the load on thread {}", REX::W32::GetCurrentThreadId());
	const auto  known = withService([](const StatsService& service) { return service.actorIds(); });
	const auto* player = RE::PlayerCharacter::GetSingleton();
	if (!known || player == nullptr) {
		return;
	}
	const ActorRaces races = currentRaces(*known, *player);
	const auto       deltas = withService([&](StatsService& service) { return renewAfterLoad(service, races); });
	if (!deltas) {
		return;
	}
	SKSE::log::info("After the load: {} saved actors, races renewed for {}", known->size(), races.size());
	logDeltas("after the load", *deltas);
}
}
