// The test hooks of TestHooks.h in a build without TOM_STATS_TEST_HOOKS: none.

#include "TestHooks.h"

namespace tom::stats
{
void testAfterLoad() {}

void testLogPlayer(std::string_view /*when*/) {}
}
