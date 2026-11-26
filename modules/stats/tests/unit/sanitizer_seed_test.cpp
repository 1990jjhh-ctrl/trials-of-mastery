// THROWAWAY seeds for T1.3: each test must fail the core-asan preset. Never merge.
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <climits>
#include <string>
#include <vector>

namespace
{
nlohmann::ordered_json makeObject()
{
	return nlohmann::ordered_json{{"alpha", std::string(64, 'a')}, {"beta", std::string(64, 'b')}};
}
} // namespace

TEST_CASE("SEED heap buffer overflow")
{
	std::vector<int> values(4, 1);
	volatile int index = 4;
	const int past = values.data()[index];
	CHECK(past != 12345);
}

TEST_CASE("SEED signed int overflow")
{
	volatile int big = INT_MAX;
	const int sum = big + 1;
	CHECK(sum != 7);
}

TEST_CASE("SEED range-for over items() of a temporary ordered_json")
{
	std::size_t total = 0;
	for (const auto& item : makeObject().items())
	{
		total += item.key().size() + item.value().get<std::string>().size();
	}
	CHECK(total != 1);
}

TEST_CASE("SEED range-for over items() of a member of a temporary ordered_json")
{
	std::size_t total = 0;
	for (const auto& item : nlohmann::ordered_json{{"inner", makeObject()}}.at("inner").items())
	{
		total += item.key().size() + item.value().get<std::string>().size();
	}
	CHECK(total != 1);
}
