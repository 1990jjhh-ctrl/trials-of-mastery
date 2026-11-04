#pragma once

// Merges the JSON files of one table folder. The load order and merge rules
// are defined in docs/PRD.md#4-shared-constraints.

#include <nlohmann/json.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tom::stats
{

// One plugin file of the game, e.g. name "Requiem" and extension ".esp".
struct PluginInfo
{
	std::string name;
	std::string extension;
};

// What the merge needs to know about the game's plugins. The game layer
// implements it; tests use a fake.
class PluginQuery
{
public:
	virtual ~PluginQuery() = default;

	// Active plugins in load order.
	[[nodiscard]] virtual std::vector<PluginInfo> activePlugins() const = 0;

	// Whether a plugin file with this name (without extension, any of the
	// plugin extensions) is installed, active or not. Tells a JSON file named
	// after an inactive plugin apart from an unnamed file.
	[[nodiscard]] virtual bool isInstalled(std::string_view name) const = 0;
};

// One JSON file of a table folder: the file name with extension, and its text.
struct TableSource
{
	std::string name;
	std::string text;
};

struct Diagnostic
{
	enum class Kind
	{
		Loaded,           // file was merged; these come in merge order
		Conflict,         // a later patch file set a key an earlier patch file set
		Removed,          // a null removed an existing key
		SameNamePlugins,  // several active plugins share the file's name
		InactivePlugin,   // file is named after an installed but inactive plugin
		InvalidFile,      // file was skipped: not JSON, not an object, unreadable
	};

	Kind        kind;
	std::string file;
	std::string key;     // JSON pointer, empty when the diagnostic is about the whole file
	std::string detail;  // Conflict: the earlier file; SameNamePlugins: the plugins; InvalidFile: the reason
};

struct MergeResult
{
	nlohmann::json          merged = nlohmann::json::object();
	std::vector<Diagnostic> diagnostics;
};

inline constexpr std::string_view kDefaultFileName = "_default.json";

// Merges the files of one table folder. `registered` holds the entries that
// mods registered through the API; it must be an object and is the lowest
// layer, below the default file.
[[nodiscard]] MergeResult mergeTable(std::span<const TableSource> sources, const PluginQuery& plugins,
	const nlohmann::json& registered = nlohmann::json::object());

}
