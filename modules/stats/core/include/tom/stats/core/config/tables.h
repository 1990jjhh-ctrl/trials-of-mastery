#pragma once

// The four tables of ST-27, built from their merged JSON (table_merge.h). The
// JSON form of each table is defined by its schema in data/schema/.

#include "tom/stats/core/config/config_error.h"
#include "tom/stats/core/curve.h"
#include "tom/stats/core/stat_registry.h"
#include "tom/stats/core/table_merge.h"

#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace tom::stats
{

// The group of a stat that is not namespaced and whose entry names none; a
// namespaced stat without a group is in its namespace's group (ST-21).
inline constexpr std::string_view kOtherGroup = "Other";

// The named effects the module implements (ST-11 to ST-13).
enum class NamedEffect
{
	CritChancePhysical,  // ST-13
	CritChanceMagic,     // ST-13
	LearningSpeed,       // ST-12
	DamageRedirection,   // ST-11
	MagicDamage,         // WIS, PRD §7.3
};

// An effect on a game actor value (ST-10), named as the game names it. The
// game layer resolves the name.
struct ActorValueTarget
{
	std::string name;

	bool operator==(const ActorValueTarget&) const = default;
};

// What a named effect's curve takes as x (ST-9). Actor-value effects always
// take the relative input (ST-10).
enum class CurveInput
{
	Relative,  // effective stat - default base
	Absolute,  // effective stat
};

struct Effect
{
	std::variant<ActorValueTarget, NamedEffect> target;
	Curve                                       curve;
	CurveInput                                  input = CurveInput::Relative;
};

// ST-3, ST-9, ST-21, ST-24. When the entry names no group, a namespaced stat
// is in its namespace's group and any other stat in kOtherGroup.
struct StatEntry
{
	std::string                         group{ kOtherGroup };
	std::optional<std::string>          displayName;  // ST-24; nullopt when the entry sets none
	std::optional<std::string>          shortName;    // ST-24; nullopt when the entry sets none
	std::int32_t                        defaultBase = 0;
	std::map<std::string, std::int32_t> raceAdjustments;  // race editor ID -> adjustment
	std::map<std::string, Effect>       effects;          // keyed by the effect's name in the file
};

// ST-14.
struct SkillEntry
{
	StatId primary;
	StatId secondary;
	double primaryShare = 1.0;  // the share of the XP that goes to the primary stat
};

// ST-15: I = the sum of the input curves; `result` maps I to the game
// quantity (Stats plan, assumption 7).
struct Check
{
	std::map<StatId, Curve>      stats;
	std::map<std::string, Curve> skills;
	Curve                        result;
};

// ST-15.
struct ActionEntry
{
	std::optional<Check>    check;
	std::map<StatId, Curve> training;  // the XP each stat gets
};

// ST-28: the names to detect.
struct IncompatibleEntry
{
	std::vector<std::string> plugins;      // game plugin file names, with extension
	std::vector<std::string> sksePlugins;  // SKSE plugin DLL file names
};

using StatsTable = std::map<StatId, StatEntry>;
using SkillsTable = std::map<std::string, SkillEntry>;
using ActionsTable = std::map<std::string, ActionEntry>;
using IncompatibleTable = std::map<std::string, IncompatibleEntry>;

struct Tables
{
	StatsTable        stats;
	SkillsTable       skills;
	ActionsTable      actions;
	IncompatibleTable incompatible;

	// Entries that were dropped on their own (PRD §9): skills and actions
	// entries that name a stat no layer of the stats table defines, and
	// entries that are invalid in themselves, which only API registrations
	// can bring here. For the log, as warnings.
	std::vector<ConfigError> dropped;
	// Race adjustments for races no loaded plugin defines, which no actor can
	// have (PRD §9). For the log at debug level only.
	std::vector<ConfigError> ignoredRaceAdjustments;
	// Every "_todo" marker in the tables, for the log. The merge carries a
	// marker over when a patch changes the value beside it but not the marker
	// itself, so a tuned value is still listed until the patch sets "_todo" to
	// null.
	std::vector<Placeholder> placeholders;
};

// The merged JSON of each table folder.
struct MergedTables
{
	MergeResult stats;
	MergeResult skills;
	MergeResult actions;
	MergeResult incompatible;
};

enum class Table
{
	Stats,
	Skills,
	Actions,
	Incompatible,
};

// The table's folder name under data/, e.g. "stats".
[[nodiscard]] std::string_view folderName(Table table);

// The names the module ships, which need no namespace (ST-27).
[[nodiscard]] std::span<const std::string_view> vanillaSkills();
[[nodiscard]] std::span<const std::string_view> builtInActions();  // the actions the module hooks (ST-15)
[[nodiscard]] std::span<const std::string_view> namedEffectNames();

// The namespacing rule for names that other mods add (ST-27): "<Mod>:<Name>",
// with one colon and neither part empty.
[[nodiscard]] bool isNamespaced(std::string_view name);

// The part of a namespaced name before the colon ("Luck" for "Luck:LUC"), or
// an empty view for a name that is not namespaced.
[[nodiscard]] std::string_view namespaceOf(std::string_view name);

// The EntryValidator (table_merge.h) of a table: it checks an entry in itself,
// its name, keys, types, value ranges and curves, but not whether the stats
// it names exist. The reason names the rejected value by its JSON pointer.
[[nodiscard]] EntryValidator entryValidator(Table table);

// Why buildTables failed: the errors, and the entries it had dropped by then,
// which the caller logs as it logs Tables::dropped.
struct TablesError
{
	std::vector<ConfigError> errors;
	std::vector<ConfigError> dropped;
};

// Builds the typed tables from merges whose files passed entryValidator().
// `races` holds the editor IDs of the races the loaded plugins define.
//
// An entry that names a stat no layer of the stats table defines, or that is
// invalid in itself, is dropped and listed in Tables::dropped; an adjustment
// for a race outside `races` is listed in Tables::ignoredRaceAdjustments.
// Each names the files that set the value ("<table>/<path>", without API
// registrations) and its JSON pointer. A core stat without an entry is an
// error, named with the file that removed it.
[[nodiscard]] std::expected<Tables, TablesError> buildTables(const MergedTables& merged,
	const std::set<std::string, std::less<>>&                                    races = {});

}
