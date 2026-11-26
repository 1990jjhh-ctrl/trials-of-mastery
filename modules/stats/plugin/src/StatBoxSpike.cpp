// THROWAWAY prototype of spike S2 (docs/stats/PLAN.md, Phase 3). It only
// answers the spike's questions; U1 replaces it.

#include "StatBoxSpike.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tom::stats
{
namespace
{
// Relative to the game folder, which is the game's working directory.
constexpr std::string_view kConfigPath = "Data/SKSE/Plugins/trials-of-mastery-stats-s2.json";

constexpr std::uint32_t kKey = RE::BSKeyboardDevice::Keys::kK;                // DIK 0x25
constexpr std::uint32_t kGamepadButton = RE::BSWin32GamepadDevice::Keys::kY;  // XInput 0x8000

// What the box shows; each field comes from the JSON file of the same name,
// or keeps this default.
struct SpikeConfig
{
	int         lines = 15;
	std::string lineText = "Intelligence (INT) {i} (40 / 100 XP)";
	std::string longLine;
	int         buttons = 3;
	std::string buttonText = "Constitution (CON) {i}";
	std::string extra = "Cost: \xE2\x88\x9E (U+221E)";  // UTF-8 of the infinity sign
	bool        hint = false;
	std::string hintText = "Trials of Mastery: press K (gamepad Y) for the stat box";
	// Set only when the file names them; otherwise the factory's values stay.
	std::optional<bool>         useHtml;
	std::optional<bool>         verticalButtons;
	std::optional<std::int32_t> menuDepth;
};

std::string replaceIndex(std::string text, int index)
{
	constexpr std::string_view placeholder = "{i}";
	const std::string          value = std::to_string(index);
	for (auto at = text.find(placeholder); at != std::string::npos; at = text.find(placeholder, at + value.size())) {
		text.replace(at, placeholder.size(), value);
	}
	return text;
}

template <class T>
void readField(const nlohmann::json& file, const char* name, T& field)
{
	if (file.contains(name)) {
		field = file.at(name).get<T>();
	}
}

template <class T>
void readField(const nlohmann::json& file, const char* name, std::optional<T>& field)
{
	if (file.contains(name)) {
		field = file.at(name).get<T>();
	}
}

SpikeConfig readConfig()
{
	SpikeConfig   config;
	std::ifstream stream{ std::filesystem::path(kConfigPath) };
	if (!stream) {
		SKSE::log::warn("S2: {} not found (game folder {}); using the defaults", kConfigPath,
			std::filesystem::current_path().string());
		return config;
	}
	try {
		const auto file = nlohmann::json::parse(stream);
		readField(file, "lines", config.lines);
		readField(file, "lineText", config.lineText);
		readField(file, "longLine", config.longLine);
		readField(file, "buttons", config.buttons);
		readField(file, "buttonText", config.buttonText);
		readField(file, "extra", config.extra);
		readField(file, "hint", config.hint);
		readField(file, "hintText", config.hintText);
		readField(file, "useHtml", config.useHtml);
		readField(file, "verticalButtons", config.verticalButtons);
		readField(file, "menuDepth", config.menuDepth);
	} catch (const nlohmann::json::exception& error) {
		SKSE::log::error("S2: {} is not valid ({}); using the defaults", kConfigPath, error.what());
		return SpikeConfig{};
	}
	return config;
}

std::string bodyText(const SpikeConfig& config)
{
	std::vector<std::string> parts;
	for (int i = 1; i <= config.lines; ++i) {
		parts.push_back(replaceIndex(config.lineText, i));
	}
	if (!config.longLine.empty()) {
		parts.push_back(config.longLine);
	}
	if (!config.extra.empty()) {
		parts.push_back(config.extra);
	}
	std::string text;
	for (const auto& part : parts) {
		if (!text.empty()) {
			text += '\n';
		}
		text += part;
	}
	return text;
}

std::vector<std::string> buttonLabels(const SpikeConfig& config)
{
	std::vector<std::string> labels;
	for (int i = 1; i <= config.buttons; ++i) {
		labels.push_back(replaceIndex(config.buttonText, i));
	}
	return labels;
}

class SpikeCallback final : public RE::IMessageBoxCallback
{
public:
	explicit SpikeCallback(std::vector<std::string> labels) :
		labels_(std::move(labels))
	{}

	void Run(std::uint8_t button) override
	{
		const std::string_view label = button < labels_.size() ? std::string_view(labels_[button]) : "out of range";
		SKSE::log::info("S2: box answered with button {} ({}); skills menu open: {}", button, label,
			RE::UI::GetSingleton()->IsMenuOpen(RE::StatsMenu::MENU_NAME));
	}

private:
	std::vector<std::string> labels_;
};

// Uses the engine's own MessageBoxData factory, then queues the data like the
// Papyrus Debug.MessageBox and Message.Show do. The data is the game's from
// then on, which destroys it when the box closes.
void showBox(const SpikeConfig& config)
{
	const auto* creator = RE::MessageDataFactoryManager::GetSingleton()->GetCreator<RE::MessageBoxData>(
		RE::InterfaceStrings::GetSingleton()->messageBoxData);
	auto* data = creator != nullptr ? creator->Create() : nullptr;
	if (data == nullptr) {
		SKSE::log::error("S2: no MessageBoxData from the factory");
		return;
	}
	SKSE::log::info(
		"S2: factory defaults: warningType {}, cancelButtonIndex {}, menuDepth {}, buttonPressOffset {}, "
		"useHtml {}, verticalButtons {}, isCancellable {}",
		data->warningType, data->cancelButtonIndex, data->menuDepth, data->buttonPressOffset, data->useHtml,
		data->verticalButtons, data->isCancellable);

	const auto text = bodyText(config);
	auto       labels = buttonLabels(config);
	data->bodyText = text.c_str();
	for (const auto& label : labels) {
		data->buttonText.push_back(RE::BSString(label.c_str()));
	}
	data->useHtml = config.useHtml.value_or(data->useHtml);
	data->verticalButtons = config.verticalButtons.value_or(data->verticalButtons);
	data->menuDepth = config.menuDepth.value_or(data->menuDepth);

	SKSE::log::info("S2: opening the box, {} body lines, {} bytes, useHtml {}, verticalButtons {}, menuDepth {}",
		config.lines, text.size(), data->useHtml, data->verticalButtons, data->menuDepth);
	SKSE::log::info("S2: body text:\n{}", text);
	for (std::size_t i = 0; i < labels.size(); ++i) {
		SKSE::log::info("S2: button {}: {}", i, labels[i]);
	}
	data->callback = RE::make_smart<SpikeCallback>(std::move(labels));
	RE::MessageBoxMenu::QueueMessage(data);
}

std::string_view deviceName(RE::INPUT_DEVICE device)
{
	switch (device) {
	case RE::INPUT_DEVICE::kKeyboard:
		return "keyboard";
	case RE::INPUT_DEVICE::kMouse:
		return "mouse";
	case RE::INPUT_DEVICE::kGamepad:
		return "gamepad";
	default:
		return "other";
	}
}

bool isTrigger(const RE::ButtonEvent& button)
{
	const auto device = button.GetDevice();
	const auto id = button.GetIDCode();
	return (device == RE::INPUT_DEVICE::kKeyboard && id == kKey) ||
	       (device == RE::INPUT_DEVICE::kGamepad && id == kGamepadButton);
}

void onButton(const RE::ButtonEvent& button)
{
	if (!button.IsDown() && !button.IsUp()) {
		return;
	}
	auto*      ui = RE::UI::GetSingleton();
	const bool statsOpen = ui->IsMenuOpen(RE::StatsMenu::MENU_NAME);
	const bool boxOpen = ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME);
	const bool trigger = isTrigger(button);
	if (!statsOpen) {
		if (trigger && button.IsDown()) {
			SKSE::log::info("S2: stat box button seen with the skills menu closed; ignored");
		}
		return;
	}
	SKSE::log::info("S2: skills menu open: {} {:#x} {}, user event '{}', stat box button {}, message box open {}",
		deviceName(button.GetDevice()), button.GetIDCode(), button.IsDown() ? "down" : "up",
		button.GetUserEvent().c_str(), trigger, boxOpen);
	if (!trigger || !button.IsDown()) {
		return;
	}
	if (boxOpen) {
		SKSE::log::info("S2: a message box is already open; not opening another");
		return;
	}
	showBox(readConfig());
}

class SpikeSink final :
    public RE::BSTEventSink<RE::InputEvent*>,
    public RE::BSTEventSink<RE::MenuOpenCloseEvent>
{
public:
	RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events,
		RE::BSTEventSource<RE::InputEvent*>*) override
	{
		if (events == nullptr) {
			return RE::BSEventNotifyControl::kContinue;
		}
		for (const auto* event = *events; event != nullptr; event = event->next) {
			if (const auto* button = event->AsButtonEvent()) {
				onButton(*button);
			}
		}
		return RE::BSEventNotifyControl::kContinue;
	}

	RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event,
		RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
	{
		if (event == nullptr) {
			return RE::BSEventNotifyControl::kContinue;
		}
		const std::string_view name{ event->menuName.c_str() };
		if (name != RE::StatsMenu::MENU_NAME && name != RE::MessageBoxMenu::MENU_NAME) {
			return RE::BSEventNotifyControl::kContinue;
		}
		SKSE::log::info("S2: {} {}", name, event->opening ? "opened" : "closed");
		if (name == RE::StatsMenu::MENU_NAME && event->opening) {
			showHint();
		}
		return RE::BSEventNotifyControl::kContinue;
	}

private:
	// ST-25 test: a HUD notification, as Papyrus Debug.Notification shows it.
	static void showHint()
	{
		const auto config = readConfig();
		if (!config.hint) {
			return;
		}
		SKSE::log::info("S2: hint shown: {}", config.hintText);
		RE::SendHUDMessage::ShowHUDMessage(config.hintText.c_str());
	}
};

SpikeSink g_sink;
}

void registerStatBoxSpike()
{
	RE::BSInputDeviceManager::GetSingleton()->AddEventSink<RE::InputEvent*>(&g_sink);
	RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(&g_sink);
	SKSE::log::info("S2: stat box spike registered, key {:#x}, gamepad button {:#x}, file {}", kKey,
		kGamepadButton, kConfigPath);
}
}
