#include "tom/stats/core/config/configuration.h"

#include "tom/stats/core/table_folder.h"

#include <array>
#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace tom::stats
{

namespace
{

// The path as UTF-8, as table_folder.cpp writes paths.
std::string utf8(const std::filesystem::path& path)
{
	const auto text = path.u8string();
	return { text.begin(), text.end() };
}

std::optional<std::string> readText(const std::filesystem::path& file)
{
	std::ifstream stream(file, std::ios::binary);
	if (!stream) {
		return std::nullopt;
	}
	std::string text{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
	if (stream.bad()) {
		return std::nullopt;
	}
	return text;
}

void loadIni(Configuration& configuration)
{
	std::error_code error;
	const bool      exists = std::filesystem::exists(configuration.iniFile, error);
	if (error) {
		configuration.settings.errors.push_back({ utf8(configuration.iniFile), "", "the file cannot be checked: " + error.message() });
		return;
	}
	if (!exists) {
		return;
	}
	const auto text = readText(configuration.iniFile);
	if (!text) {
		configuration.settings.errors.push_back({ utf8(configuration.iniFile), "", "the file cannot be read" });
		return;
	}
	configuration.iniFound = true;
	configuration.settings = loadSettings(*text);
}

MergeResult loadTable(const std::filesystem::path& tablesFolder, Table table, const PluginQuery& plugins,
	const nlohmann::json& registered)
{
	return loadTableFolder(tablesFolder / folderName(table), plugins, registered, entryValidator(table));
}

// "<file> <key>: <message>", leaving out the parts that are empty.
std::string located(const std::string& file, const std::string& key, const std::string& message)
{
	std::string text = file;
	if (!key.empty()) {
		text += (text.empty() ? "" : " ") + key;
	}
	return text.empty() ? message : text + ": " + message;
}

LogLine diagnosticLine(std::string_view table, const Diagnostic& diagnostic)
{
	const auto  prefix = std::string(table) + ": ";
	const auto& file = diagnostic.file;
	switch (diagnostic.kind) {
	case Diagnostic::Kind::Loaded:
		return { LogLevel::Info, prefix + "loaded " + file };
	case Diagnostic::Kind::Conflict:
		return { LogLevel::Warning, prefix + "conflict at " + diagnostic.key + ": " + file + " overrides " + diagnostic.detail };
	case Diagnostic::Kind::Removed:
		return { LogLevel::Info, prefix + file + " removes " + diagnostic.key };
	case Diagnostic::Kind::SameNamePlugins:
		return { LogLevel::Warning,
			prefix + "several active plugins share the name of " + file + " (" + diagnostic.detail + "); it loads at the last of them" };
	case Diagnostic::Kind::SameNameFiles:
		return { LogLevel::Warning, prefix + file + " differs from " + diagnostic.detail + " only in case; it loads after it" };
	case Diagnostic::Kind::InactivePlugin:
		return { LogLevel::Info, prefix + "skipped " + file + ": its plugin is not active" };
	case Diagnostic::Kind::InvalidFile:
		return { LogLevel::Warning, prefix + "skipped " + (file.empty() ? "the folder" : file) + ": " + diagnostic.detail };
	case Diagnostic::Kind::InvalidRegistered:
		return { LogLevel::Warning, prefix + "skipped the entries registered through the API: " + diagnostic.detail };
	case Diagnostic::Kind::InvalidEntry:
		return { LogLevel::Warning, prefix + "skipped " + file + ": entry " + diagnostic.key + ": " + diagnostic.detail };
	}
	return { LogLevel::Warning, prefix + "unknown diagnostic for " + file };
}

void describeIni(const Configuration& configuration, std::vector<LogLine>& lines)
{
	const auto ini = utf8(configuration.iniFile);
	if (configuration.iniFound) {
		lines.push_back({ LogLevel::Info, "INI: loaded " + ini });
	} else {
		lines.push_back({ LogLevel::Info, "INI: " + ini + " not loaded; the built-in defaults are in use" });
	}
	for (const auto& error : configuration.settings.errors) {
		lines.push_back({ LogLevel::Warning, "INI: " + located(error.file, error.key, error.message) });
	}
}

void describeMerges(const Configuration& configuration, std::vector<LogLine>& lines)
{
	const std::array<std::pair<Table, const MergeResult*>, 4> merges{ {
		{ Table::Stats, &configuration.merged.stats },
		{ Table::Skills, &configuration.merged.skills },
		{ Table::Actions, &configuration.merged.actions },
		{ Table::Incompatible, &configuration.merged.incompatible },
	} };
	lines.push_back({ LogLevel::Info, "Tables: " + utf8(configuration.tablesFolder) });
	for (const auto& [table, merge] : merges) {
		for (const auto& diagnostic : merge->diagnostics) {
			lines.push_back(diagnosticLine(folderName(table), diagnostic));
		}
	}
}

void describeErrors(const std::vector<ConfigError>& errors, LogLevel level, std::string_view prefix,
	std::vector<LogLine>& lines)
{
	for (const auto& error : errors) {
		lines.push_back({ level, std::string(prefix) + located(error.file, error.key, error.message) });
	}
}

void describeTables(const Configuration& configuration, std::vector<LogLine>& lines)
{
	if (!configuration.tables) {
		describeErrors(configuration.tables.error().errors, LogLevel::Error, "Tables not built: ", lines);
		describeErrors(configuration.tables.error().dropped, LogLevel::Warning, "Dropped ", lines);
		return;
	}
	const auto& tables = *configuration.tables;
	describeErrors(tables.dropped, LogLevel::Warning, "Dropped ", lines);
	describeErrors(tables.ignoredRaceAdjustments, LogLevel::Debug, "Ignored ", lines);
	for (const auto& placeholder : tables.placeholders) {
		lines.push_back({ LogLevel::Info, "Placeholder in use: " + located(placeholder.file, placeholder.key, placeholder.note) });
	}
}

}

Configuration loadConfiguration(const std::filesystem::path& iniFile, const std::filesystem::path& tablesFolder,
	const PluginQuery& plugins, const std::set<std::string, std::less<>>& races, const RegisteredEntries& registered)
{
	Configuration configuration;
	configuration.iniFile = iniFile;
	configuration.tablesFolder = tablesFolder;
	loadIni(configuration);
	configuration.merged = {
		loadTable(tablesFolder, Table::Stats, plugins, registered.stats),
		loadTable(tablesFolder, Table::Skills, plugins, registered.skills),
		loadTable(tablesFolder, Table::Actions, plugins, registered.actions),
		loadTable(tablesFolder, Table::Incompatible, plugins, registered.incompatible),
	};
	configuration.tables = buildTables(configuration.merged, races);
	return configuration;
}

std::vector<LogLine> describe(const Configuration& configuration)
{
	std::vector<LogLine> lines;
	describeIni(configuration, lines);
	describeMerges(configuration, lines);
	describeTables(configuration, lines);
	return lines;
}

}
