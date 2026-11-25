#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>

namespace tom::stats
{

// What an XP modifier scales (ST-17).
enum class XpModifierKind
{
	SkillXp,             // a skill's own XP (ST-12, ST-17)
	SkillLevelUpStatXp,  // the stat XP a skill gives on level-up (ST-14)
	ActionStatXp,        // the stat XP an action gives (ST-15)
};

// The key an XP modifier is registered under: the module or mod that owns it.
using XpModifierOwner = std::string;

// The XP modifiers that other modules register, each under its owner's key
// and for one skill or action (ST-17). Each modifier m is a factor 1 + m, and
// all factors multiply (Stats plan, assumption 4).
class XpModifierRegistry
{
public:
	// Sets the owner's modifier for the skill or action, replacing the one it
	// set before. Returns false, and changes nothing, when the owner or the
	// name is empty, or m is not finite or below -1, so no factor is negative.
	// m = -1 is valid and gives the factor 0.
	[[nodiscard]] bool set(const XpModifierOwner& owner, XpModifierKind kind, const std::string& name, double m);

	// Removes the owner's modifier for the skill or action. Does nothing when
	// it is not set.
	void remove(const XpModifierOwner& owner, XpModifierKind kind, const std::string& name);

	// The product of 1 + m over all owners' modifiers for the skill or
	// action; 1 when none is set. Never negative: a modifier of -1 gives 0.
	// The product is not clamped and can overflow to +inf; addXp
	// (stat_value.h) drops an amount that is not finite, so such a factor
	// gives no XP.
	[[nodiscard]] double factor(XpModifierKind kind, std::string_view name) const;

private:
	using ByOwner = std::map<XpModifierOwner, double>;
	using ByName = std::map<std::string, ByOwner, std::less<>>;

	std::map<XpModifierKind, ByName> modifiers_;
};

}
