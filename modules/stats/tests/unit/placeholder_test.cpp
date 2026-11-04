#include <catch2/catch_test_macros.hpp>

// Proves the test target builds, links and runs under ctest. Remove it once the
// first real core test exists.
TEST_CASE("build skeleton runs a test", "[skeleton]")
{
	REQUIRE(1 + 1 == 2);
}
