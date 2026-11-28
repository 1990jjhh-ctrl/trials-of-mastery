#include "Messaging.h"

#include "ActorLifecycle.h"
#include "GameConfig.h"
#include "LevelUpHook.h"

#include <cstdint>
#include <string_view>

namespace tom::stats
{
namespace
{
using Messaging = SKSE::MessagingInterface;

std::string_view messageName(std::uint32_t type)
{
	switch (type) {
	case Messaging::kPostLoad:
		return "kPostLoad";
	case Messaging::kPostPostLoad:
		return "kPostPostLoad";
	case Messaging::kPreLoadGame:
		return "kPreLoadGame";
	case Messaging::kPostLoadGame:
		return "kPostLoadGame";
	case Messaging::kSaveGame:
		return "kSaveGame";
	case Messaging::kDeleteGame:
		return "kDeleteGame";
	case Messaging::kInputLoaded:
		return "kInputLoaded";
	case Messaging::kNewGame:
		return "kNewGame";
	case Messaging::kDataLoaded:
		return "kDataLoaded";
	default:
		return "unknown";
	}
}

void onDataLoaded()
{
	loadConfigurationAndService();
	registerLevelUpDiagnostics();
	if (!installLevelUpHook()) {
		SKSE::log::error("The vanilla LevelUp menu is not replaced");
	}
}

void onMessage(Messaging::Message* message)
{
	SKSE::log::info("SKSE message {} ({})", messageName(message->type), message->type);
	switch (message->type) {
	case Messaging::kDataLoaded:
		onDataLoaded();
		return;
	case Messaging::kNewGame:
		onNewGame();
		return;
	case Messaging::kPostLoadGame:
		// SKSE passes whether the load succeeded as the data pointer itself.
		if (message->data == nullptr) {
			SKSE::log::warn("The load failed; the actors are not renewed");
			return;
		}
		onPostLoadGame();
		return;
	default:
		return;
	}
}
}

bool registerMessageListener()
{
	return SKSE::GetMessagingInterface()->RegisterListener(onMessage);
}
}
