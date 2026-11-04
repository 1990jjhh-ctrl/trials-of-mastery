#pragma once

namespace tom::stats
{
// Registers the listener for SKSE's own messages. It is the single place where
// the plugin reacts to them; later tasks add their calls there
// (docs/stats/PLAN.md, T1.2). Returns false if SKSE refused the listener.
[[nodiscard]] bool registerMessageListener();
}
