#pragma once

// Tables and a service for the StatsService tests.

#include "tom/stats/core/config/settings.h"
#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/curve.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/stat_registry.h"
#include "tom/stats/core/stats_service.h"

#include <array>
#include <string>
#include <string_view>

namespace service_test
{

using tom::stats::ActorId;
using tom::stats::StatId;

inline constexpr ActorId kPlayer = 0x14;
inline constexpr ActorId kNpc = 0x1A2B3;

inline const StatId kStr{ "STR" };
inline const StatId kCon{ "CON" };
inline const StatId kEnd{ "END" };
inline const StatId kLuck{ "Mod:Luck" };  // a registered stat

inline constexpr std::array<std::string_view, 8> kCoreStats{ "STR", "DEX", "CON", "INT", "WIS", "PER", "END", "CHA" };

// Curves that are not 0 at x = 0, so only the difference to curve(0) counts.
inline constexpr tom::stats::LinearCurve     kHealthCurve{ .a = 5.0, .b = 3.0 };
inline constexpr tom::stats::AsymptoticCurve kStaminaCurve{ .start = 2.0, .end = 6.0, .c = 0.1 };
inline constexpr tom::stats::LinearCurve     kLearningCurve{ .a = 0.01, .b = 0.5 };
inline constexpr tom::stats::LinearCurve     kMagickaCurve{ .a = 4.0, .b = 1.0 };

// What a stat x above its default base gives on a curve.
inline double above(const tom::stats::Curve& curve, double x)
{
	return tom::stats::evaluate(curve, x) - tom::stats::evaluate(curve, 0.0);
}

// Every core stat at a default base of 10; CON gives Health, END Stamina and
// a relative learning bonus. Mod:Luck, at a default base of 5, gives Magicka
// and a relative learning bonus once it is registered. Nords get CON +2 and
// Mod:Luck +1, Bretons CON -1; other races get no adjustment.
inline tom::stats::Tables tables()
{
	using tom::stats::ActorValueTarget;
	using tom::stats::Effect;
	using tom::stats::NamedEffect;

	tom::stats::Tables result;
	for (const auto name : kCoreStats) {
		result.stats[StatId{ std::string(name) }].defaultBase = 10;
	}
	auto& con = result.stats[kCon];
	con.raceAdjustments["NordRace"] = 2;
	con.raceAdjustments["BretonRace"] = -1;
	con.effects["Health"] = Effect{ .target = ActorValueTarget{ "Health" }, .curve = kHealthCurve };
	auto& end = result.stats[kEnd];
	end.effects["Stamina"] = Effect{ .target = ActorValueTarget{ "Stamina" }, .curve = kStaminaCurve };
	end.effects["LearningSpeed"] = Effect{ .target = NamedEffect::LearningSpeed, .curve = kLearningCurve };

	auto& luck = result.stats[kLuck];
	luck.defaultBase = 5;
	luck.raceAdjustments["NordRace"] = 1;
	luck.effects["Magicka"] = Effect{ .target = ActorValueTarget{ "Magicka" }, .curve = kMagickaCurve };
	luck.effects["LearningSpeed"] = Effect{ .target = NamedEffect::LearningSpeed, .curve = kLearningCurve };
	return result;
}

inline tom::stats::StatsService service(const tom::stats::Settings& settings = {})
{
	return tom::stats::StatsService(settings, tables(), kPlayer);
}

}
