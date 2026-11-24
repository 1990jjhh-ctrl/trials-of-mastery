#include "tom/stats/core/config/settings.h"

#include "../ascii_case.h"

#include <SimpleIni.h>

#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace tom::stats
{

namespace
{

template <class Number>
std::optional<Number> parseNumber(std::string_view text)
{
	Number     value{};
	const auto end = text.data() + text.size();
	const auto [stop, error] = std::from_chars(text.data(), end, value);
	if (error != std::errc{} || stop != end) {
		return std::nullopt;
	}
	return value;
}

std::optional<bool> parseBool(std::string_view text)
{
	if (equalsIgnoreCase(text, "true") || text == "1") {
		return true;
	}
	if (equalsIgnoreCase(text, "false") || text == "0") {
		return false;
	}
	return std::nullopt;
}

// Reads one value with `parse`, which returns nullopt for text that is not
// valid. `rule` completes "must be ..." in the error.
class IniReader
{
public:
	IniReader(const CSimpleIniA& ini, SettingsResult& result) :
		ini_(ini), errors_(result.errors) {}

	template <class Value, class Parse>
	bool read(const char* section, const char* key, Value& target, Parse parse, std::string_view rule);

	template <class Integer>
	bool readInteger(const char* section, const char* key, Integer& target, std::int64_t min, std::int64_t max);

private:
	const CSimpleIniA&        ini_;
	std::vector<ConfigError>& errors_;
};

template <class Value, class Parse>
bool IniReader::read(const char* section, const char* key, Value& target, Parse parse, std::string_view rule)
{
	const char* const text = ini_.GetValue(section, key, nullptr);
	if (text == nullptr) {
		return false;
	}
	const std::optional<Value> value = parse(std::string_view(text));
	if (!value) {
		errors_.push_back({ "", std::format("{}.{}", section, key), std::format("'{}' must be {}; keeping {}", text, rule, target) });
		return false;
	}
	target = *value;
	return true;
}

template <class Integer>
bool IniReader::readInteger(const char* section, const char* key, Integer& target, std::int64_t min, std::int64_t max)
{
	const auto parse = [min, max](std::string_view text) -> std::optional<Integer> {
		const auto value = parseNumber<std::int64_t>(text);
		if (!value || *value < min || *value > max) {
			return std::nullopt;
		}
		return static_cast<Integer>(*value);
	};
	return read(section, key, target, parse, std::format("an integer from {} to {}", min, max));
}

// ST-6: r must be greater than 1.
std::optional<double> parseCostGrowth(std::string_view text)
{
	const auto value = parseNumber<double>(text);
	if (!value || !std::isfinite(*value) || *value <= 1.0) {
		return std::nullopt;
	}
	return value;
}

// In parentheses: SimpleIni.h includes windows.h, whose max macro would
// break the call on Windows.
constexpr std::int64_t kInt32Max = (std::numeric_limits<std::int32_t>::max)();

// SKSE key code ranges.
constexpr std::int64_t kFirstKeyboardKey = 1;
constexpr std::int64_t kLastKeyboardKey = 255;
constexpr std::int64_t kFirstGamepadButton = 266;
constexpr std::int64_t kLastGamepadButton = 281;

}

SettingsResult loadSettings(std::string_view text)
{
	SettingsResult result;
	CSimpleIniA    ini;
	ini.SetUnicode(true);
	if (ini.LoadData(text.data(), text.size()) < 0) {
		result.errors.push_back({ "", "", "the INI cannot be read; keeping all defaults" });
	}

	IniReader reader(ini, result);
	auto&     settings = result.settings;
	reader.read("Progression", "CostGrowth", settings.costGrowth, parseCostGrowth, "a finite number greater than 1");
	reader.readInteger("Progression", "AdvanceInterval", settings.advanceInterval, 1, kInt32Max);
	reader.readInteger("Progression", "FreePointsPerLevelUp", settings.freePointsPerLevelUp, 0, kInt32Max);
	reader.readInteger("StatBox", "Key", settings.statBoxKey, kFirstKeyboardKey, kLastKeyboardKey);
	reader.readInteger("StatBox", "GamepadButton", settings.statBoxGamepadButton, kFirstGamepadButton, kLastGamepadButton);
	reader.read("Compatibility", "AllowIncompatibleMods", settings.allowIncompatibleMods, parseBool, "true or false");
	return result;
}

}
