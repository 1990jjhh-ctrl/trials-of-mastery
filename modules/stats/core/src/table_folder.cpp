#include "tom/stats/core/table_folder.h"

#include "ascii_case.h"

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

// The path as UTF-8. path::string() converts to the ANSI code page on Windows
// and throws for names outside it; u8string() throws only for names that are
// not valid UTF-16.
std::string utf8(const std::filesystem::path& path)
{
	const auto text = path.u8string();
	return { text.begin(), text.end() };
}

bool isJsonFile(const std::filesystem::directory_entry& entry)
{
	std::error_code error;
	if (!entry.is_regular_file(error)) {
		return false;
	}
	return equalsIgnoreCase(utf8(entry.path().extension()), ".json");
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

// How diagnostics name a folder: relative to the table folder.
std::string folderName(TableSource::Folder kind)
{
	return kind == TableSource::Folder::Plugins ? std::string(kPluginsFolderName) : "";
}

// Collects the .json files of one folder, and the files and folders that
// cannot be read.
class FolderReader
{
public:
	FolderReader(std::vector<TableSource>& sources, std::vector<Diagnostic>& readErrors) :
		sources_(sources), readErrors_(readErrors) {}

	void read(const std::filesystem::path& folder, TableSource::Folder kind);

private:
	void add(const std::filesystem::directory_entry& entry, TableSource::Folder kind);
	void addFile(const std::filesystem::directory_entry& entry, TableSource::Folder kind);
	void fail(std::string file, std::string detail) { readErrors_.push_back({ Diagnostic::Kind::InvalidFile, std::move(file), "", std::move(detail) }); }

	std::vector<TableSource>& sources_;
	std::vector<Diagnostic>&  readErrors_;
};

void FolderReader::read(const std::filesystem::path& folder, TableSource::Folder kind)
{
	std::error_code error;
	for (std::filesystem::directory_iterator it(folder, error), end; !error && it != end; it.increment(error)) {
		add(*it, kind);
	}
	if (error) {
		fail(folderName(kind), "cannot read folder: " + error.message());
	}
}

void FolderReader::add(const std::filesystem::directory_entry& entry, TableSource::Folder kind)
{
	try {
		addFile(entry, kind);
	} catch (const std::system_error& error) {
		fail(folderName(kind), std::string("a file name is not valid Unicode: ") + error.what());
	}
}

void FolderReader::addFile(const std::filesystem::directory_entry& entry, TableSource::Folder kind)
{
	if (!isJsonFile(entry)) {
		return;
	}
	TableSource source{ utf8(entry.path().filename()), "", kind };
	auto        text = readText(entry.path());
	if (!text) {
		fail(source.path(), "cannot read file");
		return;
	}
	source.text = std::move(*text);
	sources_.push_back(std::move(source));
}

}

MergeResult loadTableFolder(const std::filesystem::path& folder, const PluginQuery& plugins, const nlohmann::json& registered, const EntryValidator& validator)
{
	std::vector<TableSource> sources;
	std::vector<Diagnostic>  readErrors;
	FolderReader             reader(sources, readErrors);
	reader.read(folder, TableSource::Folder::Table);

	// A missing plugins/ folder is fine; one that cannot be checked is read and
	// reported like any unreadable folder.
	const auto      pluginsFolder = folder / kPluginsFolderName;
	std::error_code error;
	if (std::filesystem::exists(pluginsFolder, error) || error) {
		reader.read(pluginsFolder, TableSource::Folder::Plugins);
	}

	auto result = mergeTable(sources, plugins, registered, validator);
	result.diagnostics.insert(result.diagnostics.begin(), readErrors.begin(), readErrors.end());
	return result;
}

}
