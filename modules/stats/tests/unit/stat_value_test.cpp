#include "tom/stats/core/stat_value.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

using Catch::Matchers::WithinRel;
using tom::stats::addXp;
using tom::stats::advance;
using tom::stats::ascend;
using tom::stats::cost;
using tom::stats::StatValue;

namespace
{
constexpr double       kInf = std::numeric_limits<double>::infinity();
constexpr double       kMaxXp = std::numeric_limits<double>::max();
constexpr std::int32_t kMaxPoints = std::numeric_limits<std::int32_t>::max();
constexpr std::int32_t kMinPoints = std::numeric_limits<std::int32_t>::min();
}

TEST_CASE("the first point costs 100 for any r", "[cost]")
{
	CHECK(cost(1.0, 0) == 100.0);
	CHECK(cost(1.5, 0) == 100.0);
	CHECK(cost(2.0, 0) == 100.0);
}

TEST_CASE("the cost grows by r per active point", "[cost]")
{
	CHECK_THAT(cost(1.1, 2), WithinRel(121.0));
	CHECK_THAT(cost(1.5, 3), WithinRel(337.5));
	CHECK(cost(2.0, 10) == 102400.0);
	CHECK(cost(1.0, 500) == 100.0);
}

TEST_CASE("a cost beyond the double range is infinite", "[cost]")
{
	CHECK(std::isfinite(cost(10.0, 300)));
	CHECK(cost(10.0, 307) == kInf);
	CHECK(cost(2.0, 2000) == kInf);
	CHECK(cost(1.01, std::numeric_limits<std::int32_t>::max()) == kInf);
}

TEST_CASE("XP below the cost is kept without a point", "[xp]")
{
	StatValue value;

	const auto gain = addXp(value, 50.0, 2.0);

	CHECK(gain.points == 0);
	CHECK(value.active == 0);
	CHECK(value.xp == 50.0);
}

TEST_CASE("XP equal to the cost buys one point and leaves nothing", "[xp]")
{
	StatValue value;

	const auto gain = addXp(value, 100.0, 2.0);

	CHECK(gain.points == 1);
	CHECK(value.active == 1);
	CHECK(value.xp == 0.0);
}

TEST_CASE("XP that pays the cost up to rounding buys the point", "[xp]")
{
	StatValue value{ .base = 0, .accumulated = 0, .active = 2, .xp = 0.0 };
	REQUIRE(cost(1.1, 2) > 121.0);

	const auto gain = addXp(value, 121.0, 1.1);

	CHECK(gain.points == 1);
	CHECK(value.active == 3);
	CHECK(value.xp == 0.0);
}

TEST_CASE("one large XP amount buys several points and carries the rest over", "[xp]")
{
	StatValue value;

	// r = 2: the points cost 100, 200 and 400; the fourth would cost 800.
	const auto gain = addXp(value, 750.0, 2.0);

	CHECK(gain.points == 3);
	CHECK(value.active == 3);
	CHECK(value.xp == 50.0);
}

TEST_CASE("carried-over XP counts towards the next point", "[xp]")
{
	StatValue value;
	REQUIRE(addXp(value, 750.0, 2.0).points == 3);

	// 50 left plus 750 pays the fourth point at 800 exactly.
	const auto gain = addXp(value, 750.0, 2.0);

	CHECK(gain.points == 1);
	CHECK(value.active == 4);
	CHECK(value.xp == 0.0);
}

TEST_CASE("the cost starts at the current active points", "[xp]")
{
	StatValue value{ .base = 10, .accumulated = 5, .active = 2, .xp = 0.0 };

	// r = 1.5, k = 2: the points cost 225 and 337.5; 37.5 is left.
	const auto gain = addXp(value, 600.0, 1.5);

	CHECK(gain.points == 2);
	CHECK(value.active == 4);
	CHECK_THAT(value.xp, WithinRel(37.5));
	CHECK(value.base == 10);
	CHECK(value.accumulated == 5);
}

TEST_CASE("points from XP are not clamped at 100", "[xp]")
{
	StatValue value{ .base = 0, .accumulated = 0, .active = 100, .xp = 0.0 };

	const auto gain = addXp(value, cost(1.01, 100), 1.01);

	CHECK(gain.points == 1);
	CHECK(value.active == 101);
}

TEST_CASE("a huge XP amount at r close to 1 buys all points it pays for", "[xp]")
{
	StatValue value;

	// 100 * (1.0001^n - 1) / 0.0001 <= 1e20 holds up to n = 322378.
	const auto gain = addXp(value, 1e20, 1.0001);

	CHECK(gain.points == 322378);
	CHECK(value.active == 322378);
	CHECK(value.xp >= 0.0);
	CHECK(value.xp < cost(1.0001, value.active));
}

TEST_CASE("the point count stops at the int32 maximum", "[xp]")
{
	StatValue value;

	// Pays for far more than kMaxPoints points, so buying them one by one
	// would take billions of steps.
	const auto gain = addXp(value, 1e300, 1.000000001);

	CHECK(gain.points == kMaxPoints);
	CHECK(value.active == kMaxPoints);
}

TEST_CASE("r not greater than 1 grants nothing", "[xp]")
{
	const StatValue before{ .base = 0, .accumulated = 0, .active = 1, .xp = 30.0 };

	for (const double r : { 1.0, 0.5, 0.0, -2.0, std::numeric_limits<double>::quiet_NaN() }) {
		StatValue value = before;

		const auto gain = addXp(value, 1000.0, r);

		CHECK(gain.points == 0);
		CHECK(value.active == 1);
		CHECK(value.xp == 30.0);
	}
}

TEST_CASE("negative active points from a corrupt save grant nothing", "[xp]")
{
	for (const std::int32_t active : { -5, kMinPoints }) {
		StatValue value{ .base = 0, .accumulated = 0, .active = active, .xp = 30.0 };

		const auto gain = addXp(value, 1000.0, 2.0);

		CHECK(gain.points == 0);
		CHECK(value.active == active);
		CHECK(value.xp == 30.0);
	}
}

TEST_CASE("an infinite cost is never paid", "[xp]")
{
	StatValue value{ .base = 0, .accumulated = 0, .active = 400, .xp = 0.0 };

	const auto gain = addXp(value, 1e308, 10.0);

	CHECK(gain.points == 0);
	CHECK(value.active == 400);
	CHECK(value.xp == 1e308);
}

TEST_CASE("XP that would overflow stays at the largest double", "[xp]")
{
	StatValue value{ .base = 0, .accumulated = 0, .active = 400, .xp = 1e308 };

	const auto gain = addXp(value, 1e308, 10.0);

	CHECK(gain.points == 0);
	CHECK(value.xp == kMaxXp);
}

TEST_CASE("negative, zero and non-finite XP amounts change nothing", "[xp]")
{
	const StatValue before{ .base = 0, .accumulated = 0, .active = 1, .xp = 30.0 };

	for (const double xp : { -10.0, 0.0, kInf, -kInf, std::numeric_limits<double>::quiet_NaN() }) {
		StatValue value = before;

		const auto gain = addXp(value, xp, 2.0);

		CHECK(gain.points == 0);
		CHECK(value.active == 1);
		CHECK(value.xp == 30.0);
	}
}

TEST_CASE("advancing moves active into accumulated and drops leftover XP", "[advance]")
{
	StatValue value{ .base = 10, .accumulated = 3, .active = 4, .xp = 37.5 };

	advance(value);

	CHECK(value.base == 10);
	CHECK(value.accumulated == 7);
	CHECK(value.active == 0);
	CHECK(value.xp == 0.0);
}

TEST_CASE("advancing saturates the accumulated points at the int32 maximum", "[advance]")
{
	StatValue value{ .base = 0, .accumulated = kMaxPoints - 1, .active = 5, .xp = 0.0 };

	advance(value);

	CHECK(value.accumulated == kMaxPoints);
	CHECK(value.active == 0);
}

TEST_CASE("after advancing the next point costs 100 again", "[advance]")
{
	StatValue value;
	REQUIRE(addXp(value, 750.0, 2.0).points == 3);
	advance(value);

	const auto gain = addXp(value, 100.0, 2.0);

	CHECK(gain.points == 1);
	CHECK(value.accumulated == 3);
	CHECK(value.active == 1);
}

TEST_CASE("ascending converts accumulated plus active points into base", "[ascend]")
{
	StatValue    value{ .base = 10, .accumulated = 3, .active = 4, .xp = 20.0 };
	std::int32_t passed = -1;

	const bool ascended = ascend(value, [&passed](std::int32_t points) {
		passed = points;
		return points / 2.0;
	});

	CHECK(ascended);
	CHECK(passed == 7);
	CHECK(value.base == 13);
	CHECK(value.accumulated == 0);
	CHECK(value.active == 0);
	CHECK(value.xp == 0.0);
}

TEST_CASE("ascending with a zero result still clears the points", "[ascend]")
{
	StatValue value{ .base = 10, .accumulated = 1, .active = 0, .xp = 5.0 };

	const bool ascended = ascend(value, [](std::int32_t) { return 0.0; });

	CHECK(ascended);
	CHECK(value.base == 10);
	CHECK(value.accumulated == 0);
	CHECK(value.xp == 0.0);
}

TEST_CASE("ascending with no points still calls the conversion with 0", "[ascend]")
{
	StatValue    value{ .base = 10, .accumulated = 0, .active = 0, .xp = 5.0 };
	std::int32_t calls = 0;
	std::int32_t passed = -1;

	const bool ascended = ascend(value, [&calls, &passed](std::int32_t points) {
		++calls;
		passed = points;
		return 0.0;
	});

	CHECK(ascended);
	CHECK(calls == 1);
	CHECK(passed == 0);
	CHECK(value.base == 10);
	CHECK(value.xp == 0.0);
}

TEST_CASE("ascending saturates the points and the base at the int32 maximum", "[ascend]")
{
	StatValue    value{ .base = kMaxPoints, .accumulated = kMaxPoints, .active = 1, .xp = 0.0 };
	std::int32_t passed = -1;

	const bool ascended = ascend(value, [&passed](std::int32_t points) {
		passed = points;
		return static_cast<double>(points);
	});

	CHECK(ascended);
	CHECK(passed == kMaxPoints);
	CHECK(value.base == kMaxPoints);
	CHECK(value.accumulated == 0);
	CHECK(value.active == 0);
}

TEST_CASE("ascending rounds the conversion result down", "[ascend]")
{
	StatValue value{ .base = 10, .accumulated = 3, .active = 0, .xp = 0.0 };

	const bool ascended = ascend(value, [](std::int32_t) { return 2.9; });

	CHECK(ascended);
	CHECK(value.base == 12);
	CHECK(value.accumulated == 0);
}

TEST_CASE("ascending rounds a negative conversion result down", "[ascend]")
{
	StatValue value{ .base = 10, .accumulated = 3, .active = 0, .xp = 0.0 };

	const bool ascended = ascend(value, [](std::int32_t) { return -1.5; });

	CHECK(ascended);
	CHECK(value.base == 8);
	CHECK(value.accumulated == 0);
}

TEST_CASE("ascending saturates a conversion result beyond the int32 range", "[ascend]")
{
	StatValue value{ .base = 10, .accumulated = 3, .active = 0, .xp = 0.0 };

	const bool ascended = ascend(value, [](std::int32_t) { return 1e300; });

	CHECK(ascended);
	CHECK(value.base == kMaxPoints);
}

TEST_CASE("ascending saturates a negative result and the base at the int32 minimum", "[ascend]")
{
	for (const double result : { -5.0, -1e300 }) {
		StatValue value{ .base = kMinPoints + 1, .accumulated = 3, .active = 0, .xp = 0.0 };

		const bool ascended = ascend(value, [result](std::int32_t) { return result; });

		CHECK(ascended);
		CHECK(value.base == kMinPoints);
		CHECK(value.accumulated == 0);
	}
}

TEST_CASE("ascending with a non-finite conversion result changes nothing", "[ascend]")
{
	const StatValue before{ .base = 10, .accumulated = 3, .active = 4, .xp = 20.0 };

	for (const double result : { kInf, -kInf, std::numeric_limits<double>::quiet_NaN() }) {
		StatValue value = before;

		const bool ascended = ascend(value, [result](std::int32_t) { return result; });

		CHECK_FALSE(ascended);
		CHECK(value.base == 10);
		CHECK(value.accumulated == 3);
		CHECK(value.active == 4);
		CHECK(value.xp == 20.0);
	}
}
