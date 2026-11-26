#include "tom/stats/core/stat_value.h"

#include "saturating.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace tom::stats
{

namespace
{
constexpr double kFirstPointCost = 100.0;

constexpr std::int32_t kMaxPoints = std::numeric_limits<std::int32_t>::max();

[[nodiscard]] bool canPay(double xp, double price)
{
	return std::isfinite(price) && price > 0.0 && xp >= price * (1.0 - kCostTolerance);
}

// Rounds a conversion result down to whole points (ST-6), saturated at the
// int32 range. Expects a finite value.
[[nodiscard]] std::int32_t roundDownPoints(double points)
{
	constexpr double kMin = std::numeric_limits<std::int32_t>::min();
	constexpr double kMax = kMaxPoints;
	return static_cast<std::int32_t>(std::clamp(std::floor(points), kMin, kMax));
}

// r^n - 1. From r^n >= 2 on, subtracting 1 loses no precision and keeps
// exact powers exact; below, pow(r, n) - 1 would cancel, so expm1 and log1p
// take over.
[[nodiscard]] double powMinusOne(double r, std::int32_t n)
{
	const double power = std::pow(r, n);
	if (power >= 2.0) {
		return power - 1.0;
	}
	return std::expm1(n * std::log1p(r - 1.0));
}

// The XP cost of n points starting at k active points, the geometric series
// 100 r^k (r^n - 1) / (r - 1). Expects r > 1.
[[nodiscard]] double costOfPoints(double r, std::int32_t k, std::int32_t n)
{
	return cost(r, k) * powMinusOne(r, n) / (r - 1.0);
}

// The most points xp pays for at k active points, with no more than room
// points. Solves the geometric series for n, then corrects the rounding of
// the closed form by one point. Expects r > 1.
[[nodiscard]] std::int32_t affordablePoints(double xp, double r, std::int32_t k, std::int32_t room)
{
	const double growth = r - 1.0;
	const double price = cost(r, k);
	const double ratio = xp / price * growth;
	// For a ratio beyond the double range, log1p(ratio) equals log(ratio).
	const double logTerm = std::isinf(ratio) ? std::log(xp / price) + std::log(growth) : std::log1p(ratio);
	const double estimate = std::floor(logTerm / std::log1p(growth));

	// A NaN estimate fails both comparisons and leaves n at 0.
	std::int32_t n = 0;
	if (estimate >= room) {
		n = room;
	} else if (estimate > 0.0) {
		n = static_cast<std::int32_t>(estimate);
	}

	if (n > 0 && !canPay(xp, costOfPoints(r, k, n))) {
		return n - 1;
	}
	if (n < room && canPay(xp, costOfPoints(r, k, n + 1))) {
		return n + 1;
	}
	return n;
}
}

double cost(double r, std::int32_t k)
{
	// IEEE 754 arithmetic turns an overflow into +inf, both in pow and in the
	// product, so ST-7 needs no extra check here.
	return kFirstPointCost * std::pow(r, k);
}

GainResult addXp(StatValue& value, double xp, double r)
{
	GainResult gain;
	// The negated comparison also rejects a NaN r. Negative active points only
	// come from a corrupt save.
	if (!std::isfinite(xp) || xp <= 0.0 || !(r > 1.0) || value.active < 0) {
		return gain;
	}

	// Keeps the XP finite when the sum overflows.
	value.xp = std::min(value.xp + xp, std::numeric_limits<double>::max());

	gain.points = affordablePoints(value.xp, r, value.active, kMaxPoints - value.active);
	if (gain.points == 0) {
		return gain;
	}
	// Within the tolerance the cost may exceed the XP slightly.
	value.xp = std::max(value.xp - costOfPoints(r, value.active, gain.points), 0.0);
	value.active += gain.points;
	return gain;
}

void advance(StatValue& value)
{
	value.accumulated = saturatingAdd(value.accumulated, value.active);
	value.active = 0;
	value.xp = 0.0;
}

bool ascend(StatValue& value, const std::function<double(std::int32_t)>& convert)
{
	const double converted = convert(saturatingAdd(value.accumulated, value.active));
	if (!std::isfinite(converted)) {
		return false;
	}

	value.base = saturatingAdd(value.base, roundDownPoints(converted));
	value.accumulated = 0;
	value.active = 0;
	value.xp = 0.0;
	return true;
}

}
