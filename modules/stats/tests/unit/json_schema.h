#pragma once

// A small JSON Schema (draft 2020-12) validator for the tests. It knows only
// the keywords the module's schemas use and throws std::logic_error on any
// other keyword, so a test notices when a schema outgrows it.

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tom::stats::test
{

class SchemaValidator
{
public:
	// `folder` holds the schema files; "$ref"s name files relative to it.
	explicit SchemaValidator(std::filesystem::path folder) :
		folder_(std::move(folder)) {}

	// The problems of `instance` against the schema file `schemaFile`, each as
	// "<JSON pointer>: <problem>". Empty when it is valid.
	[[nodiscard]] std::vector<std::string> validate(const nlohmann::json& instance, const std::string& schemaFile);

	// Adds a schema under a file name without reading it.
	void addDocument(const std::string& file, nlohmann::json schema);

	// The schema file, parsed.
	[[nodiscard]] const nlohmann::json& document(const std::string& file);

private:
	struct Context;

	void        check(const nlohmann::json& schema, const nlohmann::json& instance, Context& context);
	void        checkKeyword(const std::string& keyword, const nlohmann::json& value, const nlohmann::json& schema,
		const nlohmann::json& instance, Context& context);
	void        checkKind(const std::string& keyword, const nlohmann::json& value, const nlohmann::json& schema,
		const nlohmann::json& instance, Context& context);
	void        checkMembers(const std::string& keyword, const nlohmann::json& value, const nlohmann::json& schema,
		const nlohmann::json& instance, Context& context);
	static void checkRequired(const nlohmann::json& names, const nlohmann::json& instance, Context& context);
	void        checkItems(const std::string& keyword, const nlohmann::json& value, const nlohmann::json& instance, Context& context);
	void        checkText(const std::string& keyword, const nlohmann::json& value, const nlohmann::json& instance, Context& context);
	void        checkRange(const std::string& keyword, const nlohmann::json& value, const nlohmann::json& instance, Context& context);
	void        checkRef(const std::string& ref, const nlohmann::json& instance, Context& context);
	// How many of the schemas in `options` the instance matches.
	[[nodiscard]] std::size_t matches(const nlohmann::json& options, const nlohmann::json& instance, const Context& context);

	std::filesystem::path                 folder_;
	std::map<std::string, nlohmann::json> documents_;
};

}
