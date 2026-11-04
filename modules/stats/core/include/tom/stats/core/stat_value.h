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
};

struct GainResult
{
	std::int32_t points = 0;
};

// The XP cost of the next point at k active points (ST-5). Returns +inf when
// the cost overflows the double range (ST-7). Expects r > 0.
[[nodiscard]] double cost(double r, std::int32_t k);

// Adds XP and buys as many points as it pays for, each at the cost for the
// current active points; the rest carries over (ST-5). Ignores amounts that
// are not positive and finite. A point with an infinite cost is never bought.
GainResult addXp(StatValue& value, double xp, double r);

// Moves the active points into the accumulated points and drops the leftover
// XP (ST-6).
void advance(StatValue& value);

// Adds convert(accumulated + active) to the base and clears accumulated,
// active and XP (ST-6). The caller supplies the conversion formula.
void ascend(StatValue& value, const std::function<std::int32_t(std::int32_t)>& convert);

}
