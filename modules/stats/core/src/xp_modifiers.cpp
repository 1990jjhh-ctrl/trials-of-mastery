#include "tom/stats/core/xp_modifiers.h"

#include <cmath>

namespace tom::stats
{

bool XpModifierRegistry::set(const XpModifierOwner& owner, XpModifierKind kind, const std::string& name, double m)
{
	if (owner.empty() || name.empty() || !std::isfinite(m) || m < -1.0) {
		return false;
	}
	modifiers_[kind][name][owner] = m;
	return true;
}

void XpModifierRegistry::remove(const XpModifierOwner& owner, XpModifierKind kind, const std::string& name)
{
	const auto byName = modifiers_.find(kind);
	if (byName == modifiers_.end()) {
		return;
	}
	const auto byOwner = byName->second.find(name);
	if (byOwner == byName->second.end()) {
		return;
	}
	byOwner->second.erase(owner);
	if (byOwner->second.empty()) {
		byName->second.erase(byOwner);
	}
}

double XpModifierRegistry::factor(XpModifierKind kind, std::string_view name) const
{
	const auto byName = modifiers_.find(kind);
	if (byName == modifiers_.end()) {
		return 1.0;
	}
	const auto byOwner = byName->second.find(name);
	if (byOwner == byName->second.end()) {
		return 1.0;
	}
	double product = 1.0;
	for (const auto& [owner, m] : byOwner->second) {
		product *= 1.0 + m;
	}
	// May be +inf; addXp drops a non-finite amount, so that means no XP.
	return product;
}

}
