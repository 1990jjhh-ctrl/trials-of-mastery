#include "LevelUpHook.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tom::stats
{
namespace
{
// Address Library IDs (AE). How they were found: docs/stats/PLAN.md, S1.
//
// StatsMenu::ProcessMessage. On a menu update with a level-up pending it
// raises the level (LevelIncrease event), opens the LevelUp menu, then adds
// one perk point.
constexpr REL::ID kStatsMenuProcessMessage{ 52510 };
// Queues the message that opens the LevelUp menu. Takes no arguments.
constexpr REL::ID kShowLevelUpMenu{ 51916 };
// LevelUpMenu::ConfirmLevelUpAttributeCallback::Run: adds the +10 to the
// chosen attribute, then finishes the level-up with
// PlayerSkills::AdvanceLevel(false), which takes the threshold off the XP and
// computes the next threshold.
constexpr REL::ID kConfirmLevelUpAttribute{ 51917 };
// PlayerSkills::AdvanceLevel; logged next to the confirm callback's call
// target, which differs when another plugin redirected that call.
constexpr REL::ID kAdvanceLevel{ 41567 };
// Shows a tutorial message by its number, once per save.
constexpr REL::ID kShowTutorialOnce{ 52692 };

// mov ecx, 0xFA: the number of the "Leveling Up" tutorial, which
// StatsMenu::ProcessMessage shows right before it raises the level.
constexpr std::array<std::uint8_t, 5> kLevelUpTutorialBytes{ 0xB9, 0xFA, 0x00, 0x00, 0x00 };

// Scan lengths for StatsMenu::ProcessMessage (0x1580 bytes on 1.7.104) and
// the confirm callback (0x115 bytes). They only bound a scan; a match must
// still be unique.
constexpr std::size_t  kScanLength = 0x2000;
constexpr std::size_t  kConfirmScanLength = 0x140;
constexpr std::uint8_t kCallOpcode = 0xE8;
constexpr std::size_t  kCallLength = 5;

// mov rcx, [rax + disp32], which loads the player's PlayerSkills right before
// the call to AdvanceLevel in the confirm callback. disp32 is the offset of
// PlayerCharacter::skills, which differs between runtimes (0x9C0 from 1.7.99
// on), so it is taken from CommonLibSSE-NG's layout at runtime.
constexpr std::array<std::uint8_t, 3> kLoadSkillsOpcode{ 0x48, 0x8B, 0x88 };
using LoadSkillsBytes = std::array<std::uint8_t, kLoadSkillsOpcode.size() + sizeof(std::uint32_t)>;

using AdvanceLevel = void (*)(RE::PlayerCharacter::PlayerSkills*, bool);
// The confirm callback's call to AdvanceLevel, and its target at install time.
std::uintptr_t g_advanceLevelCall = 0;
AdvanceLevel   g_advanceLevel = nullptr;

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

// Every E8 call in [begin, begin + length) to target.
std::vector<std::uintptr_t> findCallsTo(std::uintptr_t begin, std::size_t length, std::uintptr_t target)
{
	std::vector<std::uintptr_t> found;
	for (auto at = begin; at + kCallLength <= begin + length; ++at) {
		if (isCall(at) && callTarget(at) == target) {
			found.push_back(at);
		}
	}
	return found;
}

// The bytes of mov rcx, [rax + offset of PlayerCharacter::skills] on this
// runtime, measured on the player singleton. Empty before the player exists.
std::optional<LoadSkillsBytes> loadSkillsBytes()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (player == nullptr) {
		return std::nullopt;
	}
	const auto      skillsAt = reinterpret_cast<std::uintptr_t>(&player->GetPlayerRuntimeData().skills);
	const auto      offset = static_cast<std::uint32_t>(skillsAt - reinterpret_cast<std::uintptr_t>(player));
	LoadSkillsBytes bytes{};
	std::memcpy(bytes.data(), kLoadSkillsOpcode.data(), kLoadSkillsOpcode.size());
	std::memcpy(bytes.data() + kLoadSkillsOpcode.size(), &offset, sizeof(offset));
	return bytes;
}

// The call right after pattern, if the pattern occurs exactly once.
std::optional<std::uintptr_t> findAdvanceLevelCall(std::uintptr_t begin, std::size_t length,
	const LoadSkillsBytes& pattern)
{
	const std::span<const std::uint8_t> code{ reinterpret_cast<const std::uint8_t*>(begin), length };
	std::optional<std::uintptr_t>       found;
	for (std::size_t i = 0; i + pattern.size() + kCallLength <= code.size(); ++i) {
		if (std::memcmp(code.data() + i, pattern.data(), pattern.size()) != 0) {
			continue;
		}
		if (found) {
			return std::nullopt;
		}
		found = begin + i + pattern.size();
	}
	if (!found || !isCall(*found)) {
		return std::nullopt;
	}
	return found;
}

std::string playerState()
{
	auto* player = RE::PlayerCharacter::GetSingleton();
	if (player == nullptr) {
		return "no player";
	}
	const auto* skills = player->GetPlayerRuntimeData().skills;
	if (skills == nullptr || skills->data == nullptr) {
		return "no player skills";
	}
	auto* values = player->AsActorValueOwner();
	return std::format(
		"level {}, perk points {}, XP {:.1f}/{:.1f}, base Health {:.0f}, Magicka {:.0f}, Stamina {:.0f}, "
		"carry weight {:.0f}",
		player->GetLevel(), player->GetGameStatsData().perkCount, skills->data->xp, skills->data->levelThreshold,
		values->GetBaseActorValue(RE::ActorValue::kHealth), values->GetBaseActorValue(RE::ActorValue::kMagicka),
		values->GetBaseActorValue(RE::ActorValue::kStamina), values->GetActorValue(RE::ActorValue::kCarryWeight));
}

// The confirm callback's current AdvanceLevel target. It is read again on every
// level-up, so a plugin that redirects that call after kDataLoaded is still
// followed. If the call is no longer an E8 call, the install-time target is
// used.
AdvanceLevel currentAdvanceLevel()
{
	if (!isCall(g_advanceLevelCall)) {
		SKSE::log::warn("The confirm callback's AdvanceLevel call at {:#x} is no longer a call; using {:#x}",
			g_advanceLevelCall, reinterpret_cast<std::uintptr_t>(g_advanceLevel));
		return g_advanceLevel;
	}
	return reinterpret_cast<AdvanceLevel>(callTarget(g_advanceLevelCall));
}

// Replaces the call that opens the LevelUp menu. The level and the perk point
// are the skills menu's own work around this call; what is left is the part of
// the confirm callback that comes after the +10.
void finishLevelUp()
{
	SKSE::log::info("LevelUp menu suppressed: {}", playerState());
	currentAdvanceLevel()(RE::PlayerCharacter::GetSingleton()->GetPlayerRuntimeData().skills, false);
	SKSE::log::info("Level-up finished without the attribute reward: {}", playerState());
}

class Diagnostics final :
    public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
    public RE::BSTEventSink<RE::LevelIncrease::Event>
{
public:
	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
	{
		if (event == nullptr) {
			return RE::BSEventNotifyControl::kContinue;
		}
		const std::string_view name{ event->menuName.c_str() };
		if (name == RE::StatsMenu::MENU_NAME || name == RE::LevelUpMenu::MENU_NAME) {
			SKSE::log::debug("{} {}: {}", name, event->opening ? "opened" : "closed", playerState());
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl ProcessEvent(const RE::LevelIncrease::Event* event,
		RE::BSTEventSource<RE::LevelIncrease::Event>*) override
	{
		if (event != nullptr) {
			SKSE::log::debug("LevelIncrease event, new level {}: {}", event->newLevel, playerState());
		}
		return RE::BSEventNotifyControl::kContinue;
	}
};

Diagnostics g_diagnostics;

// Replaces the call that shows the "Leveling Up" tutorial, which asks the
// player to choose Health, Magicka or Stamina.
void skipLevelUpTutorial(std::int32_t /*tutorial*/) {}

// The call that shows the level-up tutorial in StatsMenu::ProcessMessage: the
// only call to kShowTutorialOnce there that loads kLevelUpTutorialBytes first.
std::optional<std::uintptr_t> findLevelUpTutorialCall()
{
	const auto                    calls = findCallsTo(kStatsMenuProcessMessage.address(), kScanLength, kShowTutorialOnce.address());
	std::optional<std::uintptr_t> found;
	for (const auto at : calls) {
		const auto* before = reinterpret_cast<const std::uint8_t*>(at - kLevelUpTutorialBytes.size());
		if (std::memcmp(before, kLevelUpTutorialBytes.data(), kLevelUpTutorialBytes.size()) != 0) {
			continue;
		}
		if (found) {
			return std::nullopt;
		}
		found = at;
	}
	return found;
}
}

bool installLevelUpHook()
{
	const auto pattern = loadSkillsBytes();
	if (!pattern) {
		SKSE::log::error("No player yet, so the AdvanceLevel call cannot be located");
		return false;
	}
	const auto advanceCall = findAdvanceLevelCall(kConfirmLevelUpAttribute.address(), kConfirmScanLength, *pattern);
	if (!advanceCall) {
		SKSE::log::error("AdvanceLevel call not found in the LevelUp confirm callback");
		return false;
	}
	const auto menuCalls = findCallsTo(kStatsMenuProcessMessage.address(), kScanLength, kShowLevelUpMenu.address());
	if (menuCalls.size() != 1) {
		SKSE::log::error("Expected one call that opens the LevelUp menu in StatsMenu::ProcessMessage, found {}",
			menuCalls.size());
		return false;
	}
	const auto tutorialCall = findLevelUpTutorialCall();
	if (!tutorialCall) {
		SKSE::log::error("Level-up tutorial call not found in StatsMenu::ProcessMessage");
		return false;
	}

	// Through the confirm callback's own call, so a plugin that redirected it
	// (Experience does) still finishes the level-up its way.
	g_advanceLevelCall = *advanceCall;
	g_advanceLevel = reinterpret_cast<AdvanceLevel>(callTarget(*advanceCall));
	SKSE::log::info("Confirm callback calls AdvanceLevel at {:#x} -> {:#x} (PlayerSkills::AdvanceLevel is {:#x})",
		*advanceCall, callTarget(*advanceCall), kAdvanceLevel.address());

	auto& trampoline = SKSE::GetTrampoline();
	trampoline.write_call<kCallLength>(menuCalls.front(), &finishLevelUp);
	trampoline.write_call<kCallLength>(*tutorialCall, &skipLevelUpTutorial);
	const auto base = kStatsMenuProcessMessage.address();
	SKSE::log::info("LevelUp menu replaced at StatsMenu::ProcessMessage + {:#x}, its tutorial at + {:#x}",
		menuCalls.front() - base, *tutorialCall - base);
	return true;
}

void registerLevelUpDiagnostics()
{
	RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&g_diagnostics);
	RE::LevelIncrease::GetEventSource()->AddEventSink(&g_diagnostics);
}
}
