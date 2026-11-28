// THROWAWAY prototype of spike S6b (docs/stats/PLAN.md, Phase 3). It only
// answers where the game computes the result of each default check of ST-15
// from the skill, so that G10a to G10c can replace the input or the result;
// they replace it.
//
// Where the checks are computed (disassembly of SkyrimSE.exe 1.7.104; Address
// Library IDs are AE). Each formula also runs its own perk entry point
// (BGSEntryPoint::HandleEntryPoint, 23526):
//
// - Lockpicking: the lockpicking menu sets up a lock in 51964 (reached from
//   51950). With Lockpicking from the player's actor values it calls, each
//   once: 26488 sweet spot (skill, lock level, broken picks, Mod Lockpick
//   Sweet Spot multiplier; fLockpickSkillSweetSpotMult/Base, fSweetSpot<Level>,
//   fLockpickBrokenPicksMult), 26489 partial pick angle (skill, lock level;
//   fLockpickSkillPartialPickMult/Base, fPartialPick<Level>) and 26490 pick
//   break (skill, lock level, 1.0; fLockpickBreakSkillMult/Base,
//   fLockpickBreak<Level>). Each call is right after movaps xmm0, xmm10.
// - Pickpocket: 26379 (AIFormulas::ComputePickpocketSuccess; thief and target
//   Pickpocket, value, weight, thief, target, detected, item; fPickPocket*,
//   Mod Pickpocket Chance, clamped to fPickPocketMin/MaxChance) is called by
//   the steal attempt 40654 (from 51141, rolls 0 to 99 below the chance) and by
//   the container menu's item card 51130 (shows the chance, clamped 0 to 100).
// - Sneak and detection: 37774 (from the detection update 42742) calls 26365
//   once, with the detector, the target, the detector's Sneak as its
//   perception, the target's Sneak and 21 values on the stack (distance,
//   light, sneaking, movement, combat, three out pointers). For the player,
//   26365 turns a skill into fPlayerDetectionSneakMult * skill +
//   fPlayerDetectionSneakBase; it runs Mod Detection Sneak Skill (perk owner
//   the target) and Mod Detection Movement, and returns the detection level.
//   26366 (detection of sound events, the detector's Sneak only) is called
//   twice from 37774 and is not hooked.
// - Persuasion: vanilla dialogue uses the condition GetActorValue Speechcraft
//   (index 14) on the player against the globals SpeechEasy, SpeechAverage
//   and SpeechHard; the Persuasion perk sets a quest stage. Intimidation uses
//   the condition GetIntimidateSuccess (index 655), whose condition function
//   21735 calls the formula 37500 (player and target Speech,
//   fIntimidateSpeechcraftCurve, both levels, Mod Player Intimidation, the
//   target's confidence). TESConditionItem::IsTrue (29924) calls the
//   condition function through the script function table (361120, entries of
//   0x50 bytes, conditionFunction at 0x40), so a condition is hooked by
//   replacing that pointer.
// - Barter: the barter menu (vtable 214926, slot 4, 50945) calls 26464 once
//   when it opens (Speech, out buy and sell multipliers; fBarterMin/Max,
//   fBarterBuyMin, fBarterSellMax), then Mod Buy Prices (8) and Mod Sell Prices
//   (0x3C) with the merchant, clamps buy to at least fMinBuyMult and 1, sell
//   to at most fMaxSellMult and 1, and passes both to the movie's
//   SetBarterMultipliers, which prices the items.

#include "CheckSpike.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
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
#include <unordered_map>
#include <vector>

namespace tom::stats
{
namespace
{
// Relative to the game folder, which is the game's working directory.
constexpr std::string_view kConfigPath = "Data/SKSE/Plugins/trials-of-mastery-stats-s6b.json";

constexpr REL::ID kLockSetup{ 51964 };
constexpr REL::ID kSweetSpot{ 26488 };
constexpr REL::ID kPartialPick{ 26489 };
constexpr REL::ID kPickBreak{ 26490 };
constexpr REL::ID kStealAttempt{ 40654 };
constexpr REL::ID kItemCard{ 51130 };
// AIFormulas::ComputePickpocketSuccess (CommonLibSSE-NG's RELOCATION_ID(25822, 26379)).
constexpr REL::ID kPickpocketChance{ 26379 };
constexpr REL::ID kDetectionUpdate{ 37774 };
constexpr REL::ID kDetection{ 26365 };
constexpr REL::ID kEventDetection{ 26366 };
constexpr REL::ID kIntimidateFormula{ 37500 };
constexpr REL::ID kGetActorValueCondition{ 21416 };
constexpr REL::ID kIntimidateCondition{ 21735 };
constexpr REL::ID kBarterMenuMessage{ 50945 };
constexpr REL::ID kBarterMultipliers{ 26464 };
constexpr REL::ID kHandleEntryPoint{ 23526 };

// The functions are 0x480, 0x2C0, 0x370, 0xC80 and 0xAA0 bytes long on
// 1.7.104. The lengths only bound a scan; the matches must still be as many
// as expected.
constexpr std::size_t kLockSetupScanLength = 0x480;
constexpr std::size_t kStealAttemptScanLength = 0x2C0;
constexpr std::size_t kItemCardScanLength = 0x370;
constexpr std::size_t kDetectionUpdateScanLength = 0xC80;
constexpr std::size_t kBarterMenuScanLength = 0xAA0;

constexpr std::uint8_t kCallOpcode = 0xE8;
constexpr std::size_t  kCallLength = 5;

// movaps xmm0, xmm10 (Lockpicking): before each of the three formula calls in
// the lock setup, in the order sweet spot, partial pick, pick break.
constexpr std::array<std::uint8_t, 4> kLockFormulaPrefix{ 0x41, 0x0F, 0x28, 0xC2 };
// movaps xmm3, xmm7 (weight); mov r8d, r15d (value); movaps xmm1, xmm6 (target
// skill): before the steal attempt's call of the pickpocket chance.
constexpr std::array<std::uint8_t, 9> kStealPrefix{ 0x0F, 0x28, 0xDF, 0x45, 0x8B, 0xC7, 0x0F, 0x28, 0xCE };
// movaps xmm3, xmm7; mov r8d, [rbp + 0x6F]; movaps xmm1, xmm6: the same in the
// item card.
constexpr std::array<std::uint8_t, 10> kItemCardPrefix{ 0x0F, 0x28, 0xDF, 0x44, 0x8B, 0x45, 0x6F, 0x0F, 0x28, 0xCE };
// movaps xmm3, xmm8 (target Sneak); movaps xmm2, xmm14 (detector Sneak); mov
// rdx, r14 (target); mov rcx, rsi (detector): before the call of 26365.
constexpr std::array<std::uint8_t, 14> kDetectionPrefix{ 0x41, 0x0F, 0x28, 0xD8, 0x41, 0x0F, 0x28, 0xD6, 0x49, 0x8B,
	0xD6, 0x48, 0x8B, 0xCE };
// lea r8, [rbp + 0x98] (sell); lea rdx, [rbp + 0xA0] (buy): before the call of
// 26464.
constexpr std::array<std::uint8_t, 14> kBarterPrefix{ 0x4C, 0x8D, 0x85, 0x98, 0x00, 0x00, 0x00, 0x48, 0x8D, 0x95,
	0xA0, 0x00, 0x00, 0x00 };
// mov ecx, 8 (Mod Buy Prices) and mov ecx, 0x3C (Mod Sell Prices).
constexpr std::array<std::uint8_t, 5> kBuyPerkPrefix{ 0xB9, 0x08, 0x00, 0x00, 0x00 };
constexpr std::array<std::uint8_t, 5> kSellPerkPrefix{ 0xB9, 0x3C, 0x00, 0x00, 0x00 };

// jmp [rip + 0]; dq target: what an SKSE trampoline entry holds.
constexpr std::array<std::uint8_t, 6> kTrampolineJump{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
constexpr std::uint8_t                kJumpRel32Opcode = 0xE9;

constexpr std::array<std::string_view, 5> kLockLevelNames{ "Novice", "Apprentice", "Adept", "Expert", "Master" };

constexpr auto kConfigCheckInterval = std::chrono::seconds{ 1 };
constexpr auto kSummaryInterval = std::chrono::seconds{ 10 };

// The JSON file's fields of the same name. The defaults only observe.
struct SpikeConfig
{
	// Multiplies the sweet spot and the partial pick angle.
	double lockpickScale = 1.0;
	// Added to the pickpocket chance in percent, then clamped to 0 to 100.
	std::int32_t pickpocketAdd = 0;
	// Added to the player's Sneak where the player is the sneaking target, and
	// where the player is the detector (its perception).
	double sneakSkillAdd = 0.0;
	double detectionSkillAdd = 0.0;
	// What GetActorValue Speechcraft on the player returns while the dialogue
	// menu is open.
	std::optional<double> persuadeSpeech;
	// What GetIntimidateSuccess returns.
	std::optional<bool> forceIntimidate;
	// Multiplies the buy multiplier and divides the sell multiplier, before
	// the perk entries.
	double barterScale = 1.0;
	// The shortest time between two logged detections of the same pair.
	std::int32_t detectionLogIntervalMs = 2000;
};

template <class T>
std::string optionalText(const std::optional<T>& value)
{
	return value ? std::format("{}", *value) : "null";
}

std::string describe(const SpikeConfig& config)
{
	return std::format(
		"lockpickScale {}, pickpocketAdd {}, sneakSkillAdd {}, detectionSkillAdd {}, persuadeSpeech {}, "
		"forceIntimidate {}, barterScale {}, detectionLogIntervalMs {}",
		config.lockpickScale, config.pickpocketAdd, config.sneakSkillAdd, config.detectionSkillAdd,
		optionalText(config.persuadeSpeech), optionalText(config.forceIntimidate), config.barterScale,
		config.detectionLogIntervalMs);
}

template <class T>
std::optional<T> readNullable(const nlohmann::json& file, const char* field)
{
	const auto entry = file.find(field);
	if (entry == file.end() || entry->is_null()) {
		return std::nullopt;
	}
	return entry->get<T>();
}

bool isPositive(double value)
{
	return std::isfinite(value) && value > 0.0;
}

bool isValid(const SpikeConfig& config)
{
	return isPositive(config.lockpickScale) && isPositive(config.barterScale) && std::isfinite(config.sneakSkillAdd) &&
	       std::isfinite(config.detectionSkillAdd) && (!config.persuadeSpeech || std::isfinite(*config.persuadeSpeech)) &&
	       config.detectionLogIntervalMs >= 0;
}

SpikeConfig readConfig()
{
	std::ifstream stream{ std::filesystem::path(kConfigPath) };
	if (!stream) {
		SKSE::log::warn("S6b: {} cannot be opened; using the defaults", kConfigPath);
		return {};
	}
	SpikeConfig config;
	try {
		const auto file = nlohmann::json::parse(stream);
		config.lockpickScale = file.value("lockpickScale", config.lockpickScale);
		config.pickpocketAdd = file.value("pickpocketAdd", config.pickpocketAdd);
		config.sneakSkillAdd = file.value("sneakSkillAdd", config.sneakSkillAdd);
		config.detectionSkillAdd = file.value("detectionSkillAdd", config.detectionSkillAdd);
		config.persuadeSpeech = readNullable<double>(file, "persuadeSpeech");
		config.forceIntimidate = readNullable<bool>(file, "forceIntimidate");
		config.barterScale = file.value("barterScale", config.barterScale);
		config.detectionLogIntervalMs = file.value("detectionLogIntervalMs", config.detectionLogIntervalMs);
	} catch (const nlohmann::json::exception& error) {
		SKSE::log::error("S6b: {} is not valid ({}); using the defaults", kConfigPath, error.what());
		return {};
	}
	if (!isValid(config)) {
		SKSE::log::error(
			"S6b: {} has a scale that is not positive or a value that is not finite ({}); using the "
			"defaults",
			kConfigPath, describe(config));
		return {};
	}
	return config;
}

// The config in the file, read again when the file's modification time
// changes, so it can be edited while the game runs. The file is looked at at
// most once a second, since the detection hook runs very often. The defaults
// apply while the file is missing or invalid. Hooks run on several threads.
class ConfigCache
{
public:
	SpikeConfig current();

private:
	std::mutex                                     mutex_;
	bool                                           loaded_ = false;
	std::chrono::steady_clock::time_point          checked_;
	std::optional<std::filesystem::file_time_type> stamp_;
	SpikeConfig                                    config_;
};

SpikeConfig ConfigCache::current()
{
	const std::scoped_lock lock{ mutex_ };
	const auto             now = std::chrono::steady_clock::now();
	if (loaded_ && now - checked_ < kConfigCheckInterval) {
		return config_;
	}
	checked_ = now;
	std::error_code error;
	const auto      time = std::filesystem::last_write_time(std::filesystem::path(kConfigPath), error);
	const auto      stamp = error ? std::nullopt : std::optional{ time };
	if (loaded_ && stamp == stamp_) {
		return config_;
	}
	loaded_ = true;
	stamp_ = stamp;
	if (!stamp) {
		SKSE::log::warn("S6b: {} not found (game folder {}); using the defaults", kConfigPath,
			std::filesystem::current_path().string());
		config_ = {};
	} else {
		config_ = readConfig();
	}
	SKSE::log::info("S6b config: {}", describe(config_));
	return config_;
}

ConfigCache g_config;

// --- Logging helpers --------------------------------------------------------

std::uint32_t g_mainThread = 0;

std::string threadText()
{
	const auto thread = REX::W32::GetCurrentThreadId();
	return std::format("thread {}{}", thread, thread == g_mainThread ? " (main)" : "");
}

std::string refText(const RE::TESObjectREFR* ref)
{
	if (ref == nullptr) {
		return "none";
	}
	if (ref->IsPlayerRef()) {
		return "player";
	}
	return std::format("{:08X} \"{}\"", ref->GetFormID(), ref->GetName());
}

std::string formText(const RE::TESForm* form)
{
	if (form == nullptr) {
		return "none";
	}
	return std::format("{:08X} \"{}\"", form->GetFormID(), form->GetName());
}

float actorValue(RE::Actor* actor, RE::ActorValue value)
{
	return actor != nullptr ? actor->AsActorValueOwner()->GetActorValue(value) : 0.0F;
}

std::string_view lockLevelName(std::int32_t level)
{
	if (level < 0 || static_cast<std::size_t>(level) >= kLockLevelNames.size()) {
		return "?";
	}
	return kLockLevelNames[static_cast<std::size_t>(level)];
}

float gameSetting(const char* name)
{
	auto* settings = RE::GameSettingCollection::GetSingleton();
	auto* setting = settings != nullptr ? settings->GetSetting(name) : nullptr;
	return setting != nullptr ? setting->GetFloat() : std::nanf("");
}

// At most one line per key and interval, for hooks that run very often.
class RateLimiter
{
public:
	bool allow(std::uint64_t key, std::chrono::milliseconds interval);

private:
	std::mutex                                                               mutex_;
	std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> last_;
};

bool RateLimiter::allow(std::uint64_t key, std::chrono::milliseconds interval)
{
	const std::scoped_lock lock{ mutex_ };
	const auto             now = std::chrono::steady_clock::now();
	const auto             found = last_.find(key);
	if (found != last_.end() && now - found->second < interval) {
		return false;
	}
	last_[key] = now;
	return true;
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

// The E8 call right after each occurrence of prefix in [begin, begin +
// length). Empty if one occurrence is not followed by a call. The call's
// target is not checked, so a call another plugin redirected is still found.
std::vector<std::uintptr_t> findCallsAfter(std::uintptr_t begin, std::size_t length,
	std::span<const std::uint8_t> prefix)
{
	const auto                  code = codeAt(begin, length);
	std::vector<std::uintptr_t> calls;
	for (std::size_t i = 0; i + prefix.size() + kCallLength <= code.size(); ++i) {
		if (std::memcmp(code.data() + i, prefix.data(), prefix.size()) != 0) {
			continue;
		}
		const auto at = begin + i + prefix.size();
		if (*reinterpret_cast<const std::uint8_t*>(at) != kCallOpcode) {
			return {};
		}
		calls.push_back(at);
	}
	return calls;
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

// Where a call target or function pointer leads, and whether that is vanilla.
std::string targetText(std::uintptr_t target, std::uintptr_t vanilla)
{
	if (target == vanilla) {
		return std::format("{:#x} (vanilla)", target);
	}
	const auto destination = followJumps(target);
	return std::format("{:#x} -> {:#x} {} (redirected; vanilla is {:#x})", target, destination,
		moduleText(destination), vanilla);
}

struct Site
{
	std::string_view              name;
	REL::ID                       function;
	std::size_t                   scanLength;
	std::span<const std::uint8_t> prefix;
	std::size_t                   expected;
};

// Finds a site's calls (exactly site.expected of them) and logs where each
// goes. Empty if not found.
std::vector<std::uintptr_t> findSite(const Site& site, std::span<const REL::ID> vanillaTargets)
{
	const auto base = site.function.address();
	const auto calls = findCallsAfter(base, site.scanLength, site.prefix);
	if (calls.size() != site.expected || vanillaTargets.size() != site.expected) {
		SKSE::log::error("S6b {}: {} calls after the prefix in {} ({:#x}), {} expected; not hooked", site.name,
			calls.size(), site.function.id(), base, site.expected);
		return {};
	}
	for (std::size_t i = 0; i < calls.size(); ++i) {
		SKSE::log::info("S6b {}: call {} found at {} + {:#x} ({:#x}); it goes to {}", site.name, i + 1,
			site.function.id(), calls[i] - base, calls[i],
			targetText(callTarget(calls[i]), vanillaTargets[i].address()));
	}
	return calls;
}

template <class Function>
Function hookCall(std::uintptr_t call, Function hook)
{
	return reinterpret_cast<Function>(SKSE::GetTrampoline().write_call<kCallLength>(call, hook));
}

// --- Lockpicking ------------------------------------------------------------

using SweetSpot = float (*)(float, std::int32_t, std::uint32_t, float);
using PartialPick = float (*)(float, std::int32_t);
using PickBreak = float (*)(float, std::int32_t, float);
SweetSpot   g_sweetSpot = nullptr;
PartialPick g_partialPick = nullptr;
PickBreak   g_pickBreak = nullptr;

std::string lockText(float skill, std::int32_t level)
{
	const auto target = RE::LockpickingMenu::GetTargetReference();
	return std::format("Lockpicking {:.2f}, lock level {} ({}), lock {}", skill, level, lockLevelName(level),
		refText(target.get()));
}

float sweetSpot(float skill, std::int32_t level, std::uint32_t brokenPicks, float perkMult)
{
	const float angle = g_sweetSpot(skill, level, brokenPicks, perkMult);
	const auto  scale = g_config.current().lockpickScale;
	const auto  result = static_cast<float>(angle * scale);
	SKSE::log::info(
		"S6b lockpick sweet spot: {}, broken picks {}, Mod Lockpick Sweet Spot x{:.3f} -> {:.4f} (x{} -> "
		"{:.4f}); {}",
		lockText(skill, level), brokenPicks, perkMult, angle, scale, result, threadText());
	return result;
}

float partialPick(float skill, std::int32_t level)
{
	const float angle = g_partialPick(skill, level);
	const auto  scale = g_config.current().lockpickScale;
	const auto  result = static_cast<float>(angle * scale);
	SKSE::log::info("S6b lockpick partial pick: {} -> {:.4f} (x{} -> {:.4f}); {}", lockText(skill, level), angle,
		scale, result, threadText());
	return result;
}

float pickBreak(float skill, std::int32_t level, float mult)
{
	const float value = g_pickBreak(skill, level, mult);
	SKSE::log::info("S6b lockpick pick break: {}, mult {:.3f} -> {:.4f} (not adjusted); {}", lockText(skill, level),
		mult, value, threadText());
	return value;
}

void installLockpickHooks()
{
	constexpr std::array kTargets{ kSweetSpot, kPartialPick, kPickBreak };
	const auto           calls = findSite({ "lockpick", kLockSetup, kLockSetupScanLength, kLockFormulaPrefix, 3 }, kTargets);
	if (calls.empty()) {
		return;
	}
	g_sweetSpot = hookCall(calls[0], &sweetSpot);
	g_partialPick = hookCall(calls[1], &partialPick);
	g_pickBreak = hookCall(calls[2], &pickBreak);
	SKSE::log::info("S6b lockpick: hooked");
}

// --- Pickpocket -------------------------------------------------------------

using Pickpocket = std::int32_t (*)(float, float, std::uint32_t, float, RE::Actor*, RE::Actor*, bool, RE::TESForm*);
Pickpocket g_stealChance = nullptr;
Pickpocket g_cardChance = nullptr;

std::int32_t pickpocket(std::string_view site, Pickpocket original, float thiefSkill, float targetSkill,
	std::uint32_t value, float weight, RE::Actor* thief, RE::Actor* target, bool detected, RE::TESForm* item)
{
	const auto chance = original(thiefSkill, targetSkill, value, weight, thief, target, detected, item);
	const auto add = g_config.current().pickpocketAdd;
	const auto result = add == 0 ? chance : std::clamp(chance + add, 0, 100);
	SKSE::log::info(
		"S6b pickpocket {}: {} (Pickpocket {:.2f}) from {} (Pickpocket {:.2f}), item {}, value {}, weight {:.2f}, "
		"detected {} -> chance {} (+{} -> {}); {}",
		site, refText(thief), thiefSkill, refText(target), targetSkill, formText(item), value, weight, detected,
		chance, add, result, threadText());
	return result;
}

std::int32_t stealChance(float thiefSkill, float targetSkill, std::uint32_t value, float weight, RE::Actor* thief,
	RE::Actor* target, bool detected, RE::TESForm* item)
{
	return pickpocket("attempt", g_stealChance, thiefSkill, targetSkill, value, weight, thief, target, detected,
		item);
}

std::int32_t cardChance(float thiefSkill, float targetSkill, std::uint32_t value, float weight, RE::Actor* thief,
	RE::Actor* target, bool detected, RE::TESForm* item)
{
	return pickpocket("item card", g_cardChance, thiefSkill, targetSkill, value, weight, thief, target, detected,
		item);
}

void installPickpocketHooks()
{
	constexpr std::array kTargets{ kPickpocketChance };
	const auto           steal = findSite({ "pickpocket attempt", kStealAttempt, kStealAttemptScanLength, kStealPrefix, 1 },
		kTargets);
	if (!steal.empty()) {
		g_stealChance = hookCall(steal.front(), &stealChance);
		SKSE::log::info("S6b pickpocket attempt: hooked");
	}
	const auto card = findSite({ "pickpocket item card", kItemCard, kItemCardScanLength, kItemCardPrefix, 1 },
		kTargets);
	if (!card.empty()) {
		g_cardChance = hookCall(card.front(), &cardChance);
		SKSE::log::info("S6b pickpocket item card: hooked");
	}
}

// --- Sneak and detection ----------------------------------------------------

// 26365 takes 21 values on the stack after the four register arguments. The
// hook passes them on as 8-byte slots unchanged, which keeps their bits
// whatever their types are. Their meaning comes from the GMSTs each is used
// with.
using Slot = std::uint64_t;
using Detection = std::int32_t (*)(RE::Actor*, RE::Actor*, float, float, Slot, Slot, Slot, Slot, Slot, Slot, Slot,
	Slot, Slot, Slot, Slot, Slot, Slot, Slot, Slot, Slot, Slot, Slot, Slot, Slot, Slot);
Detection g_detection = nullptr;

std::atomic<std::uint64_t> g_detectionCalls{ 0 };
std::atomic<std::uint64_t> g_playerTargetCalls{ 0 };
std::atomic<std::uint64_t> g_playerDetectorCalls{ 0 };
std::atomic<std::uint64_t> g_playerOffMainCalls{ 0 };
RateLimiter                g_detectionLimiter;

float slotFloat(Slot slot)
{
	return std::bit_cast<float>(static_cast<std::uint32_t>(slot));
}

std::int32_t slotInt(Slot slot)
{
	return static_cast<std::int32_t>(static_cast<std::uint32_t>(slot));
}

std::uint32_t slotByte(Slot slot)
{
	return static_cast<std::uint32_t>(slot & 0xFF);
}

std::string outText(Slot slot)
{
	const auto* value = reinterpret_cast<const float*>(slot);
	return value != nullptr ? std::format("{:.2f}", *value) : "-";
}

void logDetectionSummary()
{
	static RateLimiter limiter;
	if (!limiter.allow(0, kSummaryInterval)) {
		return;
	}
	SKSE::log::info(
		"S6b detection summary: {} calls of 26365, {} with the player as target, {} with the player as "
		"detector, {} of those with the player off the main thread",
		g_detectionCalls.load(), g_playerTargetCalls.load(), g_playerDetectorCalls.load(),
		g_playerOffMainCalls.load());
}

std::uint64_t pairKey(const RE::Actor* detector, const RE::Actor* target)
{
	return (static_cast<std::uint64_t>(detector->GetFormID()) << 32U) | target->GetFormID();
}

// a5 size class, a6 line of sight, a8 distance, a9 light, a12 equipped weight,
// a14 sneaking, a15 movement speed, a16 running, a21 combat, a23 to a25 out
// sound, visibility and skill term.
std::int32_t detection(RE::Actor* detector, RE::Actor* target, float detectorSneak, float targetSneak, Slot a5,
	Slot a6, Slot a7, Slot a8, Slot a9, Slot a10, Slot a11, Slot a12, Slot a13, Slot a14, Slot a15, Slot a16, Slot a17,
	Slot a18, Slot a19, Slot a20, Slot a21, Slot a22, Slot a23, Slot a24, Slot a25)
{
	++g_detectionCalls;
	const bool playerTarget = target != nullptr && target->IsPlayerRef();
	const bool playerDetector = detector != nullptr && detector->IsPlayerRef();
	if (!playerTarget && !playerDetector) {
		return g_detection(detector, target, detectorSneak, targetSneak, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14,
			a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25);
	}
	if (playerTarget) {
		++g_playerTargetCalls;
	}
	if (playerDetector) {
		++g_playerDetectorCalls;
	}
	if (REX::W32::GetCurrentThreadId() != g_mainThread) {
		++g_playerOffMainCalls;
	}
	const auto  config = g_config.current();
	const float sneakAdd = playerTarget ? static_cast<float>(config.sneakSkillAdd) : 0.0F;
	const float perceptionAdd = playerDetector ? static_cast<float>(config.detectionSkillAdd) : 0.0F;
	const auto  level = g_detection(detector, target, detectorSneak + perceptionAdd, targetSneak + sneakAdd, a5, a6,
		a7, a8, a9, a10, a11, a12, a13, a14, a15, a16, a17, a18, a19, a20, a21, a22, a23, a24, a25);
	logDetectionSummary();
	const auto interval = std::chrono::milliseconds{ config.detectionLogIntervalMs };
	if (detector == nullptr || target == nullptr || !g_detectionLimiter.allow(pairKey(detector, target), interval)) {
		return level;
	}
	SKSE::log::info(
		"S6b detection: {} (Sneak {:.2f}{:+}) detects {} (Sneak {:.2f}{:+}): size {}, LOS {}, distance {:.1f}, light "
		"{}, weight {:.1f}, sneaking {}, speed {:.1f}, running {}, combat {} -> level {} (sound {}, visibility {}, "
		"skill term {}); {}",
		refText(detector), detectorSneak, perceptionAdd, refText(target), targetSneak, sneakAdd, slotInt(a5),
		slotInt(a6), slotFloat(a8), slotInt(a9), slotFloat(a12), slotByte(a14), slotFloat(a15), slotByte(a16),
		slotByte(a21), level, outText(a23), outText(a24), outText(a25), threadText());
	return level;
}

void installDetectionHook()
{
	constexpr std::array kTargets{ kDetection };
	const auto           calls = findSite({ "detection", kDetectionUpdate, kDetectionUpdateScanLength,
											  kDetectionPrefix, 1 },
		kTargets);
	if (calls.empty()) {
		return;
	}
	g_detection = hookCall(calls.front(), &detection);
	SKSE::log::info("S6b detection: hooked");
}

// --- Dialogue menu state ----------------------------------------------------

std::atomic<bool> g_dialogueOpen{ false };

// Tracks the dialogue menu for the persuasion hook, which may run on any
// thread, and logs the menus of the checks opening and closing.
class MenuSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*                     source) override;
};

RE::BSEventNotifyControl MenuSink::ProcessEvent(const RE::MenuOpenCloseEvent* event,
	RE::BSTEventSource<RE::MenuOpenCloseEvent>* /*source*/)
{
	if (event == nullptr) {
		return RE::BSEventNotifyControl::kContinue;
	}
	const std::string_view name = event->menuName.c_str();
	if (name == RE::DialogueMenu::MENU_NAME) {
		g_dialogueOpen = event->opening;
	}
	if (name == RE::DialogueMenu::MENU_NAME || name == RE::LockpickingMenu::MENU_NAME ||
		name == RE::ContainerMenu::MENU_NAME || name == RE::BarterMenu::MENU_NAME) {
		SKSE::log::info("S6b menu: {} {}; {}", name, event->opening ? "opens" : "closes", threadText());
	}
	return RE::BSEventNotifyControl::kContinue;
}

MenuSink g_menuSink;

std::string speakerText()
{
	auto* topics = RE::MenuTopicManager::GetSingleton();
	if (topics == nullptr) {
		return "none";
	}
	const auto speaker = topics->speaker.get();
	return refText(speaker.get());
}

// --- Persuasion and intimidation (condition functions) ----------------------

using Condition = RE::SCRIPT_FUNCTION::Condition_t*;
Condition   g_getActorValue = nullptr;
Condition   g_intimidate = nullptr;
RateLimiter g_speechLimiter;

// GetActorValue's first parameter is the actor value's index, passed in the
// pointer.
bool getActorValue(RE::TESObjectREFR* subject, void* param1, void* param2, double& result)
{
	const bool done = g_getActorValue(subject, param1, param2, result);
	const auto value = static_cast<RE::ActorValue>(reinterpret_cast<std::uintptr_t>(param1));
	if (value != RE::ActorValue::kSpeech || subject == nullptr || !subject->IsPlayerRef()) {
		return done;
	}
	const auto game = result;
	const bool dialogue = g_dialogueOpen;
	const auto forced = g_config.current().persuadeSpeech;
	if (dialogue && forced) {
		result = *forced;
	}
	if (!dialogue && !g_speechLimiter.allow(0, std::chrono::seconds{ 2 })) {
		return done;
	}
	SKSE::log::info(
		"S6b persuade: GetActorValue Speechcraft on the player = {:.2f} -> {:.2f}, dialogue {}, speaker "
		"{}; {}",
		game, result, dialogue ? "open" : "closed", dialogue ? speakerText() : "-", threadText());
	return done;
}

bool intimidate(RE::TESObjectREFR* subject, void* param1, void* param2, double& result)
{
	const bool  done = g_intimidate(subject, param1, param2, result);
	const auto  game = result;
	const auto  force = g_config.current().forceIntimidate;
	auto*       player = RE::PlayerCharacter::GetSingleton();
	auto*       target = subject != nullptr ? subject->As<RE::Actor>() : nullptr;
	const float playerSpeech = actorValue(player, RE::ActorValue::kSpeech);
	if (force) {
		result = *force ? 1.0 : 0.0;
	}
	SKSE::log::info(
		"S6b intimidate: GetIntimidateSuccess on {}: player Speech {:.2f}, level {}; target Speech {:.2f}, level {}, "
		"Confidence {:.0f} -> {} (-> {}); dialogue {}; {}",
		refText(subject), playerSpeech, player != nullptr ? player->GetLevel() : 0,
		actorValue(target, RE::ActorValue::kSpeech), target != nullptr ? target->GetLevel() : 0,
		actorValue(target, RE::ActorValue::kConfidence), game, result, g_dialogueOpen ? "open" : "closed",
		threadText());
	return done;
}

// Replaces a condition function in the script function table and returns
// the previous one, or nullptr if the command is not there.
Condition replaceCondition(std::string_view name, REL::ID vanilla, Condition hook)
{
	auto* command = RE::SCRIPT_FUNCTION::LocateScriptCommand(name);
	if (command == nullptr || command->conditionFunction == nullptr) {
		SKSE::log::error("S6b {}: no script command or no condition function; not hooked", name);
		return nullptr;
	}
	const auto previous = command->conditionFunction;
	SKSE::log::info("S6b {}: script command at {:#x}, opcode {:#x}, condition function {}", name,
		reinterpret_cast<std::uintptr_t>(command), static_cast<std::uint32_t>(command->output),
		targetText(reinterpret_cast<std::uintptr_t>(previous), vanilla.address()));
	// The table is in the game's writable .data section.
	command->conditionFunction = hook;
	SKSE::log::info("S6b {}: hooked", name);
	return previous;
}

void installConditionHooks()
{
	g_getActorValue = replaceCondition("GetActorValue", kGetActorValueCondition, &getActorValue);
	g_intimidate = replaceCondition("GetIntimidateSuccess", kIntimidateCondition, &intimidate);
}

// --- Barter -----------------------------------------------------------------

using BarterMultipliers = void (*)(float, float*, float*);
using HandleEntryPoint = void (*)(std::uint32_t, RE::Actor*, RE::TESObjectREFR*, float*);
BarterMultipliers   g_barterMultipliers = nullptr;
HandleEntryPoint    g_buyPerk = nullptr;
HandleEntryPoint    g_sellPerk = nullptr;
thread_local float* t_buy = nullptr;

void barterMultipliers(float speech, float* buy, float* sell)
{
	g_barterMultipliers(speech, buy, sell);
	const auto  scale = g_config.current().barterScale;
	const float gameBuy = *buy;
	const float gameSell = *sell;
	*buy = static_cast<float>(gameBuy * scale);
	*sell = static_cast<float>(gameSell / scale);
	t_buy = buy;
	const auto merchant = RE::TESObjectREFR::LookupByHandle(RE::BarterMenu::GetTargetRefHandle());
	SKSE::log::info(
		"S6b barter: Speech {:.2f} with {} -> buy x{:.4f}, sell x{:.4f} (barterScale {} -> buy x{:.4f}, "
		"sell x{:.4f}); {}",
		speech, refText(merchant.get()), gameBuy, gameSell, scale, *buy, *sell, threadText());
}

void buyPerk(std::uint32_t entryPoint, RE::Actor* owner, RE::TESObjectREFR* merchant, float* value)
{
	const float before = *value;
	g_buyPerk(entryPoint, owner, merchant, value);
	SKSE::log::info("S6b barter Mod Buy Prices ({}) of {} with {}: buy x{:.4f} -> x{:.4f}", entryPoint,
		refText(owner), refText(merchant), before, *value);
}

void sellPerk(std::uint32_t entryPoint, RE::Actor* owner, RE::TESObjectREFR* merchant, float* value)
{
	const float before = *value;
	g_sellPerk(entryPoint, owner, merchant, value);
	const float buy = t_buy != nullptr ? *t_buy : std::nanf("");
	const float finalBuy = (std::max)({ gameSetting("fMinBuyMult"), buy, 1.0F });
	const float finalSell = (std::min)({ gameSetting("fMaxSellMult"), *value, 1.0F });
	SKSE::log::info(
		"S6b barter Mod Sell Prices ({}) of {} with {}: sell x{:.4f} -> x{:.4f}; after the clamps the "
		"movie gets buy x{:.4f}, sell x{:.4f}",
		entryPoint, refText(owner), refText(merchant), before, *value, finalBuy, finalSell);
	t_buy = nullptr;
}

void installBarterHooks()
{
	constexpr std::array kFormula{ kBarterMultipliers };
	constexpr std::array kPerk{ kHandleEntryPoint };
	const auto           formula = findSite({ "barter", kBarterMenuMessage, kBarterMenuScanLength, kBarterPrefix, 1 },
		kFormula);
	const auto           buy = findSite({ "barter Mod Buy Prices", kBarterMenuMessage, kBarterMenuScanLength, kBuyPerkPrefix, 1 },
		kPerk);
	const auto           sell = findSite(
		{ "barter Mod Sell Prices", kBarterMenuMessage, kBarterMenuScanLength, kSellPerkPrefix, 1 }, kPerk);
	if (formula.empty() || buy.empty() || sell.empty()) {
		SKSE::log::error("S6b barter: a site is missing; not hooked");
		return;
	}
	g_barterMultipliers = hookCall(formula.front(), &barterMultipliers);
	g_buyPerk = hookCall(buy.front(), &buyPerk);
	g_sellPerk = hookCall(sell.front(), &sellPerk);
	SKSE::log::info("S6b barter: hooked");
}

// --- Coexistence ------------------------------------------------------------

// Whether a function's first instruction is a jump, as an entry detour by
// another plugin would be.
void logEntry(std::string_view name, REL::ID function)
{
	const auto address = function.address();
	const auto destination = followJumps(address);
	if (destination == address) {
		SKSE::log::info("S6b coexistence: {} ({}, {:#x}) entry is not detoured", name, function.id(), address);
		return;
	}
	SKSE::log::info("S6b coexistence: {} ({}, {:#x}) entry jumps to {:#x} {}", name, function.id(), address,
		destination, moduleText(destination));
}

void logCoexistence()
{
	logEntry("lock setup", kLockSetup);
	logEntry("sweet spot", kSweetSpot);
	logEntry("partial pick", kPartialPick);
	logEntry("pick break", kPickBreak);
	logEntry("pickpocket chance", kPickpocketChance);
	logEntry("detection", kDetection);
	logEntry("event detection", kEventDetection);
	logEntry("intimidate formula", kIntimidateFormula);
	logEntry("GetActorValue condition", kGetActorValueCondition);
	logEntry("GetIntimidateSuccess condition", kIntimidateCondition);
	logEntry("barter multipliers", kBarterMultipliers);
}
}

void installCheckSpike()
{
	g_mainThread = REX::W32::GetCurrentThreadId();
	SKSE::log::info("S6b: installing on {}", threadText());
	SKSE::log::info("S6b config at install: {}", describe(g_config.current()));
	logCoexistence();
	installLockpickHooks();
	installPickpocketHooks();
	installDetectionHook();
	installConditionHooks();
	installBarterHooks();
	RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&g_menuSink);
}
}
