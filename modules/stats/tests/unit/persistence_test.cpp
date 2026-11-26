// The save format of the stats service (Stats plan, C8, PRD §8).
#include "tom/stats/core/persistence.h"

#include "stats_service_fixture.h"

#include "tom/stats/core/actor_stats.h"
#include "tom/stats/core/effects.h"
#include "tom/stats/core/stat_value.h"
#include "tom/stats/core/stats_service.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using nlohmann::json;
using tom::stats::ActorIdResolver;
using tom::stats::ActorSnapshot;
using tom::stats::ActorStats;
using tom::stats::AvDelta;
using tom::stats::AvDeltasByActor;
using tom::stats::Bytes;
using tom::stats::ContributionLedger;
using tom::stats::deserialize;
using tom::stats::DroppedActor;
using tom::stats::initStat;
using tom::stats::kRaceAdjustmentKey;
using tom::stats::kSaveFormatVersion;
using tom::stats::SaveState;
using tom::stats::serialize;
using tom::stats::Settings;
using tom::stats::StatsService;
using tom::stats::StatValue;

using namespace service_test;

namespace
{

const StatId kDormant{ "Mod:Dormant" };  // saved, with no owner this session

// Maps saved IDs as given; an ID not in the map resolves to itself.
class MapResolver : public ActorIdResolver
{
public:
	explicit MapResolver(std::map<ActorId, std::optional<ActorId>> ids = {}) : ids_(std::move(ids)) {}

	std::optional<ActorId> resolve(ActorId saved) override
	{
		const auto it = ids_.find(saved);
		return it == ids_.end() ? saved : it->second;
	}

private:
	std::map<ActorId, std::optional<ActorId>> ids_;
};

// A service with every field of PRD §8 set: XP, points, modifiers, base
// adjustments per key, a multiplier, contributions, default bases, r,
// pending points, an active and a dormant registered stat.
StatsService savedService()
{
	StatsService stats = service();
	REQUIRE(stats.registerStat(kLuck));
	stats.addDormantStat(kDormant);
	REQUIRE(stats.addActor(kPlayer, "NordRace"));
	REQUIRE(stats.addXp(kPlayer, kCon, 350.0));
	const auto plusOne = [](std::int32_t points) { return points + 1.0; };
	REQUIRE(stats.ascend(kPlayer, kCon, plusOne));
	REQUIRE(stats.addXp(kPlayer, kEnd, 150.0));
	REQUIRE(stats.setValues(kPlayer, kStr, StatValue{ .base = 10, .accumulated = 4, .active = 2, .xp = 0.75 }));
	REQUIRE(stats.setModifier(kPlayer, kEnd, "Mod:Buff", 3));
	REQUIRE(stats.setModifier(kPlayer, kEnd, "Mod:Curse", -1));
	REQUIRE(stats.setBaseAdjustment(kPlayer, kCon, "Mod:Class", -2));
	REQUIRE(stats.setMultiplier(kPlayer, 1.5));
	REQUIRE(stats.setR(1.75));
	stats.onPlayerLevelUp(2);

	ActorStats npc;
	initStat(npc, kDormant, 7);
	npc.values[kDormant].accumulated = 2;
	(void)stats.restoreActor(kNpc, "ImperialRace", npc, {});
	REQUIRE(stats.setBaseAdjustment(kNpc, kStr, "Mod:Class", 1));
	return stats;
}

// The payload of a save after the version.
json payloadOf(const Bytes& bytes)
{
	return json::from_cbor(bytes.begin() + 4, bytes.end());
}

Bytes withPayload(const json& payload, std::uint32_t version = kSaveFormatVersion)
{
	Bytes bytes{ static_cast<std::uint8_t>(version), static_cast<std::uint8_t>(version >> 8),
		static_cast<std::uint8_t>(version >> 16), static_cast<std::uint8_t>(version >> 24) };
	json::to_cbor(payload, bytes);
	return bytes;
}

}

TEST_CASE("every saved field of PRD §8 comes back from the bytes", "[persistence]")
{
	const StatsService saved = savedService();
	MapResolver        resolver;

	const auto state = deserialize(serialize(saved), resolver);

	REQUIRE(state);
	CHECK(state->r == 1.75);
	CHECK(state->pendingPoints == 3);
	CHECK(state->registeredStats == std::vector<StatId>{ kLuck, kDormant });
	CHECK(state->droppedActors.empty());
	REQUIRE(state->actors.size() == 2);
	for (const ActorId actor : saved.actorIds()) {
		CHECK(state->actors.at(actor) == *saved.actorSnapshot(actor));
	}

	// The fields one by one, so a failure names the field.
	const ActorSnapshot& player = state->actors.at(kPlayer);
	CHECK(player.race == "NordRace");
	CHECK(player.stats.multiplier == 1.5);
	CHECK(player.stats.values.at(kStr) == StatValue{ .base = 10, .accumulated = 4, .active = 2, .xp = 0.75 });
	CHECK(player.stats.values.at(kEnd).xp == 50.0);
	CHECK(player.stats.values.at(kEnd).active == 1);
	CHECK(player.stats.modifiers.at(kEnd) == std::map<std::string, std::int32_t>{ { "Mod:Buff", 3 }, { "Mod:Curse", -1 } });
	CHECK(player.stats.baseAdjustments.at(kCon) ==
		  std::map<std::string, std::int32_t>{ { std::string(kRaceAdjustmentKey), 2 }, { "Mod:Class", -2 } });
	CHECK(player.stats.baseAdjustments.at(kLuck) == std::map<std::string, std::int32_t>{ { std::string(kRaceAdjustmentKey), 1 } });
	CHECK(player.stats.defaultBases.at(kCon) == 10);
	CHECK(player.stats.defaultBases.at(kLuck) == 5);
	CHECK(player.ledger == saved.actorSnapshot(kPlayer)->ledger);
	CHECK_FALSE(player.ledger.contributions.empty());

	const ActorSnapshot& npc = state->actors.at(kNpc);
	CHECK(npc.stats.values.at(kDormant) == StatValue{ .base = 7, .accumulated = 2 });
	CHECK(npc.stats.defaultBases.at(kDormant) == 7);
	CHECK(npc.stats.baseAdjustments.at(kStr).at("Mod:Class") == 1);
}

TEST_CASE("the save format holds neither N nor the claims", "[persistence]")
{
	StatsService saved = savedService();
	REQUIRE(saved.setN(2));
	REQUIRE(saved.claimAdvancing("Mastery"));
	REQUIRE(saved.claimPointAssignment("Mastery"));

	const json payload = payloadOf(serialize(saved));

	std::vector<std::string> keys;
	for (const auto& [key, value] : payload.items()) {
		keys.push_back(key);
	}
	CHECK(keys == std::vector<std::string>{ "actors", "pendingPoints", "r", "registeredStats" });
}

TEST_CASE("every saved base has its default base last applied", "[persistence][base]")
{
	const StatsService saved = savedService();
	MapResolver        resolver;

	const auto state = deserialize(serialize(saved), resolver);

	REQUIRE(state);
	for (const auto& [actor, snapshot] : state->actors) {
		for (const auto& [stat, value] : snapshot.stats.values) {
			INFO(stat.name);
			CHECK(snapshot.stats.defaultBases.contains(stat));
		}
	}
}

TEST_CASE("the bytes start with the format version", "[persistence][version]")
{
	const Bytes bytes = serialize(service());

	REQUIRE(bytes.size() > 4);
	CHECK(bytes[0] == kSaveFormatVersion);
	CHECK(bytes[1] == 0);
	CHECK(bytes[2] == 0);
	CHECK(bytes[3] == 0);
}

TEST_CASE("other format versions are rejected explicitly", "[persistence][version]")
{
	const json  payload = payloadOf(serialize(savedService()));
	MapResolver resolver;

	const auto older = deserialize(withPayload(payload, 0), resolver);
	const auto newer = deserialize(withPayload(payload, kSaveFormatVersion + 1), resolver);

	REQUIRE_FALSE(older);
	CHECK_THAT(older.error().message, ContainsSubstring("version 0 is not supported"));
	REQUIRE_FALSE(newer);
	CHECK_THAT(newer.error().message, ContainsSubstring("version 2 is not supported"));
	CHECK(deserialize(withPayload(payload), resolver));
}

TEST_CASE("unresolved actors are dropped and listed", "[persistence][actors]")
{
	const StatsService saved = savedService();
	// The NPC is gone; the player keeps its ID.
	MapResolver resolver({ { kNpc, std::nullopt } });

	const auto state = deserialize(serialize(saved), resolver);

	REQUIRE(state);
	CHECK(state->actors.size() == 1);
	CHECK(state->actors.contains(kPlayer));
	CHECK(state->droppedActors == std::vector<DroppedActor>{ { .savedId = kNpc, .reason = "not resolved" } });
}

TEST_CASE("actors are stored under their resolved ID", "[persistence][actors]")
{
	const StatsService saved = savedService();
	MapResolver        resolver({ { kNpc, 0x0201A2B3 } });

	const auto state = deserialize(serialize(saved), resolver);

	REQUIRE(state);
	CHECK_FALSE(state->actors.contains(kNpc));
	CHECK(state->actors.at(0x0201A2B3) == *saved.actorSnapshot(kNpc));
	CHECK(state->droppedActors.empty());
}

TEST_CASE("an actor that resolves to an ID already taken is dropped", "[persistence][actors]")
{
	const StatsService saved = savedService();
	MapResolver        resolver({ { kNpc, kPlayer } });

	const auto state = deserialize(serialize(saved), resolver);

	REQUIRE(state);
	CHECK(state->actors.size() == 1);
	CHECK(state->actors.at(kPlayer) == *saved.actorSnapshot(kPlayer));
	REQUIRE(state->droppedActors.size() == 1);
	CHECK(state->droppedActors[0].savedId == kNpc);
	CHECK_THAT(state->droppedActors[0].reason, ContainsSubstring("00000014"));
}

TEST_CASE("bytes that are not a save are rejected", "[persistence][malformed]")
{
	MapResolver resolver;

	CHECK_FALSE(deserialize(Bytes{}, resolver));
	CHECK_FALSE(deserialize(Bytes{ 1, 0, 0 }, resolver));
	CHECK_FALSE(deserialize(Bytes{ 1, 0, 0, 0 }, resolver));
	CHECK_FALSE(deserialize(Bytes{ 1, 0, 0, 0, 0xFF, 0x00 }, resolver));
	// Cut off in the middle of the payload.
	Bytes cut = serialize(savedService());
	cut.resize(cut.size() / 2);
	CHECK_FALSE(deserialize(cut, resolver));
}

TEST_CASE("a CBOR length beyond the memory is rejected without an exception", "[persistence][malformed]")
{
	MapResolver resolver;

	// An array and a map that declare 2^63 elements.
	const auto array = deserialize(Bytes{ 1, 0, 0, 0, 0x9B, 0x80, 0, 0, 0, 0, 0, 0, 0 }, resolver);
	const auto map = deserialize(Bytes{ 1, 0, 0, 0, 0xBB, 0x80, 0, 0, 0, 0, 0, 0, 0 }, resolver);

	CHECK_FALSE(array);
	CHECK_FALSE(map);
}

TEST_CASE("deeply nested CBOR is rejected", "[persistence][malformed]")
{
	MapResolver resolver;
	// 100 000 nested one-element arrays.
	Bytes bytes{ 1, 0, 0, 0 };
	bytes.resize(bytes.size() + 100'000, 0x81);

	const auto state = deserialize(bytes, resolver);

	REQUIRE_FALSE(state);
	CHECK_THAT(state.error().message, ContainsSubstring("nest deeper"));
}

TEST_CASE("malformed fields are rejected with their place", "[persistence][malformed]")
{
	const json  valid = payloadOf(serialize(savedService()));
	MapResolver resolver;

	const auto rejects = [&](const json& payload, const std::string& message) {
		const auto state = deserialize(withPayload(payload), resolver);
		REQUIRE_FALSE(state);
		CHECK_THAT(state.error().message, ContainsSubstring(message));
	};

	json missing = valid;
	missing.erase("pendingPoints");
	rejects(missing, "has no 'pendingPoints'");

	json outOfRange = valid;
	outOfRange["pendingPoints"] = std::int64_t{ std::numeric_limits<std::int32_t>::max() } + 1;
	rejects(outOfRange, "the pending points is not an integer in its range");

	json negativeId = valid;
	negativeId["actors"][0]["id"] = -1;
	rejects(negativeId, "actor 0 ID is not an integer in its range");

	json fraction = valid;
	fraction["actors"][0]["values"]["CON"]["base"] = 10.5;
	rejects(fraction, "actor 0 stat 'CON' is not an integer");

	json negativePoints = valid;
	negativePoints["actors"][0]["values"]["CON"]["accumulated"] = -1;
	rejects(negativePoints, "actor 0 stat 'CON' has negative points");

	json negativeActive = valid;
	negativeActive["actors"][0]["values"]["CON"]["active"] = -1;
	rejects(negativeActive, "actor 0 stat 'CON' has negative points");

	json negativeXp = valid;
	negativeXp["actors"][0]["values"]["CON"]["xp"] = -0.5;
	rejects(negativeXp, "actor 0 stat 'CON' has negative XP");

	json infiniteXp = valid;
	infiniteXp["actors"][0]["values"]["CON"]["xp"] = std::numeric_limits<double>::infinity();
	rejects(infiniteXp, "actor 0 stat 'CON' is not finite");

	json notFinite = valid;
	notFinite["actors"][1]["multiplier"] = std::numeric_limits<double>::infinity();
	rejects(notFinite, "actor 1 multiplier is not finite");

	json emptyKey = valid;
	emptyKey["actors"][0]["modifiers"]["END"][""] = 1;
	rejects(emptyKey, "empty name");

	json emptyStat = valid;
	emptyStat["registeredStats"].push_back("");
	rejects(emptyStat, "a registered stat has an empty name");

	json wrongType = valid;
	wrongType["actors"] = json::object();
	rejects(wrongType, "the actors is not an array");
}
