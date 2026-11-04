#include "Messaging.h"

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

void onMessage(Messaging::Message* message)
{
	SKSE::log::info("SKSE message {} ({})", messageName(message->type), message->type);
}
}

bool registerMessageListener()
{
	return SKSE::GetMessagingInterface()->RegisterListener(onMessage);
}
}
