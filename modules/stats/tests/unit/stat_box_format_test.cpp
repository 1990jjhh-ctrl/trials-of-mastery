// The number format of the stat box (ST-24, Stats plan assumption 17).
#include "tom/stats/core/stat_box_format.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cstddef>
#include <limits>
#include <string>

using tom::stats::formatStatNumber;
using tom::stats::Rounding;

namespace
{
constexpr double kInf = std::numeric_limits<double>::infinity();

// The number a text of formatStatNumber stands for, so that texts compare by
// what they show.
[[nodiscard]] double shownNumber(const std::string& text)
{
	std::size_t       end = 0;
	const double      number = std::stod(text, &end);
	const std::string suffix = text.substr(end);
	if (suffix == "K") {
		return number * 1e3;
	}
	if (suffix == "M") {
		return number * 1e6;
	}
	if (suffix == "B") {
		return number * 1e9;
	}
	if (suffix == "T") {
		return number * 1e12;
	}
	return number;
}
}

TEST_CASE("numbers below 10 000 show in full as whole numbers", "[stat_box_format]")
{
	CHECK(formatStatNumber(0.0, Rounding::Down) == "0");
	CHECK(formatStatNumber(0.0, Rounding::Up) == "0");
	CHECK(formatStatNumber(12.0, Rounding::Down) == "12");
	CHECK(formatStatNumber(9999.0, Rounding::Up) == "9999");
	CHECK(formatStatNumber(9999.7, Rounding::Down) == "9999");
	CHECK(formatStatNumber(12.5, Rounding::Down) == "12");
	CHECK(formatStatNumber(12.5, Rounding::Up) == "13");
}

TEST_CASE("rounding up to 10 000 takes the K format", "[stat_box_format]")
{
	CHECK(formatStatNumber(9999.7, Rounding::Up) == "10.0K");
}

TEST_CASE("floating-point noise is not hidden", "[stat_box_format]")
{
	CHECK(formatStatNumber(100.00000000000001, Rounding::Down) == "100");
	CHECK(formatStatNumber(100.00000000000001, Rounding::Up) == "101");
}

TEST_CASE("from 10 000 on three significant digits with a suffix", "[stat_box_format]")
{
	CHECK(formatStatNumber(10000.0, Rounding::Down) == "10.0K");
	CHECK(formatStatNumber(10000.0, Rounding::Up) == "10.0K");
	CHECK(formatStatNumber(12345.0, Rounding::Down) == "12.3K");
	CHECK(formatStatNumber(12345.0, Rounding::Up) == "12.4K");
	CHECK(formatStatNumber(1638400.0, Rounding::Up) == "1.64M");
	CHECK(formatStatNumber(1638400.0, Rounding::Down) == "1.63M");
	CHECK(formatStatNumber(1.5e9, Rounding::Down) == "1.50B");
	CHECK(formatStatNumber(1.5e9, Rounding::Up) == "1.50B");
	CHECK(formatStatNumber(999e12, Rounding::Down) == "999T");
}

TEST_CASE("a rounding that carries takes the next suffix", "[stat_box_format]")
{
	CHECK(formatStatNumber(999999.0, Rounding::Down) == "999K");
	CHECK(formatStatNumber(999999.0, Rounding::Up) == "1.00M");
	CHECK(formatStatNumber(99999.0, Rounding::Up) == "100K");
	CHECK(formatStatNumber(999.1e12, Rounding::Up) == "1000T");
}

TEST_CASE("from 1000T on the whole number of T", "[stat_box_format]")
{
	CHECK(formatStatNumber(1e15, Rounding::Down) == "1000T");
	CHECK(formatStatNumber(1e15, Rounding::Up) == "1000T");
	CHECK(formatStatNumber(2.5e15, Rounding::Down) == "2500T");
	CHECK(formatStatNumber(2.5e15 + 2.0, Rounding::Down) == "2500T");
	CHECK(formatStatNumber(2.5e15 + 2.0, Rounding::Up) == "2501T");
	// Beyond the integer range of a double the digits stay exact.
	CHECK(formatStatNumber(1e20, Rounding::Down) == "100000000T");
	CHECK(formatStatNumber(1e20, Rounding::Up) == "100000000T");
}

TEST_CASE("infinite and NaN values", "[stat_box_format]")
{
	CHECK(formatStatNumber(kInf, Rounding::Down) == "\xE2\x88\x9E");
	CHECK(formatStatNumber(kInf, Rounding::Up) == "\xE2\x88\x9E");
	CHECK(formatStatNumber(-kInf, Rounding::Down) == "?");
	CHECK(formatStatNumber(-kInf, Rounding::Up) == "?");
	CHECK(formatStatNumber(std::numeric_limits<double>::quiet_NaN(), Rounding::Down) == "?");
	CHECK(formatStatNumber(std::numeric_limits<double>::quiet_NaN(), Rounding::Up) == "?");
}

TEST_CASE("negative values round as signed values", "[stat_box_format]")
{
	CHECK(formatStatNumber(-2.5, Rounding::Down) == "-3");
	CHECK(formatStatNumber(-2.5, Rounding::Up) == "-2");
	CHECK(formatStatNumber(-0.5, Rounding::Up) == "0");
	CHECK(formatStatNumber(-12345.0, Rounding::Down) == "-12.4K");
	CHECK(formatStatNumber(-12345.0, Rounding::Up) == "-12.3K");
	CHECK(formatStatNumber(-999999.0, Rounding::Down) == "-1.00M");
}

TEST_CASE("XP below a cost never shows as the cost or more", "[stat_box_format]")
{
	const auto [xp, cost] = GENERATE(table<double, double>({
		{ 99.99, 100.0 },
		{ 9999.5, 10000.0 },
		{ 1639999.0, 1640000.0 },
		{ 999999.0, 1e6 },
	}));
	CAPTURE(xp, cost);

	CHECK(shownNumber(formatStatNumber(xp, Rounding::Down)) < shownNumber(formatStatNumber(cost, Rounding::Up)));
}
