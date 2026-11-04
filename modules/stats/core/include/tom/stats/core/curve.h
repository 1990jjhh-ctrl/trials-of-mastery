#pragma once

// The curves of docs/PRD.md#41-curves, which every JSON-configured formula
// uses. The formulas and parameter meanings are defined there.

#include "tom/stats/core/error.h"

#include <nlohmann/json_fwd.hpp>

#include <expected>
#include <variant>

namespace tom::stats
{
struct LinearCurve
{
	double a = 0.0;
	double b = 0.0;
};

struct PowerCurve
{
	double alpha = 0.0;
	double lambda = 0.0;
};

struct AsymptoticCurve
{
	double start = 0.0;  // S
	double end = 0.0;    // E
	double c = 0.0;      // used as |c|
};

using Curve = std::variant<LinearCurve, PowerCurve, AsymptoticCurve>;

// Results follow IEEE arithmetic: an input far enough out gives +-infinity
// instead of a clamped value. Callers decide how to treat that.
[[nodiscard]] double evaluate(const LinearCurve& curve, double x);
// Gives 0 at x = 0 for every lambda, because sign(0) = 0.
[[nodiscard]] double evaluate(const PowerCurve& curve, double x);
// Gives E for every x when S = E, also where e^(-|c| x) overflows.
[[nodiscard]] double evaluate(const AsymptoticCurve& curve, double x);
[[nodiscard]] double evaluate(const Curve& curve, double x);

// Reads a curve from a JSON object that names it and gives its parameters:
//   { "curve": "linear", "a": ..., "b": ... }
//   { "curve": "power", "alpha": ..., "lambda": ... }
//   { "curve": "asymptotic", "S": ..., "E": ..., "c": ... }
// Every parameter is required and must be a number. Other keys are ignored.
[[nodiscard]] std::expected<Curve, Error> parseCurve(const nlohmann::json& value);
}
