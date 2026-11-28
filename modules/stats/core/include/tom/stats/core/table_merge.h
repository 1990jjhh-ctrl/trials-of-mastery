#pragma once

// Merges the JSON files of one table folder. The folder layout, load order and
// merge rules are defined in docs/PRD.md#4-shared-constraints.

#include <nlohmann/json.hpp>

#include <functional>
#include <map>
#include <optional>
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

// Splits a plugin file name at its last dot: "Requiem.esp" gives "Requiem"
// and ".esp". A name without a dot, or with only a leading one, has no
// extension.
[[nodiscard]] PluginInfo splitPluginFileName(std::string_view fileName);

// What the merge needs to know about the game's plugins. The game layer
// implements it; tests use a fake.
class PluginQuery
{
public:
	virtual ~PluginQuery() = default;

	// Active plugins in load order.
	[[nodiscard]] virtual std::vector<PluginInfo> activePlugins() const = 0;
};

// The subfolder of a table folder that holds the mod patches.
inline constexpr std::string_view kPluginsFolderName = "plugins";

// One JSON file of a table folder.
struct TableSource
{
	enum class Folder
	{
		Table,    // directly in the table folder
		Plugins,  // in its plugins/ subfolder
	};

	std::string name;  // file name with extension, UTF-8
	std::string text;
	Folder      folder = Folder::Table;

	// The file's path in the table folder ("name" or "plugins/name"), as
	// diagnostics and origins name it.
	[[nodiscard]] std::string path() const;
};

struct Diagnostic
{
	enum class Kind
	{
		Loaded,             // file was merged; these come in merge order
		Conflict,           // a later patch file set a key an earlier patch file set
		Removed,            // a null removed an existing key
		SameNamePlugins,    // several active plugins share the mod patch's name
		SameNameFiles,      // file names in one folder differ only in case
		InactivePlugin,     // mod patch was skipped: its plugin is not active
		InvalidFile,        // file was skipped: not JSON, not an object, unreadable
		InvalidRegistered,  // registered entries were skipped: not an object
		InvalidEntry,       // file was skipped: the EntryValidator rejected an entry
	};

	Kind        kind = Kind::Loaded;
	std::string file;    // TableSource::path(); empty for the registered entries.
						 // loadTableFolder() puts a folder here for some read
						 // errors: "" or "plugins" (see table_folder.h)
	std::string key;     // JSON pointer, empty when the diagnostic is about the whole file;
						 // InvalidEntry: the rejected entry
	std::string detail;  // Conflict: the earlier file; SameNamePlugins: the plugins;
						 // SameNameFiles: the file loaded before; Invalid*: the reason
};

// The layers of a table, from lowest to highest.
enum class Layer
{
	Registered,
	Default,
	Patch,
	ModPatch,
};

// Where a value of the merged table comes from.
struct Origin
{
	Layer       layer = Layer::Registered;
	std::string file;  // TableSource::path(); empty for Layer::Registered

	bool operator==(const Origin&) const = default;
};

struct MergeResult
{
	nlohmann::json          merged = nlohmann::json::object();
	std::vector<Diagnostic> diagnostics;
	// JSON pointer of each leaf of `merged` (a non-object value or an empty
	// object) -> the layer that set it.
	std::map<std::string, Origin> origins;
	// The top-level keys of `merged` that a file set, in the order the files
	// first set them: files in merge order, each file's keys in its text
	// order. A key a null removes leaves the list, and a later file that sets
	// it again appends it. Keys that only the registered entries set are not
	// listed.
	std::vector<std::string> keyOrder;
};

inline constexpr std::string_view kDefaultFileName = "_default.json";

// Checks one top-level entry of a table: (entry name, the entry after merging
// a file) -> the reason it is invalid, or nullopt when it is valid.
using EntryValidator = std::function<std::optional<std::string>(const std::string& name, const nlohmann::json& entry)>;

// Merges the files of one table folder on top of `registered`, the entries
// that mods registered through the API, which must be an object.
//
// With a validator, each file is merged into a staged copy of the merge state
// and the validator runs on every entry the file sets (not on entries it
// removes with null). If it rejects one, the file is skipped as if it had
// never been read, with an InvalidEntry diagnostic; otherwise the staged
// state is kept. Registered entries are not validated here.
[[nodiscard]] MergeResult mergeTable(std::span<const TableSource> sources, const PluginQuery& plugins,
	const nlohmann::json& registered = nlohmann::json::object(), const EntryValidator& validator = {});

}
