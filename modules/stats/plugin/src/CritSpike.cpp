// THROWAWAY prototype of spike S5 (docs/stats/PLAN.md, Phase 3). It only
// answers how DEX can set the base crit chance for physical attacks and for
// spells (ST-13); G8 replaces it.
//
// How vanilla rolls a physical crit (disassembly of SkyrimSE.exe 1.7.104;
// Address Library IDs are AE). Every HitData populator (44001 melee, 44002
// projectiles, 44004) calls 44016 on the HitData. After Mod Attack Damage
// (0x23), Mod Incoming Damage (0x24) and the sneak attack multiplier, and
// unless the hit has kIgnoreCritical, 44016:
//
// 1. calls 26441(attacker's ActorValueOwner, weapon, condition) at +0x28C for
//    the base chance in percent: the attacker's CriticalChance actor value,
//    times (condition + 1) * fWeaponConditionCriticalChanceMult (0.1, so 1.0
//    at condition 9) for a weapon that is not hand to hand, divided by the
//    fire rate of an automatic weapon, times the weapon's CRDT % Mult if it is
//    not negative. 26441 has no other caller;
// 2. runs the attacker's Calculate My Critical Hit Chance (0x01, +0x2A6) and
//    the target's Mod Enemy Critical Hit Chance (0x11, +0x2CB) perk entries on
//    that chance. Bladesman, Deep Wounds and Critical Shot *set* it (10, 15,
//    20), the Blades' blessing adds 10;
// 3. multiplies by 10 and rolls: crit if the game RNG (68276, +0x2EF) modulo
//    1000 is below the truncated per mille value. It sets kCritical;
// 4. on a crit, takes the crit damage (the weapon's CRDT damage, or
//    fCombatUnarmedCritDamageMult (0.5) of the damage for the unarmed
//    weapon), runs Calculate My Critical Hit Damage (0x02, +0x40B; the crit
//    perks multiply it by 1.25 to 2) and adds it to totalDamage if positive;
//    the crit effect comes from the weapon's CRDT spell;
// 5. then multiplies the damage by fCombatKillMoveDamageMult in a kill move
//    and by the attacker's AttackDamageMult. Armor and blocking (44014) come
//    later.
//
// Spells have no crit. ActiveEffect::AdjustForPerks (vtable slot 0, 34053,
// shared by the value modifier effects; 34316 for the dual one) runs once
// when MagicTarget's effect creation (34547) adds an effect with a caster: it
// applies Mod Spell Duration (0x1E), Mod Spell Magnitude (0x1D), Mod Incoming
// Spell Duration (0x2A) and Mod Incoming Spell Magnitude (0x29) to the
// effect's duration and magnitude. ValueModifierEffect::ModifyActorValue
// (35086) then calls DoDamage at +0x232 on each application, once for an
// instant effect and on every update for damage over time.
//
// The physical hooks are all in 44016: base (the 26441 call), perks (the 0x11
// perk entry call), roll (the RNG call) and crit damage (the 0x02 perk entry
// call). The spell candidates are adjust (slot 0 of the four value modifier
// vtables, after the perks) and tick (the DoDamage call in 35086, one roll
// per effect).

#include "CritSpike.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace tom::stats
{
namespace
{
// Relative to the game folder, which is the game's working directory.
constexpr std::string_view kConfigPath = "Data/SKSE/Plugins/trials-of-mastery-stats-s5.json";

// The crit part of the HitData computation, called by every populator.
constexpr REL::ID kHitCrit{ 44016 };
// The base crit chance (attacker's ActorValueOwner, weapon, condition).
constexpr REL::ID kBaseCritChance{ 26441 };
// BGSEntryPoint::HandleEntryPoint(entry point, perk owner, ...).
constexpr REL::ID kHandleEntryPoint{ 23526 };
// The game RNG's random integer (generator, maximum).
constexpr REL::ID kRandom{ 68276 };
// ValueModifierEffect::ModifyActorValue and Actor::DoDamage (spike S4).
constexpr REL::ID kModifyActorValue{ 35086 };
constexpr REL::ID kDoDamage{ 37335 };
// ActiveEffect::AdjustForPerks and DualValueModifierEffect's override.
constexpr REL::ID     kAdjustForPerks{ 34053 };
constexpr REL::ID     kDualAdjustForPerks{ 34316 };
constexpr std::size_t kAdjustForPerksSlot = 0;

// 44016 is 0x4F9 and 35086 0x350 bytes long on 1.7.104. The lengths only
// bound a scan; a match must still be unique.
constexpr std::size_t kHitCritScanLength = 0x500;
constexpr std::size_t kModifyActorValueScanLength = 0x360;

constexpr std::uint8_t kCallOpcode = 0xE8;
constexpr std::size_t  kCallLength = 5;

// mov r8d, r13d (condition); mov rdx, [r15 + 0x30] (weapon): right before
// 44016's call of 26441. r15 holds the HitData for the whole function.
constexpr std::array<std::uint8_t, 7> kBaseCallPrefix{ 0x45, 0x8B, 0xC5, 0x49, 0x8B, 0x57, 0x30 };
// mov rdx, rbx (target); mov ecx, 0x11 (Mod Enemy Critical Hit Chance).
constexpr std::array<std::uint8_t, 8> kPerksCallPrefix{ 0x48, 0x8B, 0xD3, 0xB9, 0x11, 0x00, 0x00, 0x00 };
// mov edx, 0xFFFFFFFF (maximum); mov rcx, rax (generator).
constexpr std::array<std::uint8_t, 8> kRollCallPrefix{ 0xBA, 0xFF, 0xFF, 0xFF, 0xFF, 0x48, 0x8B, 0xC8 };
// mov rdx, rdi (attacker); mov ecx, 2 (Calculate My Critical Hit Damage).
constexpr std::array<std::uint8_t, 8> kCritDamageCallPrefix{ 0x48, 0x8B, 0xD7, 0xB9, 0x02, 0x00, 0x00, 0x00 };
// xor r9d, r9d; mov r8, rbx (caster); movaps xmm1, xmm6 (damage); mov rcx,
// r14 (target): right before ModifyActorValue's call of DoDamage (spike S4).
// rdi holds the effect for the whole function.
constexpr std::array<std::uint8_t, 12> kTickCallPrefix{ 0x45, 0x33, 0xC9, 0x4C, 0x8B, 0xC3, 0x0F, 0x28, 0xCE, 0x49,
	0x8B, 0xCE };

// The stubs that pass a register the game keeps across the call as the
// hook's fourth argument: mov r9, r15 (the HitData) and mov r9, rdi (the
// effect). Both calls leave r9 unused or 0, and the hooks pass the original
// arguments on.
constexpr std::array<std::uint8_t, 3> kMoveR15ToR9{ 0x4D, 0x89, 0xF9 };
constexpr std::array<std::uint8_t, 3> kMoveRdiToR9{ 0x49, 0x89, 0xF9 };
// jmp [rip + 0]; dq target: what an SKSE trampoline entry holds.
constexpr std::array<std::uint8_t, 6> kTrampolineJump{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
constexpr std::uint8_t                kJumpRel32Opcode = 0xE9;

// The game rolls modulo 1000 against the chance in percent times 10.
constexpr float         kPerMillePerPercent = 10.0F;
constexpr std::uint32_t kRollRange = 1000;
constexpr float         kCertain = 100.0F;

enum class PhysicalMode
{
	kObserve,
	kReplace,
	kAdd,
	kAfterPerks,
};

enum class SpellHook
{
	kNone,
	kAdjust,
	kTick,
};

template <class T>
struct Name
{
	T                value;
	std::string_view name;
};

constexpr std::array kPhysicalModeNames{
	Name<PhysicalMode>{ PhysicalMode::kObserve, "observe" },
	Name<PhysicalMode>{ PhysicalMode::kReplace, "replace" },
	Name<PhysicalMode>{ PhysicalMode::kAdd, "add" },
	Name<PhysicalMode>{ PhysicalMode::kAfterPerks, "afterPerks" },
};

constexpr std::array kSpellHookNames{
	Name<SpellHook>{ SpellHook::kNone, "none" },
	Name<SpellHook>{ SpellHook::kAdjust, "adjust" },
	Name<SpellHook>{ SpellHook::kTick, "tick" },
};

template <class T, std::size_t N>
std::string_view nameOf(const std::array<Name<T>, N>& names, T value)
{
	for (const auto& entry : names) {
		if (entry.value == value) {
			return entry.name;
		}
	}
	return "?";
}

template <class T, std::size_t N>
std::optional<T> parseName(const std::array<Name<T>, N>& names, std::string_view name)
{
	for (const auto& entry : names) {
		if (entry.name == name) {
			return entry.value;
		}
	}
	return std::nullopt;
}

// The JSON file's fields of the same name. Chances are in percent, like the
// game's.
struct SpikeConfig
{
	// observe: vanilla. replace: the base chance is physicalChance. add: the
	// base chance is vanilla's plus physicalChance. afterPerks: physicalChance
	// is added after the crit chance perk entries.
	PhysicalMode physicalMode = PhysicalMode::kObserve;
	double       physicalChance = 0.0;
	// Where spells crit: none (observe only), adjust or tick.
	SpellHook hook = SpellHook::kNone;
	double    spellChance = 0.0;
	double    spellCritMult = 1.5;
	// Stands in for the crit shifts of other modules (G8): added to every
	// chance this spike sets.
	double shift = 0.0;
	// Every hit and spell the spike applies to crits.
	bool forceCrit = false;
	// Also log hits and spells of NPC attackers; applyToNpcs also changes them.
	bool logNpcAttackers = false;
	bool applyToNpcs = false;
	// Log every DoDamage call of the tick site, not only the crit decisions.
	bool logTicks = true;
};

std::string describe(const SpikeConfig& config)
{
	return std::format(
		"physicalMode {}, physicalChance {}, hook {}, spellChance {}, spellCritMult {}, shift {}, forceCrit {}, "
		"logNpcAttackers {}, applyToNpcs {}, logTicks {}",
		nameOf(kPhysicalModeNames, config.physicalMode), config.physicalChance, nameOf(kSpellHookNames, config.hook),
		config.spellChance, config.spellCritMult, config.shift, config.forceCrit, config.logNpcAttackers,
		config.applyToNpcs, config.logTicks);
}

template <class T, std::size_t N>
std::optional<T> readName(const nlohmann::json& file, const char* field, const std::array<Name<T>, N>& names,
	T fallback)
{
	const auto name = file.value(field, std::string{ nameOf(names, fallback) });
	const auto value = parseName(names, name);
	if (!value) {
		SKSE::log::error("S5: unknown {} \"{}\" in {}", field, name, kConfigPath);
	}
	return value;
}

bool isValid(const SpikeConfig& config)
{
	return std::isfinite(config.physicalChance) && std::isfinite(config.spellChance) &&
	       std::isfinite(config.spellCritMult) && std::isfinite(config.shift) && config.spellCritMult >= 0.0;
}

SpikeConfig readConfig()
{
	std::ifstream stream{ std::filesystem::path(kConfigPath) };
	if (!stream) {
		SKSE::log::warn("S5: {} cannot be opened; using the defaults", kConfigPath);
		return {};
	}
	SpikeConfig config;
	try {
		const auto file = nlohmann::json::parse(stream);
		config.physicalChance = file.value("physicalChance", config.physicalChance);
		config.spellChance = file.value("spellChance", config.spellChance);
		config.spellCritMult = file.value("spellCritMult", config.spellCritMult);
		config.shift = file.value("shift", config.shift);
		config.forceCrit = file.value("forceCrit", config.forceCrit);
		config.logNpcAttackers = file.value("logNpcAttackers", config.logNpcAttackers);
		config.applyToNpcs = file.value("applyToNpcs", config.applyToNpcs);
		config.logTicks = file.value("logTicks", config.logTicks);
		const auto mode = readName(file, "physicalMode", kPhysicalModeNames, config.physicalMode);
		const auto hook = readName(file, "hook", kSpellHookNames, config.hook);
		if (!mode || !hook) {
			return {};
		}
		config.physicalMode = *mode;
		config.hook = *hook;
	} catch (const nlohmann::json::exception& error) {
		SKSE::log::error("S5: {} is not valid ({}); using the defaults", kConfigPath, error.what());
		return {};
	}
	if (!isValid(config)) {
		SKSE::log::error("S5: a number in {} is not finite, or spellCritMult is negative; using the defaults",
			kConfigPath);
		return {};
	}
	return config;
}

// The config in the file, read again whenever the file's modification time
// changes, so it can be edited while the game runs. The defaults apply while
// the file is missing or invalid. Hooks may run on several threads.
class ConfigCache
{
public:
	SpikeConfig current();

private:
	std::mutex                                     mutex_;
	bool                                           loaded_ = false;
	std::optional<std::filesystem::file_time_type> stamp_;
	SpikeConfig                                    config_;
};

SpikeConfig ConfigCache::current()
{
	const std::scoped_lock lock{ mutex_ };
	std::error_code        error;
	const auto             time = std::filesystem::last_write_time(std::filesystem::path(kConfigPath), error);
	const auto             stamp = error ? std::nullopt : std::optional{ time };
	if (loaded_ && stamp == stamp_) {
		return config_;
	}
	loaded_ = true;
	stamp_ = stamp;
	if (!stamp) {
		SKSE::log::warn("S5: {} not found (game folder {}); using the defaults", kConfigPath,
			std::filesystem::current_path().string());
		config_ = {};
	} else {
		config_ = readConfig();
	}
	SKSE::log::info("S5 config: {}", describe(config_));
	return config_;
}

ConfigCache                g_config;
std::atomic<std::uint64_t> g_eventCount{ 0 };
std::uint32_t              g_mainThread = 0;

// --- Logging helpers --------------------------------------------------------

std::string formText(const RE::TESForm* form)
{
	if (form == nullptr) {
		return "none";
	}
	return std::format("{:08X} \"{}\"", form->GetFormID(), form->GetName());
}

std::string actorText(const RE::Actor* actor)
{
	if (actor == nullptr) {
		return "none";
	}
	if (actor->IsPlayerRef()) {
		return "player";
	}
	return std::format("{:08X} \"{}\"", actor->GetFormID(), actor->GetName());
}

std::string threadText()
{
	const auto thread = REX::W32::GetCurrentThreadId();
	return std::format("thread {}{}", thread, thread == g_mainThread ? " (main)" : "");
}

std::string weaponText(const RE::TESObjectWEAP* weapon)
{
	if (weapon == nullptr) {
		return "weapon none";
	}
	return std::format("weapon {} (type {}, CRDT % mult {:.2f}, damage {})", formText(weapon),
		static_cast<int>(weapon->weaponData.animationType.underlying()), weapon->criticalData.prcntMult,
		weapon->criticalData.damage);
}

float criticalChanceAv(RE::Actor* actor)
{
	return actor == nullptr ? 0.0F : actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kCriticalChance);
}

bool isPlayer(const RE::Actor* actor)
{
	return actor != nullptr && actor->IsPlayerRef();
}

bool shouldLog(const SpikeConfig& config, const RE::Actor* attacker)
{
	return config.logNpcAttackers || isPlayer(attacker);
}

bool appliesTo(const SpikeConfig& config, const RE::Actor* attacker)
{
	return attacker != nullptr && (config.applyToNpcs || attacker->IsPlayerRef());
}

// --- Physical crits ---------------------------------------------------------

// One crit computation in 44016, from the base hook to the crit damage hook.
// The hooks run in that order inside one call, on one thread.
struct HitContext
{
	std::uint64_t event = 0;
	RE::HitData*  hit = nullptr;
	SpikeConfig   config;
	bool          applies = false;
	bool          log = false;
	float         chance = 0.0F;
	std::int32_t  threshold = 0;
};

thread_local HitContext t_hit;

using BaseCritChance = float (*)(RE::ActorValueOwner*, RE::TESObjectWEAP*, std::int32_t);
BaseCritChance g_baseCritChance = nullptr;

float physicalBase(const HitContext& context, float vanilla)
{
	if (!context.applies) {
		return vanilla;
	}
	const auto chance = static_cast<float>(context.config.physicalChance + context.config.shift);
	switch (context.config.physicalMode) {
	case PhysicalMode::kReplace:
		return chance;
	case PhysicalMode::kAdd:
		return vanilla + chance;
	default:
		return vanilla;
	}
}

// Reached through a stub that puts 44016's HitData (r15) into the fourth
// argument.
float baseCritChance(RE::ActorValueOwner* owner, RE::TESObjectWEAP* weapon, std::int32_t condition, RE::HitData* hit)
{
	const float vanilla = g_baseCritChance(owner, weapon, condition);
	const auto  attacker = hit != nullptr ? hit->aggressor.get() : RE::NiPointer<RE::Actor>{};
	const auto  target = hit != nullptr ? hit->target.get() : RE::NiPointer<RE::Actor>{};
	t_hit = { .event = ++g_eventCount, .hit = hit, .config = g_config.current() };
	t_hit.applies = appliesTo(t_hit.config, attacker.get());
	t_hit.log = shouldLog(t_hit.config, attacker.get());
	const float base = physicalBase(t_hit, vanilla);
	if (t_hit.log) {
		SKSE::log::info(
			"S5 #{} base: {} -> {}, {}, CriticalChance AV {:.2f}, condition {}, flags {:#x}, totalDamage {:.2f}; "
			"vanilla base {:.2f}% -> {:.2f}% (mode {}{}); {}",
			t_hit.event, actorText(attacker.get()), actorText(target.get()), weaponText(weapon),
			criticalChanceAv(attacker.get()), condition, hit != nullptr ? hit->flags.underlying() : 0U,
			hit != nullptr ? hit->totalDamage : 0.0F, vanilla, base, nameOf(kPhysicalModeNames, t_hit.config.physicalMode),
			t_hit.applies ? "" : ", not applied", threadText());
	}
	return base;
}

using HandleEntryPoint = void (*)(std::uint32_t, RE::Actor*, void*, void*, float*);
HandleEntryPoint g_perksEntryPoint = nullptr;

float physicalAfterPerks(const HitContext& context, float chance)
{
	if (!context.applies) {
		return chance;
	}
	if (context.config.forceCrit) {
		return kCertain;
	}
	if (context.config.physicalMode == PhysicalMode::kAfterPerks) {
		return chance + static_cast<float>(context.config.physicalChance + context.config.shift);
	}
	return chance;
}

// The target's Mod Enemy Critical Hit Chance call (target, weapon, attacker,
// chance), the last change of the chance before the roll.
void perksEntryPoint(std::uint32_t entryPoint, RE::Actor* target, void* weapon, void* attacker, float* chance)
{
	g_perksEntryPoint(entryPoint, target, weapon, attacker, chance);
	if (t_hit.hit == nullptr || chance == nullptr) {
		return;
	}
	const float afterPerks = *chance;
	*chance = physicalAfterPerks(t_hit, afterPerks);
	t_hit.chance = *chance;
	t_hit.threshold = static_cast<std::int32_t>(*chance * kPerMillePerPercent);
	if (t_hit.log) {
		SKSE::log::info(
			"S5 #{} perks: chance after Calculate My Critical Hit Chance (0x01) and Mod Enemy Critical "
			"Hit Chance (0x11) {:.2f}% -> {:.2f}%{} (threshold {} of {})",
			t_hit.event, afterPerks, *chance, t_hit.config.forceCrit && t_hit.applies ? " (forceCrit)" : "",
			t_hit.threshold, kRollRange);
	}
}

using Random = std::uint32_t (*)(void*, std::uint32_t);
Random g_random = nullptr;

std::uint32_t critRoll(void* generator, std::uint32_t maximum)
{
	const auto value = g_random(generator, maximum);
	if (t_hit.hit == nullptr || !t_hit.log) {
		return value;
	}
	const auto roll = value % kRollRange;
	SKSE::log::info("S5 #{} roll: {} (random {}) against {} -> {}", t_hit.event, roll, value, t_hit.threshold,
		static_cast<std::int64_t>(roll) < t_hit.threshold ? "crit" : "no crit");
	return value;
}

HandleEntryPoint g_critDamageEntryPoint = nullptr;

// The attacker's Calculate My Critical Hit Damage call (attacker, weapon,
// target, crit damage), made only on a crit.
void critDamageEntryPoint(std::uint32_t entryPoint, RE::Actor* attacker, void* weapon, void* target, float* damage)
{
	const float before = damage != nullptr ? *damage : 0.0F;
	g_critDamageEntryPoint(entryPoint, attacker, weapon, target, damage);
	if (t_hit.hit == nullptr || !t_hit.log || damage == nullptr) {
		return;
	}
	const float total = t_hit.hit->totalDamage;
	SKSE::log::info(
		"S5 #{} crit damage: {:.2f} -> {:.2f} after Calculate My Critical Hit Damage (0x02); "
		"totalDamage {:.2f} -> {:.2f} (before kill move, AttackDamageMult, armor)",
		t_hit.event, before, *damage, total, *damage > 0.0F ? total + *damage : total);
}

// --- Spell crits ------------------------------------------------------------

struct SpellRoll
{
	float chance = 0.0F;
	float roll = 0.0F;
	bool  crit = false;
};

SpellRoll rollSpell(const SpikeConfig& config)
{
	thread_local std::mt19937             generator{ std::random_device{}() };
	std::uniform_real_distribution<float> distribution{ 0.0F, kCertain };
	const auto                            chance = config.forceCrit ? kCertain : static_cast<float>(config.spellChance + config.shift);
	const float                           roll = distribution(generator);
	return { chance, roll, roll < chance };
}

std::string rollText(const SpellRoll& roll, double mult)
{
	return std::format("chance {:.2f}%, roll {:.2f} -> {}", roll.chance, roll.roll,
		roll.crit ? std::format("crit x{}", mult) : std::string{ "no crit" });
}

std::string effectText(RE::ValueModifierEffect* effect)
{
	const auto* setting = effect->GetBaseObject();
	return std::format(
		"effect {:#x} id {}, spell {}, {} (detrimental {}), AV {}, magnitude {:.3f}, duration {:.2f}, elapsed "
		"{:.2f}, casting source {}",
		reinterpret_cast<std::uintptr_t>(effect), effect->usUniqueID, formText(effect->spell), formText(setting),
		setting != nullptr && setting->IsDetrimental(), static_cast<std::int32_t>(effect->actorValue),
		effect->magnitude, effect->duration, effect->elapsedSeconds, static_cast<std::int32_t>(effect->castingSource));
}

// Only Health damage counts as a spell attack.
bool isHealthDamage(RE::ValueModifierEffect* effect)
{
	const auto* setting = effect->GetBaseObject();
	return effect->actorValue == RE::ActorValue::kHealth && setting != nullptr && setting->IsDetrimental();
}

RE::Actor* targetActor(RE::MagicTarget* target)
{
	auto* reference = target != nullptr ? target->GetTargetStatsObject() : nullptr;
	return reference != nullptr ? reference->As<RE::Actor>() : nullptr;
}

// Candidate adjust: slot 0 of each value modifier vtable, after the perks.
using AdjustForPerks = void (*)(RE::ActiveEffect*, RE::TESObjectREFR*, RE::MagicTarget*);

struct AdjustSlot
{
	std::string_view name;
	REL::ID          vtable;
	REL::ID          vanilla;
	AdjustForPerks   original = nullptr;
};

std::array g_adjustSlots{
	AdjustSlot{ "ValueModifierEffect", REL::ID{ 206143 }, kAdjustForPerks },
	AdjustSlot{ "AccumulatingValueModifierEffect", REL::ID{ 205803 }, kAdjustForPerks },
	AdjustSlot{ "PeakValueModifierEffect", REL::ID{ 206014 }, kAdjustForPerks },
	AdjustSlot{ "DualValueModifierEffect", REL::ID{ 205876 }, kDualAdjustForPerks },
};

void afterAdjust(std::string_view name, RE::ValueModifierEffect* effect, RE::TESObjectREFR* casterReference,
	float before, RE::MagicTarget* magicTarget)
{
	const auto  config = g_config.current();
	auto* const caster = casterReference != nullptr ? casterReference->As<RE::Actor>() : nullptr;
	if (!shouldLog(config, caster) && !appliesTo(config, caster)) {
		return;
	}
	auto* const target = targetActor(magicTarget);
	std::string decision = "not a Health damage effect on another actor";
	if (isHealthDamage(effect) && target != caster && appliesTo(config, caster) && config.hook == SpellHook::kAdjust) {
		const auto roll = rollSpell(config);
		effect->magnitude *= roll.crit ? static_cast<float>(config.spellCritMult) : 1.0F;
		decision = rollText(roll, config.spellCritMult);
	} else if (isHealthDamage(effect) && target != caster) {
		decision = std::format("not applied (hook {})", nameOf(kSpellHookNames, config.hook));
	}
	if (!shouldLog(config, caster)) {
		return;
	}
	SKSE::log::info("S5 adjust ({}): {} -> {}, magnitude {:.3f} before the perks; after: {}; {}; {}", name,
		actorText(caster), actorText(target), before, effectText(effect), decision, threadText());
}

template <std::size_t I>
void adjustForPerks(RE::ActiveEffect* effect, RE::TESObjectREFR* caster, RE::MagicTarget* target)
{
	const float before = effect->magnitude;
	g_adjustSlots[I].original(effect, caster, target);
	afterAdjust(g_adjustSlots[I].name, static_cast<RE::ValueModifierEffect*>(effect), caster, before, target);
}

constexpr std::array<AdjustForPerks, 4> kAdjustHooks{ &adjustForPerks<0>, &adjustForPerks<1>, &adjustForPerks<2>,
	&adjustForPerks<3> };

// Candidate tick: DoDamage's call in ModifyActorValue, with one roll per
// effect, kept by the effect's address and unique ID.
using DoDamage = bool (*)(RE::Actor*, float, RE::Actor*, bool);
DoDamage g_tickDoDamage = nullptr;

using EffectKey = std::pair<std::uintptr_t, std::uint16_t>;
constexpr std::size_t          kMaxRememberedEffects = 4096;
std::mutex                     g_tickMutex;
std::map<EffectKey, SpellRoll> g_tickRolls;

// The roll of the effect, made on its first tick; true if it was new.
std::pair<SpellRoll, bool> tickRoll(const SpikeConfig& config, RE::ValueModifierEffect* effect)
{
	const std::scoped_lock lock{ g_tickMutex };
	const EffectKey        key{ reinterpret_cast<std::uintptr_t>(effect), effect->usUniqueID };
	if (const auto found = g_tickRolls.find(key); found != g_tickRolls.end()) {
		return { found->second, false };
	}
	if (g_tickRolls.size() >= kMaxRememberedEffects) {
		g_tickRolls.clear();
	}
	const auto roll = rollSpell(config);
	g_tickRolls.emplace(key, roll);
	return { roll, true };
}

// Reached through a stub that puts ModifyActorValue's effect (rdi) into the
// fourth argument; DoDamage gets the false the game passes there.
bool tickDoDamage(RE::Actor* target, float damage, RE::Actor* caster, RE::ValueModifierEffect* effect)
{
	const auto config = g_config.current();
	const bool apply = config.hook == SpellHook::kTick && appliesTo(config, caster) && effect != nullptr &&
	                   target != caster && isHealthDamage(effect);
	if (!apply && !(config.logTicks && shouldLog(config, caster))) {
		return g_tickDoDamage(target, damage, caster, false);
	}
	float       scaled = damage;
	std::string decision = std::format("not applied (hook {})", nameOf(kSpellHookNames, config.hook));
	bool        logIt = config.logTicks && shouldLog(config, caster);
	if (apply) {
		const auto [roll, first] = tickRoll(config, effect);
		scaled *= roll.crit ? static_cast<float>(config.spellCritMult) : 1.0F;
		decision = std::format("{} ({})", rollText(roll, config.spellCritMult), first ? "rolled now" : "earlier roll");
		logIt = logIt || (first && shouldLog(config, caster));
	}
	if (logIt) {
		SKSE::log::info("S5 tick: {} -> {}, {:.3f} damage -> {:.3f}; {}; {}; {}", actorText(caster), actorText(target),
			damage, scaled, effect != nullptr ? effectText(effect) : std::string{ "no effect" }, decision,
			threadText());
	}
	return g_tickDoDamage(target, scaled, caster, false);
}

// --- Scanning ---------------------------------------------------------------

std::span<const std::uint8_t> codeAt(std::uintptr_t begin, std::size_t length)
{
	return { reinterpret_cast<const std::uint8_t*>(begin), length };
}

std::uintptr_t callTarget(std::uintptr_t at)
{
	std::int32_t displacement = 0;
	std::memcpy(&displacement, reinterpret_cast<const void*>(at + 1), sizeof(displacement));
	return at + kCallLength + static_cast<std::intptr_t>(displacement);
}

// The offset of every occurrence of pattern in code.
std::vector<std::size_t> findAll(std::span<const std::uint8_t> code, std::span<const std::uint8_t> pattern)
{
	std::vector<std::size_t> found;
	for (std::size_t i = 0; i + pattern.size() <= code.size(); ++i) {
		if (std::memcmp(code.data() + i, pattern.data(), pattern.size()) == 0) {
			found.push_back(i);
		}
	}
	return found;
}

// The E8 call right after prefix in [begin, begin + length), if prefix occurs
// there exactly once and a call follows it. The call's target is not checked,
// so a call another plugin redirected is still found.
std::optional<std::uintptr_t> findCallAfter(std::uintptr_t begin, std::size_t length,
	std::span<const std::uint8_t> prefix)
{
	const auto found = findAll(codeAt(begin, length), prefix);
	if (found.size() != 1) {
		return std::nullopt;
	}
	const auto at = begin + found.front() + prefix.size();
	if (at + kCallLength > begin + length || *reinterpret_cast<const std::uint8_t*>(at) != kCallOpcode) {
		return std::nullopt;
	}
	return at;
}

// Follows SKSE trampoline jumps and rel32 jumps to the function they lead to.
std::uintptr_t followJumps(std::uintptr_t address)
{
	constexpr int kMaxHops = 4;
	for (int hop = 0; hop < kMaxHops; ++hop) {
		const auto* code = reinterpret_cast<const std::uint8_t*>(address);
		if (code[0] == kJumpRel32Opcode) {
			address = callTarget(address);
			continue;
		}
		if (std::memcmp(code, kTrampolineJump.data(), kTrampolineJump.size()) != 0) {
			return address;
		}
		std::memcpy(&address, reinterpret_cast<const void*>(address + kTrampolineJump.size()), sizeof(address));
	}
	return address;
}

// "Experience.dll+0x1234" for an address in a loaded module.
std::string moduleText(std::uintptr_t address)
{
	REX::W32::MEMORY_BASIC_INFORMATION info{};
	if (REX::W32::VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) == 0 ||
		info.allocationBase == nullptr) {
		return "no module";
	}
	std::array<char, 260> name{};
	const auto            length = REX::W32::GetModuleFileNameA(static_cast<REX::W32::HMODULE>(info.allocationBase),
		name.data(), static_cast<std::uint32_t>(name.size()));
	if (length == 0) {
		return "no module";
	}
	const auto file = std::filesystem::path(std::string(name.data(), length)).filename().string();
	return std::format("{}+{:#x}", file, address - reinterpret_cast<std::uintptr_t>(info.allocationBase));
}

// Where a call target or vtable entry leads, and whether that is vanilla.
std::string targetText(std::uintptr_t target, std::uintptr_t vanilla)
{
	if (target == vanilla) {
		return std::format("{:#x} (vanilla)", target);
	}
	const auto destination = followJumps(target);
	return std::format("{:#x} -> {:#x} {} (redirected; vanilla is {:#x})", target, destination,
		moduleText(destination), vanilla);
}

// A stub in the trampoline: move moveToR9, then jump to hook.
std::uintptr_t makeStub(std::span<const std::uint8_t> moveToR9, std::uintptr_t hook)
{
	const auto size = moveToR9.size() + kTrampolineJump.size() + sizeof(hook);
	auto*      stub = static_cast<std::uint8_t*>(SKSE::GetTrampoline().allocate(size));
	std::memcpy(stub, moveToR9.data(), moveToR9.size());
	std::memcpy(stub + moveToR9.size(), kTrampolineJump.data(), kTrampolineJump.size());
	std::memcpy(stub + moveToR9.size() + kTrampolineJump.size(), &hook, sizeof(hook));
	return reinterpret_cast<std::uintptr_t>(stub);
}

struct Site
{
	std::string_view              name;
	REL::ID                       function;
	std::size_t                   scanLength;
	std::span<const std::uint8_t> prefix;
	REL::ID                       vanillaTarget;
};

// Finds a site's call and logs where it goes. Empty if not found.
std::optional<std::uintptr_t> findSite(const Site& site)
{
	const auto base = site.function.address();
	const auto call = findCallAfter(base, site.scanLength, site.prefix);
	if (!call) {
		SKSE::log::error("S5 {}: the call in {} ({:#x}) was not found (unique prefix and E8 expected); not hooked",
			site.name, site.function.id(), base);
		return std::nullopt;
	}
	SKSE::log::info("S5 {}: found at {} + {:#x} ({:#x}); the call goes to {}", site.name, site.function.id(),
		*call - base, *call, targetText(callTarget(*call), site.vanillaTarget.address()));
	return call;
}

// --- Install ----------------------------------------------------------------

template <class F>
F writeCall(std::uintptr_t call, std::uintptr_t hook)
{
	return reinterpret_cast<F>(SKSE::GetTrampoline().write_call<kCallLength>(call, hook));
}

void installPhysicalHooks()
{
	const auto base = findSite({ "base", kHitCrit, kHitCritScanLength, kBaseCallPrefix, kBaseCritChance });
	const auto perks = findSite({ "perks", kHitCrit, kHitCritScanLength, kPerksCallPrefix, kHandleEntryPoint });
	const auto roll = findSite({ "roll", kHitCrit, kHitCritScanLength, kRollCallPrefix, kRandom });
	const auto damage = findSite({ "crit damage", kHitCrit, kHitCritScanLength, kCritDamageCallPrefix,
		kHandleEntryPoint });
	// The other hooks read the context the base hook sets.
	if (!base) {
		SKSE::log::error("S5: without the base site no physical hook is installed");
		return;
	}
	const auto stub = makeStub(kMoveR15ToR9, reinterpret_cast<std::uintptr_t>(&baseCritChance));
	g_baseCritChance = writeCall<BaseCritChance>(*base, stub);
	if (perks) {
		g_perksEntryPoint = writeCall<HandleEntryPoint>(*perks, reinterpret_cast<std::uintptr_t>(&perksEntryPoint));
	}
	if (roll) {
		g_random = writeCall<Random>(*roll, reinterpret_cast<std::uintptr_t>(&critRoll));
	}
	if (damage) {
		g_critDamageEntryPoint = writeCall<HandleEntryPoint>(*damage,
			reinterpret_cast<std::uintptr_t>(&critDamageEntryPoint));
	}
	SKSE::log::info("S5: physical hooks installed (base, {}perks, {}roll, {}crit damage)", perks ? "" : "no ",
		roll ? "" : "no ", damage ? "" : "no ");
}

void installAdjustHooks()
{
	for (std::size_t i = 0; i < g_adjustSlots.size(); ++i) {
		auto&                           slot = g_adjustSlots[i];
		REL::Relocation<std::uintptr_t> vtable{ slot.vtable };
		const auto                      previous = *reinterpret_cast<const std::uintptr_t*>(vtable.address() +
																							sizeof(void*) * kAdjustForPerksSlot);
		slot.original = reinterpret_cast<AdjustForPerks>(vtable.write_vfunc(kAdjustForPerksSlot, kAdjustHooks[i]));
		SKSE::log::info("S5 adjust: {} vtable (ID {}) {:#x} slot {:#x} hooked; it held {}", slot.name,
			slot.vtable.id(), vtable.address(), kAdjustForPerksSlot, targetText(previous, slot.vanilla.address()));
	}
}

void installTickHook()
{
	const auto call = findSite({ "tick", kModifyActorValue, kModifyActorValueScanLength, kTickCallPrefix,
		kDoDamage });
	if (!call) {
		return;
	}
	const auto stub = makeStub(kMoveRdiToR9, reinterpret_cast<std::uintptr_t>(&tickDoDamage));
	g_tickDoDamage = writeCall<DoDamage>(*call, stub);
	SKSE::log::info("S5 tick: hooked");
}
}

void installCritSpike()
{
	g_mainThread = REX::W32::GetCurrentThreadId();
	SKSE::log::info("S5: 44016 {:#x}, 26441 {:#x}, HandleEntryPoint {:#x}, random {:#x}, ModifyActorValue {:#x}, {}",
		kHitCrit.address(), kBaseCritChance.address(), kHandleEntryPoint.address(), kRandom.address(),
		kModifyActorValue.address(), threadText());
	SKSE::log::info("S5 config at install: {}", describe(g_config.current()));
	installPhysicalHooks();
	installAdjustHooks();
	installTickHook();
}
}
