#pragma once

// Integer math for points, bases and adjustments, which saturates at the int32
// range instead of overflowing (Stats plan, assumption 6).

#include <algorithm>
#include <cstdint>
#include <limits>

namespace tom::stats
{

// Clamps a sum computed in 64 bits to the int32 range.
[[nodiscard]] constexpr std::int32_t saturateToInt32(std::int64_t value)
{
	return static_cast<std::int32_t>(std::clamp<std::int64_t>(value, std::numeric_limits<std::int32_t>::min(),
		std::numeric_limits<std::int32_t>::max()));
}

[[nodiscard]] constexpr std::int32_t saturatingAdd(std::int32_t a, std::int32_t b)
{
	return saturateToInt32(std::int64_t{ a } + b);
}

}
