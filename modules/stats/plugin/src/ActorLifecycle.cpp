#include "ActorLifecycle.h"

#include "DeltaLog.h"
#include "Service.h"

#include "tom/stats/core/actor_lifecycle.h"
#include "tom/stats/core/stats_service.h"

#include <optional>
#include <string>
#include <string_view>
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

// Sets the actor's race in the service, adding the player if it is not known
// yet. An NPC the service does not know is left alone: its stats start when
// something first needs them.
void followRace(const RE::Actor& actor, std::string_view cause)
{
	const auto    race = raceOf(actor);
	const ActorId id = actor.GetFormID();
	SKSE::log::info("{}: actor {:08X} on thread {}", cause, id, REX::W32::GetCurrentThreadId());
	if (!race) {
		SKSE::log::warn("{}: actor {:08X} has a race without an editor ID; its stats keep the race they have", cause, id);
		return;
	}
	const auto deltas = withService([&](StatsService& service) -> std::optional<std::vector<AvDelta>> {
		if (id != service.player() && !service.hasActor(id)) {
			return std::nullopt;
		}
		return ensureActor(service, id, *race);
	});
	if (!deltas || !*deltas) {
		return;
	}
	SKSE::log::info("{}: actor {:08X} is of race {}", cause, id, *race);
	logDeltas(cause, AvDeltasByActor{ { id, **deltas } });
}

// TESSwitchRaceCompleteEvent is what the game sends when an actor's race
// switch is complete, the event behind Papyrus' OnRaceSwitchComplete: for
// the console's setrace, a script's SetRace, vampirism and the werewolf and
// vampire lord transformations. The race menu of character creation and of
// showracemenu may change the player's race without it, so its closing
// checks the player's race too; setting the same race again changes nothing.
class RaceChanges final :
    public RE::BSTEventSink<RE::TESSwitchRaceCompleteEvent>,
    public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	RE::BSEventNotifyControl ProcessEvent(const RE::TESSwitchRaceCompleteEvent* event,
		RE::BSTEventSource<RE::TESSwitchRaceCompleteEvent>*) override
	{
		const auto* actor = event != nullptr && event->subject ? event->subject->As<RE::Actor>() : nullptr;
		if (actor != nullptr) {
			followRace(*actor, "race switch");
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
	{
		if (event == nullptr || event->opening ||
			std::string_view{ event->menuName.c_str() } != RE::RaceSexMenu::MENU_NAME) {
			return RE::BSEventNotifyControl::kContinue;
		}
		if (const auto* player = RE::PlayerCharacter::GetSingleton()) {
			followRace(*player, "race menu closed");
		}
		return RE::BSEventNotifyControl::kContinue;
	}
};

RaceChanges g_raceChanges;
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

void registerRaceChangeSinks()
{
	RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESSwitchRaceCompleteEvent>(&g_raceChanges);
	RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&g_raceChanges);
}
}
