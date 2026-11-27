// THROWAWAY prototype of spike S3 (docs/stats/PLAN.md, Phase 3). It only
// answers where skill XP can be multiplied so that a factor stacks with the
// game's own bonuses; G6 replaces it.
//
// How the game turns a skill use into skill XP (disassembly of SkyrimSE.exe
// 1.7.104; Address Library IDs are AE):
//
// - Every skill use is a virtual call of Actor::UseSkill, which only the
//   player implements: PlayerCharacter::UseSkill (40488). The console's
//   player.advskill (AdvancePCSkill, 22367) calls it the same way.
// - UseSkill calls the skill XP routine (41561) with isUse = true. The
//   routine takes XP = useMult * points + useOffset (the skill's AVSK), then
//   runs the perk entry point Mod Skill Use (BGSEntryPoint::HandleEntryPoint,
//   23526) on it, adds the result to the skill and levels the skill up.
// - The Guardian Stones (x1.2 for their skills), Rested (x1.05), Well Rested
//   (x1.1) and Lover's Comfort (x1.15) are perks with a Mod Skill Use
//   "Multiply Value" entry (Skyrim.esm), so they all act inside that one
//   entry point call. Fortify <Skill> effects raise the skill level, not XP.
// - Whole-level grants (trainers, skill books, incpcs) go through
//   PlayerSkills::IncrementSkill (41562), which calls the routine with
//   isUse = false: XP = the XP missing to the next level, then the same entry
//   point.
// - Experience replaces both calls of the routine (in 40488 and 41562) with
//   a wrapper that skips the routine while the skill is at its cap.
//
// The candidates, from early to late: use_skill (the vtable slot), the
// routine's call in UseSkill (xp_routine_call), and the entry point call in
// the routine (perk_entry).

#include "SkillXpSpike.h"

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
using PlayerSkills = RE::PlayerCharacter::PlayerSkills;
using SkillData = PlayerSkills::Data::SkillData;

// Relative to the game folder, which is the game's working directory.
constexpr std::string_view kConfigPath = "Data/SKSE/Plugins/trials-of-mastery-stats-s3.json";

// PlayerCharacter::UseSkill(skill, points, object, action), which
// CommonLibSSE-NG calls AddSkillExperience. It has no direct callers; the game
// calls it through slot 0xF7 of the PlayerCharacter vtable.
constexpr REL::ID kUseSkill{ 40488 };
// The PlayerCharacter vtable (RE::VTABLE_PlayerCharacter[0]).
constexpr REL::ID     kPlayerCharacterVtable{ 208040 };
constexpr std::size_t kUseSkillSlot = 0xF7;
// The skill XP routine (skills, skill, points, object, action, isUse,
// noNotify).
constexpr REL::ID kSkillXpRoutine{ 41561 };
// PlayerSkills::IncrementSkill(skill, count), for whole-level grants.
constexpr REL::ID kIncrementSkill{ 41562 };
// BGSEntryPoint::HandleEntryPoint(entry point, perk owner, ...).
constexpr REL::ID kHandleEntryPoint{ 23526 };

// The functions are 0x2F, 0x3F0 and 0xF0 bytes long on 1.7.104. The lengths
// only bound a scan; a match must still be unique.
constexpr std::size_t kUseSkillScanLength = 0x40;
constexpr std::size_t kSkillXpRoutineScanLength = 0x400;
constexpr std::size_t kIncrementSkillScanLength = 0x100;

constexpr std::uint8_t kCallOpcode = 0xE8;
constexpr std::size_t  kCallLength = 5;

// mov byte ptr [rsp + 0x28], 1 (isUse); mov dword ptr [rsp + 0x20], eax
// (action): right before UseSkill's call of the routine.
constexpr std::array<std::uint8_t, 9> kUseSkillCallPrefix{ 0xC6, 0x44, 0x24, 0x28, 0x01, 0x89, 0x44, 0x24, 0x20 };
// mov dword ptr [rsp + 0x20], r13d (action 0): right before
// IncrementSkill's call of the routine.
constexpr std::array<std::uint8_t, 5> kIncrementSkillCallPrefix{ 0x44, 0x89, 0x6C, 0x24, 0x20 };
// mov ecx, 0x16 (Mod Skill Use): right before the routine's entry point call.
constexpr std::array<std::uint8_t, 5> kModSkillUsePrefix{ 0xB9,
	static_cast<std::uint8_t>(RE::BGSEntryPoint::ENTRY_POINT::kModSkillUse), 0x00, 0x00, 0x00 };
// mov rax, [rip + player]; mov [rax + disp32], esi: the routine stores the
// skill in the player's advanceSkill, which the perk conditions
// (EPModSkillUsage_IsAdvanceSkill) read. disp32 is taken from the code.
constexpr std::array<std::uint8_t, 3> kLoadPlayerOpcode{ 0x48, 0x8B, 0x05 };
constexpr std::array<std::uint8_t, 2> kStoreSkillOpcode{ 0x89, 0xB0 };
// movss xmm0, [rsp + 0x30]; mulss xmm0, xmm6: the routine's useMult * points.
// Beyond Mastery replaces it when bScaleXPGainAbove100 is set.
constexpr std::array<std::uint8_t, 10> kUseXpBytes{ 0xF3, 0x0F, 0x10, 0x44, 0x24, 0x30, 0xF3, 0x0F, 0x59, 0xC6 };
// jmp [rip + 0]; dq target: what an SKSE trampoline entry holds.
constexpr std::array<std::uint8_t, 6> kTrampolineJump{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };

enum class Hook
{
	kNone,
	kUseSkill,
	kXpRoutineCall,
	kPerkEntry,
};

struct HookName
{
	Hook             hook;
	std::string_view name;
};

constexpr std::array kHookNames{
	HookName{ Hook::kNone, "none" },
	HookName{ Hook::kUseSkill, "use_skill" },
	HookName{ Hook::kXpRoutineCall, "xp_routine_call" },
	HookName{ Hook::kPerkEntry, "perk_entry" },
};

std::string_view hookName(Hook hook)
{
	for (const auto& entry : kHookNames) {
		if (entry.hook == hook) {
			return entry.name;
		}
	}
	return "?";
}

std::optional<Hook> parseHook(std::string_view name)
{
	for (const auto& entry : kHookNames) {
		if (entry.name == name) {
			return entry.hook;
		}
	}
	return std::nullopt;
}

// The JSON file's fields of the same name.
struct SpikeConfig
{
	double factor = 1.0;
	Hook   hook = Hook::kPerkEntry;
	// perk_entry only: also multiply whole-level grants, which are not uses.
	bool applyToWholeLevel = false;
};

std::string describe(const SpikeConfig& config)
{
	return std::format("factor {}, hook {}, applyToWholeLevel {}", config.factor, hookName(config.hook),
		config.applyToWholeLevel);
}

SpikeConfig readConfig()
{
	std::ifstream stream{ std::filesystem::path(kConfigPath) };
	if (!stream) {
		SKSE::log::warn("S3: {} cannot be opened; using the defaults", kConfigPath);
		return {};
	}
	SpikeConfig config;
	try {
		const auto file = nlohmann::json::parse(stream);
		config.factor = file.value("factor", config.factor);
		config.applyToWholeLevel = file.value("applyToWholeLevel", config.applyToWholeLevel);
		const auto name = file.value("hook", std::string{ hookName(config.hook) });
		const auto hook = parseHook(name);
		if (!hook) {
			SKSE::log::error("S3: unknown hook \"{}\" in {}; using the defaults", name, kConfigPath);
			return {};
		}
		config.hook = *hook;
	} catch (const nlohmann::json::exception& error) {
		SKSE::log::error("S3: {} is not valid ({}); using the defaults", kConfigPath, error.what());
		return {};
	}
	if (!std::isfinite(config.factor) || config.factor < 0.0) {
		SKSE::log::error("S3: factor {} in {} is not a finite value of 0 or more; using the defaults", config.factor,
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
		SKSE::log::warn("S3: {} not found (game folder {}); using the defaults", kConfigPath,
			std::filesystem::current_path().string());
		config_ = {};
	} else {
		config_ = readConfig();
	}
	SKSE::log::info("S3 config: {}", describe(config_));
	return config_;
}

ConfigCache g_config;

// Groups the lines that one skill use logs: use_skill numbers the use, the
// hooks inside it log the same number. 0 outside a use (whole-level grants,
// or callers that bypass the vtable).
std::atomic<std::uint64_t> g_useCount{ 0 };
thread_local std::uint64_t t_use = 0;
// Set while the routine runs from UseSkill's call, so the entry point hook can
// tell a use from a whole-level grant.
thread_local bool            t_insideUseCall = false;
std::optional<std::uint32_t> g_advanceSkillOffset;
bool                         g_xpRoutineCallHooked = false;

float applyFactor(const SpikeConfig& config, Hook at, float xp)
{
	if (config.hook != at) {
		return xp;
	}
	return static_cast<float>(xp * config.factor);
}

std::string factorMark(const SpikeConfig& config, Hook at)
{
	if (config.hook != at) {
		return "";
	}
	return std::format(" [factor x{} applied here]", config.factor);
}

std::optional<std::size_t> skillIndex(RE::ActorValue skill)
{
	const auto first = static_cast<std::uint32_t>(RE::ActorValue::kOneHanded);
	const auto value = static_cast<std::uint32_t>(skill);
	if (value < first || value >= first + PlayerSkills::Data::Skill::kTotal) {
		return std::nullopt;
	}
	return value - first;
}

std::string skillName(RE::ActorValue skill)
{
	const auto* info = static_cast<std::uint32_t>(skill) < static_cast<std::uint32_t>(RE::ActorValue::kTotal) ?
	                       RE::ActorValueList::GetActorValueInfo(skill) :
	                       nullptr;
	if (info == nullptr || info->enumName == nullptr) {
		return std::format("AV {}", static_cast<std::int32_t>(skill));
	}
	return info->enumName;
}

std::string formText(const RE::TESForm* form)
{
	if (form == nullptr) {
		return "none";
	}
	return std::format("{:08X} \"{}\"", form->GetFormID(), form->GetName());
}

// The skill's AVSK use values (CommonLibSSE-NG calls the use offset at +4
// offsetMult) and the player's <Skill>SkillAdvance actor value, which no
// vanilla bonus uses but other mods might.
std::string gameValues(RE::ActorValue skill)
{
	const auto  index = skillIndex(skill);
	const auto* info = RE::ActorValueList::GetActorValueInfo(skill);
	if (!index || info == nullptr || info->skill == nullptr) {
		return "no skill data";
	}
	const auto advance = static_cast<RE::ActorValue>(
		static_cast<std::uint32_t>(RE::ActorValue::kOneHandedSkillAdvance) + static_cast<std::uint32_t>(*index));
	return std::format("AVSK useMult {:.3f} useOffset {:.3f}, {} {:.2f}", info->skill->useMult,
		info->skill->offsetMult, skillName(advance),
		RE::PlayerCharacter::GetSingleton()->AsActorValueOwner()->GetActorValue(advance));
}

std::optional<SkillData> skillData(RE::ActorValue skill)
{
	const auto  index = skillIndex(skill);
	const auto* skills = RE::PlayerCharacter::GetSingleton()->GetPlayerRuntimeData().skills;
	if (!index || skills == nullptr || skills->data == nullptr) {
		return std::nullopt;
	}
	return skills->data->skills[*index];
}

std::string skillText(const std::optional<SkillData>& data)
{
	if (!data) {
		return "no skill data";
	}
	return std::format("level {:.0f}, XP {:.3f}/{:.3f}", data->level, data->xp, data->levelThreshold);
}

std::string gainText(const std::optional<SkillData>& before, const std::optional<SkillData>& after)
{
	if (!before || !after) {
		return "unknown";
	}
	if (before->level != after->level) {
		return std::format("level {:.0f} -> {:.0f}", before->level, after->level);
	}
	return std::format("{:.3f} XP", after->xp - before->xp);
}

// The skill the routine stored in the player's advanceSkill for the perk
// conditions, read at the offset taken from the routine's code.
std::string advanceSkillName(const RE::Actor* owner)
{
	if (!g_advanceSkillOffset || owner == nullptr) {
		return "skill unknown";
	}
	std::uint32_t skill = 0;
	std::memcpy(&skill, reinterpret_cast<const std::byte*>(owner) + *g_advanceSkillOffset, sizeof(skill));
	return skillName(static_cast<RE::ActorValue>(skill));
}

std::string_view grantKind(bool insideUseCall)
{
	if (!g_xpRoutineCallHooked) {
		return "use or whole-level grant";
	}
	return insideUseCall ? "use" : "whole-level grant";
}

// --- Candidate 1: the vtable slot of PlayerCharacter::UseSkill --------------

using UseSkill = void (*)(RE::PlayerCharacter*, RE::ActorValue, float, RE::TESForm*, std::uint32_t);
UseSkill g_useSkill = nullptr;

void useSkill(RE::PlayerCharacter* player, RE::ActorValue skill, float points, RE::TESForm* object,
	std::uint32_t action)
{
	const auto config = g_config.current();
	const auto use = ++g_useCount;
	const auto outer = t_use;
	t_use = use;
	const float applied = applyFactor(config, Hook::kUseSkill, points);
	const auto  before = skillData(skill);
	SKSE::log::info("S3 #{} use_skill: {} {:.3f} points -> {:.3f}{}, object {}, action {}; {}; {}", use,
		skillName(skill), points, applied, factorMark(config, Hook::kUseSkill), formText(object), action,
		gameValues(skill), skillText(before));
	g_useSkill(player, skill, applied, object, action);
	const auto after = skillData(skill);
	SKSE::log::info("S3 #{} done: {} gained {}; {}", use, skillName(skill), gainText(before, after),
		skillText(after));
	t_use = outer;
}

// --- Candidate 2: UseSkill's call of the skill XP routine -------------------

using SkillXpRoutine = void (*)(PlayerSkills*, RE::ActorValue, float, RE::TESForm*, std::uint32_t, bool, bool);
SkillXpRoutine g_xpRoutineTarget = nullptr;
std::string    g_xpRoutineTargetText;

void xpRoutineCall(PlayerSkills* skills, RE::ActorValue skill, float points, RE::TESForm* object,
	std::uint32_t action, bool isUse, bool noNotify)
{
	const auto  config = g_config.current();
	const float applied = applyFactor(config, Hook::kXpRoutineCall, points);
	SKSE::log::info("S3 #{} xp_routine_call: {} {:.3f} points -> {:.3f}{}, isUse {}, into {}", t_use,
		skillName(skill), points, applied, factorMark(config, Hook::kXpRoutineCall), isUse, g_xpRoutineTargetText);
	const bool outer = t_insideUseCall;
	t_insideUseCall = true;
	g_xpRoutineTarget(skills, skill, applied, object, action, isUse, noNotify);
	t_insideUseCall = outer;
}

// --- Candidate 3: the routine's Mod Skill Use entry point call --------------

using HandleEntryPoint = void (*)(std::uint32_t, RE::Actor*, float*);
HandleEntryPoint g_handleEntryPoint = nullptr;

void perkEntry(std::uint32_t entryPoint, RE::Actor* owner, float* xp)
{
	const float beforePerks = *xp;
	g_handleEntryPoint(entryPoint, owner, xp);
	const float afterPerks = *xp;
	const auto  config = g_config.current();
	const bool  isUse = t_insideUseCall;
	// Without the xp_routine_call hook a use cannot be told apart, so the
	// factor applies to everything.
	const bool applies = isUse || !g_xpRoutineCallHooked || config.applyToWholeLevel;
	if (applies) {
		*xp = applyFactor(config, Hook::kPerkEntry, afterPerks);
	}
	const auto mark = applies || config.hook != Hook::kPerkEntry ? factorMark(config, Hook::kPerkEntry) :
	                                                               std::string{ " [factor not applied: no use]" };
	const auto multiplier = beforePerks != 0.0F ? std::format("x{:.4f}", afterPerks / beforePerks) : "n/a";
	SKSE::log::info(
		"S3 #{} perk_entry: {} ({}): {:.3f} XP before the entry point, {:.3f} after (perk multiplier {}), "
		"{:.3f} added{}",
		t_use, advanceSkillName(owner), grantKind(isUse), beforePerks, afterPerks, multiplier, *xp, mark);
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
// there exactly once and a call follows it. The call's target is not checked.
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

// Follows SKSE trampoline jumps to the function they lead to.
std::uintptr_t followJumps(std::uintptr_t address)
{
	constexpr int kMaxHops = 4;
	for (int hop = 0; hop < kMaxHops; ++hop) {
		if (std::memcmp(reinterpret_cast<const void*>(address), kTrampolineJump.data(), kTrampolineJump.size()) != 0) {
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

// Where a call target leads, and whether that is the function it calls in
// vanilla.
std::string targetText(std::uintptr_t target, std::uintptr_t vanilla)
{
	if (target == vanilla) {
		return std::format("{:#x} (vanilla)", target);
	}
	const auto destination = followJumps(target);
	return std::format("{:#x} -> {:#x} {} (redirected; vanilla is {:#x})", target, destination,
		moduleText(destination), vanilla);
}

// --- Install ----------------------------------------------------------------

void installUseSkillHook()
{
	REL::Relocation<std::uintptr_t> vtable{ kPlayerCharacterVtable };
	const auto                      slot = vtable.address() + sizeof(void*) * kUseSkillSlot;
	const auto                      previous = *reinterpret_cast<const std::uintptr_t*>(slot);
	g_useSkill = reinterpret_cast<UseSkill>(previous);
	vtable.write_vfunc(kUseSkillSlot, &useSkill);
	SKSE::log::info("S3 use_skill: PlayerCharacter vtable (ID {}) {:#x} slot {:#x} hooked; it held {}",
		kPlayerCharacterVtable.id(), vtable.address(), kUseSkillSlot, targetText(previous, kUseSkill.address()));
}

void installXpRoutineCallHook()
{
	const auto base = kUseSkill.address();
	const auto call = findCallAfter(base, kUseSkillScanLength, kUseSkillCallPrefix);
	if (!call) {
		SKSE::log::error("S3 xp_routine_call: the routine's call in UseSkill ({:#x}) was not found; not hooked", base);
		return;
	}
	const auto target = callTarget(*call);
	g_xpRoutineTarget = reinterpret_cast<SkillXpRoutine>(target);
	g_xpRoutineTargetText = targetText(target, kSkillXpRoutine.address());
	SKSE::GetTrampoline().write_call<kCallLength>(*call, &xpRoutineCall);
	g_xpRoutineCallHooked = true;
	SKSE::log::info("S3 xp_routine_call: hooked at {} + {:#x} ({:#x}); the call went to {}", kUseSkill.id(),
		*call - base, *call, g_xpRoutineTargetText);
}

// Only logs where IncrementSkill's call of the routine goes; not hooked.
void logIncrementSkillCall()
{
	const auto base = kIncrementSkill.address();
	const auto call = findCallAfter(base, kIncrementSkillScanLength, kIncrementSkillCallPrefix);
	if (!call) {
		SKSE::log::warn("S3: the routine's call in IncrementSkill ({:#x}) was not found", base);
		return;
	}
	SKSE::log::info("S3: IncrementSkill ({}) + {:#x} calls {}", kIncrementSkill.id(), *call - base,
		targetText(callTarget(*call), kSkillXpRoutine.address()));
}

void findAdvanceSkillOffset()
{
	const auto                   base = kSkillXpRoutine.address();
	const auto                   code = codeAt(base, kSkillXpRoutineScanLength);
	std::optional<std::uint32_t> offset;
	int                          matches = 0;
	for (const auto at : findAll(code, kLoadPlayerOpcode)) {
		const auto store = at + kLoadPlayerOpcode.size() + sizeof(std::int32_t);
		if (store + kStoreSkillOpcode.size() + sizeof(std::uint32_t) > code.size() ||
			std::memcmp(code.data() + store, kStoreSkillOpcode.data(), kStoreSkillOpcode.size()) != 0) {
			continue;
		}
		std::uint32_t value = 0;
		std::memcpy(&value, code.data() + store + kStoreSkillOpcode.size(), sizeof(value));
		offset = value;
		++matches;
	}
	if (matches != 1) {
		SKSE::log::warn("S3: the routine's store of advanceSkill was found {} times; perk_entry logs no skill",
			matches);
		return;
	}
	g_advanceSkillOffset = offset;
	SKSE::log::info("S3: the routine stores advanceSkill at player + {:#x}", *offset);
}

void installPerkEntryHook()
{
	const auto                  base = kSkillXpRoutine.address();
	const auto                  code = codeAt(base, kSkillXpRoutineScanLength);
	std::vector<std::uintptr_t> calls;
	for (const auto at : findAll(code, kModSkillUsePrefix)) {
		const auto call = base + at + kModSkillUsePrefix.size();
		if (call + kCallLength <= base + code.size() && *reinterpret_cast<const std::uint8_t*>(call) == kCallOpcode &&
			callTarget(call) == kHandleEntryPoint.address()) {
			calls.push_back(call);
		}
	}
	if (calls.size() != 1) {
		SKSE::log::error(
			"S3 perk_entry: expected one Mod Skill Use call of HandleEntryPoint ({:#x}) in the routine ({:#x}), "
			"found {}; not hooked",
			kHandleEntryPoint.address(), base, calls.size());
		return;
	}
	g_handleEntryPoint = reinterpret_cast<HandleEntryPoint>(kHandleEntryPoint.address());
	SKSE::GetTrampoline().write_call<kCallLength>(calls.front(), &perkEntry);
	SKSE::log::info("S3 perk_entry: hooked at {} + {:#x} ({:#x}), the call of HandleEntryPoint ({}, {:#x})",
		kSkillXpRoutine.id(), calls.front() - base, calls.front(), kHandleEntryPoint.id(), kHandleEntryPoint.address());

	const auto useXp = findAll(code, kUseXpBytes).size();
	SKSE::log::info("S3: the routine's useMult * points is {}", useXp == 1 ? "vanilla" :
																			 "replaced (Beyond Mastery's XP scaling?)");
}
}

void installSkillXpSpike()
{
	SKSE::log::info("S3: UseSkill {} {:#x}, skill XP routine {} {:#x}, IncrementSkill {} {:#x}, HandleEntryPoint {} {:#x}",
		kUseSkill.id(), kUseSkill.address(), kSkillXpRoutine.id(), kSkillXpRoutine.address(), kIncrementSkill.id(),
		kIncrementSkill.address(), kHandleEntryPoint.id(), kHandleEntryPoint.address());
	SKSE::log::info("S3 config at install: {}", describe(g_config.current()));
	logIncrementSkillCall();
	findAdvanceSkillOffset();
	installPerkEntryHook();
	installXpRoutineCallHook();
	installUseSkillHook();
}
}
