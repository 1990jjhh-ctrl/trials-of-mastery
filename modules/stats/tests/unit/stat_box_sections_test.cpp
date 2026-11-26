// The registry of the stat box sections that modules fill (ST-24, ST-26).
#include "tom/stats/core/stat_box_sections.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

using tom::stats::SectionProvider;
using tom::stats::StatBoxSections;

namespace
{
SectionProvider linesOf(std::vector<std::string> lines)
{
	return [lines = std::move(lines)] { return lines; };
}
}

TEST_CASE("a section provider is found by its namespace", "[stat_box_sections]")
{
	StatBoxSections sections;
	REQUIRE(sections.set("Luck", linesOf({ "Lockpicking +1 %" })));

	const SectionProvider* provider = sections.find("Luck");
	REQUIRE(provider != nullptr);
	CHECK((*provider)() == std::vector<std::string>{ "Lockpicking +1 %" });
	CHECK(sections.find("Mage") == nullptr);
}

TEST_CASE("a second provider for a namespace replaces the first", "[stat_box_sections]")
{
	StatBoxSections sections;
	REQUIRE(sections.set("Luck", linesOf({ "first" })));
	REQUIRE(sections.set("Luck", linesOf({ "second" })));

	REQUIRE(sections.find("Luck") != nullptr);
	CHECK((*sections.find("Luck"))() == std::vector<std::string>{ "second" });
}

TEST_CASE("a provider without a valid namespace or function is rejected", "[stat_box_sections]")
{
	StatBoxSections sections;
	REQUIRE(sections.set("Luck", linesOf({ "kept" })));

	CHECK_FALSE(sections.set("", linesOf({ "empty namespace" })));
	CHECK_FALSE(sections.set("Luck:LUC", linesOf({ "namespace with a colon" })));
	CHECK_FALSE(sections.set("Luck", SectionProvider{}));

	CHECK(sections.find("") == nullptr);
	CHECK(sections.find("Luck:LUC") == nullptr);
	REQUIRE(sections.find("Luck") != nullptr);
	CHECK((*sections.find("Luck"))() == std::vector<std::string>{ "kept" });
}

TEST_CASE("remove drops a provider and ignores an unknown namespace", "[stat_box_sections]")
{
	StatBoxSections sections;
	REQUIRE(sections.set("Luck", linesOf({ "Lockpicking +1 %" })));
	REQUIRE(sections.set("Fate", linesOf({ "Doom +1" })));

	sections.remove("Unknown");
	CHECK(sections.find("Luck") != nullptr);

	sections.remove("Luck");
	CHECK(sections.find("Luck") == nullptr);
	CHECK(sections.find("Fate") != nullptr);
}
