#pragma once

// Typed access to one merged table that reports each problem with the files
// that set the value and its JSON pointer.

#include "tom/stats/core/config/config_error.h"
#include "tom/stats/core/curve.h"
#include "tom/stats/core/table_merge.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tom::stats
{

// The key that marks a placeholder value (Stats plan, C3). Every object of a
// table entry may hold one.
inline constexpr std::string_view kTodoKey = "_todo";

class TableReader
{
public:
	using Pointer = nlohmann::json::json_pointer;

	// Without `merged`, errors name no files.
	TableReader(std::string_view table, const MergeResult* merged, std::vector<ConfigError>& errors) :
		table_(table), merged_(merged), errors_(errors) {}

	void fail(const Pointer& path, std::string message);

	// Reports and returns false when `value` is not an object.
	bool expectObject(const nlohmann::json& value, const Pointer& path);

	// Reports the keys of `object` that are neither in `allowed` nor a text
	// "_todo".
	void checkKeys(const nlohmann::json& object, const Pointer& path, std::span<const std::string_view> allowed);

	// The value under `key`, or nullptr when it is missing, which is reported
	// when the key is required.
	const nlohmann::json* field(const nlohmann::json& object, const Pointer& path, const std::string& key, bool required);

	// Each returns nullopt, after reporting, for a value of the wrong kind, and
	// without reporting for nullptr.
	std::optional<std::string>  text(const nlohmann::json* value, const Pointer& path);
	std::optional<std::int32_t> int32(const nlohmann::json* value, const Pointer& path);
	std::optional<double>       number(const nlohmann::json* value, const Pointer& path, double min, double max);
	std::optional<Curve>        curve(const nlohmann::json* value, const Pointer& path);

	// Every "_todo" of the merged table.
	void collectPlaceholders(std::vector<Placeholder>& placeholders) const;

	// The files that set `pointer` or the values below it, as
	// "<table>/<path>", without API registrations. When there are none, as
	// for a missing key, those of the closest enclosing entry.
	[[nodiscard]] std::string filesOf(std::string pointer) const;

private:
	void                      collectPlaceholders(const nlohmann::json& value, const Pointer& path, std::vector<Placeholder>& placeholders) const;
	[[nodiscard]] std::string filesAt(const std::string& pointer) const;

	std::string_view          table_;
	const MergeResult*        merged_;
	std::vector<ConfigError>& errors_;
};

}
