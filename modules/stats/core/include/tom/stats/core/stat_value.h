#pragma once

#include <cstdint>
#include <functional>

namespace tom::stats
{

// One stat of one actor (PRD §6, ST-5 to ST-7).
struct StatValue
{
	std::int32_t base = 0;
	std::int32_t accumulated = 0;
	std::int32_t active = 0;
	double       xp = 0.0;

	bool operator==(const StatValue&) const = default;
};

struct GainResult
{
	std::int32_t points = 0;
};

// Lets XP pay a cost that is larger only by rounding, e.g. cost(1.1, 2) =
// 121.00000000000001 (ST-5). The tolerance is relative to the price, which
// addXp takes as the cost of all points one call buys, not the cost of the
// last of them. The stat box takes it off a cost before rounding it up, so it
// shows the cost the XP has to reach.
inline constexpr double kCostTolerance = 1e-9;

// The XP cost of the next point at k active points (ST-5). Returns +inf when
// the cost overflows the double range (ST-7). Expects r > 0.
[[nodiscard]] double cost(double r, std::int32_t k);

// Adds XP and buys as many points as it pays for, each at the cost for the
// current active points; the rest carries over (ST-5). Ignores amounts that
// are not positive and finite. A point with an infinite cost is never bought.
// The XP saturates at the largest double. r must be greater than 1 (ST-6);
// for any other r, and for negative active points, the call changes nothing.
GainResult addXp(StatValue& value, double xp, double r);

// Moves the active points into the accumulated points and drops the leftover
// XP (ST-6). The sum saturates at the int32 range.
void advance(StatValue& value);

// Adds convert(accumulated + active), rounded down to whole points, to the
// base and clears accumulated, active and XP (ST-6). The caller supplies the
// conversion formula; convert is called even with 0 points. Both sums
// saturate at the int32 range. Returns false, and changes nothing, when the
// result is not finite.
[[nodiscard]] bool ascend(StatValue& value, const std::function<double(std::int32_t)>& convert);

}
