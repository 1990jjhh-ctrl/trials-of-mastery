#pragma once

// The number format of the stat box (ST-24): the effective value, the XP and
// the cost of a stat's line and button. The choices ST-24 leaves open are in
// the Stats plan, assumption 17.

#include <string>

namespace tom::stats
{

// The direction in which formatStatNumber rounds: Down towards -inf, Up
// towards +inf.
enum class Rounding
{
	Down,
	Up,
};

// ST-24: below 10 000 the whole number, from 10 000 on three significant
// digits with K, M, B or T; +inf as "--", since the message box font has no
// infinity sign.
//
// The value is rounded to a whole number in the given direction first, then,
// from 10 000 on, to three significant digits in the same direction, with the
// largest suffix that leaves at least one digit before the point (12 345
// rounded down gives "12.3K", 999 999 rounded up "1.00M"). From 1000T on it
// shows the whole number of T ("2500T"). A negative value gets "-" before the
// text of its absolute value, rounded as the signed value (-2.5 rounded down
// gives "-3"). NaN and -inf, which no stat value should reach, give "?".
//
// Floating-point noise is not hidden: 100.00000000000001 rounded up gives
// "101", so a caller takes its own tolerance off first.
[[nodiscard]] std::string formatStatNumber(double value, Rounding rounding);

}
