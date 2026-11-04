#include "tom/stats/core/table_merge.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

using nlohmann::json;
using tom::stats::Diagnostic;
using tom::stats::MergeResult;
using tom::stats::mergeTable;
using tom::stats::PluginInfo;
using tom::stats::PluginQuery;
using tom::stats::TableSource;

namespace
{

class FakePlugins final : public PluginQuery
{
public:
	FakePlugins(std::vector<PluginInfo> active = {}, std::vector<std::string> inactive = {}) :
		active_(std::move(active)), inactive_(std::move(inactive)) {}

	[[nodiscard]] std::vector<PluginInfo> activePlugins() const override { return active_; }

	[[nodiscard]] bool isInstalled(std::string_view name) const override
	{
		const auto isName = [name](const PluginInfo& plugin) { return plugin.name == name; };
		return std::ranges::any_of(active_, isName) || std::ranges::find(inactive_, name) != inactive_.end();
	}

private:
	std::vector<PluginInfo>  active_;
	std::vector<std::string> inactive_;
};

std::vector<std::string> filesOf(const MergeResult& result, Diagnostic::Kind kind)
{
	std::vector<std::string> files;
	for (const auto& diagnostic : result.diagnostics) {
		if (diagnostic.kind == kind) {
			files.push_back(diagnostic.file);
		}
	}
	return files;
}

std::vector<Diagnostic> ofKind(const MergeResult& result, Diagnostic::Kind kind)
{
	std::vector<Diagnostic> found;
	std::ranges::copy_if(result.diagnostics, std::back_inserter(found),
		[kind](const Diagnostic& diagnostic) { return diagnostic.kind == kind; });
	return found;
}

// Each file sets "who" to its own name, so the merged value shows the last file.
TableSource writer(const std::string& name)
{
	return { name, json{ { "who", name } }.dump() };
}

}

TEST_CASE("the default file loads first", "[merge][order]")
{
	const std::vector<TableSource> sources{ writer("aaa.json"), writer("_default.json") };

	const auto result = mergeTable(sources, FakePlugins{});

	CHECK(filesOf(result, Diagnostic::Kind::Loaded) == std::vector<std::string>{ "_default.json", "aaa.json" });
	CHECK(result.merged.at("who") == "aaa.json");
}

TEST_CASE("unnamed files load in alphabetical order", "[merge][order]")
{
	const std::vector<TableSource> sources{ writer("zeta.json"), writer("Beta.json"), writer("alpha.json"), writer("_default.json") };

	const auto result = mergeTable(sources, FakePlugins{});

	CHECK(filesOf(result, Diagnostic::Kind::Loaded) ==
		  std::vector<std::string>{ "_default.json", "alpha.json", "Beta.json", "zeta.json" });
	CHECK(result.merged.at("who") == "zeta.json");
}

TEST_CASE("plugin-named files load after unnamed files, in plugin load order", "[merge][order]")
{
	const FakePlugins              plugins{ { { "Zed", ".esm" }, { "Alpha", ".esp" } } };
	const std::vector<TableSource> sources{ writer("Alpha.json"), writer("Zed.json"), writer("zz_tweaks.json"), writer("_default.json") };

	const auto result = mergeTable(sources, plugins);

	CHECK(filesOf(result, Diagnostic::Kind::Loaded) ==
		  std::vector<std::string>{ "_default.json", "zz_tweaks.json", "Zed.json", "Alpha.json" });
	CHECK(result.merged.at("who") == "Alpha.json");
}

TEST_CASE("plugin names match without regard to case", "[merge][order]")
{
	const FakePlugins              plugins{ { { "Requiem", ".esp" } } };
	const std::vector<TableSource> sources{ writer("requiem.json"), writer("zz_tweaks.json") };

	const auto result = mergeTable(sources, plugins);

	CHECK(filesOf(result, Diagnostic::Kind::Loaded) == std::vector<std::string>{ "zz_tweaks.json", "requiem.json" });
}

TEST_CASE("a file named after an inactive plugin does not load", "[merge][order]")
{
	const FakePlugins              plugins{ {}, { "Requiem" } };
	const std::vector<TableSource> sources{ writer("_default.json"), writer("Requiem.json") };

	const auto result = mergeTable(sources, plugins);

	CHECK(filesOf(result, Diagnostic::Kind::Loaded) == std::vector<std::string>{ "_default.json" });
	CHECK(filesOf(result, Diagnostic::Kind::InactivePlugin) == std::vector<std::string>{ "Requiem.json" });
	CHECK(result.merged.at("who") == "_default.json");
}

TEST_CASE("same-name plugins load the file at the last of them, with a warning", "[merge][order]")
{
	const FakePlugins              plugins{ { { "Foo", ".esm" }, { "Bar", ".esp" }, { "Foo", ".esp" } } };
	const std::vector<TableSource> sources{ writer("Foo.json"), writer("Bar.json") };

	const auto result = mergeTable(sources, plugins);

	CHECK(filesOf(result, Diagnostic::Kind::Loaded) == std::vector<std::string>{ "Bar.json", "Foo.json" });
	const auto warnings = ofKind(result, Diagnostic::Kind::SameNamePlugins);
	REQUIRE(warnings.size() == 1);
	CHECK(warnings[0].file == "Foo.json");
	CHECK(warnings[0].detail == "Foo.esm, Foo.esp");
}

TEST_CASE("objects merge key by key, recursively", "[merge][keys]")
{
	const std::vector<TableSource> sources{
		{ "_default.json", R"({"STR": {"base": 10, "group": "body", "effects": {"carry": 5}}, "INT": {"base": 10}})" },
		{ "patch.json", R"({"STR": {"base": 12, "effects": {"stamina": 2}}, "LCK": {"base": 7}})" },
	};

	const auto result = mergeTable(sources, FakePlugins{});

	const json expected = json::parse(R"({
		"STR": {"base": 12, "group": "body", "effects": {"carry": 5, "stamina": 2}},
		"INT": {"base": 10},
		"LCK": {"base": 7}
	})");
	CHECK(result.merged == expected);
}

TEST_CASE("a non-object value replaces the whole key", "[merge][keys]")
{
	const std::vector<TableSource> sources{
		{ "_default.json", R"({"a": {"x": 1}, "b": [1, 2, 3], "c": 5})" },
		{ "patch.json", R"({"a": 2, "b": [4], "c": {"y": 1}})" },
	};

	const auto result = mergeTable(sources, FakePlugins{});

	CHECK(result.merged == json::parse(R"({"a": 2, "b": [4], "c": {"y": 1}})"));
}

TEST_CASE("null removes a key", "[merge][keys]")
{
	const std::vector<TableSource> sources{
		{ "_default.json", R"({"STR": {"base": 10, "group": "body"}, "INT": {"base": 10}})" },
		{ "patch.json", R"({"STR": {"group": null}, "INT": null, "MISSING": null, "NEW": {"gone": null, "kept": 1}})" },
	};

	const auto result = mergeTable(sources, FakePlugins{});

	CHECK(result.merged == json::parse(R"({"STR": {"base": 10}, "NEW": {"kept": 1}})"));
	const auto removed = ofKind(result, Diagnostic::Kind::Removed);
	REQUIRE(removed.size() == 2);
	CHECK(removed[0].file == "patch.json");
	CHECK(removed[0].key == "/INT");
	CHECK(removed[1].key == "/STR/group");
}

TEST_CASE("registered entries are defaults below the JSON files", "[merge][registered]")
{
	const json                     registered = json::parse(R"({"Mod:Focus": {"base": 3, "group": "mind"}, "STR": {"base": 1, "extra": true}})");
	const std::vector<TableSource> sources{
		{ "_default.json", R"({"STR": {"base": 10}})" },
		{ "patch.json", R"({"Mod:Focus": {"base": 5}})" },
	};

	const auto result = mergeTable(sources, FakePlugins{}, registered);

	CHECK(result.merged == json::parse(R"({"Mod:Focus": {"base": 5, "group": "mind"}, "STR": {"base": 10, "extra": true}})"));
	CHECK(ofKind(result, Diagnostic::Kind::Conflict).empty());
}

TEST_CASE("overriding the default file is not a conflict", "[merge][conflict]")
{
	const std::vector<TableSource> sources{
		{ "_default.json", R"({"STR": {"base": 10, "group": "body"}})" },
		{ "patch.json", R"({"STR": {"base": 12, "group": null}})" },
	};

	const auto result = mergeTable(sources, FakePlugins{});

	CHECK(ofKind(result, Diagnostic::Kind::Conflict).empty());
}

TEST_CASE("two patch files setting the same key is a conflict, the later wins", "[merge][conflict]")
{
	const std::vector<TableSource> sources{
		{ "_default.json", R"({"STR": {"base": 10}})" },
		{ "a.json", R"({"STR": {"base": 11, "group": "body"}})" },
		{ "b.json", R"({"STR": {"base": 12, "other": 1}})" },
	};

	const auto result = mergeTable(sources, FakePlugins{});

	CHECK(result.merged.at("STR").at("base") == 12);
	const auto conflicts = ofKind(result, Diagnostic::Kind::Conflict);
	REQUIRE(conflicts.size() == 1);
	CHECK(conflicts[0].file == "b.json");
	CHECK(conflicts[0].key == "/STR/base");
	CHECK(conflicts[0].detail == "a.json");
}

TEST_CASE("removing or replacing a key another patch set below is a conflict", "[merge][conflict]")
{
	const std::vector<TableSource> sources{
		{ "a.json", R"({"STR": {"base": 11}, "INT": 3})" },
		{ "b.json", R"({"STR": null, "INT": {"base": 4}})" },
		{ "c.json", R"({"STR": {"base": 13}})" },
	};

	const auto result = mergeTable(sources, FakePlugins{});

	CHECK(result.merged == json::parse(R"({"STR": {"base": 13}, "INT": {"base": 4}})"));
	const auto conflicts = ofKind(result, Diagnostic::Kind::Conflict);
	REQUIRE(conflicts.size() == 3);
	CHECK((conflicts[0].file == "b.json" && conflicts[0].key == "/INT" && conflicts[0].detail == "a.json"));
	CHECK((conflicts[1].file == "b.json" && conflicts[1].key == "/STR/base" && conflicts[1].detail == "a.json"));
	CHECK((conflicts[2].file == "c.json" && conflicts[2].key == "/STR" && conflicts[2].detail == "b.json"));
}

TEST_CASE("an invalid file is skipped and the remaining files still load", "[merge][invalid]")
{
	const std::vector<TableSource> sources{
		{ "_default.json", R"({"STR": {"base": 10}})" },
		{ "broken.json", R"({"STR": {"base": 99)" },
		{ "list.json", R"([1, 2])" },
		{ "notes.txt", R"({"STR": {"base": 98}})" },
		{ "patch.json", R"({"INT": {"base": 4}})" },
	};

	const auto result = mergeTable(sources, FakePlugins{});

	CHECK(result.merged == json::parse(R"({"STR": {"base": 10}, "INT": {"base": 4}})"));
	CHECK(filesOf(result, Diagnostic::Kind::Loaded) == std::vector<std::string>{ "_default.json", "patch.json" });
	const auto invalid = ofKind(result, Diagnostic::Kind::InvalidFile);
	REQUIRE(invalid.size() == 3);
	CHECK(invalid[0].file == "broken.json");
	CHECK(invalid[1].file == "list.json");
	CHECK(invalid[2].file == "notes.txt");
	const auto hasReason = [](const Diagnostic& diagnostic) { return !diagnostic.detail.empty(); };
	CHECK(std::ranges::all_of(invalid, hasReason));
}

TEST_CASE("an invalid default file is skipped too", "[merge][invalid]")
{
	const std::vector<TableSource> sources{ { "_default.json", "{" }, writer("patch.json") };

	const auto result = mergeTable(sources, FakePlugins{});

	CHECK(filesOf(result, Diagnostic::Kind::InvalidFile) == std::vector<std::string>{ "_default.json" });
	CHECK(result.merged.at("who") == "patch.json");
}
