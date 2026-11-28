// THROWAWAY prototype of spike S6a (docs/stats/PLAN.md, Phase 3). It only
// answers where each default training action of ST-15 can be hooked, which
// data the hook has and which thread it runs on; G9a to G9c replace it.
//
// The candidates (disassembly of SkyrimSE.exe 1.7.104; Address Library IDs
// are AE). None patches code, so no call site has to be found by a scan; the
// three vtable hooks log whether their slot still holds the vanilla function.
//
// Combat (G9a):
// - Swing: the player's animation graph events weaponSwing (right hand) and
//   weaponLeftSwing (left hand), through the PlayerCharacter vtable of its
//   BSTEventSink<BSAnimationGraphEvent> (208044, slot 1, vanilla
//   TESObjectREFR::ProcessEvent 37997). The event names are the ones the
//   character behavior files (Skyrim - Animations.bsa) use.
// - Bow draw: bowDrawStart and bowDrawn; arrowRelease and BowRelease on
//   release. Crossbow load: reloadStart and reloadStop.
// - Hit: TESHitEvent with the player as cause; spells and projectiles come
//   with the spell or weapon as source.
// - Staff use: TESSpellCastEvent with a staff enchantment, and the animation
//   events MLh_SpellFire_Event and MRh_SpellFire_Event with a staff in that
//   hand.
// - Unarmed: a swing or hit without a weapon; the gloves are the armor worn
//   in the hands slot.
// - Fighting: a once-a-second tick in PlayerCharacter::Update (208040, slot
//   0xAD, 40447) while the player is in combat; TESCombatEvent for context.
// - Spell cast: TESSpellCastEvent with the player as caster.
//
// Body (G9b):
// - Damage taken: PlayerCharacter::HandleHealthDamage (208040, slot 0x104,
//   38606), which the actor-value change with a cause (38468) calls after
//   Health dropped (spike S4).
// - Disease recovery: a disease leaving the player's added spells, checked in
//   the Update tick; cure effects through TESMagicEffectApplyEvent.
// - Poison resistance: Actor::CheckResistance (the PlayerCharacter's
//   MagicTarget vtable 208048, slot 0xA, 38737). It returns the factor left
//   after PoisonResist (AV 40) for poisons, ResistMagic (44) otherwise, times
//   the effect's own resist AV; 1 means nothing was resisted.
// - Sprinting with Stamina use and overencumbered walking: the Update tick.
//
// Social (G9c):
// - Trade: TESContainerChangedEvent moving gold (form 0xF) to or from the
//   player while the barter menu is open.
// - Persuasion, intimidation, bribe: TESTrackedStatsEvent "Persuasions",
//   "Intimidations", "Bribes", which the vanilla FavorDialogueScript sends
//   through Game.IncrementStat; the target is the dialogue speaker that
//   MenuTopicManager holds.
// - Normal interactions: TESTopicInfoEvent while the dialogue menu is open,
//   and MenuOpenCloseEvent for the dialogue and barter menus.

#include "TrainingSpike.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
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
constexpr std::string_view kConfigPath = "Data/SKSE/Plugins/trials-of-mastery-stats-s6a.json";

constexpr REL::ID     kPlayerCharacterVtable{ 208040 };
constexpr REL::ID     kPlayerAnimationSinkVtable{ 208044 };
constexpr REL::ID     kPlayerMagicTargetVtable{ 208048 };
constexpr REL::ID     kReferenceAnimationEvent{ 37997 };
constexpr REL::ID     kPlayerUpdate{ 40447 };
constexpr REL::ID     kHandleHealthDamage{ 38606 };
constexpr REL::ID     kActorCheckResistance{ 38737 };
constexpr std::size_t kAnimationEventSlot = 1;
constexpr std::size_t kUpdateSlot = 0xAD;
constexpr std::size_t kHandleHealthDamageSlot = 0x104;
constexpr std::size_t kCheckResistanceSlot = 0xA;

constexpr RE::FormID kPlayerRefId = 0x14;
constexpr RE::FormID kGoldId = 0xF;

// Seconds between two ticks of the Update hook.
constexpr float kTickSeconds = 1.0F;
// A position change above this in one frame is a teleport, not walking.
constexpr float kTeleportDistance = 1000.0F;

// The default training of modules/stats/data/actions/_default.json. A
// linear curve with a = 0 gives b per action, whatever the input; the
// prototype calls each per-time action once a tick.
constexpr double kWeightAlpha = 0.665;
constexpr double kWeightLambda = 0.589;
constexpr double kTradeAlpha = 0.1;
constexpr double kTradeLambda = 0.5;
constexpr double kHitDexXp = 0.5;           // TODO(OQ-7)
constexpr double kFightingPerXp = 0.1;      // TODO(OQ-10)
constexpr double kDamageTakenXp = 0.2;      // CON and END each, TODO(OQ-7)
constexpr double kDiseaseRecoveryXp = 2.0;  // CON and END each, TODO(OQ-7)
constexpr double kPoisonResistedXp = 0.5;   // CON and END each, TODO(OQ-7)
constexpr double kSpellCastXp = 0.2;        // INT and WIS each, TODO(OQ-7)
constexpr double kSprintEndXp = 0.1;        // TODO(OQ-7)
constexpr double kEncumberedStrXp = 0.1;    // TODO(OQ-7)
constexpr double kPersuadeChaXp = 1.0;      // TODO(OQ-7)
constexpr double kInteractionChaXp = 1.0;

double power(double alpha, double lambda, double x)
{
	return x <= 0.0 ? 0.0 : alpha * std::pow(x, lambda);
}

double weightXp(double weight)
{
	return power(kWeightAlpha, kWeightLambda, weight);
}

// --- Configuration ----------------------------------------------------------

// The JSON file's fields of the same name. The hooks run on several threads,
// so they read the switches through atomics; only the Update tick (main
// thread) reads the file.
std::atomic<bool> g_combat{ true };
std::atomic<bool> g_body{ true };
std::atomic<bool> g_social{ true };
std::atomic<bool> g_verbose{ false };

std::optional<std::filesystem::file_time_type> g_configStamp;
bool                                           g_configLoaded = false;

void applyConfig(bool combat, bool body, bool social, bool verbose)
{
	g_combat = combat;
	g_body = body;
	g_social = social;
	g_verbose = verbose;
	SKSE::log::info("S6a config: combat {}, body {}, social {}, verbose {}", combat, body, social, verbose);
}

void readConfig()
{
	std::ifstream stream{ std::filesystem::path(kConfigPath) };
	if (!stream) {
		SKSE::log::warn("S6a: {} cannot be opened; every group on, verbose off", kConfigPath);
		applyConfig(true, true, true, false);
		return;
	}
	try {
		const auto file = nlohmann::json::parse(stream);
		applyConfig(file.value("combat", true), file.value("body", true), file.value("social", true),
			file.value("verbose", false));
	} catch (const nlohmann::json::exception& error) {
		SKSE::log::error("S6a: {} is not valid ({}); every group on, verbose off", kConfigPath, error.what());
		applyConfig(true, true, true, false);
	}
}

// Reads the file again when its modification time changed. Main thread only.
void refreshConfig()
{
	std::error_code error;
	const auto      time = std::filesystem::last_write_time(std::filesystem::path(kConfigPath), error);
	const auto      stamp = error ? std::nullopt : std::optional{ time };
	if (g_configLoaded && stamp == g_configStamp) {
		return;
	}
	g_configLoaded = true;
	g_configStamp = stamp;
	if (!stamp) {
		SKSE::log::warn("S6a: {} not found (game folder {}); every group on, verbose off", kConfigPath,
			std::filesystem::current_path().string());
		applyConfig(true, true, true, false);
		return;
	}
	readConfig();
}

// --- Logging helpers --------------------------------------------------------

std::uint32_t g_mainThread = 0;

std::string threadText()
{
	const auto thread = REX::W32::GetCurrentThreadId();
	return std::format("thread {}{}", thread, thread == g_mainThread ? " (main)" : "");
}

std::string formText(const RE::TESForm* form)
{
	if (form == nullptr) {
		return "none";
	}
	return std::format("{:08X} {} \"{}\"", form->GetFormID(), RE::FormTypeToString(form->GetFormType()),
		form->GetName());
}

std::string formIdText(RE::FormID id)
{
	if (id == 0) {
		return "none";
	}
	return formText(RE::TESForm::LookupByID(id));
}

std::string refText(RE::TESObjectREFR* ref)
{
	if (ref == nullptr) {
		return "none";
	}
	if (ref->IsPlayerRef()) {
		return "player";
	}
	return std::format("{:08X} \"{}\" (base {})", ref->GetFormID(), ref->GetDisplayFullName(),
		formText(ref->GetBaseObject()));
}

bool isPlayer(const RE::TESObjectREFR* ref)
{
	return ref != nullptr && ref->IsPlayerRef();
}

float playerValue(RE::ActorValue value)
{
	return RE::PlayerCharacter::GetSingleton()->AsActorValueOwner()->GetActorValue(value);
}

bool equalsNoCase(std::string_view left, std::string_view right)
{
	if (left.size() != right.size()) {
		return false;
	}
	for (std::size_t i = 0; i < left.size(); ++i) {
		const auto a = std::tolower(static_cast<unsigned char>(left[i]));
		const auto b = std::tolower(static_cast<unsigned char>(right[i]));
		if (a != b) {
			return false;
		}
	}
	return true;
}

std::string_view handName(bool leftHand)
{
	return leftHand ? "left" : "right";
}

RE::TESObjectWEAP* handWeapon(bool leftHand)
{
	auto* object = RE::PlayerCharacter::GetSingleton()->GetEquippedObject(leftHand);
	return object != nullptr ? object->As<RE::TESObjectWEAP>() : nullptr;
}

RE::TESObjectARMO* gloves()
{
	return RE::PlayerCharacter::GetSingleton()->GetWornArmor(RE::BGSBipedObjectForm::BipedObjectSlot::kHands);
}

std::string glovesText()
{
	auto* worn = gloves();
	if (worn == nullptr) {
		return "no gloves, weight 0";
	}
	return std::format("gloves {}, weight {:.2f}", formText(worn), worn->GetWeight());
}

double glovesStrXp()
{
	auto* worn = gloves();
	return worn != nullptr ? weightXp(worn->GetWeight()) : 0.0;
}

bool isUnarmed(const RE::TESObjectWEAP* weapon)
{
	return weapon == nullptr || weapon->IsHandToHandMelee();
}

// Speaker of the current or last dialogue line.
RE::TESObjectREFR* dialogueSpeaker()
{
	auto* topics = RE::MenuTopicManager::GetSingleton();
	if (topics == nullptr) {
		return nullptr;
	}
	if (auto speaker = topics->speaker.get()) {
		return speaker.get();
	}
	return topics->lastSpeaker.get().get();
}

std::string targetText(RE::TESObjectREFR* ref)
{
	auto* actor = ref != nullptr ? ref->As<RE::Actor>() : nullptr;
	if (actor == nullptr) {
		return refText(ref);
	}
	return std::format("{}, level {}, Speech {:.0f}, player level {}", refText(ref), actor->GetLevel(),
		actor->AsActorValueOwner()->GetActorValue(RE::ActorValue::kSpeech),
		RE::PlayerCharacter::GetSingleton()->GetLevel());
}

// --- Vtable hooks -----------------------------------------------------------

constexpr std::array<std::uint8_t, 6> kTrampolineJump{ 0xFF, 0x25, 0x00, 0x00, 0x00, 0x00 };
constexpr std::uint8_t                kJumpRel32Opcode = 0xE9;
constexpr std::size_t                 kJumpRel32Length = 5;

// Follows SKSE trampoline jumps and rel32 jumps to the function they lead to.
std::uintptr_t followJumps(std::uintptr_t address)
{
	constexpr int kMaxHops = 4;
	for (int hop = 0; hop < kMaxHops; ++hop) {
		const auto* code = reinterpret_cast<const std::uint8_t*>(address);
		if (code[0] == kJumpRel32Opcode) {
			std::int32_t displacement = 0;
			std::memcpy(&displacement, code + 1, sizeof(displacement));
			address += kJumpRel32Length + static_cast<std::intptr_t>(displacement);
			continue;
		}
		if (std::memcmp(code, kTrampolineJump.data(), kTrampolineJump.size()) != 0) {
			return address;
		}
		std::memcpy(&address, code + kTrampolineJump.size(), sizeof(address));
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

// Replaces a vtable slot and returns what it held, which the hook calls. A
// slot another plugin already took over is chained the same way; the log
// says whose function it was.
template <class F>
F hookVtableSlot(std::string_view name, REL::ID vtableId, std::size_t slot, REL::ID vanilla, F hook)
{
	REL::Relocation<std::uintptr_t> vtable{ vtableId };
	const auto                      previous = *reinterpret_cast<const std::uintptr_t*>(
		vtable.address() + sizeof(void*) * slot);
	if (previous == vanilla.address()) {
		SKSE::log::info("S6a {}: vtable {} slot {:#x} holds the vanilla {} ({:#x}); hooked", name, vtableId.id(), slot,
			vanilla.id(), previous);
	} else {
		const auto destination = followJumps(previous);
		SKSE::log::warn("S6a {}: vtable {} slot {:#x} holds {:#x} -> {:#x} {}, not the vanilla {} ({:#x}); chained",
			name, vtableId.id(), slot, previous, destination, moduleText(destination), vanilla.id(),
			vanilla.address());
	}
	return reinterpret_cast<F>(vtable.write_vfunc(slot, hook));
}

// --- Combat: animation events (swing, bow draw, crossbow load, staff) -------

enum class AnimKind
{
	kSwing,
	kBowDraw,
	kBowRelease,
	kCrossbowLoad,
	kSpellFire,
};

struct AnimTag
{
	std::string_view tag;
	AnimKind         kind;
	bool             leftHand;
};

constexpr std::array kAnimTags{
	AnimTag{ "weaponSwing", AnimKind::kSwing, false },
	AnimTag{ "weaponLeftSwing", AnimKind::kSwing, true },
	AnimTag{ "bowDrawStart", AnimKind::kBowDraw, false },
	AnimTag{ "bowDrawn", AnimKind::kBowDraw, false },
	AnimTag{ "arrowRelease", AnimKind::kBowRelease, false },
	AnimTag{ "BowRelease", AnimKind::kBowRelease, false },
	AnimTag{ "reloadStart", AnimKind::kCrossbowLoad, false },
	AnimTag{ "reloadStop", AnimKind::kCrossbowLoad, false },
	AnimTag{ "MRh_SpellFire_Event", AnimKind::kSpellFire, false },
	AnimTag{ "MLh_SpellFire_Event", AnimKind::kSpellFire, true },
};

std::optional<AnimTag> findAnimTag(std::string_view tag)
{
	for (const auto& entry : kAnimTags) {
		if (equalsNoCase(entry.tag, tag)) {
			return entry;
		}
	}
	return std::nullopt;
}

void logSwing(const AnimTag& tag)
{
	auto* weapon = handWeapon(tag.leftHand);
	if (isUnarmed(weapon)) {
		SKSE::log::info(
			"S6a combat WeaponSwing ({}, {} hand): unarmed ({}), {} -> STR {:.3f} if unarmed STR came per swing "
			"(the table gives it per hit, OQ-9); {}",
			tag.tag, handName(tag.leftHand), formText(weapon), glovesText(), glovesStrXp(), threadText());
		return;
	}
	if (weapon->IsBow() || weapon->IsCrossbow() || weapon->IsStaff()) {
		SKSE::log::info("S6a combat WeaponSwing ({}, {} hand): {} is a bow, crossbow or staff; not a swing; {}",
			tag.tag, handName(tag.leftHand), formText(weapon), threadText());
		return;
	}
	SKSE::log::info("S6a combat WeaponSwing ({}, {} hand): {}, weight W = {:.2f} -> STR {:.3f}; {}", tag.tag,
		handName(tag.leftHand), formText(weapon), weapon->GetWeight(), weightXp(weapon->GetWeight()), threadText());
}

void logRangedDraw(const AnimTag& tag, std::string_view action)
{
	auto* weapon = handWeapon(false);
	if (weapon == nullptr || !(weapon->IsBow() || weapon->IsCrossbow())) {
		SKSE::log::info("S6a combat {} ({}): no bow or crossbow in the right hand ({}); {}", action, tag.tag,
			formText(weapon), threadText());
		return;
	}
	const double weight = weapon->GetWeight();
	const double damage = weapon->GetAttackDamage();
	SKSE::log::info(
		"S6a combat {} ({}): {} {}, weight {:.2f} + damage {:.0f} = W {:.2f} -> STR {:.3f}; arrows {}; {}", action,
		tag.tag, weapon->IsCrossbow() ? "crossbow" : "bow", formText(weapon), weight, damage, weight + damage,
		weightXp(weight + damage), formText(RE::PlayerCharacter::GetSingleton()->GetCurrentAmmo()), threadText());
}

void logSpellFire(const AnimTag& tag)
{
	auto* weapon = handWeapon(tag.leftHand);
	if (weapon == nullptr || !weapon->IsStaff()) {
		SKSE::log::info(
			"S6a combat spell fire ({}, {} hand): {} (no staff; the cast itself is logged by "
			"TESSpellCastEvent); {}",
			tag.tag, handName(tag.leftHand), formText(RE::PlayerCharacter::GetSingleton()->GetEquippedObject(tag.leftHand)),
			threadText());
		return;
	}
	const auto charge = tag.leftHand ? RE::ActorValue::kLeftItemCharge : RE::ActorValue::kRightItemCharge;
	SKSE::log::info(
		"S6a combat StaffUse candidate ({}, {} hand): staff {}, enchantment {}, weight W = {:.2f} -> INT {:.3f}; "
		"charge AV {:.2f}; {}",
		tag.tag, handName(tag.leftHand), formText(weapon), formText(weapon->formEnchanting), weapon->GetWeight(),
		weightXp(weapon->GetWeight()), playerValue(charge), threadText());
}

void onAnimationEvent(const RE::BSAnimationGraphEvent& event)
{
	const std::string_view tagName{ event.tag.c_str() };
	if (g_verbose) {
		SKSE::log::info("S6a anim: tag \"{}\", payload \"{}\", holder {}; {}", tagName, event.payload.c_str(),
			refText(event.holder), threadText());
	}
	const auto tag = findAnimTag(tagName);
	if (!tag || !g_combat) {
		return;
	}
	switch (tag->kind) {
	case AnimKind::kSwing:
		logSwing(*tag);
		return;
	case AnimKind::kBowDraw:
	case AnimKind::kCrossbowLoad:
		logRangedDraw(*tag, tag->kind == AnimKind::kBowDraw ? "BowDraw candidate" : "CrossbowLoad candidate");
		return;
	case AnimKind::kBowRelease:
		logRangedDraw(*tag, "bow release (context)");
		return;
	case AnimKind::kSpellFire:
		logSpellFire(*tag);
		return;
	}
}

using AnimationEvent = RE::BSEventNotifyControl (*)(RE::BSTEventSink<RE::BSAnimationGraphEvent>*,
	const RE::BSAnimationGraphEvent*, RE::BSTEventSource<RE::BSAnimationGraphEvent>*);
AnimationEvent g_animationEvent = nullptr;

RE::BSEventNotifyControl animationEvent(RE::BSTEventSink<RE::BSAnimationGraphEvent>* sink,
	const RE::BSAnimationGraphEvent* event, RE::BSTEventSource<RE::BSAnimationGraphEvent>* source)
{
	if (event != nullptr) {
		onAnimationEvent(*event);
	}
	return g_animationEvent(sink, event, source);
}

// --- Body: damage taken and poison resistance -------------------------------

using HandleHealthDamage = void (*)(RE::Actor*, RE::Actor*, float);
HandleHealthDamage g_handleHealthDamage = nullptr;

void handleHealthDamage(RE::Actor* actor, RE::Actor* attacker, float damage)
{
	g_handleHealthDamage(actor, attacker, damage);
	if (!g_body) {
		return;
	}
	SKSE::log::info("S6a body DamageTaken: {} from {}, Health now {:.2f} -> CON {:.2f}, END {:.2f}; {}", damage,
		refText(attacker), playerValue(RE::ActorValue::kHealth), kDamageTakenXp, kDamageTakenXp, threadText());
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

void logResistance(RE::MagicItem* item, RE::Effect* effect, RE::TESBoundObject* object, float result)
{
	auto*      setting = effect != nullptr ? effect->baseEffect : nullptr;
	const auto resist = setting != nullptr ? setting->data.resistVariable : RE::ActorValue::kNone;
	const auto type = item != nullptr ? item->GetSpellType() : RE::MagicSystem::SpellType::kSpell;
	const bool poison = (item != nullptr && type == RE::MagicSystem::SpellType::kPoison) ||
	                    resist == RE::ActorValue::kPoisonResist;
	if (!poison && !g_verbose) {
		return;
	}
	const bool resisted = result < 1.0F;
	const auto action = poison && resisted ? "PoisonResisted" : "resistance check";
	SKSE::log::info(
		"S6a body {}: item {} (spell type {}), effect {} (resist {}), source {}, factor {:.3f} ({:.0f} % "
		"resisted); PoisonResist {:.0f}, ResistMagic {:.0f}{}; {}",
		action, formText(item), static_cast<std::int32_t>(type), formText(setting), avName(resist), formText(object),
		result, (1.0F - result) * 100.0F, playerValue(RE::ActorValue::kPoisonResist),
		playerValue(RE::ActorValue::kResistMagic),
		poison && resisted ? std::format(" -> CON {:.2f}, END {:.2f}", kPoisonResistedXp, kPoisonResistedXp) : "",
		threadText());
}

using CheckResistance = float (*)(RE::MagicTarget*, RE::MagicItem*, RE::Effect*, RE::TESBoundObject*);
CheckResistance g_checkResistance = nullptr;

float checkResistance(RE::MagicTarget* target, RE::MagicItem* item, RE::Effect* effect, RE::TESBoundObject* object)
{
	const float result = g_checkResistance(target, item, effect, object);
	if (g_body) {
		logResistance(item, effect, object, result);
	}
	return result;
}

// --- Update tick: fighting, sprinting, overencumbered walking, diseases -----

struct TickState
{
	float                   sinceTick = 0.0F;
	bool                    inCombat = false;
	float                   combatSeconds = 0.0F;
	bool                    sprinting = false;
	float                   sprintSeconds = 0.0F;
	float                   sprintStartStamina = 0.0F;
	float                   tickStamina = 0.0F;
	RE::NiPoint3            lastPosition;
	float                   encumberedDistance = 0.0F;
	float                   encumberedSeconds = 0.0F;
	bool                    diseasesKnown = false;
	std::vector<RE::FormID> diseases;
};

// Main thread only: PlayerCharacter::Update runs there.
TickState g_tick;

void trackSprint(RE::PlayerCharacter& player, float delta)
{
	const bool  sprinting = player.AsActorState()->IsSprinting();
	const float stamina = playerValue(RE::ActorValue::kStamina);
	if (sprinting && !g_tick.sprinting) {
		g_tick.sprintSeconds = 0.0F;
		g_tick.sprintStartStamina = stamina;
		g_tick.tickStamina = stamina;
		SKSE::log::info("S6a body sprint start: Stamina {:.2f}; {}", stamina, threadText());
	}
	if (!sprinting && g_tick.sprinting) {
		SKSE::log::info("S6a body sprint end: {:.2f} s, Stamina {:.2f} -> {:.2f} (spent {:.2f}); {}",
			g_tick.sprintSeconds, g_tick.sprintStartStamina, stamina, g_tick.sprintStartStamina - stamina,
			threadText());
	}
	g_tick.sprinting = sprinting;
	if (sprinting) {
		g_tick.sprintSeconds += delta;
	}
}

void trackEncumbrance(RE::PlayerCharacter& player, float delta)
{
	const auto position = player.GetPosition();
	const auto moved = position.GetDistance(g_tick.lastPosition);
	g_tick.lastPosition = position;
	if (!player.IsOverEncumbered() || moved > kTeleportDistance) {
		return;
	}
	g_tick.encumberedDistance += moved;
	g_tick.encumberedSeconds += delta;
}

void tickSprint()
{
	if (!g_tick.sprinting) {
		return;
	}
	const float stamina = playerValue(RE::ActorValue::kStamina);
	const float spent = g_tick.tickStamina - stamina;
	g_tick.tickStamina = stamina;
	SKSE::log::info("S6a body Sprint tick: {:.2f} s sprinted so far, Stamina spent this tick {:.2f} (now {:.2f}){}; {}",
		g_tick.sprintSeconds, spent, stamina,
		spent > 0.0F ? std::format(" -> END {:.2f}", kSprintEndXp) : " -> no XP, no Stamina used", threadText());
}

void tickEncumbrance(RE::PlayerCharacter& player)
{
	if (g_tick.encumberedSeconds <= 0.0F) {
		return;
	}
	auto* state = player.AsActorState();
	SKSE::log::info(
		"S6a body OverencumberedWalk tick: moved {:.1f} units in {:.2f} s overencumbered (walking {}, running {}, "
		"moving {}); inventory weight {:.1f}, carry weight {:.1f}, GetTotalCarryWeight {:.1f}{}; {}",
		g_tick.encumberedDistance, g_tick.encumberedSeconds, state->IsWalking(), state->IsRunning(), player.IsMoving(),
		playerValue(RE::ActorValue::kInventoryWeight), playerValue(RE::ActorValue::kCarryWeight),
		player.GetTotalCarryWeight(),
		g_tick.encumberedDistance > 0.0F ? std::format(" -> STR {:.2f}", kEncumberedStrXp) : " -> no XP, not moving",
		threadText());
	g_tick.encumberedDistance = 0.0F;
	g_tick.encumberedSeconds = 0.0F;
}

void tickCombat(RE::PlayerCharacter& player)
{
	const bool inCombat = player.IsInCombat();
	if (inCombat != g_tick.inCombat) {
		SKSE::log::info("S6a combat Fighting {}: {:.0f} s in combat so far; {}", inCombat ? "begins" : "ends",
			g_tick.combatSeconds, threadText());
		g_tick.inCombat = inCombat;
	}
	if (!inCombat) {
		return;
	}
	g_tick.combatSeconds += kTickSeconds;
	SKSE::log::info("S6a combat Fighting tick: {:.0f} s in combat -> PER {:.2f}; {}", g_tick.combatSeconds,
		kFightingPerXp, threadText());
}

std::vector<RE::FormID> playerDiseases(RE::PlayerCharacter& player)
{
	std::vector<RE::FormID> found;
	for (auto* spell : player.GetActorRuntimeData().addedSpells) {
		if (spell != nullptr && spell->GetSpellType() == RE::MagicSystem::SpellType::kDisease) {
			found.push_back(spell->GetFormID());
		}
	}
	return found;
}

bool contains(const std::vector<RE::FormID>& ids, RE::FormID id)
{
	return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void tickDiseases(RE::PlayerCharacter& player)
{
	auto current = playerDiseases(player);
	for (const auto id : current) {
		if (!contains(g_tick.diseases, id)) {
			SKSE::log::info("S6a body disease {}: {}; {}", g_tick.diseasesKnown ? "contracted" : "present",
				formIdText(id), threadText());
		}
	}
	for (const auto id : g_tick.diseases) {
		if (!contains(current, id)) {
			SKSE::log::info("S6a body DiseaseRecovery: {} left the added spells -> CON {:.2f}, END {:.2f}; {}",
				formIdText(id), kDiseaseRecoveryXp, kDiseaseRecoveryXp, threadText());
		}
	}
	g_tick.diseases = std::move(current);
	g_tick.diseasesKnown = true;
}

void onPlayerUpdate(RE::PlayerCharacter& player, float delta)
{
	if (g_body) {
		trackSprint(player, delta);
		trackEncumbrance(player, delta);
	}
	g_tick.sinceTick += delta;
	if (g_tick.sinceTick < kTickSeconds) {
		return;
	}
	g_tick.sinceTick = 0.0F;
	refreshConfig();
	if (g_combat) {
		tickCombat(player);
	}
	if (!g_body) {
		return;
	}
	tickSprint();
	tickEncumbrance(player);
	tickDiseases(player);
}

using PlayerUpdate = void (*)(RE::PlayerCharacter*, float);
PlayerUpdate g_playerUpdate = nullptr;

void playerUpdate(RE::PlayerCharacter* player, float delta)
{
	g_playerUpdate(player, delta);
	onPlayerUpdate(*player, delta);
}

// --- Event sinks ------------------------------------------------------------

// The conditions of a topic info that look like a speech check.
std::string speechConditions(const RE::TESTopicInfo& info)
{
	using Function = RE::FUNCTION_DATA::FunctionID;
	std::string text;
	for (auto* item = info.objConditions.head; item != nullptr; item = item->next) {
		const auto function = item->data.functionData.function.get();
		const auto parameter = reinterpret_cast<std::uintptr_t>(item->data.functionData.params[0]);
		const bool speechValue = function == Function::kGetActorValue &&
		                         parameter == static_cast<std::uintptr_t>(RE::ActorValue::kSpeech);
		if (speechValue || function == Function::kGetIntimidateSuccess || function == Function::kGetBribeSuccess) {
			text += std::format("{}function {}{}", text.empty() ? "" : ", ", static_cast<std::int32_t>(function),
				speechValue ? " (GetActorValue Speech)" : "");
		}
	}
	return text.empty() ? "none" : text;
}

std::string lastChoiceText()
{
	auto* topics = RE::MenuTopicManager::GetSingleton();
	if (topics == nullptr || topics->lastSelectedDialogue == nullptr) {
		return "none";
	}
	const auto* choice = topics->lastSelectedDialogue;
	return std::format("\"{}\" (topic {}, info {})", choice->topicText.c_str(), formText(choice->parentTopic),
		formText(choice->parentTopicInfo));
}

class Sinks final :
    public RE::BSTEventSink<RE::TESHitEvent>,
    public RE::BSTEventSink<RE::TESSpellCastEvent>,
    public RE::BSTEventSink<RE::TESCombatEvent>,
    public RE::BSTEventSink<RE::TESMagicEffectApplyEvent>,
    public RE::BSTEventSink<RE::TESActiveEffectApplyRemoveEvent>,
    public RE::BSTEventSink<RE::TESContainerChangedEvent>,
    public RE::BSTEventSink<RE::TESTrackedStatsEvent>,
    public RE::BSTEventSink<RE::TESTopicInfoEvent>,
    public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	using Control = RE::BSEventNotifyControl;

	Control ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*) override;
	Control ProcessEvent(const RE::TESSpellCastEvent* event, RE::BSTEventSource<RE::TESSpellCastEvent>*) override;
	Control ProcessEvent(const RE::TESCombatEvent* event, RE::BSTEventSource<RE::TESCombatEvent>*) override;
	Control ProcessEvent(const RE::TESMagicEffectApplyEvent* event,
		RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*) override;
	Control ProcessEvent(const RE::TESActiveEffectApplyRemoveEvent* event,
		RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>*) override;
	Control ProcessEvent(const RE::TESContainerChangedEvent* event,
		RE::BSTEventSource<RE::TESContainerChangedEvent>*) override;
	Control ProcessEvent(const RE::TESTrackedStatsEvent* event, RE::BSTEventSource<RE::TESTrackedStatsEvent>*) override;
	Control ProcessEvent(const RE::TESTopicInfoEvent* event, RE::BSTEventSource<RE::TESTopicInfoEvent>*) override;
	Control ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override;
};

Sinks::Control Sinks::ProcessEvent(const RE::TESHitEvent* event, RE::BSTEventSource<RE::TESHitEvent>*)
{
	if (event == nullptr || !g_combat || !isPlayer(event->cause.get())) {
		return Control::kContinue;
	}
	auto*       source = RE::TESForm::LookupByID(event->source);
	auto*       weapon = source != nullptr ? source->As<RE::TESObjectWEAP>() : nullptr;
	const auto  flags = event->flags.underlying();
	const auto  target = refText(event->target.get());
	const auto  projectile = formIdText(event->projectile);
	const bool  unarmed = weapon != nullptr && weapon->IsHandToHandMelee();
	const auto* action = unarmed ? "UnarmedHit" : "Hit";
	SKSE::log::info("S6a combat {}: target {}, source {}, projectile {}, flags {:#x} -> DEX {:.2f}{}; {}", action,
		target, formText(source), projectile, flags, kHitDexXp,
		unarmed ? std::format(", STR {:.3f} ({})", glovesStrXp(), glovesText()) : "", threadText());
	return Control::kContinue;
}

void logStaffCast(RE::MagicItem& item, bool leftHand, RE::TESObjectWEAP& staff)
{
	const auto charge = leftHand ? RE::ActorValue::kLeftItemCharge : RE::ActorValue::kRightItemCharge;
	SKSE::log::info(
		"S6a combat StaffUse (TESSpellCastEvent): {} from staff {} in the {} hand, weight W = {:.2f} -> INT {:.3f}; "
		"charge AV {:.2f}; {}",
		formText(&item), formText(&staff), handName(leftHand), staff.GetWeight(), weightXp(staff.GetWeight()),
		playerValue(charge), threadText());
}

Sinks::Control Sinks::ProcessEvent(const RE::TESSpellCastEvent* event, RE::BSTEventSource<RE::TESSpellCastEvent>*)
{
	if (event == nullptr || !g_combat || !isPlayer(event->object.get())) {
		return Control::kContinue;
	}
	auto* item = RE::TESForm::LookupByID<RE::MagicItem>(event->spell);
	if (item == nullptr) {
		SKSE::log::info("S6a combat cast of {:08X}, not a magic item; {}", event->spell, threadText());
		return Control::kContinue;
	}
	for (const bool leftHand : { true, false }) {
		auto* weapon = handWeapon(leftHand);
		if (weapon != nullptr && weapon->IsStaff() && weapon->formEnchanting == item) {
			logStaffCast(*item, leftHand, *weapon);
			return Control::kContinue;
		}
	}
	const auto type = item->GetSpellType();
	if (type != RE::MagicSystem::SpellType::kSpell) {
		SKSE::log::info("S6a combat cast of {} (spell type {}, not a spell or staff); {}", formText(item),
			static_cast<std::int32_t>(type), threadText());
		return Control::kContinue;
	}
	auto* player = RE::PlayerCharacter::GetSingleton();
	SKSE::log::info(
		"S6a combat SpellCast: {}, casting type {}, Magicka cost {:.1f}, Magicka now {:.1f} -> INT {:.2f}, WIS "
		"{:.2f}; {}",
		formText(item), static_cast<std::int32_t>(item->GetCastingType()), item->CalculateMagickaCost(player),
		playerValue(RE::ActorValue::kMagicka), kSpellCastXp, kSpellCastXp, threadText());
	return Control::kContinue;
}

Sinks::Control Sinks::ProcessEvent(const RE::TESCombatEvent* event, RE::BSTEventSource<RE::TESCombatEvent>*)
{
	if (event == nullptr || !g_combat) {
		return Control::kContinue;
	}
	if (!isPlayer(event->actor.get()) && !isPlayer(event->targetActor.get())) {
		return Control::kContinue;
	}
	SKSE::log::info("S6a combat TESCombatEvent: {} -> {}, new state {}; player in combat {}; {}",
		refText(event->actor.get()), refText(event->targetActor.get()), static_cast<std::int32_t>(event->newState.get()),
		RE::PlayerCharacter::GetSingleton()->IsInCombat(), threadText());
	return Control::kContinue;
}

Sinks::Control Sinks::ProcessEvent(const RE::TESMagicEffectApplyEvent* event,
	RE::BSTEventSource<RE::TESMagicEffectApplyEvent>*)
{
	if (event == nullptr || !g_body || !isPlayer(event->target.get())) {
		return Control::kContinue;
	}
	auto*      setting = RE::TESForm::LookupByID<RE::EffectSetting>(event->magicEffect);
	const auto archetype = setting != nullptr ? setting->GetArchetype() : RE::EffectSetting::Archetype::kNone;
	const bool cure = archetype == RE::EffectSetting::Archetype::kCureDisease || archetype == RE::EffectSetting::Archetype::kCurePoison;
	if (!cure && !g_verbose) {
		return Control::kContinue;
	}
	SKSE::log::info("S6a body magic effect applied to the player: {} (archetype {}{}) from {}; {}", formText(setting),
		static_cast<std::int32_t>(archetype), cure ? ", a cure" : "", refText(event->caster.get()), threadText());
	return Control::kContinue;
}

Sinks::Control Sinks::ProcessEvent(const RE::TESActiveEffectApplyRemoveEvent* event,
	RE::BSTEventSource<RE::TESActiveEffectApplyRemoveEvent>*)
{
	if (event == nullptr || !g_body || !g_verbose || !isPlayer(event->target.get())) {
		return Control::kContinue;
	}
	std::string effectText = "not in the list";
	RE::PlayerCharacter::GetSingleton()->AsMagicTarget()->VisitActiveEffects([&](RE::ActiveEffect* effect) {
		if (effect == nullptr || effect->usUniqueID != event->activeEffectUniqueID) {
			return RE::BSContainer::ForEachResult::kContinue;
		}
		effectText = std::format("spell {} (type {}), effect {}", formText(effect->spell),
			effect->spell != nullptr ? static_cast<std::int32_t>(effect->spell->GetSpellType()) : -1,
			formText(effect->GetBaseObject()));
		return RE::BSContainer::ForEachResult::kStop;
	});
	SKSE::log::info("S6a body active effect {} {} on the player: {}, caster {}; {}", event->activeEffectUniqueID,
		event->isApplied ? "applied" : "removed", effectText, refText(event->caster.get()), threadText());
	return Control::kContinue;
}

Sinks::Control Sinks::ProcessEvent(const RE::TESContainerChangedEvent* event,
	RE::BSTEventSource<RE::TESContainerChangedEvent>*)
{
	if (event == nullptr || !g_social) {
		return Control::kContinue;
	}
	if (event->oldContainer != kPlayerRefId && event->newContainer != kPlayerRefId) {
		return Control::kContinue;
	}
	const bool barter = RE::UI::GetSingleton()->IsMenuOpen(RE::BarterMenu::MENU_NAME);
	const bool toPlayer = event->newContainer == kPlayerRefId;
	const auto other = toPlayer ? event->oldContainer : event->newContainer;
	if (event->baseObj == kGoldId && barter) {
		const double money = event->itemCount;
		SKSE::log::info(
			"S6a social Trade: player {} {} septims, other side {} (speaker {}); player gold now {} -> CHA {:.3f}; "
			"{}",
			toPlayer ? "receives" : "pays", event->itemCount, formIdText(other), refText(dialogueSpeaker()),
			RE::PlayerCharacter::GetSingleton()->GetGoldAmount(), power(kTradeAlpha, kTradeLambda, money),
			threadText());
		return Control::kContinue;
	}
	if (!barter && !g_verbose) {
		return Control::kContinue;
	}
	SKSE::log::info("S6a social item {} the player: {} x{}, other side {}, barter menu {}; player gold {}; {}",
		toPlayer ? "to" : "from", formIdText(event->baseObj), event->itemCount, formIdText(other), barter,
		RE::PlayerCharacter::GetSingleton()->GetGoldAmount(), threadText());
	return Control::kContinue;
}

Sinks::Control Sinks::ProcessEvent(const RE::TESTrackedStatsEvent* event,
	RE::BSTEventSource<RE::TESTrackedStatsEvent>*)
{
	if (event == nullptr || !g_social) {
		return Control::kContinue;
	}
	const std::string_view stat{ event->stat.c_str() };
	const auto*            action = equalsNoCase(stat, "Persuasions")   ? "Persuade" :
	                                equalsNoCase(stat, "Intimidations") ? "Intimidate" :
	                                equalsNoCase(stat, "Bribes")        ? "Bribe" :
	                                                                      nullptr;
	if (action == nullptr) {
		SKSE::log::info("S6a social tracked stat \"{}\" = {}; {}", stat, event->value, threadText());
		return Control::kContinue;
	}
	SKSE::log::info(
		"S6a social {}: tracked stat \"{}\" = {}, target {}, last choice {} -> CHA {:.2f} "
		"(amount by target open, OQ-7); {}",
		action, stat, event->value, targetText(dialogueSpeaker()), lastChoiceText(), kPersuadeChaXp, threadText());
	return Control::kContinue;
}

Sinks::Control Sinks::ProcessEvent(const RE::TESTopicInfoEvent* event, RE::BSTEventSource<RE::TESTopicInfoEvent>*)
{
	if (event == nullptr || !g_social) {
		return Control::kContinue;
	}
	const bool begin = event->type.get() == RE::TESTopicInfoEvent::TopicInfoEventType::kTopicBegin;
	const bool dialogue = RE::UI::GetSingleton()->IsMenuOpen(RE::DialogueMenu::MENU_NAME);
	if (!g_verbose && (!begin || !dialogue)) {
		return Control::kContinue;
	}
	auto*      info = RE::TESForm::LookupByID<RE::TESTopicInfo>(event->topicInfoFormID);
	auto*      topic = info != nullptr ? info->parentTopic : nullptr;
	const auto subtype = topic != nullptr ? static_cast<std::int32_t>(topic->data.subtype.get()) : -1;
	SKSE::log::info(
		"S6a social Interaction candidate ({}, dialogue menu {}): speaker {}, info {}, topic {} subtype {}, speech "
		"conditions {}, last choice {}{}; {}",
		begin ? "begin" : "end", dialogue, refText(event->speakerRef.get()), formIdText(event->topicInfoFormID),
		formText(topic), subtype, info != nullptr ? speechConditions(*info) : "no info", lastChoiceText(),
		begin && dialogue ? std::format(" -> CHA {:.2f}", kInteractionChaXp) : "", threadText());
	return Control::kContinue;
}

Sinks::Control Sinks::ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*)
{
	if (event == nullptr || !g_social) {
		return Control::kContinue;
	}
	const std::string_view name{ event->menuName.c_str() };
	if (name != RE::DialogueMenu::MENU_NAME && name != RE::BarterMenu::MENU_NAME) {
		return Control::kContinue;
	}
	SKSE::log::info("S6a social {} {}: speaker {}; player gold {}; {}", name, event->opening ? "opened" : "closed",
		targetText(dialogueSpeaker()), RE::PlayerCharacter::GetSingleton()->GetGoldAmount(), threadText());
	return Control::kContinue;
}

Sinks g_sinks;

void registerSinks()
{
	auto* events = RE::ScriptEventSourceHolder::GetSingleton();
	events->AddEventSink<RE::TESHitEvent>(&g_sinks);
	events->AddEventSink<RE::TESSpellCastEvent>(&g_sinks);
	events->AddEventSink<RE::TESCombatEvent>(&g_sinks);
	events->AddEventSink<RE::TESMagicEffectApplyEvent>(&g_sinks);
	events->AddEventSink<RE::TESActiveEffectApplyRemoveEvent>(&g_sinks);
	events->AddEventSink<RE::TESContainerChangedEvent>(&g_sinks);
	events->AddEventSink<RE::TESTrackedStatsEvent>(&g_sinks);
	events->AddEventSink<RE::TESTopicInfoEvent>(&g_sinks);
	RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&g_sinks);
	SKSE::log::info(
		"S6a: event sinks registered (hit, spell cast, combat, magic effect apply, active effect "
		"apply/remove, container changed, tracked stats, topic info, menu open/close)");
}
}

void installTrainingSpike()
{
	g_mainThread = REX::W32::GetCurrentThreadId();
	SKSE::log::info("S6a: installing; {}", threadText());
	refreshConfig();
	g_animationEvent = hookVtableSlot("animation events", kPlayerAnimationSinkVtable, kAnimationEventSlot,
		kReferenceAnimationEvent, &animationEvent);
	g_playerUpdate = hookVtableSlot("PlayerCharacter::Update", kPlayerCharacterVtable, kUpdateSlot, kPlayerUpdate,
		&playerUpdate);
	g_handleHealthDamage = hookVtableSlot("HandleHealthDamage", kPlayerCharacterVtable, kHandleHealthDamageSlot,
		kHandleHealthDamage, &handleHealthDamage);
	g_checkResistance = hookVtableSlot("CheckResistance", kPlayerMagicTargetVtable, kCheckResistanceSlot,
		kActorCheckResistance, &checkResistance);
	registerSinks();
}
}
