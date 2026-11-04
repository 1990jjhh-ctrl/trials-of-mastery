// parseCurve: the JSON form of the curves of docs/PRD.md#41-curves.
#include "tom/stats/core/curve.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <variant>

using nlohmann::json;
using tom::stats::AsymptoticCurve;
using tom::stats::LinearCurve;
using tom::stats::parseCurve;
using tom::stats::PowerCurve;

namespace
{
// The error message of a rejected curve, or "" when the curve was accepted.
std::string errorOf(const json& value)
{
	const auto result = parseCurve(value);
	return result ? std::string{} : result.error().message;
}
}

TEST_CASE("parses a linear curve", "[curve][parse]")
{
	const auto result = parseCurve(json::parse(R"({ "curve": "linear", "a": 2.5, "b": -3 })"));

	REQUIRE(result);
	const auto* curve = std::get_if<LinearCurve>(&*result);
	REQUIRE(curve != nullptr);
	CHECK(curve->a == 2.5);
	CHECK(curve->b == -3.0);
}

TEST_CASE("parses a power curve", "[curve][parse]")
{
	const auto result = parseCurve(json::parse(R"({ "curve": "power", "alpha": 0.665, "lambda": 0.589 })"));

	REQUIRE(result);
	const auto* curve = std::get_if<PowerCurve>(&*result);
	REQUIRE(curve != nullptr);
	CHECK(curve->alpha == 0.665);
	CHECK(curve->lambda == 0.589);
}

TEST_CASE("parses an asymptotic curve", "[curve][parse]")
{
	const auto result = parseCurve(json::parse(R"({ "curve": "asymptotic", "S": 10, "E": 50, "c": -0.1 })"));

	REQUIRE(result);
	const auto* curve = std::get_if<AsymptoticCurve>(&*result);
	REQUIRE(curve != nullptr);
	CHECK(curve->start == 10.0);
	CHECK(curve->end == 50.0);
	CHECK(curve->c == -0.1);
}

TEST_CASE("ignores keys that are not parameters of the curve", "[curve][parse]")
{
	// For example the "_todo" markers of placeholder values (Stats plan, C3).
	const auto result = parseCurve(json::parse(R"j({ "curve": "linear", "a": 1, "b": 0, "_todo": "TODO(OQ-2)" })j"));

	REQUIRE(result);
	CHECK(std::holds_alternative<LinearCurve>(*result));
}

TEST_CASE("rejects an unknown curve name", "[curve][parse]")
{
	CHECK(errorOf(json::parse(R"({ "curve": "cubic", "a": 1 })")) == "unknown curve 'cubic'");
	// Names are case-sensitive, like every other key and name in the JSON files.
	CHECK(errorOf(json::parse(R"({ "curve": "Linear", "a": 1, "b": 0 })")) == "unknown curve 'Linear'");
}

TEST_CASE("rejects a missing parameter", "[curve][parse]")
{
	CHECK(errorOf(json::parse(R"({ "curve": "linear", "a": 1 })")) == "curve 'linear' is missing parameter 'b'");
	CHECK(errorOf(json::parse(R"({ "curve": "power", "alpha": 1 })")) == "curve 'power' is missing parameter 'lambda'");
	CHECK(errorOf(json::parse(R"({ "curve": "asymptotic", "S": 1, "c": 1 })")) == "curve 'asymptotic' is missing parameter 'E'");
}

TEST_CASE("rejects a parameter that is not a number", "[curve][parse]")
{
	CHECK(errorOf(json::parse(R"({ "curve": "linear", "a": "1", "b": 0 })")) == "parameter 'a' of curve 'linear' is not a number");
	CHECK(errorOf(json::parse(R"({ "curve": "power", "alpha": 1, "lambda": null })")) == "parameter 'lambda' of curve 'power' is not a number");
}

TEST_CASE("rejects a curve without a name", "[curve][parse]")
{
	CHECK(errorOf(json::parse(R"({ "a": 1, "b": 0 })")) == "curve has no 'curve' name");
	CHECK(errorOf(json::parse(R"({ "curve": 1, "a": 1, "b": 0 })")) == "curve name is not a string");
}

TEST_CASE("rejects a curve that is not an object", "[curve][parse]")
{
	CHECK(errorOf(json::parse(R"("linear")")) == "curve is not an object");
	CHECK(errorOf(json::parse(R"([1, 2])")) == "curve is not an object");
}
