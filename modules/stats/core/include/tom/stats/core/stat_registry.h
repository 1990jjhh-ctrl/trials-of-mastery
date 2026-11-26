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

// Whether a stat takes free level-up points (ST-2, ST-20). A registered stat
// can opt out, e.g. LUC (LU-1); core stats always take them.
enum class FreePoints
{
	Granted,
	OptedOut,
};

// The known stats: the core stats plus the registered ones (ST-1, ST-2). A
// stat whose saved data exists but whose owner has not registered it is
// dormant (Stats plan, assumption 8): it is known, but not active.
class StatRegistry
{
public:
	// Starts with the core stats.
	StatRegistry();

	// Registers a stat, or brings a dormant one back with the given free-point
	// setting. Returns false, and changes nothing, when the name is empty or
	// the stat is already active, which includes the core stats.
	[[nodiscard]] bool registerStat(const StatId& id, FreePoints freePoints = FreePoints::Granted);

	// Adds a stat from saved data as dormant. Does nothing for a known stat.
	// Its free-point setting comes with the registration that brings it back.
	void addDormant(const StatId& id);

	// Forgets the dormant stats, e.g. when the saved data they came from is
	// reverted. Active stats stay.
	void removeDormant();

	[[nodiscard]] bool contains(const StatId& id) const;
	[[nodiscard]] bool isCore(const StatId& id) const;
	[[nodiscard]] bool isDormant(const StatId& id) const;

	// False for a stat opted out of free points and for an unknown stat.
	[[nodiscard]] bool getsFreePoints(const StatId& id) const;

	// The core stats in PRD order, then the registered stats in the order they
	// were first added. Dormant stats are left out.
	[[nodiscard]] std::vector<StatId> activeStats() const;

	// The registered stats, dormant ones included, in the order they were
	// first added; the core stats are left out. These are the registered
	// stats a save stores (PRD §8, Stats plan, assumption 8).
	[[nodiscard]] std::vector<StatId> registeredStats() const;

private:
	struct Entry
	{
		StatId     id;
		bool       core = false;
		bool       dormant = false;
		FreePoints freePoints = FreePoints::Granted;
	};

	// One definition for const and non-const callers; defined in the .cpp,
	// which is its only user.
	template <typename Entries>
	[[nodiscard]] static auto* findIn(Entries& entries, const StatId& id);

	std::vector<Entry> entries_;
};

}
