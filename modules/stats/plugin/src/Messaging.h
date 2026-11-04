#pragma once

namespace tom::stats
{
// Registers the listener for SKSE's own messages. It is the single place where
// the plugin reacts to them (kPostLoad, kDataLoaded, kPreLoadGame,
// kPostLoadGame, kNewGame); later tasks add their calls there. Returns false if
// SKSE refused the listener.
[[nodiscard]] bool registerMessageListener();
}
