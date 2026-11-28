#pragma once

#include <string_view>

namespace tom::stats
{
// Hooks for the in-game checks (modules/stats/tests/ingame/G2.md). Only a
// test build has them (CMake option TOM_STATS_TEST_HOOKS, TestHooks.cpp); in
// every other build they do nothing (NoTestHooks.cpp).

// After a load: if the file kTestChangeFile names exists in the game folder,
// sets a fixed, known change on the player through the service, where the
// API (A1) will later make one. Then logs the player as testLogPlayer() does.
void testAfterLoad();

// Logs everything the service holds for the player, its contributions
// included, with `when` in each line.
void testLogPlayer(std::string_view when);
}
