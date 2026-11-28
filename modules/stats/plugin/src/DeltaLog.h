#pragma once

#include "tom/stats/core/stats_service.h"

#include <string_view>

namespace tom::stats
{
// Logs each AvDelta with its actor and what caused it. G3 applies them to the
// actors (docs/stats/PLAN.md, G3); until then they are only logged.
void logDeltas(std::string_view cause, const AvDeltasByActor& deltas);
}
