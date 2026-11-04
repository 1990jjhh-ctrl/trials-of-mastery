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
	// 1 - e^(-|c| |x|), in [0, 1] for finite x, so S = E gives S there.
	const double progress = -std::expm1(-std::abs(curve.c) * std::abs(x));
	const double offset = (curve.end - curve.start) * progress;
	return x < 0.0 ? curve.start - offset : curve.start + offset;
}

double evaluate(const Curve& curve, double x)
{
	return std::visit([x](const auto& alternative) { return evaluate(alternative, x); }, curve);
}
}
