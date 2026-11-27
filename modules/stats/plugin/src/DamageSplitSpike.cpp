// THROWAWAY prototype of spike S4 (docs/stats/PLAN.md, Phase 3). It only
// answers where incoming damage can be split before it reaches Health,
// separately for physical and magic damage (ST-11); G7 replaces it.
//
// How Health damage reaches an actor (disassembly of SkyrimSE.exe 1.7.104;
// Address Library IDs are AE):
//
// - A melee hit (Actor::HitTarget-like, 38627) fills a HitData with
//   HitData::Populate (44001): attack damage, the attacker's perk entries
//   (Calculate Weapon Damage, Mod Power Attack Damage, Mod Bashing Damage),
//   then 44016 (Mod Attack Damage, the target's Mod Incoming Damage, crits),
//   44014 (armor through Mod Target Damage Resistance, blocking through Mod
//   Percent Blocked) and 44015. It then calls ProcessHit (38586) at +0x4A8,
//   the call combat overhauls usually redirect. Projectile hits (arrows,
//   bolts) reach ProcessHit from other callers.
// - ProcessHit(target, HitData&) calls DoDamage (37335) at +0x4A7 with
//   HitData::totalDamage, so armor, blocking and perks are already in it.
// - Magic Health damage goes through ValueModifierEffect::ModifyActorValue
//   (35086, vtable slot 0x20, shared by the accumulating and peak variants):
//   for Health and a negative value it calls DoDamage at +0x232 with the
//   caster. Resistances and Mod Incoming Spell Magnitude are in the effect's
//   magnitude already. Damage over time calls it on every update.
// - DoDamage(target, damage, source, dontAdjustDifficulty) applies the
//   difficulty multiplier (26503), then calls the actor-value change (38468)
//   at +0x151 with the damage modifier, Health and -damage. Fall damage
//   (37998+0xC7) and drowning (37348) call DoDamage too.
// - 38468 changes the damage modifier, runs the death checks, and only then
//   calls Actor::HandleHealthDamage (vtable slot 0x104): that one sees the
//   damage after Health has changed, too late for a split.
// - The console's damageav and Papyrus DamageActorValue call the actor value
//   owner's ModActorValue (38467), which calls 38468 directly, past DoDamage.
//
// The candidates, from early to late: hit (DoDamage's call in ProcessHit),
// magic (DoDamage's call in ModifyActorValue) and health (38468's call in
// DoDamage, after the difficulty multiplier, classified by the other two).

#include "DamageSplitSpike.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace tom::stats
{
namespace
{
// Relative to the game folder, which is the game's working directory.
constexpr std::string_view kConfigPath = "Data/SKSE/Plugins/trials-of-mastery-stats-s4.json";

// Actor::DoDamage(target, damage, source, dontAdjustDifficulty).
constexpr REL::ID kDoDamage{ 37335 };
// Actor::ProcessHit(target, HitData&), which calls DoDamage.
constexpr REL::ID kProcessHit{ 38586 };
// The melee hit function that populates a HitData and calls ProcessHit.
constexpr REL::ID kMeleeHit{ 38627 };
// HitData::Populate (CommonLibSSE-NG's RELOCATION_ID(42832, 44001)).
constexpr REL::ID kHitDataPopulate{ 44001 };
// ValueModifierEffect::ModifyActorValue(target, value, actorValue).
constexpr REL::ID kModifyActorValue{ 35086 };
// Actor::ModActorValue with a cause (actor, modifier, actorValue, delta,
// cause). The actor value owner's ModActorValue (38467) calls it with none.
constexpr REL::ID kModActorValueWithCause{ 38468 };
constexpr REL::ID kModActorValue{ 38467 };
// Actor::HandleHealthDamage, called by 38468 after Health has changed.
constexpr REL::ID kHandleHealthDamage{ 38606 };

// The vtables whose slots other plugins may have taken over: PlayerCharacter
// and Character (slot 0x104, HandleHealthDamage), the PlayerCharacter's
// ActorValueOwner (slot 6, ModActorValue) and ValueModifierEffect (slot
// 0x20, ModifyActorValue).
constexpr REL::ID     kPlayerCharacterVtable{ 208040 };
constexpr REL::ID     kCharacterVtable{ 207886 };
constexpr REL::ID     kPlayerValueOwnerVtable{ 208050 };
constexpr REL::ID     kValueModifierEffectVtable{ 206143 };
constexpr std::size_t kHandleHealthDamageSlot = 0x104;
constexpr std::size_t kModActorValueSlot = 6;
constexpr std::size_t kModifyActorValueSlot = 0x20;

// The functions are 0x11E0, 0x350, 0x1D0 and 0x560 bytes long on 1.7.104.
// The lengths only bound a scan; a match must still be unique.
constexpr std::size_t kProcessHitScanLength = 0x1200;
constexpr std::size_t kModifyActorValueScanLength = 0x360;
constexpr std::size_t kDoDamageScanLength = 0x1E0;
constexpr std::size_t kMeleeHitScanLength = 0x580;

constexpr std::uint8_t kCallOpcode = 0xE8;
constexpr std::size_t  kCallLength = 5;

// xor r9d, r9d; mov r8, rbx (attacker); movaps xmm1, xmm10 (totalDamage);
// mov rcx, r15 (target): right before ProcessHit's call of DoDamage. r13
// holds the HitData for the whole function.
constexpr std::array<std::uint8_t, 13> kHitCallPrefix{ 0x45, 0x33, 0xC9, 0x4C, 0x8B, 0xC3, 0x41, 0x0F, 0x28, 0xCA,
	0x49, 0x8B, 0xCF };
// xor r9d, r9d; mov r8, rbx (caster); movaps xmm1, xmm6 (damage); mov rcx,
// r14 (target): right before ModifyActorValue's call of DoDamage. rdi holds
// the effect for the whole function.
constexpr std::array<std::uint8_t, 12> kMagicCallPrefix{ 0x45, 0x33, 0xC9, 0x4C, 0x8B, 0xC3, 0x0F, 0x28, 0xCE, 0x49,
	0x8B, 0xCE };
// mov edx, 2 (damage modifier); movaps xmm3, xmm6 (-damage); mov [rsp + 0x20],
// rdi (source); mov rcx, rbx; lea r8d, [rdx + 0x16] (Health): right before
// DoDamage's call of 38468.
constexpr std::array<std::uint8_t, 20> kHealthCallPrefix{ 0xBA, 0x02, 0x00, 0x00, 0x00, 0x0F, 0x28, 0xDE, 0x48, 0x89,
	0x7C, 0x24, 0x20, 0x48, 0x8B, 0xCB, 0x44, 0x8D, 0x42, 0x16 };
// lea rdx, [rsp + 0x50] (HitData); mov rcx, rdi (target): right before the
// melee hit's call of ProcessHit. Only logged, not hooked.
constexpr std::array<std::uint8_t, 8> kMeleeProcessHitPrefix{ 0x48, 0x8D, 0x54, 0x24, 0x50, 0x48, 0x8B, 0xCF };

// The stubs that pass a register the game keeps across the call (the
// HitData, the effect) as the hook's fourth argument: mov r9, r13 and
// mov r9, rdi. Both calls pass 0 in r9 (dontAdjustDifficulty false), which
// the hooks pass on to DoDamage.
constexpr std::array<std::uint8_t, 3> kMoveR13ToR9{ 0x4D, 0x89, 0xE9 };
constexpr std::array<std::uint8_t, 3> kMoveRdiToR9{ 0x49, 0x89, 0xF9 };
// jmp [rip + 0]; dq target: what an SKSE trampoline entry holds.
constexpr std::array<std::uint8_t, 6> kTrampolineJump{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
constexpr std::uint8_t                kJumpRel32Opcode = 0xE9;

enum class Hook
{
	kNone,
	kHit,
	kMagic,
	kHealth,
};

enum class Pool
{
	kAuto,
	kStamina,
	kMagicka,
};

enum class Kind
{
	kOther,
	kPhysical,
	kMagic,
};

template <class T>
struct Name
{
	T                value;
	std::string_view name;
};

constexpr std::array kHookNames{
	Name<Hook>{ Hook::kNone, "none" },
	Name<Hook>{ Hook::kHit, "hit" },
	Name<Hook>{ Hook::kMagic, "magic" },
	Name<Hook>{ Hook::kHealth, "health" },
};

constexpr std::array kPoolNames{
	Name<Pool>{ Pool::kAuto, "auto" },
	Name<Pool>{ Pool::kStamina, "stamina" },
	Name<Pool>{ Pool::kMagicka, "magicka" },
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

std::string_view kindName(Kind kind)
{
	switch (kind) {
	case Kind::kPhysical:
		return "physical";
	case Kind::kMagic:
		return "magic";
	default:
		return "other";
	}
}

// The JSON file's fields of the same name.
struct SpikeConfig
{
	// The share of the damage redirected into the pool; 0 only observes.
	double fraction = 0.0;
	Hook   hook = Hook::kHealth;
	// auto: physical into Stamina, magic into Magicka (ST-11); other damage
	// (fall, drowning) is not split. stamina or magicka: every kind.
	Pool pool = Pool::kAuto;
	// Also log damage to actors other than the player (never split).
	bool logAllTargets = false;
};

std::string describe(const SpikeConfig& config)
{
	return std::format("fraction {}, hook {}, pool {}, logAllTargets {}", config.fraction,
		nameOf(kHookNames, config.hook), nameOf(kPoolNames, config.pool), config.logAllTargets);
}

template <class T, std::size_t N>
std::optional<T> readName(const nlohmann::json& file, const char* field, const std::array<Name<T>, N>& names,
	T fallback)
{
	const auto name = file.value(field, std::string{ nameOf(names, fallback) });
	const auto value = parseName(names, name);
	if (!value) {
		SKSE::log::error("S4: unknown {} \"{}\" in {}", field, name, kConfigPath);
	}
	return value;
}

SpikeConfig readConfig()
{
	std::ifstream stream{ std::filesystem::path(kConfigPath) };
	if (!stream) {
		SKSE::log::warn("S4: {} cannot be opened; using the defaults", kConfigPath);
		return {};
	}
	SpikeConfig config;
	try {
		const auto file = nlohmann::json::parse(stream);
		config.fraction = file.value("fraction", config.fraction);
		config.logAllTargets = file.value("logAllTargets", config.logAllTargets);
		const auto hook = readName(file, "hook", kHookNames, config.hook);
		const auto pool = readName(file, "pool", kPoolNames, config.pool);
		if (!hook || !pool) {
			return {};
		}
		config.hook = *hook;
		config.pool = *pool;
	} catch (const nlohmann::json::exception& error) {
		SKSE::log::error("S4: {} is not valid ({}); using the defaults", kConfigPath, error.what());
		return {};
	}
	if (!std::isfinite(config.fraction) || config.fraction < 0.0 || config.fraction > 1.0) {
		SKSE::log::error("S4: fraction {} in {} is not between 0 and 1; using the defaults", config.fraction,
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
		SKSE::log::warn("S4: {} not found (game folder {}); using the defaults", kConfigPath,
			std::filesystem::current_path().string());
		config_ = {};
	} else {
		config_ = readConfig();
	}
	SKSE::log::info("S4 config: {}", describe(config_));
	return config_;
}

ConfigCache g_config;

// Groups the lines of one damage event: hit and magic number the event, the
// health hook inside the same DoDamage call logs the same number and kind.
// 0 and kOther for DoDamage calls from elsewhere (fall, drowning, deferred
// tasks).
std::atomic<std::uint64_t> g_eventCount{ 0 };
thread_local std::uint64_t t_event = 0;
thread_local Kind          t_kind = Kind::kOther;
std::uint32_t              g_mainThread = 0;

class EventScope
{
public:
	explicit EventScope(Kind kind) :
		event_(++g_eventCount), outerEvent_(t_event), outerKind_(t_kind)
	{
		t_event = event_;
		t_kind = kind;
	}
	~EventScope()
	{
		t_event = outerEvent_;
		t_kind = outerKind_;
	}
	EventScope(const EventScope&) = delete;
	EventScope& operator=(const EventScope&) = delete;
	EventScope(EventScope&&) = delete;
	EventScope& operator=(EventScope&&) = delete;

	[[nodiscard]] std::uint64_t event() const noexcept { return event_; }

private:
	std::uint64_t event_;
	std::uint64_t outerEvent_;
	Kind          outerKind_;
};

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

std::string avName(RE::ActorValue value)
{
	const auto* info = static_cast<std::uint32_t>(value) < static_cast<std::uint32_t>(RE::ActorValue::kTotal) ?
	                       RE::ActorValueList::GetActorValueInfo(value) :
	                       nullptr;
	if (info == nullptr || info->enumName == nullptr) {
		return std::format("AV {}", static_cast<std::int32_t>(value));
	}
	return info->enumName;
}

float currentValue(RE::Actor* actor, RE::ActorValue value)
{
	return actor->AsActorValueOwner()->GetActorValue(value);
}

std::string poolsText(RE::Actor* actor)
{
	return std::format("Health {:.2f}, Stamina {:.2f}, Magicka {:.2f}", currentValue(actor, RE::ActorValue::kHealth),
		currentValue(actor, RE::ActorValue::kStamina), currentValue(actor, RE::ActorValue::kMagicka));
}

std::string threadText()
{
	const auto thread = REX::W32::GetCurrentThreadId();
	return std::format("thread {}{}", thread, thread == g_mainThread ? " (main)" : "");
}

std::string difficultyText()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (player == nullptr) {
		return "difficulty ?";
	}
	return std::format("difficulty {}", player->GetGameStatsData().difficulty);
}

struct FlagName
{
	RE::HitData::Flag flag;
	std::string_view  name;
};

constexpr std::array kHitFlagNames{
	FlagName{ RE::HitData::Flag::kMeleeAttack, "melee" },
	FlagName{ RE::HitData::Flag::kBash, "bash" },
	FlagName{ RE::HitData::Flag::kTimedBash, "timedBash" },
	FlagName{ RE::HitData::Flag::kPowerAttack, "power" },
	FlagName{ RE::HitData::Flag::kSneakAttack, "sneak" },
	FlagName{ RE::HitData::Flag::kCritical, "critical" },
	FlagName{ RE::HitData::Flag::kBlocked, "blocked" },
	FlagName{ RE::HitData::Flag::kBlockWithWeapon, "blockWithWeapon" },
	FlagName{ RE::HitData::Flag::kLeftHand, "leftHand" },
	FlagName{ RE::HitData::Flag::kExplosion, "explosion" },
	FlagName{ RE::HitData::Flag::kFatal, "fatal" },
};

std::string hitFlagsText(const RE::HitData& hit)
{
	std::string text;
	for (const auto& entry : kHitFlagNames) {
		if (!hit.flags.all(entry.flag)) {
			continue;
		}
		text += text.empty() ? "" : "|";
		text += entry.name;
	}
	return std::format("{:#x} {}", hit.flags.underlying(), text.empty() ? "-" : text);
}

std::string projectileText(const RE::HitData& hit)
{
	const auto source = hit.sourceRef.get();
	if (!source) {
		return "none";
	}
	return formText(source->GetBaseObject());
}

std::string hitText(const RE::HitData* hit)
{
	if (hit == nullptr) {
		return "no HitData";
	}
	return std::format(
		"weapon {}, projectile {}, flags {}, physicalDamage {:.2f}, totalDamage {:.2f}, resistedPhysical {:.2f}, "
		"resistedTyped {:.2f}, percentBlocked {:.2f}, sneakBonus {:.2f}, bonusHealthMult {:.2f}, reflected {:.2f}, "
		"skill {}",
		formText(hit->weapon), projectileText(*hit), hitFlagsText(*hit), hit->physicalDamage, hit->totalDamage,
		hit->resistedPhysicalDamage, hit->resistedTypedDamage, hit->percentBlocked, hit->sneakAttackBonus,
		hit->bonusHealthDamageMult, hit->reflectedDamage, avName(hit->skill));
}

std::string effectText(RE::ValueModifierEffect* effect)
{
	if (effect == nullptr) {
		return "no effect";
	}
	const auto* setting = effect->GetBaseObject();
	return std::format(
		"spell {}, effect {} (archetype {}), AV {}, magnitude {:.2f}, duration {:.2f}, elapsed {:.2f}, "
		"casting source {}",
		formText(effect->spell), formText(setting),
		setting != nullptr ? static_cast<std::int32_t>(setting->GetArchetype()) : -1, avName(effect->actorValue),
		effect->magnitude, effect->duration, effect->elapsedSeconds, static_cast<std::int32_t>(effect->castingSource));
}

bool shouldLog(const SpikeConfig& config, const RE::Actor* target)
{
	return config.logAllTargets || (target != nullptr && target->IsPlayerRef());
}

// --- The split --------------------------------------------------------------

std::optional<RE::ActorValue> poolFor(Pool pool, Kind kind)
{
	switch (pool) {
	case Pool::kStamina:
		return RE::ActorValue::kStamina;
	case Pool::kMagicka:
		return RE::ActorValue::kMagicka;
	default:
		break;
	}
	if (kind == Kind::kPhysical) {
		return RE::ActorValue::kStamina;
	}
	if (kind == Kind::kMagic) {
		return RE::ActorValue::kMagicka;
	}
	return std::nullopt;
}

struct Split
{
	float       health = 0.0F;
	std::string text;
};

// ST-11: the redirected share comes out of the pool while it lasts; what the
// pool cannot take goes to Health with the rest. Only the player is split,
// and only at the hook the config chooses.
Split split(const SpikeConfig& config, Hook at, Kind kind, RE::Actor* target, float damage)
{
	if (config.hook != at) {
		return { damage, "" };
	}
	if (target == nullptr || !target->IsPlayerRef() || damage <= 0.0F || config.fraction <= 0.0) {
		return { damage, " [split here: nothing to split]" };
	}
	const auto pool = poolFor(config.pool, kind);
	if (!pool) {
		return { damage, std::format(" [split here: not split, {} damage under pool auto]", kindName(kind)) };
	}
	const auto  redirected = static_cast<float>(damage * config.fraction);
	const float available = (std::max)(currentValue(target, *pool), 0.0F);
	const float taken = (std::min)(redirected, available);
	if (taken > 0.0F) {
		target->AsActorValueOwner()->DamageActorValue(*pool, taken);
	}
	const float health = damage - taken;
	return { health, std::format(" [split here x{}: {:.2f} of {:.2f} redirected into {} ({:.2f} available), {:.2f} to "
								 "Health; {} now {:.2f}]",
						 config.fraction, taken, redirected, avName(*pool), available, health, avName(*pool),
						 currentValue(target, *pool)) };
}

// --- Candidate 1: ProcessHit's call of DoDamage (physical) ------------------

using DoDamage = bool (*)(RE::Actor*, float, RE::Actor*, bool);
DoDamage    g_hitDoDamage = nullptr;
std::string g_hitTargetText;

// Reached through a stub that puts ProcessHit's HitData (r13) into the
// fourth argument; DoDamage gets the false the game passes there.
bool hitDoDamage(RE::Actor* target, float damage, RE::Actor* attacker, RE::HitData* hit)
{
	const auto config = g_config.current();
	const auto log = shouldLog(config, target);
	if (!log && config.hook != Hook::kHit) {
		return g_hitDoDamage(target, damage, attacker, false);
	}
	const EventScope scope{ Kind::kPhysical };
	const auto       parts = split(config, Hook::kHit, Kind::kPhysical, target, damage);
	if (log) {
		SKSE::log::info(
			"S4 #{} hit: {} -> {}, {:.2f} damage (after armor, block, perks; before difficulty) -> "
			"{:.2f}{}; {}; {}; {}; into {}",
			scope.event(), actorText(attacker), actorText(target), damage, parts.health, parts.text, hitText(hit),
			poolsText(target), threadText(), g_hitTargetText);
	}
	const bool result = g_hitDoDamage(target, parts.health, attacker, false);
	if (log) {
		SKSE::log::info("S4 #{} hit done: {}, DoDamage returned {}", scope.event(), poolsText(target), result);
	}
	return result;
}

// --- Candidate 2: ModifyActorValue's call of DoDamage (magic) ---------------

DoDamage    g_magicDoDamage = nullptr;
std::string g_magicTargetText;

// Reached through a stub that puts ModifyActorValue's effect (rdi) into the
// fourth argument; DoDamage gets the false the game passes there.
bool magicDoDamage(RE::Actor* target, float damage, RE::Actor* caster, RE::ValueModifierEffect* effect)
{
	const auto config = g_config.current();
	const auto log = shouldLog(config, target);
	if (!log && config.hook != Hook::kMagic) {
		return g_magicDoDamage(target, damage, caster, false);
	}
	const EventScope scope{ Kind::kMagic };
	const auto       parts = split(config, Hook::kMagic, Kind::kMagic, target, damage);
	if (log) {
		SKSE::log::info(
			"S4 #{} magic: {} -> {}, {:.3f} damage (after resistances; before difficulty) -> "
			"{:.3f}{}; {}; {}; {}; into {}",
			scope.event(), actorText(caster), actorText(target), damage, parts.health, parts.text, effectText(effect),
			poolsText(target), threadText(), g_magicTargetText);
	}
	const bool result = g_magicDoDamage(target, parts.health, caster, false);
	if (log) {
		SKSE::log::info("S4 #{} magic done: {}, DoDamage returned {}", scope.event(), poolsText(target), result);
	}
	return result;
}

// --- Candidate 3: DoDamage's call of the actor-value change (all kinds) -----

using ModActorValueWithCause = void (*)(RE::Actor*, RE::ACTOR_VALUE_MODIFIER, RE::ActorValue, float, RE::Actor*);
ModActorValueWithCause g_healthChange = nullptr;
std::string            g_healthTargetText;

void healthChange(RE::Actor* actor, RE::ACTOR_VALUE_MODIFIER modifier, RE::ActorValue value, float delta,
	RE::Actor* source)
{
	const auto config = g_config.current();
	const auto log = shouldLog(config, actor);
	const bool isHealthDamage = modifier == RE::ACTOR_VALUE_MODIFIER::kDamage && value == RE::ActorValue::kHealth &&
	                            delta < 0.0F;
	if (!isHealthDamage || (!log && config.hook != Hook::kHealth)) {
		g_healthChange(actor, modifier, value, delta, source);
		return;
	}
	const auto kind = t_kind;
	const auto parts = split(config, Hook::kHealth, kind, actor, -delta);
	if (log) {
		SKSE::log::info(
			"S4 #{} health ({}): {} -> {}, {:.3f} damage (after difficulty, {}) -> {:.3f}{}; {}; {}; "
			"into {}",
			t_event, kindName(kind), actorText(source), actorText(actor), -delta, difficultyText(), parts.health,
			parts.text, poolsText(actor), threadText(), g_healthTargetText);
	}
	g_healthChange(actor, modifier, value, -parts.health, source);
	if (log) {
		SKSE::log::info("S4 #{} health done: {}", t_event, poolsText(actor));
	}
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

// How many E8 calls in [begin, begin + length) go to target.
std::size_t countCallsTo(std::uintptr_t begin, std::size_t length, std::uintptr_t target)
{
	std::size_t count = 0;
	for (auto at = begin; at + kCallLength <= begin + length; ++at) {
		if (*reinterpret_cast<const std::uint8_t*>(at) == kCallOpcode && callTarget(at) == target) {
			++count;
		}
	}
	return count;
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
	const auto vanillaCalls = countCallsTo(base, site.scanLength, site.vanillaTarget.address());
	const auto call = findCallAfter(base, site.scanLength, site.prefix);
	if (!call) {
		SKSE::log::error(
			"S4 {}: the call in {} ({:#x}) was not found (unique prefix and E8 expected; {} vanilla "
			"calls of {} there)",
			site.name, site.function.id(), base, vanillaCalls, site.vanillaTarget.id());
		return std::nullopt;
	}
	SKSE::log::info("S4 {}: found at {} + {:#x} ({:#x}), {} vanilla calls of {} in the function; the call goes to {}",
		site.name, site.function.id(), *call - base, *call, vanillaCalls, site.vanillaTarget.id(),
		targetText(callTarget(*call), site.vanillaTarget.address()));
	return call;
}

// --- Install ----------------------------------------------------------------

void installHitHook()
{
	const auto call = findSite({ "hit", kProcessHit, kProcessHitScanLength, kHitCallPrefix, kDoDamage });
	if (!call) {
		return;
	}
	g_hitTargetText = targetText(callTarget(*call), kDoDamage.address());
	const auto stub = makeStub(kMoveR13ToR9, reinterpret_cast<std::uintptr_t>(&hitDoDamage));
	g_hitDoDamage = reinterpret_cast<DoDamage>(SKSE::GetTrampoline().write_call<kCallLength>(*call, stub));
	SKSE::log::info("S4 hit: hooked");
}

void installMagicHook()
{
	const auto call = findSite({ "magic", kModifyActorValue, kModifyActorValueScanLength, kMagicCallPrefix,
		kDoDamage });
	if (!call) {
		return;
	}
	g_magicTargetText = targetText(callTarget(*call), kDoDamage.address());
	const auto stub = makeStub(kMoveRdiToR9, reinterpret_cast<std::uintptr_t>(&magicDoDamage));
	g_magicDoDamage = reinterpret_cast<DoDamage>(SKSE::GetTrampoline().write_call<kCallLength>(*call, stub));
	SKSE::log::info("S4 magic: hooked");
}

void installHealthHook()
{
	const auto call = findSite({ "health", kDoDamage, kDoDamageScanLength, kHealthCallPrefix,
		kModActorValueWithCause });
	if (!call) {
		return;
	}
	g_healthTargetText = targetText(callTarget(*call), kModActorValueWithCause.address());
	g_healthChange = reinterpret_cast<ModActorValueWithCause>(
		SKSE::GetTrampoline().write_call<kCallLength>(*call, &healthChange));
	SKSE::log::info("S4 health: hooked");
}

// Whether a function's first instruction is a jump, as an entry detour by
// another plugin would be.
void logEntry(std::string_view name, REL::ID function)
{
	const auto address = function.address();
	const auto destination = followJumps(address);
	if (destination == address) {
		SKSE::log::info("S4 coexistence: {} ({}, {:#x}) entry is not detoured", name, function.id(), address);
		return;
	}
	SKSE::log::info("S4 coexistence: {} ({}, {:#x}) entry jumps to {:#x} {}", name, function.id(), address,
		destination, moduleText(destination));
}

void logVtableSlot(std::string_view name, REL::ID vtable, std::size_t slot, REL::ID vanilla)
{
	const auto entry = vtable.address() + sizeof(void*) * slot;
	const auto target = *reinterpret_cast<const std::uintptr_t*>(entry);
	SKSE::log::info("S4 coexistence: {} (vtable {} slot {:#x}) holds {}", name, vtable.id(), slot,
		targetText(target, vanilla.address()));
}

// Logs whether other plugins patched the damage functions or the call
// combat overhauls redirect, before the spike patches anything.
void logCoexistence()
{
	findSite({ "coexistence: melee ProcessHit call", kMeleeHit, kMeleeHitScanLength, kMeleeProcessHitPrefix,
		kProcessHit });
	logEntry("HitData::Populate", kHitDataPopulate);
	logEntry("ProcessHit", kProcessHit);
	logEntry("DoDamage", kDoDamage);
	logEntry("ModifyActorValue", kModifyActorValue);
	logEntry("ModActorValue with cause", kModActorValueWithCause);
	logVtableSlot("PlayerCharacter::HandleHealthDamage", kPlayerCharacterVtable, kHandleHealthDamageSlot,
		kHandleHealthDamage);
	logVtableSlot("Character::HandleHealthDamage", kCharacterVtable, kHandleHealthDamageSlot, kHandleHealthDamage);
	logVtableSlot("PlayerCharacter ActorValueOwner::ModActorValue", kPlayerValueOwnerVtable, kModActorValueSlot,
		kModActorValue);
	logVtableSlot("ValueModifierEffect::ModifyActorValue", kValueModifierEffectVtable, kModifyActorValueSlot,
		kModifyActorValue);
}
}

void installDamageSplitSpike()
{
	g_mainThread = REX::W32::GetCurrentThreadId();
	SKSE::log::info("S4: DoDamage {} {:#x}, ProcessHit {} {:#x}, ModifyActorValue {} {:#x}, 38468 {:#x}, {}",
		kDoDamage.id(), kDoDamage.address(), kProcessHit.id(), kProcessHit.address(), kModifyActorValue.id(),
		kModifyActorValue.address(), kModActorValueWithCause.address(), threadText());
	SKSE::log::info("S4 config at install: {}", describe(g_config.current()));
	logCoexistence();
	installHitHook();
	installMagicHook();
	installHealthHook();
}
}
