// Curves of docs/PRD.md#41-curves. Expected values are computed by hand from
// the formulas there.
#include "tom/stats/core/curve.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>

using Catch::Matchers::WithinRel;
using tom::stats::AsymptoticCurve;
using tom::stats::evaluate;
using tom::stats::LinearCurve;
using tom::stats::PowerCurve;

namespace
{
constexpr double kInf = std::numeric_limits<double>::infinity();
}

TEST_CASE("linear curve is a times x plus b", "[curve]")
{
	const LinearCurve curve{ .a = 2.5, .b = -3.0 };

	CHECK(evaluate(curve, 0.0) == -3.0);
	CHECK(evaluate(curve, 4.0) == 7.0);
	CHECK(evaluate(curve, -2.0) == -8.0);
}

TEST_CASE("power curve with lambda 0.5 is the square root", "[curve]")
{
	const PowerCurve curve{ .alpha = 1.0, .lambda = 0.5 };

	CHECK(evaluate(curve, 0.0) == 0.0);
	CHECK(evaluate(curve, 16.0) == 4.0);
	CHECK(evaluate(curve, 2.0) == std::sqrt(2.0));
}

TEST_CASE("power curve is mirrored for negative x", "[curve]")
{
	const PowerCurve curve{ .alpha = 3.0, .lambda = 2.0 };

	CHECK(evaluate(curve, 2.0) == 12.0);
	CHECK(evaluate(curve, -2.0) == -12.0);
	CHECK(evaluate(PowerCurve{ .alpha = 1.0, .lambda = 0.5 }, -9.0) == -3.0);
}

TEST_CASE("power curve gives 0 at x = 0 for every lambda", "[curve]")
{
	// sign(0) = 0, so the product is 0 even where |0|^lambda is 1 or infinite.
	CHECK(evaluate(PowerCurve{ .alpha = 2.0, .lambda = 0.0 }, 0.0) == 0.0);
	CHECK(evaluate(PowerCurve{ .alpha = 2.0, .lambda = -1.0 }, 0.0) == 0.0);
}

TEST_CASE("power curve overflows to signed infinity", "[curve]")
{
	const PowerCurve curve{ .alpha = 1.0, .lambda = 2.0 };

	CHECK(evaluate(curve, 1e200) == kInf);
	CHECK(evaluate(curve, -1e200) == -kInf);
}

TEST_CASE("asymptotic curve gives S at 0 and approaches E", "[curve]")
{
	const AsymptoticCurve curve{ .start = 10.0, .end = 50.0, .c = 0.1 };

	CHECK(evaluate(curve, 0.0) == 10.0);
	CHECK_THAT(evaluate(curve, 10.0), WithinRel(50.0 - 40.0 * std::exp(-1.0)));
	CHECK_THAT(evaluate(curve, 100.0), WithinRel(50.0, 1e-4));
	CHECK(evaluate(curve, 100.0) < 50.0);
}

TEST_CASE("asymptotic curve approaches E from above when S > E", "[curve]")
{
	const AsymptoticCurve curve{ .start = 1.0, .end = 0.25, .c = 0.5 };

	CHECK(evaluate(curve, 0.0) == 1.0);
	CHECK_THAT(evaluate(curve, 2.0), WithinRel(0.25 + 0.75 * std::exp(-1.0)));
	CHECK_THAT(evaluate(curve, 20.0), WithinRel(0.25, 1e-3));
	CHECK(evaluate(curve, 20.0) > 0.25);
}

TEST_CASE("asymptotic curve treats c as |c|", "[curve]")
{
	const AsymptoticCurve positive{ .start = 10.0, .end = 50.0, .c = 0.1 };
	const AsymptoticCurve negative{ .start = 10.0, .end = 50.0, .c = -0.1 };

	for (const double x : { -5.0, 0.0, 3.0, 10.0, 100.0 }) {
		CHECK(evaluate(negative, x) == evaluate(positive, x));
	}
}

TEST_CASE("asymptotic curve with c = 0 stays at S", "[curve]")
{
	const AsymptoticCurve curve{ .start = 7.0, .end = 50.0, .c = 0.0 };

	CHECK(evaluate(curve, 0.0) == 7.0);
	CHECK(evaluate(curve, 1e6) == 7.0);
}

TEST_CASE("asymptotic curve diverges from E for negative x", "[curve]")
{
	const AsymptoticCurve curve{ .start = 10.0, .end = 50.0, .c = 0.1 };

	CHECK_THAT(evaluate(curve, -10.0), WithinRel(50.0 - 40.0 * std::exp(1.0)));
	CHECK(evaluate(curve, -1e5) == -kInf);
}

TEST_CASE("asymptotic curve with S = E is constant even where the exponential overflows", "[curve]")
{
	const AsymptoticCurve curve{ .start = 5.0, .end = 5.0, .c = 1.0 };

	CHECK(evaluate(curve, -1e5) == 5.0);
	CHECK(evaluate(curve, 1e5) == 5.0);
}

TEST_CASE("curve variant dispatches to its alternative", "[curve]")
{
	CHECK(evaluate(tom::stats::Curve{ LinearCurve{ .a = 1.0, .b = 1.0 } }, 2.0) == 3.0);
	CHECK(evaluate(tom::stats::Curve{ PowerCurve{ .alpha = 1.0, .lambda = 0.5 } }, 4.0) == 2.0);
	CHECK(evaluate(tom::stats::Curve{ AsymptoticCurve{ .start = 1.0, .end = 2.0, .c = 1.0 } }, 0.0) == 1.0);
}

// The ST-15 examples in docs/stats/PRD.md#74-training-and-actions. The PRD gives
// the weapon fit parameters and results as approximations, hence the tolerance.
TEST_CASE("weapon fit gives the ST-15 example XP", "[curve][ST-15]")
{
	const PowerCurve weapon{ .alpha = 0.665, .lambda = 0.589 };

	CHECK_THAT(evaluate(weapon, 2.0), WithinRel(1.0, 0.01));
	CHECK_THAT(evaluate(weapon, 100.0), WithinRel(10.0, 0.01));
}

TEST_CASE("trade curve gives the ST-15 example XP", "[curve][ST-15]")
{
	const PowerCurve trade{ .alpha = 0.1, .lambda = 0.5 };

	CHECK_THAT(evaluate(trade, 100.0), WithinRel(1.0));
	CHECK_THAT(evaluate(trade, 1000.0), WithinRel(3.0, 0.06));
	CHECK_THAT(evaluate(trade, 10000.0), WithinRel(10.0));
}
