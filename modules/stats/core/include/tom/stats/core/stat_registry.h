#pragma once

#include <compare>
#include <string>
#include <vector>

namespace tom::stats
{

// Names a stat: a core stat abbreviation (ST-1) or the name of a registered
// stat (ST-2).
struct StatId
{
	std::string name;

	friend auto operator<=>(const StatId&, const StatId&) = default;
};

// The known stats: the core stats plus the registered ones (ST-1, ST-2). A
// stat whose saved data exists but whose owner has not registered it is
// dormant (Stats plan, assumption 8): it is known, but not active.
class StatRegistry
{
public:
	// Starts with the core stats.
	StatRegistry();

	// Registers a stat, or brings a dormant one back. Returns false when the
	// stat is already active, which includes the core stats.
	[[nodiscard]] bool registerStat(const StatId& id);

	// Adds a stat from saved data as dormant. Does nothing for a known stat.
	void addDormant(const StatId& id);

	[[nodiscard]] bool contains(const StatId& id) const;
	[[nodiscard]] bool isCore(const StatId& id) const;
	[[nodiscard]] bool isDormant(const StatId& id) const;

	// The core stats in PRD order, then the registered stats in the order they
	// were first added. Dormant stats are left out.
	[[nodiscard]] std::vector<StatId> activeStats() const;

private:
	struct Entry
	{
		StatId id;
		bool   core = false;
		bool   dormant = false;
	};

	[[nodiscard]] const Entry* find(const StatId& id) const;
	[[nodiscard]] Entry*       find(const StatId& id);

	std::vector<Entry> entries_;
};

}
