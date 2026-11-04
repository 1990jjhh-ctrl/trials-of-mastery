#include "tom/stats/core/table_folder.h"

#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace tom::stats
{

namespace
{

bool isJsonFile(const std::filesystem::directory_entry& entry)
{
	std::error_code error;
	if (!entry.is_regular_file(error)) {
		return false;
	}
	auto extension = entry.path().extension().string();
	for (auto& c : extension) {
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}
	return extension == ".json";
}

std::optional<std::string> readText(const std::filesystem::path& file)
{
	std::ifstream stream(file, std::ios::binary);
	if (!stream) {
		return std::nullopt;
	}
	std::string text{ std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>() };
	if (stream.bad()) {
		return std::nullopt;
	}
	return text;
}

void addSource(const std::filesystem::directory_entry& entry, std::vector<TableSource>& sources, std::vector<Diagnostic>& readErrors)
{
	if (!isJsonFile(entry)) {
		return;
	}
	auto name = entry.path().filename().string();
	auto text = readText(entry.path());
	if (!text) {
		readErrors.push_back({ Diagnostic::Kind::InvalidFile, std::move(name), "", "cannot read file" });
		return;
	}
	sources.push_back({ std::move(name), std::move(*text) });
}

}

MergeResult loadTableFolder(const std::filesystem::path& folder, const PluginQuery& plugins, const nlohmann::json& registered)
{
	std::vector<TableSource> sources;
	std::vector<Diagnostic>  readErrors;
	std::error_code          error;
	for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
		addSource(*it, sources, readErrors);
	}
	if (error) {
		readErrors.push_back({ Diagnostic::Kind::InvalidFile, folder.string(), "", "cannot read folder: " + error.message() });
	}

	auto result = mergeTable(sources, plugins, registered);
	result.diagnostics.insert(result.diagnostics.begin(), readErrors.begin(), readErrors.end());
	return result;
}

}
