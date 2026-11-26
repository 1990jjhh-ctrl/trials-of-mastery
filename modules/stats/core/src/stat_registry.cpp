#include "tom/stats/core/stat_registry.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace tom::stats
{

namespace
{
// ST-1, in the order of PRD §6.
constexpr std::array<std::string_view, 8> kCoreStatNames{ "STR", "DEX", "CON", "INT", "WIS", "PER", "END", "CHA" };
}

template <typename Entries>
auto* StatRegistry::findIn(Entries& entries, const StatId& id)
{
	const auto it = std::ranges::find(entries, id, &Entry::id);
	return it == entries.end() ? nullptr : &*it;
}

StatRegistry::StatRegistry()
{
	entries_.reserve(kCoreStatNames.size());
	for (const auto name : kCoreStatNames) {
		entries_.push_back(Entry{ .id = StatId{ std::string(name) }, .core = true, .dormant = false, .freePoints = FreePoints::Granted });
	}
}

bool StatRegistry::registerStat(const StatId& id, FreePoints freePoints)
{
	if (id.name.empty()) {
		return false;
	}
	Entry* entry = findIn(entries_, id);
	if (entry == nullptr) {
		entries_.push_back(Entry{ .id = id, .core = false, .dormant = false, .freePoints = freePoints });
		return true;
	}
	if (!entry->dormant) {
		return false;
	}
	entry->dormant = false;
	entry->freePoints = freePoints;
	return true;
}

void StatRegistry::addDormant(const StatId& id)
{
	if (contains(id)) {
		return;
	}
	entries_.push_back(Entry{ .id = id, .core = false, .dormant = true, .freePoints = FreePoints::Granted });
}

void StatRegistry::removeDormant()
{
	std::erase_if(entries_, [](const Entry& entry) { return entry.dormant; });
}

bool StatRegistry::contains(const StatId& id) const
{
	return findIn(entries_, id) != nullptr;
}

bool StatRegistry::isCore(const StatId& id) const
{
	const Entry* entry = findIn(entries_, id);
	return entry != nullptr && entry->core;
}

bool StatRegistry::isDormant(const StatId& id) const
{
	const Entry* entry = findIn(entries_, id);
	return entry != nullptr && entry->dormant;
}

bool StatRegistry::getsFreePoints(const StatId& id) const
{
	const Entry* entry = findIn(entries_, id);
	return entry != nullptr && entry->freePoints == FreePoints::Granted;
}

std::vector<StatId> StatRegistry::activeStats() const
{
	std::vector<StatId> result;
	result.reserve(entries_.size());
	for (const auto& entry : entries_) {
		if (!entry.dormant) {
			result.push_back(entry.id);
		}
	}
	return result;
}

std::vector<StatId> StatRegistry::registeredStats() const
{
	std::vector<StatId> result;
	for (const auto& entry : entries_) {
		if (!entry.core) {
			result.push_back(entry.id);
		}
	}
	return result;
}

}
