#pragma once

// Loads the INI and the four tables of ST-27 in one go, as the game layer does
// at kDataLoaded (Stats plan, G1), and describes the result for the log.

#include "tom/stats/core/config/settings.h"
#include "tom/stats/core/config/tables.h"
#include "tom/stats/core/table_merge.h"

#include <nlohmann/json.hpp>

#include <expected>
#include <filesystem>
#include <functional>
#include <set>
#include <string>
#include <vector>

namespace tom::stats
{

// The entries mods registered through the API, the lowest layer of each table
// (Stats plan, assumption 3). Each must be an object.
struct RegisteredEntries
{
	nlohmann::json stats = nlohmann::json::object();
	nlohmann::json skills = nlohmann::json::object();
	nlohmann::json actions = nlohmann::json::object();
	nlohmann::json incompatible = nlohmann::json::object();
};

struct Configuration
{
	std::filesystem::path iniFile;
	bool                  iniFound = false;  // false: the built-in defaults are in use
	SettingsResult        settings;

	std::filesystem::path              tablesFolder;
	MergedTables                       merged;
	std::expected<Tables, TablesError> tables;
};

// Reads `iniFile` with loadSettings() and each table folder under
// `tablesFolder` (named by folderName()) with loadTableFolder() and the
// table's entryValidator(), on top of its registered entries, then builds the
// tables with buildTables() and `races`. A missing INI file keeps every
// built-in default.
[[nodiscard]] Configuration loadConfiguration(const std::filesystem::path& iniFile,
	const std::filesystem::path& tablesFolder, const PluginQuery& plugins,
	const std::set<std::string, std::less<>>& races, const RegisteredEntries& registered = {});

enum class LogLevel
{
	Debug,
	Info,
	Warning,
	Error,
};

struct LogLine
{
	LogLevel    level = LogLevel::Info;
	std::string text;

	bool operator==(const LogLine&) const = default;
};

// What the log says about a configuration: the INI file and its rejected
// values; per table, every merge diagnostic in order (so the loaded files show
// the merge order); then the build errors, the dropped entries, the race
// adjustments no actor can have (debug level) and each placeholder in use.
[[nodiscard]] std::vector<LogLine> describe(const Configuration& configuration);

}
