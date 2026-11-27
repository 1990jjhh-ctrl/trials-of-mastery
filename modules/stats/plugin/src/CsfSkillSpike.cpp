// THROWAWAY prototype of spike S7 (docs/stats/PLAN.md, Phase 3). It only
// answers how the module learns about skill level-ups, including those of
// Custom Skills Framework (CSF) skills and those above 100, and where the XP
// of a CSF skill use can be multiplied; G4 and G6 replace it.
//
// How CSF 3.2.0 levels its skills (its MIT source, tag v3.2.0, and a
// disassembly of CustomSkills.dll with its PDB; RVAs are of that build):
//
// - A CSF skill is not an engine skill. Its level and progress are two
//   TESGlobals, and all its XP goes through Skill::Advance(magnitude, object,
//   isSkillUse, hideNotification) (RVA 0x4CAE0), never through the game's
//   skill XP routine (41561).
// - Advance takes XP = magnitude * useMult + useOffset for a use (a call of
//   fmaf at +0xB4, skipped when isSkillUse is false), sets the player's
//   advanceSkill to none, runs the perk entry point Mod Skill Use
//   (BGSEntryPoint::HandleEntryPoint, 23526) on the XP (an indirect call at
//   +0x15B through CSF's own pointer to 23526), then runs its level loop and
//   writes both globals. If the level rose, it sends its SkillIncreaseEvent
//   (only the skill's ID) and the Papyrus event OnCustomSkillIncrease.
// - Uses reach Advance with isSkillUse = true from four callers: the SKSE
//   interface's AdvanceSkill, Papyrus CustomSkills.AdvanceSkill, CSF's
//   UseSkill vtable hook (objects with a CustomSkillAdvance_<id> keyword) and
//   workbenches. Whole levels (trainers, skill books, IncrementSkill) go
//   through Skill::Increment (RVA 0x4D1D0), which calls Advance once per level
//   with isSkillUse = false.
// - CSF publishes an SKSE interface at its kDataLoaded, as a message of type
//   0 from the sender "CustomSkills". It holds AdvanceSkill, IncrementSkill
//   and the event source of the skill increase event.
//
// The candidates, from early to late: CSF's calls of Advance
// (csf_advance_call, the magnitude, before useMult) and its Mod Skill Use
// call (csf_perk_entry, the XP after the perks). The vanilla perk entry of S3
// (perk_entry) stays in, so both paths can be compared in one run.

#include "CsfSkillSpike.h"

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
#include <limits>
#include <map>
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
constexpr std::string_view kConfigPath = "Data/SKSE/Plugins/trials-of-mastery-stats-s7.json";

// --- Vanilla (Address Library IDs are AE; how they were found: S3 result) ---

// PlayerCharacter::UseSkill, reached through slot 0xF7 of the PlayerCharacter
// vtable. Only the slot's holder is logged here (CSF hooks it).
constexpr REL::ID     kUseSkill{ 40488 };
constexpr REL::ID     kPlayerCharacterVtable{ 208040 };
constexpr std::size_t kUseSkillSlot = 0xF7;
// The skill XP routine and PlayerSkills::IncrementSkill.
constexpr REL::ID kSkillXpRoutine{ 41561 };
constexpr REL::ID kIncrementSkill{ 41562 };
// BGSEntryPoint::HandleEntryPoint(entry point, perk owner, ...).
constexpr REL::ID kHandleEntryPoint{ 23526 };

constexpr std::size_t kUseSkillScanLength = 0x40;
constexpr std::size_t kSkillXpRoutineScanLength = 0x400;
constexpr std::size_t kIncrementSkillScanLength = 0x100;

constexpr std::uint8_t kCallOpcode = 0xE8;
constexpr std::size_t  kCallLength = 5;

// mov byte ptr [rsp + 0x28], 1 (isUse); mov dword ptr [rsp + 0x20], eax:
// right before UseSkill's call of the routine.
constexpr std::array<std::uint8_t, 9> kUseSkillCallPrefix{ 0xC6, 0x44, 0x24, 0x28, 0x01, 0x89, 0x44, 0x24, 0x20 };
// mov dword ptr [rsp + 0x20], r13d: right before IncrementSkill's call.
constexpr std::array<std::uint8_t, 5> kIncrementSkillCallPrefix{ 0x44, 0x89, 0x6C, 0x24, 0x20 };
constexpr auto                        kModSkillUse = static_cast<std::uint8_t>(RE::BGSEntryPoint::ENTRY_POINT::kModSkillUse);
// mov ecx, 0x16 (Mod Skill Use): right before the routine's entry point call.
constexpr std::array<std::uint8_t, 5> kModSkillUsePrefix{ 0xB9, kModSkillUse, 0x00, 0x00, 0x00 };
// mov rax, [rip + player]; mov [rax + disp32], esi: the routine stores the
// skill in the player's advanceSkill. disp32 is taken from the code.
constexpr std::array<std::uint8_t, 3> kLoadPlayerOpcode{ 0x48, 0x8B, 0x05 };
constexpr std::array<std::uint8_t, 2> kStoreSkillOpcode{ 0x89, 0xB0 };
// jmp [rip + 0]; dq target: what an SKSE trampoline entry holds.
constexpr std::array<std::uint8_t, 6> kTrampolineJump{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };

// --- CSF ---------------------------------------------------------------------

constexpr const wchar_t* kCsfModule = L"CustomSkills.dll";
constexpr const char*    kCsfSender = "CustomSkills";

// The interface and its event, as CSF's public headers define them
// (include/CustomSkills/Stubs.h and Events.h, MIT): message type 0
// (kCustomSkillsInterface) carries a pointer to the interface, and dispatcher
// 0 (kSkillIncreaseEvent) is the event source of CsfSkillIncreaseEvent.
constexpr std::uint32_t kCsfInterfaceMessage = 0;
constexpr std::uint32_t kCsfSkillIncreaseDispatcher = 0;
constexpr std::uint32_t kCsfInterfaceVersion = 1;

struct CsfInterface
{
	std::uint32_t interfaceVersion;
	void (*showStatsMenu)(const char* skillId);
	void (*advanceSkill)(const char* skillId, float magnitude);
	void (*incrementSkill)(const char* skillId, std::uint32_t count);
	void* (*getEventDispatcher)(std::uint32_t dispatcherId);
};
static_assert(sizeof(CsfInterface) == 0x28);

struct CsfSkillIncreaseEvent
{
	const char* skillId;
};
static_assert(sizeof(CsfSkillIncreaseEvent) == 0x8);

// mov ecx, 0x16 (Mod Skill Use); call [rip + disp32]: Advance's perk entry
// call through CSF's pointer to HandleEntryPoint. Unique in 3.2.0.
constexpr std::array<std::uint8_t, 7> kCsfPerkEntryBytes{ 0xB9, kModSkillUse, 0x00, 0x00, 0x00, 0xFF, 0x15 };
constexpr std::size_t                 kIndirectCallLength = 6;
// mov r14, rcx in Advance's prologue and mov rax, [r14 + 0x40] (the level
// global) right after the perk entry call: r14 holds `this` at the call.
constexpr std::array<std::uint8_t, 3> kMoveSelfToR14{ 0x4C, 0x8B, 0xF1 };
constexpr std::array<std::uint8_t, 4> kLoadLevelFromR14{ 0x49, 0x8B, 0x46, 0x40 };
constexpr std::size_t                 kSelfCheckLength = 0x20;
// The stub between the perk entry call and csfPerkEntry passes `this` as the
// fourth argument: mov r9, r14 (or xor r9d, r9d when `this` is not known),
// then jmp [rip + 0] to the hook. r9 is free: HandleEntryPoint takes one
// argument after the owner for Mod Skill Use.
constexpr std::array<std::uint8_t, 3> kStubPassSelf{ 0x4D, 0x8B, 0xCE };
constexpr std::array<std::uint8_t, 3> kStubPassNone{ 0x45, 0x33, 0xC9 };
// What a caller of Advance does to its isSkillUse argument within the 0x20
// bytes before the call: mov r9b, 1, or xor r9d, r9d.
constexpr std::array<std::uint8_t, 3> kSetIsUse{ 0x41, 0xB1, 0x01 };
constexpr std::array<std::uint8_t, 3> kClearIsUse{ 0x45, 0x33, 0xC9 };
constexpr std::size_t                 kCallerLookBack = 0x20;
// test r9b, r9b; je rel8: Advance skips the use XP (fmaf) when it is not a
// use. Beyond Mastery replaces that call with bScaleXPGainAbove100.
constexpr std::array<std::uint8_t, 4> kTestIsUse{ 0x45, 0x84, 0xC9, 0x74 };
// cmp esi, 100: Advance's two level limit checks in 3.2.0. Beyond Mastery
// replaces both when it raises a custom skill's cap.
constexpr std::array<std::uint8_t, 3> kCompareLevel100{ 0x83, 0xFE, 0x64 };
// From CustomSkills.pdb of 3.2.0. Only logged, to say whether the scans found
// the expected functions; nothing is patched by these.
constexpr std::uint32_t kAdvanceRva320 = 0x4CAE0;
constexpr std::uint32_t kIncrementRva320 = 0x4D1D0;
constexpr std::uint32_t kPerkEntryOffset320 = 0x15B;
// Holds the stub, one 8-byte pointer and one 14-byte jump; SKSE's trampoline
// may be out of rel32 reach of CustomSkills.dll.
constexpr std::size_t kCsfTrampolineSize = 0x100;

// The members of CSF's Skill (src/CustomSkills/Skill.h) that the spike
// reads. The offsets are those of the code in 3.2.0.
constexpr std::size_t kSkillIdOffset = 0x00;
constexpr std::size_t kSkillLevelOffset = 0x40;
constexpr std::size_t kSkillRatioOffset = 0x48;
constexpr std::size_t kSkillInfoOffset = 0x68;
// Skill::ID is an MSVC std::string: the characters in place up to 15, else a
// pointer at +0, the size at +0x10, the capacity at +0x18.
constexpr std::size_t kStringSizeOffset = 0x10;
constexpr std::size_t kStringCapacityOffset = 0x18;
constexpr std::size_t kStringInPlaceCapacity = 15;

constexpr float kSkillLevel100 = 100.0F;

// --- Config ------------------------------------------------------------------

enum class Hook
{
	kPerkEntry,
	kCsfAdvanceCall,
	kCsfPerkEntry,
};

struct HookName
{
	Hook             hook;
	std::string_view name;
};

constexpr std::array kHookNames{
	HookName{ Hook::kPerkEntry, "perk_entry" },
	HookName{ Hook::kCsfAdvanceCall, "csf_advance_call" },
	HookName{ Hook::kCsfPerkEntry, "csf_perk_entry" },
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

// A call of CSF's interface, run when a menu closes (see commandId).
struct CsfCommand
{
	std::string                  skill;
	std::optional<float>         advance;
	std::optional<std::uint32_t> increment;
};

// The JSON file's fields of the same name.
struct SpikeConfig
{
	double factor = 1.0;
	// Where the factor applies; "hook" in the file, a name or a list of names.
	std::vector<Hook> hooks{ Hook::kPerkEntry, Hook::kCsfPerkEntry };
	// perk_entry and csf_perk_entry only: also multiply whole-level grants.
	bool applyToWholeLevel = false;
	// The commands run once whenever this changes, when a menu next closes.
	std::int64_t            commandId = 0;
	std::vector<CsfCommand> commands;

	[[nodiscard]] bool applies(Hook hook) const
	{
		return std::find(hooks.begin(), hooks.end(), hook) != hooks.end();
	}
};

std::string describe(const SpikeConfig& config)
{
	std::string hooks;
	for (const auto hook : config.hooks) {
		hooks += std::format("{}{}", hooks.empty() ? "" : ", ", hookName(hook));
	}
	return std::format("factor {} at [{}], applyToWholeLevel {}, commandId {} with {} command(s)", config.factor,
		hooks, config.applyToWholeLevel, config.commandId, config.commands.size());
}

std::optional<std::vector<Hook>> parseHooks(const nlohmann::json& value)
{
	std::vector<std::string> names;
	if (value.is_string()) {
		names.push_back(value.get<std::string>());
	} else if (value.is_array()) {
		names = value.get<std::vector<std::string>>();
	} else {
		return std::nullopt;
	}
	std::vector<Hook> hooks;
	for (const auto& name : names) {
		if (name == "none") {
			continue;
		}
		const auto hook = parseHook(name);
		if (!hook) {
			SKSE::log::error("S7: unknown hook \"{}\" in {}", name, kConfigPath);
			return std::nullopt;
		}
		hooks.push_back(*hook);
	}
	return hooks;
}

CsfCommand parseCommand(const nlohmann::json& entry)
{
	CsfCommand command{ .skill = entry.at("skill").get<std::string>(), .advance = {}, .increment = {} };
	if (entry.contains("advance")) {
		command.advance = entry.at("advance").get<float>();
	}
	if (entry.contains("increment")) {
		command.increment = entry.at("increment").get<std::uint32_t>();
	}
	return command;
}

// Fills config from the parsed file. False if "hook" is invalid.
bool fillConfig(const nlohmann::json& file, SpikeConfig& config)
{
	config.factor = file.value("factor", config.factor);
	config.applyToWholeLevel = file.value("applyToWholeLevel", config.applyToWholeLevel);
	config.commandId = file.value("commandId", config.commandId);
	if (file.contains("hook")) {
		const auto hooks = parseHooks(file.at("hook"));
		if (!hooks) {
			SKSE::log::error("S7: \"hook\" in {} is not a hook name or a list of them; using the defaults",
				kConfigPath);
			return false;
		}
		config.hooks = *hooks;
	}
	for (const auto& entry : file.value("commands", nlohmann::json::array())) {
		config.commands.push_back(parseCommand(entry));
	}
	return true;
}

SpikeConfig readConfig()
{
	std::ifstream stream{ std::filesystem::path(kConfigPath) };
	if (!stream) {
		SKSE::log::warn("S7: {} cannot be opened; using the defaults", kConfigPath);
		return {};
	}
	SpikeConfig config;
	try {
		if (!fillConfig(nlohmann::json::parse(stream), config)) {
			return {};
		}
	} catch (const nlohmann::json::exception& error) {
		SKSE::log::error("S7: {} is not valid ({}); using the defaults", kConfigPath, error.what());
		return {};
	}
	if (!std::isfinite(config.factor) || config.factor < 0.0) {
		SKSE::log::error("S7: factor {} in {} is not a finite value of 0 or more; using the defaults", config.factor,
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
		SKSE::log::warn("S7: {} not found (game folder {}); using the defaults", kConfigPath,
			std::filesystem::current_path().string());
		config_ = {};
	} else {
		config_ = readConfig();
	}
	SKSE::log::info("S7 config: {}", describe(config_));
	return config_;
}

ConfigCache g_config;

std::string factorMark(const SpikeConfig& config, Hook at, bool applied)
{
	if (!config.applies(at)) {
		return "";
	}
	if (!applied) {
		return " [factor not applied: no use]";
	}
	return std::format(" [factor x{} applied here]", config.factor);
}

// --- Common helpers ----------------------------------------------------------

std::atomic<std::uint32_t> g_mainThread{ 0 };

// The hooks run inside the game's computation, so G1's thread rule
// (assumption 15) needs to know where.
std::string threadText()
{
	const auto id = REX::W32::GetCurrentThreadId();
	return std::format("thread {}{}", id, id == g_mainThread ? " (main)" : "");
}

std::string aboveText(float level)
{
	return level > kSkillLevel100 ? "above 100" : "not above 100";
}

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

bool isCall(std::uintptr_t at)
{
	return *reinterpret_cast<const std::uint8_t*>(at) == kCallOpcode;
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

bool contains(std::span<const std::uint8_t> code, std::span<const std::uint8_t> pattern)
{
	return !findAll(code, pattern).empty();
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
	if (at + kCallLength > begin + length || !isCall(at)) {
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

// --- Vanilla skills ------------------------------------------------------------

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

// The level of each vanilla skill when the routine last ran its perk entry,
// before the XP was added, so a level-up event can name the old level.
std::mutex                                                          g_vanillaLevelsMutex;
std::array<std::optional<float>, PlayerSkills::Data::Skill::kTotal> g_vanillaLevels;

void rememberVanillaLevel(RE::ActorValue skill, float level)
{
	const auto index = skillIndex(skill);
	if (!index) {
		return;
	}
	const std::scoped_lock lock{ g_vanillaLevelsMutex };
	g_vanillaLevels[*index] = level;
}

std::optional<float> exchangeVanillaLevel(RE::ActorValue skill, float level)
{
	const auto index = skillIndex(skill);
	if (!index) {
		return std::nullopt;
	}
	const std::scoped_lock lock{ g_vanillaLevelsMutex };
	const auto             previous = g_vanillaLevels[*index];
	g_vanillaLevels[*index] = level;
	return previous;
}

std::string levelText(std::optional<float> level)
{
	return level ? std::format("{:.0f}", *level) : std::string{ "unknown" };
}

// Set while the routine runs from UseSkill's call, so the perk entry hook can
// tell a use from a whole-level grant. The count tells whether the routine ran
// its perk entry at all (Experience skips the routine at its cap).
thread_local bool            t_insideUseCall = false;
thread_local std::uint64_t   t_vanillaPerkEntries = 0;
std::atomic<std::uint64_t>   g_vanillaUseCount{ 0 };
thread_local std::uint64_t   t_vanillaUse = 0;
std::optional<std::uint32_t> g_advanceSkillOffset;
bool                         g_xpRoutineCallHooked = false;

// The skill the routine stored in the player's advanceSkill for the perk
// conditions, read at the offset taken from the routine's code.
std::optional<RE::ActorValue> advanceSkill(const RE::Actor* owner)
{
	if (!g_advanceSkillOffset || owner == nullptr) {
		return std::nullopt;
	}
	std::uint32_t skill = 0;
	std::memcpy(&skill, reinterpret_cast<const std::byte*>(owner) + *g_advanceSkillOffset, sizeof(skill));
	return static_cast<RE::ActorValue>(skill);
}

std::string_view vanillaKind(bool insideUseCall)
{
	if (!g_xpRoutineCallHooked) {
		return "use or whole-level grant";
	}
	return insideUseCall ? "use" : "whole-level grant";
}

// UseSkill's call of the skill XP routine (S3's xp_routine_call). Only sets
// the use flag and logs; no factor applies here.
using SkillXpRoutine = void (*)(PlayerSkills*, RE::ActorValue, float, RE::TESForm*, std::uint32_t, bool, bool);
SkillXpRoutine g_xpRoutineTarget = nullptr;
std::string    g_xpRoutineTargetText;

void xpRoutineCall(PlayerSkills* skills, RE::ActorValue skill, float points, RE::TESForm* object,
	std::uint32_t action, bool isUse, bool noNotify)
{
	const auto use = ++g_vanillaUseCount;
	const auto outerUse = t_vanillaUse;
	t_vanillaUse = use;
	SKSE::log::info("S7 vanilla #{} xp_routine_call: {} {:.3f} points, isUse {}, {}; into {}; {}", use,
		skillName(skill), points, isUse, skillText(skillData(skill)), g_xpRoutineTargetText, threadText());
	const bool outer = t_insideUseCall;
	const auto perkEntriesBefore = t_vanillaPerkEntries;
	t_insideUseCall = true;
	g_xpRoutineTarget(skills, skill, points, object, action, isUse, noNotify);
	t_insideUseCall = outer;
	const auto ran = t_vanillaPerkEntries != perkEntriesBefore;
	SKSE::log::info("S7 vanilla #{} done: {}{}", use, skillText(skillData(skill)),
		ran ? "" : "; the routine ran no perk entry (Experience's or Beyond Mastery's cap?)");
	t_vanillaUse = outerUse;
}

// The routine's Mod Skill Use entry point call (S3's hook 3, perk_entry).
using HandleEntryPoint = void (*)(std::uint32_t, RE::Actor*, float*);
HandleEntryPoint g_handleEntryPoint = nullptr;

void perkEntry(std::uint32_t entryPoint, RE::Actor* owner, float* xp)
{
	++t_vanillaPerkEntries;
	const auto skill = advanceSkill(owner);
	const auto before = skill ? skillData(*skill) : std::nullopt;
	if (skill && before) {
		rememberVanillaLevel(*skill, before->level);
	}
	const float beforePerks = *xp;
	g_handleEntryPoint(entryPoint, owner, xp);
	const float afterPerks = *xp;
	const auto  config = g_config.current();
	const bool  isUse = t_insideUseCall;
	// Without the xp_routine_call hook a use cannot be told apart, so the
	// factor applies to everything.
	const bool applies = config.applies(Hook::kPerkEntry) &&
	                     (isUse || !g_xpRoutineCallHooked || config.applyToWholeLevel);
	if (applies) {
		*xp = static_cast<float>(afterPerks * config.factor);
	}
	const auto multiplier = beforePerks != 0.0F ? std::format("x{:.4f}", afterPerks / beforePerks) : "n/a";
	SKSE::log::info(
		"S7 vanilla #{} perk_entry: {} ({}), {}: {:.3f} XP before the entry point, {:.3f} after (perk multiplier "
		"{}), {:.3f} added{}; {}",
		t_vanillaUse, skill ? skillName(*skill) : "skill unknown", vanillaKind(isUse), skillText(before),
		beforePerks, afterPerks, multiplier, *xp, factorMark(config, Hook::kPerkEntry, applies), threadText());
}

// --- CSF skills ------------------------------------------------------------------

// A CSF Skill object, read through the offsets above.
using CsfSkill = std::byte;

template <class T>
T* pointerAt(const CsfSkill* skill, std::size_t offset)
{
	T* value = nullptr;
	std::memcpy(&value, skill + offset, sizeof(value));
	return value;
}

std::string csfSkillId(const CsfSkill* skill)
{
	if (skill == nullptr) {
		return "skill unknown";
	}
	std::size_t size = 0;
	std::size_t capacity = 0;
	std::memcpy(&size, skill + kSkillIdOffset + kStringSizeOffset, sizeof(size));
	std::memcpy(&capacity, skill + kSkillIdOffset + kStringCapacityOffset, sizeof(capacity));
	const auto* text = capacity > kStringInPlaceCapacity ? pointerAt<const char>(skill, kSkillIdOffset) :
	                                                       reinterpret_cast<const char*>(skill + kSkillIdOffset);
	return { text, size };
}

std::optional<float> csfGlobal(const CsfSkill* skill, std::size_t offset)
{
	const auto* global = skill != nullptr ? pointerAt<RE::TESGlobal>(skill, offset) : nullptr;
	if (global == nullptr) {
		return std::nullopt;
	}
	return global->value;
}

std::string csfSkillText(const CsfSkill* skill)
{
	if (skill == nullptr) {
		return "skill unknown";
	}
	const auto* info = pointerAt<RE::ActorValueInfo>(skill, kSkillInfoOffset);
	const auto* name = info != nullptr ? info->GetFullName() : nullptr;
	const auto* use = info != nullptr ? info->skill : nullptr;
	const auto  ratio = csfGlobal(skill, kSkillRatioOffset);
	return std::format("{} \"{}\" level {}, progress {}{}", csfSkillId(skill), name != nullptr ? name : "",
		levelText(csfGlobal(skill, kSkillLevelOffset)), ratio ? std::format("{:.4f}", *ratio) : "unknown",
		use != nullptr ? std::format(", useMult {:.3f} useOffset {:.3f}", use->useMult, use->offsetMult) : "");
}

// Every CSF skill seen by a hook, by ID, so the skill increase event, which
// carries only the ID, can read the level.
std::mutex                                g_csfSkillsMutex;
std::map<std::string, const CsfSkill*>    g_csfSkills;
std::map<std::string, float, std::less<>> g_csfLevels;

void rememberCsfSkill(const CsfSkill* skill)
{
	if (skill == nullptr) {
		return;
	}
	const auto             id = csfSkillId(skill);
	const auto             level = csfGlobal(skill, kSkillLevelOffset);
	const std::scoped_lock lock{ g_csfSkillsMutex };
	g_csfSkills[id] = skill;
	if (level) {
		g_csfLevels[id] = *level;
	}
}

// One call of Advance through a hooked caller.
struct CsfAdvance
{
	std::uint64_t   number;
	const CsfSkill* skill;
	bool            isUse;
};

std::atomic<std::uint64_t> g_csfAdvanceCount{ 0 };
thread_local CsfAdvance*   t_csfAdvance = nullptr;
bool                       g_csfAdvanceCallsHooked = false;

std::string_view csfKind(const CsfAdvance* advance)
{
	if (advance == nullptr) {
		return "use or whole-level grant";
	}
	return advance->isUse ? "use" : "whole-level grant";
}

// The calls of Skill::Advance (csf_advance_call). Its magnitude comes before
// useMult and useOffset, so a factor here is purely multiplicative only for a
// skill whose useOffset is 0.
using CsfAdvanceFunction = void (*)(CsfSkill*, float, RE::TESForm*, bool, bool);
CsfAdvanceFunction g_csfAdvanceFunction = nullptr;

void csfAdvanceCall(CsfSkill* skill, float magnitude, RE::TESForm* object, bool isUse, bool hideNotification)
{
	const auto config = g_config.current();
	const bool applies = config.applies(Hook::kCsfAdvanceCall) && (isUse || config.applyToWholeLevel);
	const auto applied = applies ? static_cast<float>(magnitude * config.factor) : magnitude;
	CsfAdvance advance{ .number = ++g_csfAdvanceCount, .skill = skill, .isUse = isUse };
	rememberCsfSkill(skill);
	const auto levelBefore = csfGlobal(skill, kSkillLevelOffset);
	SKSE::log::info("S7 csf #{} csf_advance_call: {} ({}), magnitude {:.3f} -> {:.3f}{}, hideNotification {}; {}",
		advance.number, csfSkillText(skill), csfKind(&advance), magnitude, applied,
		factorMark(config, Hook::kCsfAdvanceCall, applies), hideNotification, threadText());
	auto* const outer = t_csfAdvance;
	t_csfAdvance = &advance;
	g_csfAdvanceFunction(skill, applied, object, isUse, hideNotification);
	t_csfAdvance = outer;
	const auto levelAfter = csfGlobal(skill, kSkillLevelOffset);
	rememberCsfSkill(skill);
	const auto rose = levelBefore && levelAfter && *levelAfter > *levelBefore;
	SKSE::log::info("S7 csf #{} done: {}{}", advance.number, csfSkillText(skill),
		rose ? std::format("; level {:.0f} -> {:.0f} ({})", *levelBefore, *levelAfter, aboveText(*levelAfter)) : "");
}

// Advance's Mod Skill Use call (csf_perk_entry), reached through the stub,
// which passes the Skill in r9.
HandleEntryPoint g_csfHandleEntryPoint = nullptr;

void csfPerkEntry(std::uint32_t entryPoint, RE::Actor* owner, float* xp, const CsfSkill* skill)
{
	const float beforePerks = *xp;
	g_csfHandleEntryPoint(entryPoint, owner, xp);
	const float afterPerks = *xp;
	const auto  config = g_config.current();
	const auto* advance = t_csfAdvance;
	const bool  isUse = advance != nullptr && advance->isUse;
	// Without the csf_advance_call hooks a use cannot be told apart, so the
	// factor applies to everything.
	const bool applies = config.applies(Hook::kCsfPerkEntry) &&
	                     (isUse || !g_csfAdvanceCallsHooked || config.applyToWholeLevel);
	if (applies) {
		*xp = static_cast<float>(afterPerks * config.factor);
	}
	const auto multiplier = beforePerks != 0.0F ? std::format("x{:.4f}", afterPerks / beforePerks) : "n/a";
	SKSE::log::info(
		"S7 csf #{} csf_perk_entry: {} ({}): {:.3f} XP before the entry point, {:.3f} after (perk multiplier {}), "
		"{:.3f} added{}; {}",
		advance != nullptr ? advance->number : 0, csfSkillText(skill), csfKind(advance), beforePerks, afterPerks,
		multiplier, *xp, factorMark(config, Hook::kCsfPerkEntry, applies), threadText());
}

// --- Level-up events ---------------------------------------------------------

std::atomic<CsfInterface*> g_csf{ nullptr };

// The CSF skill's level now and before, from the Skill a hook saw.
std::string csfLevelUpText(std::string_view id)
{
	const std::scoped_lock lock{ g_csfSkillsMutex };
	const auto             skill = g_csfSkills.find(std::string{ id });
	if (skill == g_csfSkills.end()) {
		return "level unknown (no hook saw this skill)";
	}
	const auto level = csfGlobal(skill->second, kSkillLevelOffset);
	const auto known = g_csfLevels.find(id);
	const auto before = known != g_csfLevels.end() ? std::optional{ known->second } : std::nullopt;
	if (level) {
		g_csfLevels[std::string{ id }] = *level;
	}
	return std::format("level {} -> {} ({})", levelText(before), levelText(level),
		level ? aboveText(*level) : "unknown");
}

class LevelUpSinks final :
    public RE::BSTEventSink<RE::SkillIncrease::Event>,
    public RE::BSTEventSink<RE::TESTrackedStatsEvent>,
    public RE::BSTEventSink<CsfSkillIncreaseEvent>,
    public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	RE::BSEventNotifyControl ProcessEvent(const RE::SkillIncrease::Event* event,
		RE::BSTEventSource<RE::SkillIncrease::Event>*                     source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::TESTrackedStatsEvent* event,
		RE::BSTEventSource<RE::TESTrackedStatsEvent>*                     source) override;
	RE::BSEventNotifyControl ProcessEvent(const CsfSkillIncreaseEvent* event,
		RE::BSTEventSource<CsfSkillIncreaseEvent>*                     source) override;
	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*                     source) override;
};

RE::BSEventNotifyControl LevelUpSinks::ProcessEvent(const RE::SkillIncrease::Event* event,
	RE::BSTEventSource<RE::SkillIncrease::Event>*)
{
	if (event == nullptr) {
		return RE::BSEventNotifyControl::kContinue;
	}
	const auto skill = event->actorValue;
	const auto data = skillData(skill);
	const auto base = RE::PlayerCharacter::GetSingleton()->AsActorValueOwner()->GetBaseActorValue(skill);
	const auto before = data ? exchangeVanillaLevel(skill, data->level) : std::nullopt;
	SKSE::log::info(
		"S7 level-up via SkillIncrease::Event: {}, level {} -> {} (base AV {:.0f}, {}), inside vanilla #{}; {}",
		skillName(skill), levelText(before), data ? levelText(data->level) : "unknown", base, aboveText(base),
		t_vanillaUse, threadText());
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl LevelUpSinks::ProcessEvent(const RE::TESTrackedStatsEvent* event,
	RE::BSTEventSource<RE::TESTrackedStatsEvent>*)
{
	if (event == nullptr || std::string_view{ event->stat.c_str() } != "Skill Increases") {
		return RE::BSEventNotifyControl::kContinue;
	}
	SKSE::log::info("S7 level-up via TESTrackedStatsEvent: \"Skill Increases\" = {}, inside vanilla #{}, csf #{}; {}",
		event->value, t_vanillaUse, t_csfAdvance != nullptr ? t_csfAdvance->number : 0, threadText());
	return RE::BSEventNotifyControl::kContinue;
}

RE::BSEventNotifyControl LevelUpSinks::ProcessEvent(const CsfSkillIncreaseEvent* event,
	RE::BSTEventSource<CsfSkillIncreaseEvent>*)
{
	if (event == nullptr || event->skillId == nullptr) {
		return RE::BSEventNotifyControl::kContinue;
	}
	const std::string_view id{ event->skillId };
	SKSE::log::info("S7 level-up via CSF SkillIncreaseEvent: {}, {}, inside csf #{}; {}", id, csfLevelUpText(id),
		t_csfAdvance != nullptr ? t_csfAdvance->number : 0, threadText());
	return RE::BSEventNotifyControl::kContinue;
}

// The commandId last run; the file's commands run when it changes.
std::int64_t g_lastCommandId = 0;

void runCsfCommand(const CsfCommand& command)
{
	auto* const csf = g_csf.load();
	if (csf == nullptr) {
		SKSE::log::error("S7 command for {}: CSF's interface was never received; not run", command.skill);
		return;
	}
	if (command.advance) {
		SKSE::log::info("S7 command: AdvanceSkill(\"{}\", {}) through CSF's interface; {}", command.skill,
			*command.advance, threadText());
		csf->advanceSkill(command.skill.c_str(), *command.advance);
	}
	if (command.increment) {
		SKSE::log::info("S7 command: IncrementSkill(\"{}\", {}) through CSF's interface; {}", command.skill,
			*command.increment, threadText());
		csf->incrementSkill(command.skill.c_str(), *command.increment);
	}
}

// Runs the file's commands if commandId changed. Any menu closing is the
// trigger: the bridge runs console commands without opening the console, but
// it can open and close a menu (Tab opens the TweenMenu).
RE::BSEventNotifyControl LevelUpSinks::ProcessEvent(const RE::MenuOpenCloseEvent* event,
	RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (event == nullptr || event->opening) {
		return RE::BSEventNotifyControl::kContinue;
	}
	const auto config = g_config.current();
	if (config.commandId == g_lastCommandId) {
		return RE::BSEventNotifyControl::kContinue;
	}
	g_lastCommandId = config.commandId;
	SKSE::log::info("S7: {} closed, running the {} command(s) of commandId {} on the next task",
		event->menuName.c_str(), config.commands.size(), config.commandId);
	SKSE::GetTaskInterface()->AddTask([commands = config.commands] {
		for (const auto& command : commands) {
			runCsfCommand(command);
		}
	});
	return RE::BSEventNotifyControl::kContinue;
}

LevelUpSinks g_sinks;

void onCsfMessage(SKSE::MessagingInterface::Message* message)
{
	SKSE::log::info("S7: message {} from {} ({} bytes)", message->type, message->sender, message->dataLen);
	if (message->type != kCsfInterfaceMessage || message->data == nullptr) {
		return;
	}
	auto* const csf = static_cast<CsfInterface*>(message->data);
	g_csf = csf;
	SKSE::log::info("S7: CSF's interface received, version {} (this spike knows {}); {}", csf->interfaceVersion,
		kCsfInterfaceVersion, threadText());
	auto* const source =
		static_cast<RE::BSTEventSource<CsfSkillIncreaseEvent>*>(csf->getEventDispatcher(kCsfSkillIncreaseDispatcher));
	if (source == nullptr) {
		SKSE::log::error("S7: CSF has no skill increase event source");
		return;
	}
	source->AddEventSink(&g_sinks);
	SKSE::log::info("S7: registered for CSF's SkillIncreaseEvent ({:#x}, {})", reinterpret_cast<std::uintptr_t>(source),
		moduleText(reinterpret_cast<std::uintptr_t>(source)));
}

// --- Install: vanilla --------------------------------------------------------

void logUseSkillSlot()
{
	REL::Relocation<std::uintptr_t> vtable{ kPlayerCharacterVtable };
	const auto                      slot = vtable.address() + sizeof(void*) * kUseSkillSlot;
	const auto                      holder = *reinterpret_cast<const std::uintptr_t*>(slot);
	SKSE::log::info("S7: PlayerCharacter vtable slot {:#x} (UseSkill) holds {}; not hooked", kUseSkillSlot,
		targetText(holder, kUseSkill.address()));
}

void installXpRoutineCallHook()
{
	const auto base = kUseSkill.address();
	const auto call = findCallAfter(base, kUseSkillScanLength, kUseSkillCallPrefix);
	if (!call) {
		SKSE::log::error("S7 xp_routine_call: the routine's call in UseSkill ({:#x}) was not found; not hooked", base);
		return;
	}
	const auto target = callTarget(*call);
	g_xpRoutineTarget = reinterpret_cast<SkillXpRoutine>(target);
	g_xpRoutineTargetText = targetText(target, kSkillXpRoutine.address());
	SKSE::GetTrampoline().write_call<kCallLength>(*call, &xpRoutineCall);
	g_xpRoutineCallHooked = true;
	SKSE::log::info("S7 xp_routine_call: hooked at {} + {:#x} ({:#x}); the call went to {}", kUseSkill.id(),
		*call - base, *call, g_xpRoutineTargetText);
}

// Only logs where IncrementSkill's call of the routine goes; not hooked.
void logIncrementSkillCall()
{
	const auto base = kIncrementSkill.address();
	const auto call = findCallAfter(base, kIncrementSkillScanLength, kIncrementSkillCallPrefix);
	if (!call) {
		SKSE::log::warn("S7: the routine's call in IncrementSkill ({:#x}) was not found", base);
		return;
	}
	SKSE::log::info("S7: IncrementSkill ({}) + {:#x} calls {}", kIncrementSkill.id(), *call - base,
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
		SKSE::log::warn("S7: the routine's store of advanceSkill was found {} times; perk_entry logs no skill",
			matches);
		return;
	}
	g_advanceSkillOffset = offset;
	SKSE::log::info("S7: the routine stores advanceSkill at player + {:#x}", *offset);
}

void installPerkEntryHook()
{
	const auto                  base = kSkillXpRoutine.address();
	const auto                  code = codeAt(base, kSkillXpRoutineScanLength);
	std::vector<std::uintptr_t> calls;
	for (const auto at : findAll(code, kModSkillUsePrefix)) {
		const auto call = base + at + kModSkillUsePrefix.size();
		if (call + kCallLength <= base + code.size() && isCall(call) && callTarget(call) == kHandleEntryPoint.address()) {
			calls.push_back(call);
		}
	}
	if (calls.size() != 1) {
		SKSE::log::error(
			"S7 perk_entry: expected one Mod Skill Use call of HandleEntryPoint ({:#x}) in the routine ({:#x}), "
			"found {}; not hooked",
			kHandleEntryPoint.address(), base, calls.size());
		return;
	}
	g_handleEntryPoint = reinterpret_cast<HandleEntryPoint>(kHandleEntryPoint.address());
	SKSE::GetTrampoline().write_call<kCallLength>(calls.front(), &perkEntry);
	SKSE::log::info("S7 perk_entry: hooked at {} + {:#x} ({:#x}), the call of HandleEntryPoint ({}, {:#x})",
		kSkillXpRoutine.id(), calls.front() - base, calls.front(), kHandleEntryPoint.id(), kHandleEntryPoint.address());
}

// --- Install: CSF ------------------------------------------------------------

// IMAGE_SCN_MEM_EXECUTE and IMAGE_DIRECTORY_ENTRY_EXCEPTION, whose names
// windows.h takes as macros.
constexpr std::uint32_t kExecutableSection = 0x20000000;
constexpr std::size_t   kExceptionDirectory = 3;

struct RuntimeFunction
{
	std::uint32_t begin;
	std::uint32_t end;
	std::uint32_t unwindInfo;
};
static_assert(sizeof(RuntimeFunction) == 0xC);

struct CodeRange
{
	std::uintptr_t begin;
	std::size_t    size;
};

// A loaded DLL's executable sections and its function table (.pdata).
struct ModuleImage
{
	std::uintptr_t                   base = 0;
	std::size_t                      size = 0;
	std::vector<CodeRange>           code;
	std::span<const RuntimeFunction> functions;
};

std::optional<ModuleImage> moduleImage(const wchar_t* name)
{
	const auto handle = REX::W32::GetModuleHandleW(name);
	if (handle == nullptr) {
		return std::nullopt;
	}
	const auto  base = reinterpret_cast<std::uintptr_t>(handle);
	const auto* dos = reinterpret_cast<const REX::W32::IMAGE_DOS_HEADER*>(base);
	const auto* nt = reinterpret_cast<const REX::W32::IMAGE_NT_HEADERS64*>(base + dos->lfanew);
	ModuleImage image{ .base = base, .size = nt->optionalHeader.imageSize, .code = {}, .functions = {} };
	// The section headers follow the optional header.
	const auto* first = reinterpret_cast<const REX::W32::IMAGE_SECTION_HEADER*>(
		reinterpret_cast<const std::byte*>(&nt->optionalHeader) + nt->fileHeader.optionalHeaderSize);
	const std::span sections{ first, nt->fileHeader.sectionCount };
	for (const auto& section : sections) {
		if ((section.characteristics & kExecutableSection) != 0) {
			image.code.push_back({ base + section.virtualAddress, section.virtualSize });
		}
	}
	const auto& exceptions = nt->optionalHeader.dataDirectory[kExceptionDirectory];
	image.functions = { reinterpret_cast<const RuntimeFunction*>(base + exceptions.virtualAddress),
		exceptions.size / sizeof(RuntimeFunction) };
	return image;
}

std::optional<RuntimeFunction> functionAt(const ModuleImage& image, std::uintptr_t address)
{
	const auto rva = address - image.base;
	for (const auto& function : image.functions) {
		if (rva >= function.begin && rva < function.end) {
			return function;
		}
	}
	return std::nullopt;
}

std::string functionText(const ModuleImage& image, std::uintptr_t address)
{
	const auto function = functionAt(image, address);
	if (!function) {
		return std::format("RVA {:#x} (no function)", address - image.base);
	}
	return std::format("RVA {:#x} = function {:#x} + {:#x}", address - image.base, function->begin,
		address - image.base - function->begin);
}

bool inRel32(std::uintptr_t from, std::uintptr_t to)
{
	const auto distance = static_cast<std::int64_t>(to) - static_cast<std::int64_t>(from);
	// In parentheses, since windows.h defines min and max as macros.
	return distance >= (std::numeric_limits<std::int32_t>::min)() &&
	       distance <= (std::numeric_limits<std::int32_t>::max)();
}

// Whether the call [rip + disp32] at call goes through a pointer in the DLL
// that holds HandleEntryPoint.
bool callsHandleEntryPoint(const ModuleImage& image, std::uintptr_t call)
{
	std::int32_t displacement = 0;
	std::memcpy(&displacement, reinterpret_cast<const void*>(call + 2), sizeof(displacement));
	const auto     slot = call + kIndirectCallLength + static_cast<std::intptr_t>(displacement);
	std::uintptr_t pointer = 0;
	if (slot < image.base || slot + sizeof(pointer) > image.base + image.size) {
		return false;
	}
	std::memcpy(&pointer, reinterpret_cast<const void*>(slot), sizeof(pointer));
	return pointer == kHandleEntryPoint.address();
}

void addCsfPerkEntryCalls(const ModuleImage& image, const CodeRange& range, std::vector<std::uintptr_t>& calls)
{
	for (const auto at : findAll(codeAt(range.begin, range.size), kCsfPerkEntryBytes)) {
		const auto call = range.begin + at + kModSkillUsePrefix.size();
		if (callsHandleEntryPoint(image, call)) {
			calls.push_back(call);
		}
	}
}

// Advance's Mod Skill Use calls: the bytes of kCsfPerkEntryBytes whose pointer
// holds HandleEntryPoint.
std::vector<std::uintptr_t> findCsfPerkEntryCalls(const ModuleImage& image)
{
	std::vector<std::uintptr_t> calls;
	for (const auto& range : image.code) {
		addCsfPerkEntryCalls(image, range, calls);
	}
	return calls;
}

// Whether r14 holds `this` at Advance's perk entry call.
bool selfInR14(const ModuleImage& image, const RuntimeFunction& advance, std::uintptr_t call)
{
	const auto begin = image.base + advance.begin;
	const auto before = findAll(codeAt(begin, call - begin), kMoveSelfToR14).size();
	const auto after = findAll(codeAt(call + kIndirectCallLength, kSelfCheckLength), kLoadLevelFromR14).size();
	return before == 1 && after == 1;
}

// Where Advance's use XP call (fmaf, skipped unless isSkillUse) goes: the
// first E8 call in the bytes that the isSkillUse test jumps over.
std::string useXpCallText(std::uintptr_t begin, std::span<const std::uint8_t> code)
{
	const auto tests = findAll(code, kTestIsUse);
	if (tests.size() != 1) {
		return std::format("not found (the isSkillUse test occurs {} times)", tests.size());
	}
	const auto skipped = static_cast<std::size_t>(code[tests.front() + kTestIsUse.size()]);
	const auto from = tests.front() + kTestIsUse.size() + 1;
	for (auto at = from; at + kCallLength <= from + skipped; ++at) {
		if (isCall(begin + at)) {
			return std::format("at + {:#x} calls {}", at, moduleText(followJumps(callTarget(begin + at))));
		}
	}
	return "has no E8 call";
}

// Logs what Beyond Mastery changes in Advance: the use XP call, and the two
// level limit checks.
void logAdvanceSites(const ModuleImage& image, const RuntimeFunction& advance, std::uintptr_t perkCall)
{
	const auto begin = image.base + advance.begin;
	SKSE::log::info("S7: Advance's use XP call (skipped unless isSkillUse) {}; not hooked",
		useXpCallText(begin, codeAt(begin, perkCall - begin)));
	const auto length = static_cast<std::size_t>(advance.end - advance.begin);
	SKSE::log::info(
		"S7: Advance has {} level limit check(s) cmp esi, 100 (2 in CSF 3.2.0; Beyond Mastery replaces them when it "
		"raises a custom skill's cap)",
		findAll(codeAt(begin, length), kCompareLevel100).size());
}

SKSE::Trampoline g_csfTrampoline{ "S7 CSF" };
std::uintptr_t   g_csfTrampolineBase = 0;

// Whether a rel32 branch or call ending at from reaches the whole trampoline.
bool trampolineInReach(std::uintptr_t from)
{
	return inRel32(from, g_csfTrampolineBase) && inRel32(from, g_csfTrampolineBase + kCsfTrampolineSize);
}

// Writes the stub and patches Advance's perk entry call to reach csfPerkEntry
// through it.
void installCsfPerkEntryHook(const ModuleImage& image, const RuntimeFunction& advance, std::uintptr_t call)
{
	if (!trampolineInReach(call + kIndirectCallLength)) {
		SKSE::log::error("S7 csf_perk_entry: the trampoline ({:#x}) is out of reach of {:#x}; not hooked",
			g_csfTrampolineBase, call);
		return;
	}
	const bool self = selfInR14(image, advance, call);
	auto*      stub = static_cast<std::uint8_t*>(
		g_csfTrampoline.allocate(kStubPassSelf.size() + kTrampolineJump.size() + sizeof(std::uintptr_t)));
	const auto  stubAddress = reinterpret_cast<std::uintptr_t>(stub);
	const auto& load = self ? kStubPassSelf : kStubPassNone;
	const auto  hook = reinterpret_cast<std::uintptr_t>(&csfPerkEntry);
	std::memcpy(stub, load.data(), load.size());
	std::memcpy(stub + load.size(), kTrampolineJump.data(), kTrampolineJump.size());
	std::memcpy(stub + load.size() + kTrampolineJump.size(), &hook, sizeof(hook));
	g_csfHandleEntryPoint = reinterpret_cast<HandleEntryPoint>(kHandleEntryPoint.address());
	g_csfTrampoline.write_call<kIndirectCallLength>(call, stubAddress);
	SKSE::log::info("S7 csf_perk_entry: hooked at {} (expected for 3.2.0: {:#x} + {:#x}), stub {:#x}, {}",
		functionText(image, call), kAdvanceRva320, kPerkEntryOffset320, stubAddress,
		self ? "the Skill passed from r14" : "the Skill not known (r14 not confirmed as `this`)");
}

bool inIncrement320(const ModuleImage& image, std::uintptr_t call)
{
	const auto function = functionAt(image, call);
	return function && function->begin == kIncrementRva320;
}

enum class CallerKind
{
	kUse,
	kWholeLevel,
	kUnknown,
};

CallerKind callerKind(std::uintptr_t call)
{
	const auto before = codeAt(call - kCallerLookBack, kCallerLookBack);
	const bool sets = contains(before, kSetIsUse);
	const bool clears = contains(before, kClearIsUse);
	if (sets == clears) {
		return CallerKind::kUnknown;
	}
	return sets ? CallerKind::kUse : CallerKind::kWholeLevel;
}

std::string_view callerKindText(CallerKind kind)
{
	switch (kind) {
	case CallerKind::kUse:
		return "use (isSkillUse = 1)";
	case CallerKind::kWholeLevel:
		return "whole-level grant (isSkillUse = 0)";
	default:
		return "unknown isSkillUse";
	}
}

void addCallsTo(const CodeRange& range, std::uintptr_t target, std::vector<std::uintptr_t>& calls)
{
	for (auto at = range.begin; at + kCallLength <= range.begin + range.size; ++at) {
		if (isCall(at) && callTarget(at) == target) {
			calls.push_back(at);
		}
	}
}

// Every E8 call to target in the DLL's code.
std::vector<std::uintptr_t> findCallsTo(const ModuleImage& image, std::uintptr_t target)
{
	std::vector<std::uintptr_t> calls;
	for (const auto& range : image.code) {
		addCallsTo(range, target, calls);
	}
	return calls;
}

// Patches every direct call of Advance whose isSkillUse is clear from the code
// to go through csfAdvanceCall. A call that is not clear is logged and left.
void installCsfAdvanceCallHooks(const ModuleImage& image, std::uintptr_t advance)
{
	g_csfAdvanceFunction = reinterpret_cast<CsfAdvanceFunction>(advance);
	int hooked = 0;
	for (const auto call : findCallsTo(image, advance)) {
		const auto kind = callerKind(call);
		if (kind == CallerKind::kUnknown || !trampolineInReach(call + kCallLength)) {
			SKSE::log::warn("S7 csf_advance_call: call at {} has {}{}; not hooked", functionText(image, call),
				callerKindText(kind), trampolineInReach(call + kCallLength) ? "" : ", out of the trampoline's reach");
			continue;
		}
		g_csfTrampoline.write_call<kCallLength>(call, &csfAdvanceCall);
		++hooked;
		SKSE::log::info("S7 csf_advance_call: hooked the call at {}, a {}{}", functionText(image, call),
			callerKindText(kind), inIncrement320(image, call) ? " (Skill::Increment in 3.2.0)" : "");
	}
	g_csfAdvanceCallsHooked = hooked > 0;
	SKSE::log::info("S7 csf_advance_call: {} call(s) of Advance hooked (5 in CSF 3.2.0: 4 uses, 1 in Increment)",
		hooked);
}

void installCsfHooks()
{
	const auto image = moduleImage(kCsfModule);
	if (!image) {
		SKSE::log::warn("S7: CustomSkills.dll is not loaded; no CSF hooks");
		return;
	}
	SKSE::log::info("S7: CustomSkills.dll at {:#x}, {:#x} bytes, {} code range(s), {} functions", image->base,
		image->size, image->code.size(), image->functions.size());
	const auto calls = findCsfPerkEntryCalls(*image);
	if (calls.size() != 1) {
		SKSE::log::error(
			"S7 csf_perk_entry: expected one Mod Skill Use call through a pointer to HandleEntryPoint in "
			"CustomSkills.dll, found {}; no CSF hooks",
			calls.size());
		return;
	}
	const auto call = calls.front();
	const auto advance = functionAt(*image, call);
	if (!advance) {
		SKSE::log::error("S7: the Mod Skill Use call at {:#x} is in no function of the table; no CSF hooks", call);
		return;
	}
	const auto advanceAddress = image->base + advance->begin;
	SKSE::log::info("S7: Skill::Advance found at RVA {:#x} ({}), {:#x} bytes", advance->begin,
		advance->begin == kAdvanceRva320 ? "as in 3.2.0" : "not where 3.2.0 has it", advance->end - advance->begin);
	logAdvanceSites(*image, *advance, call);
	// Near the end of the DLL, as SKSE does for the game, so every site in
	// the DLL is in reach.
	g_csfTrampoline.create(kCsfTrampolineSize, reinterpret_cast<void*>(image->base + image->size));
	g_csfTrampolineBase = reinterpret_cast<std::uintptr_t>(g_csfTrampoline.allocate(0));
	SKSE::log::info("S7: CSF trampoline at {:#x}, {:#x} bytes", g_csfTrampolineBase, kCsfTrampolineSize);
	installCsfPerkEntryHook(*image, *advance, call);
	installCsfAdvanceCallHooks(*image, advanceAddress);
}
}

void registerCsfSkillSpikeListener()
{
	if (!SKSE::GetMessagingInterface()->RegisterListener(kCsfSender, onCsfMessage)) {
		SKSE::log::warn("S7: could not register for messages from {} (not loaded?)", kCsfSender);
		return;
	}
	SKSE::log::info("S7: listening for messages from {}", kCsfSender);
}

void installCsfSkillSpike()
{
	g_mainThread = REX::W32::GetCurrentThreadId();
	SKSE::log::info("S7: installing on {}; UseSkill {} {:#x}, skill XP routine {} {:#x}, HandleEntryPoint {} {:#x}",
		threadText(), kUseSkill.id(), kUseSkill.address(), kSkillXpRoutine.id(), kSkillXpRoutine.address(),
		kHandleEntryPoint.id(), kHandleEntryPoint.address());
	SKSE::log::info("S7 config at install: {}", describe(g_config.current()));
	RE::SkillIncrease::GetEventSource()->AddEventSink(&g_sinks);
	RE::ScriptEventSourceHolder::GetSingleton()->AddEventSink<RE::TESTrackedStatsEvent>(&g_sinks);
	RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&g_sinks);
	SKSE::log::info(
		"S7: registered for SkillIncrease::Event, TESTrackedStatsEvent and menus closing; CSF's "
		"interface {}",
		g_csf.load() != nullptr ? "already received" : "not received yet");
	logUseSkillSlot();
	logIncrementSkillCall();
	findAdvanceSkillOffset();
	installPerkEntryHook();
	installXpRoutineCallHook();
	installCsfHooks();
}
}
