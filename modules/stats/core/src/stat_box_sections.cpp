#include "tom/stats/core/stat_box_sections.h"

#include <utility>

namespace tom::stats
{

bool StatBoxSections::set(std::string namespaceName, SectionProvider provider)
{
	if (namespaceName.empty() || namespaceName.contains(':') || !provider) {
		return false;
	}
	providers_.insert_or_assign(std::move(namespaceName), std::move(provider));
	return true;
}

void StatBoxSections::remove(std::string_view namespaceName)
{
	const auto it = providers_.find(namespaceName);
	if (it != providers_.end()) {
		providers_.erase(it);
	}
}

const SectionProvider* StatBoxSections::find(std::string_view namespaceName) const
{
	const auto it = providers_.find(namespaceName);
	return it == providers_.end() ? nullptr : &it->second;
}

}
