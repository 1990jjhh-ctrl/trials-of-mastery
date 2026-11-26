#include "tom/stats/core/config/tables.h"

#include "table_reader.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace tom::stats
{

namespace
{

using nlohmann::json;
using Pointer = json::json_pointer;

// The vanilla skills, as the skills and actions tables name them.
constexpr std::array<std::string_view, 18> kVanillaSkills{ "OneHanded", "TwoHanded", "Archery", "Block", "Smithing",
	"HeavyArmor", "LightArmor", "Pickpocket", "Lockpicking", "Sneak", "Alchemy", "Speech", "Alteration", "Conjuration",
	"Destruction", "Illusion", "Restoration", "Enchanting" };

constexpr std::array<std::string_view, 21> kBuiltInActions{ "WeaponSwing", "BowDraw", "CrossbowLoad", "Hit", "StaffUse",
	"UnarmedHit", "Fighting", "DamageTaken", "DiseaseRecovery", "PoisonResisted", "SpellCast", "Sprint",
	"OverencumberedWalk", "Trade", "Persuade", "Intimidate", "Interaction", "Lockpicking", "Pickpocket", "Sneak",
	"Detection" };

// In the order of NamedEffect.
constexpr std::array<std::string_view, 5> kNamedEffectNames{ "CritChancePhysical", "CritChanceMagic", "LearningSpeed",
	"DamageRedirection", "MagicDamage" };

constexpr std::array<std::string_view, 6> kStatKeys{ "group", "displayName", "shortName", "base", "raceAdjustments", "effects" };
constexpr std::array<std::string_view, 4> kEffectKeys{ "actorValue", "named", "input", "curve" };
constexpr std::array<std::string_view, 3> kSkillKeys{ "primary", "secondary", "primaryShare" };
constexpr std::array<std::string_view, 2> kActionKeys{ "check", "training" };
constexpr std::array<std::string_view, 3> kCheckKeys{ "stats", "skills", "result" };
constexpr std::array<std::string_view, 2> kIncompatibleKeys{ "plugins", "sksePlugins" };

bool contains(std::span<const std::string_view> names, std::string_view name)
{
	return std::ranges::find(names, name) != names.end();
}

bool isCoreStat(const std::string& name)
{
	static const StatRegistry registry;
	return registry.isCore(StatId{ name });
}

bool isShipped(Table table, const std::string& name)
{
	switch (table) {
	case Table::Stats:
		return isCoreStat(name);
	case Table::Skills:
		return contains(kVanillaSkills, name);
	case Table::Actions:
		return contains(kBuiltInActions, name);
	case Table::Incompatible:
		return true;
	}
	return false;
}

// Reports and returns false for a name that the module does not ship and that
// is not namespaced (ST-27).
bool checkName(TableReader& reader, const std::string& name, const Pointer& path, bool shipped)
{
	if (shipped || isNamespaced(name)) {
		return true;
	}
	reader.fail(path, "is not shipped by the module, so it must be namespaced as <Mod>:<Name>");
	return false;
}

// Stat names in skills and actions: a core stat or a namespaced one. Whether
// the stat exists is checked by buildTables.
std::optional<StatId> statName(TableReader& reader, const std::string& name, const Pointer& path)
{
	if (!checkName(reader, name, path, isCoreStat(name))) {
		return std::nullopt;
	}
	return StatId{ name };
}

std::optional<NamedEffect> namedEffect(std::string_view name)
{
	const auto it = std::ranges::find(kNamedEffectNames, name);
	if (it == kNamedEffectNames.end()) {
		return std::nullopt;
	}
	return static_cast<NamedEffect>(it - kNamedEffectNames.begin());
}

std::optional<std::variant<ActorValueTarget, NamedEffect>> effectTarget(TableReader& reader, const json& effect, const Pointer& path)
{
	const auto* actorValue = reader.field(effect, path, "actorValue", false);
	const auto* named = reader.field(effect, path, "named", false);
	if ((actorValue == nullptr) == (named == nullptr)) {
		reader.fail(path, "needs exactly one of 'actorValue' and 'named'");
		return std::nullopt;
	}
	if (actorValue != nullptr) {
		auto name = reader.text(actorValue, path / "actorValue");
		return name ? std::optional(ActorValueTarget{ std::move(*name) }) : std::nullopt;
	}
	const auto name = reader.text(named, path / "named");
	if (!name) {
		return std::nullopt;
	}
	const auto known = namedEffect(*name);
	if (!known) {
		reader.fail(path / "named", "is not a named effect of the module");
	}
	return known;
}

// The input of a named effect's curve (ST-9); relative when the effect sets
// none. Only named effects may set it.
std::optional<CurveInput> curveInput(TableReader& reader, const json& effect, const Pointer& path, bool named)
{
	const auto* input = reader.field(effect, path, "input", false);
	if (input == nullptr) {
		return CurveInput::Relative;
	}
	if (!named) {
		reader.fail(path / "input", "is only allowed on a named effect");
		return std::nullopt;
	}
	const auto text = reader.text(input, path / "input");
	if (text == "relative") {
		return CurveInput::Relative;
	}
	if (text == "absolute") {
		return CurveInput::Absolute;
	}
	if (text) {
		reader.fail(path / "input", "must be 'relative' or 'absolute'");
	}
	return std::nullopt;
}

std::optional<Effect> readEffect(TableReader& reader, const json& value, const Pointer& path)
{
	if (!reader.expectObject(value, path)) {
		return std::nullopt;
	}
	reader.checkKeys(value, path, kEffectKeys);
	auto target = effectTarget(reader, value, path);
	auto curve = reader.curve(reader.field(value, path, "curve", true), path / "curve");
	if (!target || !curve) {
		return std::nullopt;
	}
	const auto input = curveInput(reader, value, path, std::holds_alternative<NamedEffect>(*target));
	if (!input) {
		return std::nullopt;
	}
	return Effect{ std::move(*target), *curve, *input };
}

std::map<std::string, Effect> readEffects(TableReader& reader, const json* value, const Pointer& path)
{
	std::map<std::string, Effect> effects;
	if (value == nullptr || !reader.expectObject(*value, path)) {
		return effects;
	}
	for (const auto& [name, effect] : value->items()) {
		if (auto read = readEffect(reader, effect, path / name)) {
			effects.emplace(name, std::move(*read));
		}
	}
	return effects;
}

std::map<std::string, std::int32_t> readRaceAdjustments(TableReader& reader, const json* value, const Pointer& path)
{
	std::map<std::string, std::int32_t> adjustments;
	if (value == nullptr || !reader.expectObject(*value, path)) {
		return adjustments;
	}
	for (const auto& [race, adjustment] : value->items()) {
		if (const auto number = reader.int32(&adjustment, path / race)) {
			adjustments.emplace(race, *number);
		}
	}
	return adjustments;
}

// The group of a stat whose entry names none (ST-21).
std::string implicitGroup(std::string_view name)
{
	const auto space = namespaceOf(name);
	return std::string(space.empty() ? kOtherGroup : space);
}

StatEntry readStat(TableReader& reader, const std::string& name, const json& value, const Pointer& path)
{
	reader.checkKeys(value, path, kStatKeys);
	StatEntry entry;
	auto      group = reader.text(reader.field(value, path, "group", false), path / "group");
	entry.group = group ? std::move(*group) : implicitGroup(name);
	entry.displayName = reader.text(reader.field(value, path, "displayName", false), path / "displayName");
	entry.shortName = reader.text(reader.field(value, path, "shortName", false), path / "shortName");
	entry.defaultBase = reader.int32(reader.field(value, path, "base", true), path / "base").value_or(0);
	entry.raceAdjustments = readRaceAdjustments(reader, reader.field(value, path, "raceAdjustments", false), path / "raceAdjustments");
	entry.effects = readEffects(reader, reader.field(value, path, "effects", false), path / "effects");
	return entry;
}

SkillEntry readSkill(TableReader& reader, const json& value, const Pointer& path)
{
	reader.checkKeys(value, path, kSkillKeys);
	const auto primary = reader.text(reader.field(value, path, "primary", true), path / "primary");
	const auto secondary = reader.text(reader.field(value, path, "secondary", true), path / "secondary");
	SkillEntry entry;
	entry.primary = primary ? statName(reader, *primary, path / "primary").value_or(StatId{}) : StatId{};
	entry.secondary = secondary ? statName(reader, *secondary, path / "secondary").value_or(StatId{}) : StatId{};
	entry.primaryShare = reader.number(reader.field(value, path, "primaryShare", true), path / "primaryShare", 0.0, 1.0).value_or(0.0);
	return entry;
}

// An object of stat name -> curve.
std::map<StatId, Curve> readStatCurves(TableReader& reader, const json* value, const Pointer& path)
{
	std::map<StatId, Curve> curves;
	if (value == nullptr || !reader.expectObject(*value, path)) {
		return curves;
	}
	for (const auto& [name, curveValue] : value->items()) {
		auto stat = statName(reader, name, path / name);
		auto curve = reader.curve(&curveValue, path / name);
		if (stat && curve) {
			curves.emplace(std::move(*stat), *curve);
		}
	}
	return curves;
}

// An object of skill name -> curve.
std::map<std::string, Curve> readSkillCurves(TableReader& reader, const json* value, const Pointer& path)
{
	std::map<std::string, Curve> curves;
	if (value == nullptr || !reader.expectObject(*value, path)) {
		return curves;
	}
	for (const auto& [name, curveValue] : value->items()) {
		const bool known = checkName(reader, name, path / name, contains(kVanillaSkills, name));
		auto       curve = reader.curve(&curveValue, path / name);
		if (known && curve) {
			curves.emplace(name, *curve);
		}
	}
	return curves;
}

std::optional<Check> readCheck(TableReader& reader, const json& value, const Pointer& path)
{
	if (!reader.expectObject(value, path)) {
		return std::nullopt;
	}
	reader.checkKeys(value, path, kCheckKeys);
	auto statCurves = readStatCurves(reader, reader.field(value, path, "stats", false), path / "stats");
	auto skillCurves = readSkillCurves(reader, reader.field(value, path, "skills", false), path / "skills");
	auto result = reader.curve(reader.field(value, path, "result", true), path / "result");
	if (!result) {
		return std::nullopt;
	}
	return Check{ std::move(statCurves), std::move(skillCurves), *result };
}

ActionEntry readAction(TableReader& reader, const json& value, const Pointer& path)
{
	reader.checkKeys(value, path, kActionKeys);
	ActionEntry entry;
	if (const auto* check = reader.field(value, path, "check", false)) {
		entry.check = readCheck(reader, *check, path / "check");
	}
	entry.training = readStatCurves(reader, reader.field(value, path, "training", false), path / "training");
	return entry;
}

// An optional list of non-empty names.
std::vector<std::string> readNames(TableReader& reader, const json* value, const Pointer& path)
{
	std::vector<std::string> names;
	if (value == nullptr) {
		return names;
	}
	if (!value->is_array()) {
		reader.fail(path, "must be a list of names");
		return names;
	}
	for (std::size_t i = 0; i < value->size(); ++i) {
		if (auto name = reader.text(&(*value)[i], path / i)) {
			names.push_back(std::move(*name));
		}
	}
	return names;
}

IncompatibleEntry readIncompatible(TableReader& reader, const json& value, const Pointer& path)
{
	reader.checkKeys(value, path, kIncompatibleKeys);
	const auto*       plugins = reader.field(value, path, "plugins", false);
	const auto*       sksePlugins = reader.field(value, path, "sksePlugins", false);
	IncompatibleEntry entry{ readNames(reader, plugins, path / "plugins"), readNames(reader, sksePlugins, path / "sksePlugins") };
	// Counts the names as written, so a blank name is not reported twice.
	const auto written = [](const json* names) { return names == nullptr || !names->is_array() ? 0 : names->size(); };
	if (written(plugins) + written(sksePlugins) == 0) {
		reader.fail(path, "names no plugin to detect");
	}
	return entry;
}

using TableEntry = std::variant<StatEntry, SkillEntry, ActionEntry, IncompatibleEntry>;

// Reads one entry and checks its name. The caller decides what reported
// errors mean.
TableEntry readEntry(Table table, TableReader& reader, const std::string& name, const json& value)
{
	const auto path = Pointer{} / name;
	checkName(reader, name, path, isShipped(table, name));
	if (!reader.expectObject(value, path)) {
		return StatEntry{};
	}
	switch (table) {
	case Table::Stats:
		return readStat(reader, name, value, path);
	case Table::Skills:
		return readSkill(reader, value, path);
	case Table::Actions:
		return readAction(reader, value, path);
	case Table::Incompatible:
		return readIncompatible(reader, value, path);
	}
	return StatEntry{};
}

// Reads every entry of a merged table; an entry with errors is dropped and
// its errors go to `dropped`.
template <class Entry>
std::map<std::string, Entry> readTable(Table table, const MergeResult& merged, std::vector<ConfigError>& dropped)
{
	std::map<std::string, Entry> entries;
	for (const auto& [name, value] : merged.merged.items()) {
		std::vector<ConfigError> errors;
		TableReader              reader(folderName(table), &merged, errors);
		auto                     entry = readEntry(table, reader, name, value);
		if (errors.empty()) {
			entries.emplace(name, std::get<Entry>(std::move(entry)));
		}
		dropped.insert(dropped.end(), errors.begin(), errors.end());
	}
	return entries;
}

// The first stat a skills or actions entry names that the stats table does
// not define, as its JSON pointer and name.
std::optional<std::pair<Pointer, StatId>> undefinedStat(const SkillEntry& skill, const Pointer& path, const StatsTable& stats)
{
	for (const auto& [key, stat] : { std::pair{ "primary", &skill.primary }, std::pair{ "secondary", &skill.secondary } }) {
		if (!stats.contains(*stat)) {
			return std::pair{ path / key, *stat };
		}
	}
	return std::nullopt;
}

std::optional<std::pair<Pointer, StatId>> undefinedStat(const ActionEntry& action, const Pointer& path, const StatsTable& stats)
{
	const auto firstUndefined = [&stats](const std::map<StatId, Curve>& curves, const Pointer& at) -> std::optional<std::pair<Pointer, StatId>> {
		const auto it = std::ranges::find_if(curves, [&stats](const auto& entry) { return !stats.contains(entry.first); });
		return it == curves.end() ? std::nullopt : std::optional(std::pair{ at / it->first.name, it->first });
	};
	if (action.check) {
		if (auto found = firstUndefined(action.check->stats, path / "check" / "stats")) {
			return found;
		}
	}
	return firstUndefined(action.training, path / "training");
}

// Drops the entries that name a stat the stats table does not define.
template <class Entry>
void dropUndefinedStats(std::map<std::string, Entry>& entries, Table table, const MergeResult& merged, const StatsTable& stats,
	std::vector<ConfigError>& dropped)
{
	std::vector<ConfigError> unused;
	const TableReader        reader(folderName(table), &merged, unused);
	std::erase_if(entries, [&](const auto& entry) {
		const auto found = undefinedStat(entry.second, Pointer{} / entry.first, stats);
		if (found) {
			dropped.push_back({ reader.filesOf(found->first.to_string()), found->first.to_string(),
				std::format("names the stat {}, which no layer of the stats table defines; the entry is dropped", found->second.name) });
		}
		return found.has_value();
	});
}

using RaceSet = std::set<std::string, std::less<>>;

// Removes one stat's adjustments for races outside `races` and lists them in
// `ignored`.
void ignoreUnknownRaces(const StatId& id, StatEntry& stat, const RaceSet& races, const TableReader& reader, std::vector<ConfigError>& ignored)
{
	std::erase_if(stat.raceAdjustments, [&](const auto& adjustment) {
		if (races.contains(adjustment.first)) {
			return false;
		}
		const auto key = (Pointer{} / id.name / "raceAdjustments" / adjustment.first).to_string();
		ignored.push_back({ reader.filesOf(key), key, "no loaded plugin defines this race; the adjustment is ignored" });
		return true;
	});
}

void ignoreUnknownRaces(StatsTable& stats, const MergeResult& merged, const RaceSet& races, std::vector<ConfigError>& ignored)
{
	std::vector<ConfigError> unused;
	const TableReader        reader(folderName(Table::Stats), &merged, unused);
	for (auto& [id, stat] : stats) {
		ignoreUnknownRaces(id, stat, races, reader, ignored);
	}
}

// The groups the stats files set, in the order the files first name a stat
// of each (Stats plan, assumption 17).
std::vector<std::string> statGroups(const StatsTable& stats, const MergeResult& merged)
{
	std::vector<std::string> groups;
	for (const auto& name : merged.keyOrder) {
		const auto stat = stats.find(StatId{ name });
		const auto origin = merged.origins.find((Pointer{} / name / "group").to_string());
		if (stat == stats.end() || origin == merged.origins.end() || origin->second.layer == Layer::Registered) {
			continue;
		}
		if (std::ranges::find(groups, stat->second.group) == groups.end()) {
			groups.push_back(stat->second.group);
		}
	}
	return groups;
}

// A core stat without an entry, named with the file that removed it.
void checkCoreStats(const StatsTable& stats, const MergeResult& merged, std::vector<ConfigError>& errors)
{
	for (const auto& id : StatRegistry{}.activeStats()) {
		if (stats.contains(id)) {
			continue;
		}
		const auto key = (Pointer{} / id.name).to_string();
		const auto removal = std::ranges::find_if(merged.diagnostics, [&key](const Diagnostic& diagnostic) {
			return diagnostic.kind == Diagnostic::Kind::Removed && diagnostic.key == key;
		});
		const auto file = removal == merged.diagnostics.end() ? std::string() : std::format("stats/{}", removal->file);
		errors.push_back({ file, key, "is a core stat and needs a valid entry" });
	}
}

}

std::string_view folderName(Table table)
{
	switch (table) {
	case Table::Stats:
		return "stats";
	case Table::Skills:
		return "skills";
	case Table::Actions:
		return "actions";
	case Table::Incompatible:
		return "incompatible";
	}
	return "";
}

std::span<const std::string_view> vanillaSkills()
{
	return kVanillaSkills;
}

std::span<const std::string_view> builtInActions()
{
	return kBuiltInActions;
}

std::span<const std::string_view> namedEffectNames()
{
	return kNamedEffectNames;
}

bool isNamespaced(std::string_view name)
{
	const auto colon = name.find(':');
	return colon != std::string_view::npos && colon > 0 && colon + 1 < name.size() && name.find(':', colon + 1) == std::string_view::npos;
}

std::string_view namespaceOf(std::string_view name)
{
	if (!isNamespaced(name)) {
		return {};
	}
	return name.substr(0, name.find(':'));
}

EntryValidator entryValidator(Table table)
{
	return [table](const std::string& name, const json& entry) -> std::optional<std::string> {
		std::vector<ConfigError> errors;
		TableReader              reader(folderName(table), nullptr, errors);
		static_cast<void>(readEntry(table, reader, name, entry));
		if (errors.empty()) {
			return std::nullopt;
		}
		return std::format("{}: {}", errors.front().key, errors.front().message);
	};
}

std::expected<Tables, TablesError> buildTables(const MergedTables& merged, const std::set<std::string, std::less<>>& races)
{
	Tables tables;
	for (auto& [name, stat] : readTable<StatEntry>(Table::Stats, merged.stats, tables.dropped)) {
		tables.stats.emplace(StatId{ name }, std::move(stat));
	}
	std::vector<ConfigError> errors;
	checkCoreStats(tables.stats, merged.stats, errors);
	if (!errors.empty()) {
		return std::unexpected(TablesError{ std::move(errors), std::move(tables.dropped) });
	}
	ignoreUnknownRaces(tables.stats, merged.stats, races, tables.ignoredRaceAdjustments);
	tables.statGroups = statGroups(tables.stats, merged.stats);

	tables.skills = readTable<SkillEntry>(Table::Skills, merged.skills, tables.dropped);
	dropUndefinedStats(tables.skills, Table::Skills, merged.skills, tables.stats, tables.dropped);
	tables.actions = readTable<ActionEntry>(Table::Actions, merged.actions, tables.dropped);
	dropUndefinedStats(tables.actions, Table::Actions, merged.actions, tables.stats, tables.dropped);
	tables.incompatible = readTable<IncompatibleEntry>(Table::Incompatible, merged.incompatible, tables.dropped);

	const std::array<std::pair<Table, const MergeResult*>, 4> all{ { { Table::Stats, &merged.stats },
		{ Table::Skills, &merged.skills }, { Table::Actions, &merged.actions }, { Table::Incompatible, &merged.incompatible } } };
	for (const auto& [table, result] : all) {
		std::vector<ConfigError> unused;
		TableReader(folderName(table), result, unused).collectPlaceholders(tables.placeholders);
	}
	return tables;
}

}
