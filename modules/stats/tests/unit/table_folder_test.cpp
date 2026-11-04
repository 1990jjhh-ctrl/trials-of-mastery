#include "tom/stats/core/table_folder.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

using nlohmann::json;
using tom::stats::Diagnostic;
using tom::stats::loadTableFolder;
using tom::stats::PluginInfo;
using tom::stats::PluginQuery;

namespace
{

class OnePlugin final : public PluginQuery
{
public:
	[[nodiscard]] std::vector<PluginInfo> activePlugins() const override { return { { "Requiem", ".esp" } }; }
};

// A fresh folder under the system temp directory, removed again at the end.
class TempFolder
{
public:
	TempFolder() :
		path_(std::filesystem::temp_directory_path() / ("tom-table-folder-" + std::to_string(std::random_device{}())))
	{
		std::filesystem::create_directories(path_);
	}
	TempFolder(const TempFolder&) = delete;
	TempFolder& operator=(const TempFolder&) = delete;
	~TempFolder() { std::filesystem::remove_all(path_); }

	[[nodiscard]] const std::filesystem::path& path() const { return path_; }

	void write(const std::filesystem::path& relative, const std::string& text) const
	{
		std::filesystem::create_directories((path_ / relative).parent_path());
		std::ofstream(path_ / relative) << text;
	}

private:
	std::filesystem::path path_;
};

std::vector<std::string> filesOf(const tom::stats::MergeResult& result, Diagnostic::Kind kind)
{
	std::vector<std::string> files;
	for (const auto& diagnostic : result.diagnostics) {
		if (diagnostic.kind == kind) {
			files.push_back(diagnostic.file);
		}
	}
	return files;
}

std::string fromUtf8(std::u8string_view text)
{
	return { text.begin(), text.end() };
}

}

TEST_CASE("a table folder merges its .json files", "[merge][folder]")
{
	const TempFolder folder;
	folder.write("_default.json", R"({"STR": {"base": 10}, "INT": {"base": 10}})");
	folder.write("tweaks.json", R"({"STR": {"base": 12}})");
	folder.write("broken.json", R"({"INT": )");
	folder.write("README.txt", "not a table");
	folder.write("nested/deep.json", R"({"INT": {"base": 99}})");

	const auto result = loadTableFolder(folder.path(), OnePlugin{});

	CHECK(result.merged == json::parse(R"({"STR": {"base": 12}, "INT": {"base": 10}})"));
	CHECK(filesOf(result, Diagnostic::Kind::Loaded) == std::vector<std::string>{ "_default.json", "tweaks.json" });
	CHECK(filesOf(result, Diagnostic::Kind::InvalidFile) == std::vector<std::string>{ "broken.json" });
}

TEST_CASE("mod patches load from the plugins subfolder while their plugin is active", "[merge][folder]")
{
	const TempFolder folder;
	folder.write("_default.json", R"({"STR": {"base": 10}})");
	folder.write("Requiem.json", R"({"STR": {"base": 11}})");
	folder.write("plugins/Requiem.json", R"({"STR": {"base": 12}})");
	folder.write("plugins/Inactive.json", R"({"STR": {"base": 13}})");
	folder.write("plugins/README.txt", "not a table");

	const auto result = loadTableFolder(folder.path(), OnePlugin{});

	CHECK(result.merged == json::parse(R"({"STR": {"base": 12}})"));
	CHECK(filesOf(result, Diagnostic::Kind::Loaded) ==
		  std::vector<std::string>{ "_default.json", "Requiem.json", "plugins/Requiem.json" });
	CHECK(filesOf(result, Diagnostic::Kind::InactivePlugin) == std::vector<std::string>{ "plugins/Inactive.json" });
	CHECK(filesOf(result, Diagnostic::Kind::InvalidFile).empty());
}

TEST_CASE("file names come out as UTF-8", "[merge][folder]")
{
	const TempFolder folder;
	folder.write(std::filesystem::path(u8"Grüße.json"), R"({"STR": 1})");

	const auto result = loadTableFolder(folder.path(), OnePlugin{});

	CHECK(result.merged == json::parse(R"({"STR": 1})"));
	CHECK(filesOf(result, Diagnostic::Kind::Loaded) == std::vector<std::string>{ fromUtf8(u8"Grüße.json") });
}

TEST_CASE("a missing table folder is reported", "[merge][folder]")
{
	const TempFolder folder;

	const auto result = loadTableFolder(folder.path() / "missing", OnePlugin{});

	CHECK(result.merged == json::object());
	REQUIRE(result.diagnostics.size() == 1);
	CHECK(result.diagnostics[0].kind == Diagnostic::Kind::InvalidFile);
	CHECK(result.diagnostics[0].file.empty());
	CHECK_FALSE(result.diagnostics[0].detail.empty());
}

TEST_CASE("an unreadable plugins subfolder is reported by its name", "[merge][folder]")
{
	const TempFolder folder;
	folder.write("_default.json", R"({"STR": 1})");
	folder.write("plugins", "a file, not a folder");

	const auto result = loadTableFolder(folder.path(), OnePlugin{});

	CHECK(result.merged == json::parse(R"({"STR": 1})"));
	CHECK(filesOf(result, Diagnostic::Kind::InvalidFile) == std::vector<std::string>{ "plugins" });
}
