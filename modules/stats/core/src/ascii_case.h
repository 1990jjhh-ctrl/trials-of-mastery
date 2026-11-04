#pragma once

// Case-insensitive comparison of plugin and file names, which are
// case-insensitive in the game (Windows file system). Folds ASCII letters
// only, independent of the locale; other bytes compare as they are.

#include <algorithm>
#include <string_view>

namespace tom::stats
{

constexpr char foldAsciiCase(char c)
{
	return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
	return std::ranges::equal(a, b, {}, foldAsciiCase, foldAsciiCase);
}

constexpr bool lessIgnoreCase(std::string_view a, std::string_view b)
{
	return std::ranges::lexicographical_compare(a, b, {}, foldAsciiCase, foldAsciiCase);
}

}
