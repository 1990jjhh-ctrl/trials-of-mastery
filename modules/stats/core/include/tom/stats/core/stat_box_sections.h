#pragma once

// The stat box sections that modules fill themselves (ST-24, ST-26; Stats
// plan, C9b). The plugin holds the registry next to the stats service, the
// APIs fill it and the stat box reads it when it opens.

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace tom::stats
{

// The lines a module shows in the stat box section of its namespace's
// group, in place of the stat lines (ST-24, ST-26).
using SectionProvider = std::function<std::vector<std::string>()>;

// The section providers, one per namespace. Who may set which namespace is
// for the API to check.
class StatBoxSections
{
public:
	// Sets the provider of the namespace, replacing an earlier one. Returns
	// false, and changes nothing, for an empty namespace, one with a colon
	// or an empty function.
	[[nodiscard]] bool set(std::string namespaceName, SectionProvider provider);

	// Removes the provider of the namespace; does nothing when it has none.
	void remove(std::string_view namespaceName);

	// The provider of the namespace, or nullptr. The pointer is valid until
	// the next set() or remove() of that namespace.
	[[nodiscard]] const SectionProvider* find(std::string_view namespaceName) const;

private:
	std::map<std::string, SectionProvider, std::less<>> providers_;
};

}
