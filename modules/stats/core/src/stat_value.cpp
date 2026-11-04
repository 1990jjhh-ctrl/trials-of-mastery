#include "tom/stats/core/stat_value.h"

#include <cmath>
#include <limits>

namespace tom::stats
{

namespace
{
constexpr double kFirstPointCost = 100.0;

[[nodiscard]] bool canPay(double xp, double price)
{
	return std::isfinite(price) && price > 0.0 && xp >= price;
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
	if (!std::isfinite(xp) || xp <= 0.0) {
		return gain;
	}

	value.xp += xp;
	// The upper bound on active keeps the int32 point count from overflowing.
	while (value.active < std::numeric_limits<std::int32_t>::max()) {
		const double price = cost(r, value.active);
		if (!canPay(value.xp, price)) {
			break;
		}
		value.xp -= price;
		++value.active;
		++gain.points;
	}
	return gain;
}

void advance(StatValue& value)
{
	value.accumulated += value.active;
	value.active = 0;
	value.xp = 0.0;
}

void ascend(StatValue& value, const std::function<std::int32_t(std::int32_t)>& convert)
{
	value.base += convert(value.accumulated + value.active);
	value.accumulated = 0;
	value.active = 0;
	value.xp = 0.0;
}

}
