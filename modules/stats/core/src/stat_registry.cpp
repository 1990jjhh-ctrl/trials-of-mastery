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

StatRegistry::StatRegistry()
{
	entries_.reserve(kCoreStatNames.size());
	for (const auto name : kCoreStatNames) {
		entries_.push_back(Entry{ .id = StatId{ std::string(name) }, .core = true, .dormant = false });
	}
}

bool StatRegistry::registerStat(const StatId& id)
{
	Entry* entry = find(id);
	if (entry == nullptr) {
		entries_.push_back(Entry{ .id = id, .core = false, .dormant = false });
		return true;
	}
	if (!entry->dormant) {
		return false;
	}
	entry->dormant = false;
	return true;
}

void StatRegistry::addDormant(const StatId& id)
{
	if (contains(id)) {
		return;
	}
	entries_.push_back(Entry{ .id = id, .core = false, .dormant = true });
}

bool StatRegistry::contains(const StatId& id) const
{
	return find(id) != nullptr;
}

bool StatRegistry::isCore(const StatId& id) const
{
	const Entry* entry = find(id);
	return entry != nullptr && entry->core;
}

bool StatRegistry::isDormant(const StatId& id) const
{
	const Entry* entry = find(id);
	return entry != nullptr && entry->dormant;
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

const StatRegistry::Entry* StatRegistry::find(const StatId& id) const
{
	const auto it = std::ranges::find(entries_, id, &Entry::id);
	return it == entries_.end() ? nullptr : &*it;
}

StatRegistry::Entry* StatRegistry::find(const StatId& id)
{
	const auto it = std::ranges::find(entries_, id, &Entry::id);
	return it == entries_.end() ? nullptr : &*it;
}

}
