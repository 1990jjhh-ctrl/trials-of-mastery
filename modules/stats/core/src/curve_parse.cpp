#include "tom/stats/core/curve.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstddef>
#include <format>
#include <string>

namespace tom::stats
{
namespace
{
using nlohmann::json;

// The values of the parameters named in keys, in the same order.
template <std::size_t N>
std::expected<std::array<double, N>, Error> parameters(const json& value, const std::string& curve, const std::array<const char*, N>& keys)
{
	std::array<double, N> result{};
	for (std::size_t i = 0; i < N; ++i) {
		const auto entry = value.find(keys[i]);
		if (entry == value.end()) {
			return std::unexpected(Error{ std::format("curve '{}' is missing parameter '{}'", curve, keys[i]) });
		}
		if (!entry->is_number()) {
			return std::unexpected(Error{ std::format("parameter '{}' of curve '{}' is not a number", keys[i], curve) });
		}
		result[i] = entry->template get<double>();
	}
	return result;
}
}

std::expected<Curve, Error> parseCurve(const json& value)
{
	if (!value.is_object()) {
		return std::unexpected(Error{ "curve is not an object" });
	}
	const auto nameEntry = value.find("curve");
	if (nameEntry == value.end()) {
		return std::unexpected(Error{ "curve has no 'curve' name" });
	}
	if (!nameEntry->is_string()) {
		return std::unexpected(Error{ "curve name is not a string" });
	}

	const auto& name = nameEntry->get_ref<const std::string&>();
	if (name == "linear") {
		return parameters<2>(value, name, { "a", "b" }).transform([](const auto& p) -> Curve {
			return LinearCurve{ .a = p[0], .b = p[1] };
		});
	}
	if (name == "power") {
		return parameters<2>(value, name, { "alpha", "lambda" }).transform([](const auto& p) -> Curve {
			return PowerCurve{ .alpha = p[0], .lambda = p[1] };
		});
	}
	if (name == "asymptotic") {
		return parameters<3>(value, name, { "S", "E", "c" }).transform([](const auto& p) -> Curve {
			return AsymptoticCurve{ .start = p[0], .end = p[1], .c = p[2] };
		});
	}
	return std::unexpected(Error{ std::format("unknown curve '{}'", name) });
}
}
