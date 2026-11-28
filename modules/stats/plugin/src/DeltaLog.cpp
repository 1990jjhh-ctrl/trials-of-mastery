#include "DeltaLog.h"

namespace tom::stats
{
void logDeltas(std::string_view cause, const AvDeltasByActor& deltas)
{
	for (const auto& [actor, changes] : deltas) {
		for (const auto& change : changes) {
			SKSE::log::info("AvDelta {}: actor {:08X} {} {:+}", cause, actor, change.actorValue, change.delta);
		}
	}
}
}
