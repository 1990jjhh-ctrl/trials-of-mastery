#include "GameConfig.h"

#include "Service.h"

#include "tom/stats/core/config/configuration.h"
#include "tom/stats/core/stats_service.h"
#include "tom/stats/core/table_merge.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace tom::stats
{
namespace
{
// Install paths, relative to the game's folder, the working directory of the
// game (docs/stats/PLAN.md, assumption 2).
const std::filesystem::path kIniFile{ "Data/SKSE/Plugins/trials-of-mastery-stats.ini" };
const std::filesystem::path kTablesFolder{ "Data/SKSE/Plugins/trials-of-mastery/data/stats" };

// The player's FormID, which the service takes as its player.
constexpr ActorId kPlayer = 0x14;

// A plugin that the game did not load has this compile index; a light plugin
// has kLightCompileIndex and its own index among the light plugins.
constexpr std::uint8_t kNotLoaded = 0xFF;
constexpr std::uint8_t kLightCompileIndex = 0xFE;

// The game's loaded plugins, read once at kDataLoaded.
class GamePluginQuery final : public PluginQuery
{
public:
	explicit GamePluginQuery(std::vector<PluginInfo> plugins) :
		plugins_(std::move(plugins)) {}

	[[nodiscard]] std::vector<PluginInfo> activePlugins() const override { return plugins_; }

private:
	std::vector<PluginInfo> plugins_;
};

std::string indexText(const RE::TESFile& file)
{
	if (file.GetCompileIndex() == kLightCompileIndex) {
		return std::format("FE:{:03X}", file.GetSmallFileCompileIndex());
	}
	return std::format("{:02X}", file.GetCompileIndex());
}

// The indices the game gave the plugins seen so far. The indices of full and
// of light plugins each grow in load order, so each kind is checked against
// the last plugin of the same kind: full 02, light FE:000, then full 01 is
// out of order.
class IndexOrder
{
public:
	// Whether `file` comes after the plugins seen so far; then counts it in.
	bool follows(const RE::TESFile& file)
	{
		const bool light = file.GetCompileIndex() == kLightCompileIndex;
		const auto index = light ? std::uint32_t{ file.GetSmallFileCompileIndex() } : std::uint32_t{ file.GetCompileIndex() };
		auto&      last = light ? lastLight_ : lastFull_;
		const bool inOrder = !last || *last < index;
		last = index;
		return inOrder;
	}

private:
	std::optional<std::uint32_t> lastFull_;
	std::optional<std::uint32_t> lastLight_;
};

// The loaded plugins in load order: the data handler's file list holds every
// plugin file the game knows, in load order, and an unloaded one has no
// compile index. The list and the indices are logged, so the order can be
// checked against the load order of the mod manager. Not const: BSSimpleList
// has no working const iteration.
std::vector<PluginInfo> loadedPlugins(RE::TESDataHandler& dataHandler)
{
	std::vector<PluginInfo> plugins;
	std::string             listed;
	IndexOrder              order;
	for (const auto* file : dataHandler.files) {
		if (file == nullptr || file->GetCompileIndex() == kNotLoaded) {
			continue;
		}
		if (!order.follows(*file)) {
			SKSE::log::warn("Plugin {} [{}] comes before its index order in the file list", file->GetFilename(),
				indexText(*file));
		}
		plugins.push_back(splitPluginFileName(file->GetFilename()));
		listed += std::format("{}{} [{}]", listed.empty() ? "" : ", ", file->GetFilename(), indexText(*file));
	}
	SKSE::log::info("Active plugins in load order ({}): {}", plugins.size(), listed);
	return plugins;
}

// The editor IDs of the races the loaded plugins define (C3's buildTables).
std::set<std::string, std::less<>> raceEditorIds(RE::TESDataHandler& dataHandler)
{
	std::set<std::string, std::less<>> races;
	for (const auto* race : dataHandler.GetFormArray<RE::TESRace>()) {
		const char* editorId = race != nullptr ? race->GetFormEditorID() : nullptr;
		if (editorId != nullptr && *editorId != '\0') {
			races.emplace(editorId);
		}
	}
	SKSE::log::info("{} races with an editor ID", races.size());
	return races;
}

void log(const LogLine& line)
{
	switch (line.level) {
	case LogLevel::Debug:
		SKSE::log::debug("{}", line.text);
		return;
	case LogLevel::Info:
		SKSE::log::info("{}", line.text);
		return;
	case LogLevel::Warning:
		SKSE::log::warn("{}", line.text);
		return;
	case LogLevel::Error:
		SKSE::log::error("{}", line.text);
		return;
	}
}

// Builds the service in the order of docs/stats/PLAN.md, assumption 14: the
// tables, which hold the API-registered entries as their lowest layer, fixed
// at construction, then registerStat() for each API-registered stat. Until A1
// exists no entries are registered, so there is nothing to register.
std::unique_ptr<StatsService> buildService(Configuration configuration)
{
	if (!configuration.tables) {
		return nullptr;
	}
	return std::make_unique<StatsService>(configuration.settings.settings, std::move(*configuration.tables), kPlayer);
}

void load()
{
	auto* dataHandler = RE::TESDataHandler::GetSingleton();
	if (dataHandler == nullptr) {
		SKSE::log::error("No data handler; the configuration is not loaded and there is no stats service");
		return;
	}
	SKSE::log::info("Loading the configuration on thread {}", REX::W32::GetCurrentThreadId());
	const GamePluginQuery plugins{ loadedPlugins(*dataHandler) };
	auto                  configuration = loadConfiguration(kIniFile, kTablesFolder, plugins, raceEditorIds(*dataHandler));
	for (const auto& line : describe(configuration)) {
		log(line);
	}
	auto service = buildService(std::move(configuration));
	if (!service) {
		SKSE::log::error("The tables could not be built; there is no stats service this session");
		return;
	}
	if (installService(std::move(service))) {
		SKSE::log::info("Stats service built");
		SKSE::log::info(
			"TODO(OQ-1): placeholder NPC values, the default base plus the race adjustment until the API "
			"sets them");
	}
}
}

void loadConfigurationAndService()
{
	try {
		load();
	} catch (const std::exception& error) {
		SKSE::log::error("Loading the configuration failed: {}; there is no stats service this session", error.what());
	}
}
}
