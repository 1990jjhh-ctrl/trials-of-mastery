#include "tom/stats/core/persistence.h"

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/stat_value.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstddef>
#include <exception>
#include <format>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tom::stats
{

namespace
{

using nlohmann::json;

// The fields of format version 1. The CBOR payload is an object:
//
//   r                number
//   pendingPoints    int32
//   registeredStats  [stat name]
//   actors           [{ id               uint32, the saved ID
//                       race             string, the race's editor ID
//                       multiplier       number
//                       values           { stat: { base, accumulated, active: int32; xp: number } }
//                       modifiers        { stat: { key: int32 } }
//                       baseAdjustments  { stat: { key: int32 } }
//                       defaultBases     { stat: int32 }
//                       contributions    { actor value: number } }]
//
// Every number but r is finite, and every name and key is non-empty.
constexpr const char* kR = "r";
constexpr const char* kPendingPoints = "pendingPoints";
constexpr const char* kRegisteredStats = "registeredStats";
constexpr const char* kActors = "actors";
constexpr const char* kId = "id";
constexpr const char* kRace = "race";
constexpr const char* kMultiplier = "multiplier";
constexpr const char* kValues = "values";
constexpr const char* kModifiers = "modifiers";
constexpr const char* kBaseAdjustments = "baseAdjustments";
constexpr const char* kDefaultBases = "defaultBases";
constexpr const char* kContributions = "contributions";
constexpr const char* kBase = "base";
constexpr const char* kAccumulated = "accumulated";
constexpr const char* kActive = "active";
constexpr const char* kXp = "xp";

constexpr std::size_t kVersionSize = 4;

[[nodiscard]] std::unexpected<Error> rejected(std::string message)
{
	return std::unexpected(Error{ std::move(message) });
}

// Writing

template <typename Value, typename Convert>
[[nodiscard]] json byStat(const std::map<StatId, Value>& map, Convert convert)
{
	json object = json::object();
	for (const auto& [id, value] : map) {
		object[id.name] = convert(value);
	}
	return object;
}

[[nodiscard]] json encodeValue(const StatValue& value)
{
	return json{ { kBase, value.base }, { kAccumulated, value.accumulated }, { kActive, value.active }, { kXp, value.xp } };
}

[[nodiscard]] json encodeKeyed(const std::map<std::string, std::int32_t>& amounts)
{
	return json(amounts);
}

[[nodiscard]] json encodeActor(ActorId id, const ActorSnapshot& actor)
{
	const ActorStats& stats = actor.stats;
	return json{
		{ kId, id },
		{ kRace, actor.race },
		{ kMultiplier, stats.multiplier },
		{ kValues, byStat(stats.values, encodeValue) },
		{ kModifiers, byStat(stats.modifiers, encodeKeyed) },
		{ kBaseAdjustments, byStat(stats.baseAdjustments, encodeKeyed) },
		{ kDefaultBases, byStat(stats.defaultBases, [](std::int32_t base) { return base; }) },
		{ kContributions, json(actor.ledger.contributions) },
	};
}

// Reading. A malformed payload throws MalformedSave, which deserialize()
// turns into its error.

class MalformedSave : public std::runtime_error
{
public:
	using std::runtime_error::runtime_error;
};

[[nodiscard]] const json& member(const json& object, const char* key, std::string_view where)
{
	if (!object.is_object()) {
		throw MalformedSave(std::format("{} is not an object", where));
	}
	const auto it = object.find(key);
	if (it == object.end()) {
		throw MalformedSave(std::format("{} has no '{}'", where, key));
	}
	return *it;
}

template <typename Int>
[[nodiscard]] Int toInteger(const json& value, std::string_view what)
{
	// CBOR keeps non-negative integers apart from negative ones.
	if (value.is_number_unsigned() && std::in_range<Int>(value.get<std::uint64_t>())) {
		return static_cast<Int>(value.get<std::uint64_t>());
	}
	if (value.is_number_integer() && !value.is_number_unsigned() && std::in_range<Int>(value.get<std::int64_t>())) {
		return static_cast<Int>(value.get<std::int64_t>());
	}
	throw MalformedSave(std::format("{} is not an integer in its range", what));
}

[[nodiscard]] double toNumber(const json& value, std::string_view what)
{
	if (!value.is_number()) {
		throw MalformedSave(std::format("{} is not a number", what));
	}
	return value.get<double>();
}

[[nodiscard]] double toFinite(const json& value, std::string_view what)
{
	const double number = toNumber(value, what);
	if (!std::isfinite(number)) {
		throw MalformedSave(std::format("{} is not finite", what));
	}
	return number;
}

[[nodiscard]] std::string toText(const json& value, std::string_view what)
{
	if (!value.is_string()) {
		throw MalformedSave(std::format("{} is not a string", what));
	}
	return value.get<std::string>();
}

[[nodiscard]] std::string toName(const std::string& name, std::string_view what)
{
	if (name.empty()) {
		throw MalformedSave(std::format("{} has an empty name", what));
	}
	return name;
}

[[nodiscard]] const json& toArray(const json& value, std::string_view what)
{
	if (!value.is_array()) {
		throw MalformedSave(std::format("{} is not an array", what));
	}
	return value;
}

// Reads an object whose keys are names, converting each value.
template <typename Key, typename Value, typename Convert>
[[nodiscard]] std::map<Key, Value> toMap(const json& object, std::string_view what, Convert convert)
{
	if (!object.is_object()) {
		throw MalformedSave(std::format("{} is not an object", what));
	}
	std::map<Key, Value> result;
	for (const auto& [name, value] : object.items()) {
		const std::string where = std::format("{} '{}'", what, name);
		result.emplace(Key{ toName(name, what) }, convert(value, where));
	}
	return result;
}

// Points and XP follow the rules of StatsService::setValues(): not negative,
// and the XP finite.
[[nodiscard]] StatValue decodeValue(const json& value, std::string_view where)
{
	const StatValue result{
		.base = toInteger<std::int32_t>(member(value, kBase, where), where),
		.accumulated = toInteger<std::int32_t>(member(value, kAccumulated, where), where),
		.active = toInteger<std::int32_t>(member(value, kActive, where), where),
		.xp = toFinite(member(value, kXp, where), where),
	};
	if (result.accumulated < 0 || result.active < 0) {
		throw MalformedSave(std::format("{} has negative points", where));
	}
	if (result.xp < 0.0) {
		throw MalformedSave(std::format("{} has negative XP", where));
	}
	return result;
}

[[nodiscard]] std::int32_t decodeInt32(const json& value, std::string_view where)
{
	return toInteger<std::int32_t>(value, where);
}

[[nodiscard]] std::map<std::string, std::int32_t> decodeKeyed(const json& value, std::string_view where)
{
	return toMap<std::string, std::int32_t>(value, where, decodeInt32);
}

// The saved ID and the actor.
[[nodiscard]] std::pair<ActorId, ActorSnapshot> decodeActor(const json& actor, std::size_t index)
{
	const std::string where = std::format("actor {}", index);
	ActorSnapshot     snapshot;
	snapshot.race = toText(member(actor, kRace, where), where + " race");
	ActorStats& stats = snapshot.stats;
	stats.multiplier = toFinite(member(actor, kMultiplier, where), where + " multiplier");
	stats.values = toMap<StatId, StatValue>(member(actor, kValues, where), where + " stat", decodeValue);
	stats.modifiers = toMap<StatId, std::map<std::string, std::int32_t>>(member(actor, kModifiers, where),
		where + " modifiers of stat", decodeKeyed);
	stats.baseAdjustments = toMap<StatId, std::map<std::string, std::int32_t>>(member(actor, kBaseAdjustments, where),
		where + " base adjustments of stat", decodeKeyed);
	stats.defaultBases = toMap<StatId, std::int32_t>(member(actor, kDefaultBases, where),
		where + " default base of stat", decodeInt32);
	snapshot.ledger.contributions = toMap<std::string, double>(member(actor, kContributions, where),
		where + " contribution to", toFinite);
	return { toInteger<ActorId>(member(actor, kId, where), where + " ID"), std::move(snapshot) };
}

void addResolved(SaveState& state, ActorIdResolver& resolver, ActorId savedId, ActorSnapshot actor)
{
	const std::optional<ActorId> current = resolver.resolve(savedId);
	if (!current) {
		state.droppedActors.push_back(DroppedActor{ .savedId = savedId, .reason = "not resolved" });
		return;
	}
	if (!state.actors.try_emplace(*current, std::move(actor)).second) {
		state.droppedActors.push_back(DroppedActor{ .savedId = savedId,
			.reason = std::format("resolves to {:08X}, which an earlier saved actor has", *current) });
	}
}

[[nodiscard]] SaveState decodeState(const json& payload, ActorIdResolver& resolver)
{
	constexpr std::string_view where = "the save";
	SaveState                  state;
	state.r = toNumber(member(payload, kR, where), "r");
	state.pendingPoints = toInteger<std::int32_t>(member(payload, kPendingPoints, where), "the pending points");
	for (const auto& stat : toArray(member(payload, kRegisteredStats, where), "the registered stats")) {
		state.registeredStats.push_back(StatId{ toName(toText(stat, "a registered stat"), "a registered stat") });
	}
	const json& actors = toArray(member(payload, kActors, where), "the actors");
	for (std::size_t index = 0; index < actors.size(); ++index) {
		auto [savedId, actor] = decodeActor(actors[index], index);
		addResolved(state, resolver, savedId, std::move(actor));
	}
	return state;
}

// The deepest nesting of arrays and objects the CBOR payload may have.
// Version 1 needs 5 (the save, its actors, an actor, its values, a stat's
// value); the limit leaves room and keeps the parser, which recurses once
// per level, from running out of stack on corrupt data.
constexpr std::size_t kMaxDepth = 8;

// A SAX handler for json::sax_parse() that builds the payload like
// json::from_cbor(), with two differences for corrupt data: it fails beyond
// kMaxDepth, and it ignores the element counts the CBOR headers declare.
// from_cbor() reserves room for them and throws for a count beyond
// max_size(), even when asked not to throw.
class BoundedDomReader
{
public:
	explicit BoundedDomReader(json& root) : root_(root) {}

	bool null() { return add(nullptr); }
	bool boolean(bool value) { return add(value); }
	bool number_integer(json::number_integer_t value) { return add(value); }
	bool number_unsigned(json::number_unsigned_t value) { return add(value); }
	bool number_float(json::number_float_t value, const json::string_t& /*text*/) { return add(value); }
	bool string(json::string_t& value) { return add(std::move(value)); }
	bool binary(json::binary_t& value) { return add(json::binary(std::move(value))); }
	bool start_object(std::size_t /*elements*/) { return open(json::object()); }
	bool end_object() { return close(); }
	bool start_array(std::size_t /*elements*/) { return open(json::array()); }
	bool end_array() { return close(); }

	bool key(json::string_t& name)
	{
		key_ = std::move(name);
		return true;
	}

	bool parse_error(std::size_t /*position*/, const std::string& /*token*/, const json::exception& error)
	{
		error_ = error.what();
		return false;
	}

	[[nodiscard]] const std::string& error() const { return error_; }

private:
	// Puts the value where the parser is: at the root, at the end of the open
	// array, or in the open object under the last key.
	json& place(json value);

	bool add(json value)
	{
		place(std::move(value));
		return true;
	}

	bool open(json container);

	bool close()
	{
		open_.pop_back();
		return true;
	}

	json& root_;
	// The open arrays and objects, innermost last. Each is the last value
	// placed in the one before it, which gets nothing new while it is open,
	// so the pointers stay valid.
	std::vector<json*> open_;
	std::string        key_;
	std::string        error_;
};

json& BoundedDomReader::place(json value)
{
	if (open_.empty()) {
		root_ = std::move(value);
		return root_;
	}
	json& parent = *open_.back();
	if (parent.is_array()) {
		parent.push_back(std::move(value));
		return parent.back();
	}
	json& slot = parent[key_];
	slot = std::move(value);
	return slot;
}

bool BoundedDomReader::open(json container)
{
	if (open_.size() >= kMaxDepth) {
		error_ = std::format("arrays and objects nest deeper than {} levels", kMaxDepth);
		return false;
	}
	open_.push_back(&place(std::move(container)));
	return true;
}

[[nodiscard]] std::uint32_t readVersion(std::span<const std::uint8_t> bytes)
{
	std::uint32_t version = 0;
	for (std::size_t i = 0; i < kVersionSize; ++i) {
		version |= std::uint32_t{ bytes[i] } << (8 * i);
	}
	return version;
}

}

Bytes serialize(const StatsService& service)
{
	json registered = json::array();
	for (const auto& id : service.registry().registeredStats()) {
		registered.push_back(id.name);
	}
	json actors = json::array();
	for (const ActorId id : service.actorIds()) {
		// The IDs come from the service, so the snapshot exists.
		actors.push_back(encodeActor(id, *service.actorSnapshot(id)));
	}
	const json payload{
		{ kR, service.r() },
		{ kPendingPoints, service.pendingPoints() },
		{ kRegisteredStats, std::move(registered) },
		{ kActors, std::move(actors) },
	};

	Bytes bytes;
	for (std::size_t i = 0; i < kVersionSize; ++i) {
		bytes.push_back(static_cast<std::uint8_t>(kSaveFormatVersion >> (8 * i)));
	}
	json::to_cbor(payload, bytes);
	return bytes;
}

std::expected<SaveState, Error> deserialize(std::span<const std::uint8_t> bytes, ActorIdResolver& resolver)
{
	if (bytes.size() < kVersionSize) {
		return rejected("the save data is too short to hold a format version");
	}
	const std::uint32_t version = readVersion(bytes);
	if (version != kSaveFormatVersion) {
		return rejected(std::format("save format version {} is not supported; this build reads version {} only",
			version, kSaveFormatVersion));
	}
	// Nothing may escape: in game, an exception here would end the game on
	// a corrupt co-save.
	try {
		json             payload;
		BoundedDomReader reader(payload);
		if (!json::sax_parse(bytes.begin() + kVersionSize, bytes.end(), &reader, json::input_format_t::cbor, true)) {
			return rejected(std::format("the save data is not valid CBOR: {}", reader.error()));
		}
		return decodeState(payload, resolver);
	} catch (const MalformedSave& error) {
		return rejected(std::format("malformed save data: {}", error.what()));
	} catch (const std::exception& error) {
		// json::exception, and std::bad_alloc on a payload too large.
		return rejected(std::format("the save data could not be read: {}", error.what()));
	}
}

}
