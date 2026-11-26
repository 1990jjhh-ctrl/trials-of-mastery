#include "tom/stats/core/stat_box_format.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <string_view>

namespace tom::stats
{

namespace
{
// The message box font has no infinity sign (U+221E), so +inf shows as this.
constexpr std::string_view kInfinite = "--";
constexpr std::string_view kUnknown = "?";

// Up to this many digits a number shows in full.
constexpr std::size_t kFullDigits = 4;
constexpr std::size_t kSignificantDigits = 3;
// The digits a power of 1000 spans, and the digits T (10^12) drops.
constexpr std::size_t kSuffixDigits = 3;
constexpr std::size_t kTDigits = 12;
// From 10^15 (1000T) on the whole number of T shows.
constexpr std::size_t                     kWholeTDigits = 16;
constexpr std::array<std::string_view, 5> kSuffixes = { "", "K", "M", "B", "T" };

[[nodiscard]] Rounding opposite(Rounding rounding)
{
	return rounding == Rounding::Down ? Rounding::Up : Rounding::Down;
}

// Adds one to a string of decimal digits; all nines carry to a new digit.
void incrementDigits(std::string& digits)
{
	for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
		if (*it != '9') {
			++*it;
			return;
		}
		*it = '0';
	}
	digits.insert(digits.begin(), '1');
}

// The text of a whole number >= 0 given by its decimal digits, which works on
// the digits so that it stays exact beyond the integer range of a double.
[[nodiscard]] std::string magnitudeText(const std::string& digits, Rounding rounding)
{
	const std::size_t size = digits.size();
	if (size <= kFullDigits) {
		return digits;
	}
	const bool        wholeT = size >= kWholeTDigits;
	const std::size_t kept = wholeT ? size - kTDigits : kSignificantDigits;
	std::string       prefix = digits.substr(0, kept);
	if (rounding == Rounding::Up && digits.find_first_not_of('0', kept) != std::string::npos) {
		incrementDigits(prefix);
	}
	if (wholeT) {
		return prefix + std::string(kSuffixes.back());
	}
	if (prefix.size() > kSignificantDigits) {
		// The rounding carried to 10^size, which is exact and so needs no
		// further rounding, but may take the next suffix.
		return magnitudeText("1" + std::string(size, '0'), rounding);
	}
	const std::size_t suffix = (size - 1) / kSuffixDigits;
	const std::size_t beforePoint = size - (suffix * kSuffixDigits);
	std::string       text = prefix.substr(0, beforePoint);
	if (beforePoint < kSignificantDigits) {
		text += '.';
		text += prefix.substr(beforePoint);
	}
	return text + std::string(kSuffixes.at(suffix));
}
}

std::string formatStatNumber(double value, Rounding rounding)
{
	if (std::isnan(value) || value == -std::numeric_limits<double>::infinity()) {
		return std::string(kUnknown);
	}
	if (value == std::numeric_limits<double>::infinity()) {
		return std::string(kInfinite);
	}
	const double whole = rounding == Rounding::Down ? std::floor(value) : std::ceil(value);
	const bool   negative = whole < 0.0;
	// Rounding the signed value down rounds its absolute value up.
	const Rounding magnitudeRounding = negative ? opposite(rounding) : rounding;
	// A whole double prints exactly with no decimals.
	std::string text = magnitudeText(std::format("{:.0f}", std::fabs(whole)), magnitudeRounding);
	return negative ? "-" + text : text;
}

}
