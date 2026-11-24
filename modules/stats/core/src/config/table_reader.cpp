#include "table_reader.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <set>
#include <utility>
#include <variant>

namespace tom::stats
{

namespace
{

using nlohmann::json;

constexpr std::array<std::string_view, 3> kLinearKeys{ "curve", "a", "b" };
constexpr std::array<std::string_view, 3> kPowerKeys{ "curve", "alpha", "lambda" };
constexpr std::array<std::string_view, 4> kAsymptoticKeys{ "curve", "S", "E", "c" };

std::span<const std::string_view> keysOf(const Curve& curve)
{
	if (std::holds_alternative<LinearCurve>(curve)) {
		return kLinearKeys;
	}
	if (std::holds_alternative<PowerCurve>(curve)) {
		return kPowerKeys;
	}
	return kAsymptoticKeys;
}

}

void TableReader::fail(const Pointer& path, std::string message)
{
	const auto pointer = path.to_string();
	errors_.push_back({ filesOf(pointer), pointer, std::move(message) });
}

bool TableReader::expectObject(const json& value, const Pointer& path)
{
	if (value.is_object()) {
		return true;
	}
	fail(path, "must be an object");
	return false;
}

void TableReader::checkKeys(const json& object, const Pointer& path, std::span<const std::string_view> allowed)
{
	for (const auto& [key, value] : object.items()) {
		if (key == kTodoKey && !value.is_string()) {
			fail(path / key, "must be text");
		} else if (key != kTodoKey && std::ranges::find(allowed, key) == allowed.end()) {
			fail(path / key, "is not a known key");
		}
	}
}

const json* TableReader::field(const json& object, const Pointer& path, const std::string& key, bool required)
{
	const auto it = object.find(key);
	if (it != object.end()) {
		return &*it;
	}
	if (required) {
		fail(path / key, "is missing");
	}
	return nullptr;
}

std::optional<std::string> TableReader::text(const json* value, const Pointer& path)
{
	if (value == nullptr) {
		return std::nullopt;
	}
	if (!value->is_string() || value->get_ref<const std::string&>().empty()) {
		fail(path, "must be a non-empty text");
		return std::nullopt;
	}
	return value->get<std::string>();
}

std::optional<std::int32_t> TableReader::int32(const json* value, const Pointer& path)
{
	if (value == nullptr) {
		return std::nullopt;
	}
	if (!value->is_number()) {
		fail(path, "must be an integer");
		return std::nullopt;
	}
	// As in JSON Schema, a number without a fractional part, such as 10.0,
	// is an integer too.
	const double number = value->get<double>();
	if (!std::isfinite(number) || std::trunc(number) != number) {
		fail(path, "must be an integer");
		return std::nullopt;
	}
	// Every int32 is exact as a double, so the range check is exact too.
	using Limits = std::numeric_limits<std::int32_t>;
	if (number < Limits::min() || number > Limits::max()) {
		fail(path, std::format("must be an integer from {} to {}", Limits::min(), Limits::max()));
		return std::nullopt;
	}
	return static_cast<std::int32_t>(number);
}

std::optional<double> TableReader::number(const json* value, const Pointer& path, double min, double max)
{
	if (value == nullptr) {
		return std::nullopt;
	}
	if (!value->is_number() || !std::isfinite(value->get<double>())) {
		fail(path, "must be a number");
		return std::nullopt;
	}
	const auto number = value->get<double>();
	if (number < min || number > max) {
		fail(path, std::format("must be a number from {} to {}", min, max));
		return std::nullopt;
	}
	return number;
}

std::optional<Curve> TableReader::curve(const json* value, const Pointer& path)
{
	if (value == nullptr) {
		return std::nullopt;
	}
	auto parsed = parseCurve(*value);
	if (!parsed) {
		fail(path, parsed.error().message);
		return std::nullopt;
	}
	checkKeys(*value, path, keysOf(*parsed));
	return *parsed;
}

void TableReader::collectPlaceholders(std::vector<Placeholder>& placeholders) const
{
	if (merged_ != nullptr) {
		collectPlaceholders(merged_->merged, Pointer{}, placeholders);
	}
}

void TableReader::collectPlaceholders(const json& value, const Pointer& path, std::vector<Placeholder>& placeholders) const
{
	if (!value.is_object()) {
		return;
	}
	for (const auto& [key, child] : value.items()) {
		if (key == kTodoKey && child.is_string()) {
			placeholders.push_back({ filesOf((path / key).to_string()), path.to_string(), child.get<std::string>() });
			continue;
		}
		collectPlaceholders(child, path / key, placeholders);
	}
}

std::string TableReader::filesOf(std::string pointer) const
{
	if (merged_ == nullptr) {
		return "";
	}
	while (!pointer.empty()) {
		auto files = filesAt(pointer);
		if (!files.empty()) {
			return files;
		}
		pointer.erase(pointer.rfind('/'));
	}
	return "";
}

// Ordered by layer, then by file name. Empty when only API registrations set
// the value.
std::string TableReader::filesAt(const std::string& pointer) const
{
	const auto&                             origins = merged_->origins;
	std::set<std::pair<Layer, std::string>> found;
	const auto                              add = [&found](const Origin& origin) {
		if (origin.layer != Layer::Registered) {
			found.emplace(origin.layer, origin.file);
		}
	};
	if (const auto it = origins.find(pointer); it != origins.end()) {
		add(it->second);
	}
	const auto prefix = pointer + "/";
	for (auto it = origins.lower_bound(prefix); it != origins.end() && it->first.starts_with(prefix); ++it) {
		add(it->second);
	}

	std::string files;
	for (const auto& [layer, file] : found) {
		files += std::format("{}{}/{}", files.empty() ? "" : ", ", table_, file);
	}
	return files;
}

}
