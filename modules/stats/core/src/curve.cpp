#include "tom/stats/core/curve.h"

#include <cmath>

namespace tom::stats
{
double evaluate(const LinearCurve& curve, double x)
{
	return curve.a * x + curve.b;
}

double evaluate(const PowerCurve& curve, double x)
{
	if (x == 0.0) {
		return 0.0;
	}
	const double magnitude = curve.alpha * std::pow(std::abs(x), curve.lambda);
	return x < 0.0 ? -magnitude : magnitude;
}

double evaluate(const AsymptoticCurve& curve, double x)
{
	// Without this, (S - E) * infinity would be NaN for a large negative x.
	if (curve.start == curve.end) {
		return curve.end;
	}
	return curve.end + (curve.start - curve.end) * std::exp(-std::abs(curve.c) * x);
}

double evaluate(const Curve& curve, double x)
{
	return std::visit([x](const auto& alternative) { return evaluate(alternative, x); }, curve);
}
}
