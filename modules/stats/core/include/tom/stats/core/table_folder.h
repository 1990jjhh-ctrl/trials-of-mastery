#pragma once

// Reads a table folder from disk and merges it with mergeTable(). The folder
// layout and merge rules are defined in docs/PRD.md#4-shared-constraints.

#include "tom/stats/core/table_merge.h"

#include <filesystem>

namespace tom::stats
{

// Merges the .json files directly in `folder` (subfolders are not read).
// Folders or files that cannot be read are reported as InvalidFile.
[[nodiscard]] MergeResult loadTableFolder(const std::filesystem::path& folder, const PluginQuery& plugins,
	const nlohmann::json& registered = nlohmann::json::object());

}
