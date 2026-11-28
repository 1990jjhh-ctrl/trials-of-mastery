#include "Serialization.h"

#include "DeltaLog.h"
#include "Service.h"

#include "tom/stats/core/persistence.h"
#include "tom/stats/core/stats_service.h"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <optional>
#include <utility>

namespace tom::stats
{
namespace
{
// A four-character code as SKSE shows it in its log, e.g. 'TMST'.
constexpr std::uint32_t fourCharacterCode(const char (&code)[5])
{
	return (std::uint32_t{ static_cast<unsigned char>(code[0]) } << 24) |
	       (std::uint32_t{ static_cast<unsigned char>(code[1]) } << 16) |
	       (std::uint32_t{ static_cast<unsigned char>(code[2]) } << 8) | std::uint32_t{ static_cast<unsigned char>(code[3]) };
}

// The plugin's ID in the co-save and its one record, which holds the bytes of
// serialize(). The record's version says how the record is laid out; the
// bytes carry their own format version (kSaveFormatVersion).
constexpr std::uint32_t kUniqueId = fourCharacterCode("TMST");
constexpr std::uint32_t kStatsRecord = fourCharacterCode("STAT");
constexpr std::uint32_t kStatsRecordVersion = 1;

// Maps a saved FormID to the current one through SKSE, which knows the load
// order of the save and of this game. A FormID of a plugin that is no longer
// loaded does not resolve, so its actor is dropped.
class CoSaveActorIds final : public ActorIdResolver
{
public:
	explicit CoSaveActorIds(const SKSE::SerializationInterface& coSave) :
		coSave_(coSave) {}

	[[nodiscard]] std::optional<ActorId> resolve(ActorId saved) override
	{
		RE::FormID current = 0;
		if (!coSave_.ResolveFormID(saved, current)) {
			return std::nullopt;
		}
		return current;
	}

private:
	const SKSE::SerializationInterface& coSave_;
};

struct SavedStats
{
	Bytes       bytes;
	std::size_t actors = 0;
};

void save(SKSE::SerializationInterface& coSave)
{
	SKSE::log::info("Co-save: saving on thread {}", REX::W32::GetCurrentThreadId());
	const auto saved = withService([](const StatsService& service) {
		return SavedStats{ .bytes = serialize(service), .actors = service.actorIds().size() };
	});
	if (!saved) {
		SKSE::log::warn("Co-save: no stats service, so the save holds no stats");
		return;
	}
	if (saved->bytes.size() > (std::numeric_limits<std::uint32_t>::max)()) {
		SKSE::log::error("Co-save: {} bytes of stats do not fit in a record; the save holds no stats",
			saved->bytes.size());
		return;
	}
	const auto length = static_cast<std::uint32_t>(saved->bytes.size());
	if (!coSave.OpenRecord(kStatsRecord, kStatsRecordVersion) || !coSave.WriteRecordData(saved->bytes.data(), length)) {
		SKSE::log::error("Co-save: the stats record could not be written");
		return;
	}
	SKSE::log::info("Co-save: wrote record STAT version {}, format version {}, {} actors, {} bytes", kStatsRecordVersion,
		kSaveFormatVersion, saved->actors, length);
}

void restoreRecord(const SKSE::SerializationInterface& coSave, const Bytes& bytes)
{
	// SKSE's FormID resolution is a game call, so it runs before the lock.
	CoSaveActorIds resolver{ coSave };
	auto           state = deserialize(bytes, resolver);
	if (!state) {
		SKSE::log::error("Co-save: record STAT version {} not read: {}; the save's stats are not restored",
			kStatsRecordVersion, state.error().message);
		return;
	}
	for (const auto& dropped : state->droppedActors) {
		SKSE::log::warn("Co-save: saved actor {:08X} dropped: {}", dropped.savedId, dropped.reason);
	}
	const auto actors = state->actors.size();
	const auto dropped = state->droppedActors.size();
	const auto result = withService([&](StatsService& service) { return restore(service, std::move(*state)); });
	if (!result) {
		SKSE::log::warn("Co-save: no stats service, so the save's stats are not restored");
		return;
	}
	SKSE::log::info("Co-save: read record STAT version {}, format version {}, {} actors restored, {} dropped",
		kStatsRecordVersion, kSaveFormatVersion, actors, dropped);
	for (const auto& error : result->errors) {
		SKSE::log::warn("Co-save: {}", error.message);
	}
	logDeltas("restore", result->deltas);
}

void load(SKSE::SerializationInterface& coSave)
{
	SKSE::log::info("Co-save: loading on thread {}", REX::W32::GetCurrentThreadId());
	std::uint32_t type = 0;
	std::uint32_t version = 0;
	std::uint32_t length = 0;
	while (coSave.GetNextRecordInfo(type, version, length)) {
		if (type != kStatsRecord) {
			SKSE::log::warn("Co-save: unknown record {:08X} version {} skipped", type, version);
			continue;
		}
		if (version != kStatsRecordVersion) {
			SKSE::log::error(
				"Co-save: record STAT version {} is not supported, this build reads version {}; the "
				"save's stats are not restored",
				version, kStatsRecordVersion);
			continue;
		}
		Bytes bytes(length);
		if (coSave.ReadRecordData(bytes.data(), length) != length) {
			SKSE::log::error("Co-save: record STAT is shorter than its {} bytes; the save's stats are not restored", length);
			continue;
		}
		restoreRecord(coSave, bytes);
	}
}

void revert(SKSE::SerializationInterface& /*coSave*/)
{
	SKSE::log::info("Co-save: revert on thread {}", REX::W32::GetCurrentThreadId());
	if (!withService([](StatsService& service) { service.resetSaveState(); })) {
		SKSE::log::warn("Co-save: no stats service to revert");
	}
}

// Nothing may escape into the game, which would end it.
template <void (*Callback)(SKSE::SerializationInterface&)>
void guarded(SKSE::SerializationInterface* coSave)
{
	try {
		Callback(*coSave);
	} catch (const std::exception& error) {
		SKSE::log::error("Co-save: {}", error.what());
	}
}
}

void registerSerialization()
{
	const auto* coSave = SKSE::GetSerializationInterface();
	coSave->SetUniqueID(kUniqueId);
	coSave->SetSaveCallback(guarded<save>);
	coSave->SetLoadCallback(guarded<load>);
	coSave->SetRevertCallback(guarded<revert>);
}
}
