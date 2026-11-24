#include "json_schema.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace tom::stats::test
{

namespace
{

using nlohmann::json;

// Keywords that only describe or hold definitions.
constexpr std::array<std::string_view, 6> kAnnotations{ "$schema", "$id", "title", "description", "$comment", "$defs" };

bool hasType(const json& instance, const std::string& type)
{
	if (type == "object") {
		return instance.is_object();
	}
	if (type == "array") {
		return instance.is_array();
	}
	if (type == "string") {
		return instance.is_string();
	}
	if (type == "integer") {
		// A number without a fractional part, such as 10.0, is an integer.
		return instance.is_number_integer() || (instance.is_number_float() && std::trunc(instance.get<double>()) == instance.get<double>());
	}
	if (type == "number") {
		return instance.is_number();
	}
	if (type == "boolean") {
		return instance.is_boolean();
	}
	if (type == "null") {
		return instance.is_null();
	}
	throw std::invalid_argument("unknown type " + type);
}

std::string escaped(const std::string& key)
{
	return (json::json_pointer{} / key).to_string();
}

}

struct SchemaValidator::Context
{
	std::string               file;  // the schema file that "#..." refs resolve in
	std::string               path;  // JSON pointer of the instance
	std::vector<std::string>* errors = nullptr;

	void                  fail(const std::string& problem) const { errors->push_back(path + ": " + problem); }
	[[nodiscard]] Context at(const std::string& suffix) const { return { file, path + suffix, errors }; }
};

const json& SchemaValidator::document(const std::string& file)
{
	if (const auto it = documents_.find(file); it != documents_.end()) {
		return it->second;
	}
	std::ifstream stream(folder_ / file);
	if (!stream) {
		throw std::runtime_error("cannot read schema " + file);
	}
	return documents_.emplace(file, json::parse(stream)).first->second;
}

void SchemaValidator::addDocument(const std::string& file, nlohmann::json schema)
{
	documents_.insert_or_assign(file, std::move(schema));
}

std::vector<std::string> SchemaValidator::validate(const json& instance, const std::string& schemaFile)
{
	std::vector<std::string> errors;
	Context                  context{ schemaFile, "", &errors };
	check(document(schemaFile), instance, context);
	return errors;
}

void SchemaValidator::check(const json& schema, const json& instance, Context& context)
{
	if (schema.is_boolean()) {
		if (!schema.get<bool>()) {
			context.fail("not allowed");
		}
		return;
	}
	for (const auto& [keyword, value] : schema.items()) {
		checkKeyword(keyword, value, schema, instance, context);
	}
}

void SchemaValidator::checkKeyword(const std::string& keyword, const json& value, const json& schema, const json& instance, Context& context)
{
	if (std::ranges::find(kAnnotations, keyword) != kAnnotations.end()) {
		return;
	}
	if (keyword == "$ref") {
		checkRef(value.get<std::string>(), instance, context);
	} else if (keyword == "type") {
		if (!hasType(instance, value.get<std::string>())) {
			context.fail("is not of type " + value.get<std::string>());
		}
	} else if (keyword == "enum") {
		if (std::ranges::find(value, instance) == value.end()) {
			context.fail("is not one of " + value.dump());
		}
	} else if (keyword == "const") {
		if (instance != value) {
			context.fail("is not " + value.dump());
		}
	} else if (keyword == "oneOf") {
		if (matches(value, instance, context) != 1) {
			context.fail("does not match exactly one of oneOf");
		}
	} else if (keyword == "anyOf") {
		if (matches(value, instance, context) == 0) {
			context.fail("matches none of anyOf");
		}
	} else {
		checkKind(keyword, value, schema, instance, context);
	}
}

// The keywords for one kind of instance; each ignores other kinds.
void SchemaValidator::checkKind(const std::string& keyword, const json& value, const json& schema, const json& instance, Context& context)
{
	if (keyword == "properties" || keyword == "required" || keyword == "additionalProperties" || keyword == "propertyNames") {
		checkMembers(keyword, value, schema, instance, context);
	} else if (keyword == "items" || keyword == "minItems") {
		checkItems(keyword, value, instance, context);
	} else if (keyword == "pattern" || keyword == "minLength") {
		checkText(keyword, value, instance, context);
	} else if (keyword == "minimum" || keyword == "maximum") {
		checkRange(keyword, value, instance, context);
	} else {
		// Thrown, not reported: inside anyOf or oneOf a report would only make
		// an option fail to match.
		throw std::logic_error("schema uses the unsupported keyword " + keyword);
	}
}

void SchemaValidator::checkMembers(const std::string& keyword, const json& value, const json& schema, const json& instance, Context& context)
{
	if (!instance.is_object()) {
		return;
	}
	if (keyword == "required") {
		checkRequired(value, instance, context);
		return;
	}
	for (const auto& [key, child] : instance.items()) {
		auto       childContext = context.at(escaped(key));
		const bool declared = schema.contains("properties") && schema["properties"].contains(key);
		if (keyword == "properties" && declared) {
			check(value[key], child, childContext);
		} else if (keyword == "additionalProperties" && !declared) {
			check(value, child, childContext);
		} else if (keyword == "propertyNames") {
			check(value, json(key), childContext);
		}
	}
}

void SchemaValidator::checkRequired(const json& names, const json& instance, Context& context)
{
	for (const auto& name : names) {
		if (!instance.contains(name.get<std::string>())) {
			context.fail("misses " + name.get<std::string>());
		}
	}
}

void SchemaValidator::checkItems(const std::string& keyword, const json& value, const json& instance, Context& context)
{
	if (!instance.is_array()) {
		return;
	}
	if (keyword == "minItems") {
		if (instance.size() < value.get<std::size_t>()) {
			context.fail("has too few items");
		}
		return;
	}
	for (std::size_t i = 0; i < instance.size(); ++i) {
		auto childContext = context.at("/" + std::to_string(i));
		check(value, instance[i], childContext);
	}
}

void SchemaValidator::checkText(const std::string& keyword, const json& value, const json& instance, Context& context)
{
	if (!instance.is_string()) {
		return;
	}
	const auto& text = instance.get_ref<const std::string&>();
	if (keyword == "pattern" && !std::regex_search(text, std::regex(value.get<std::string>()))) {
		context.fail("does not match " + value.get<std::string>());
	}
	if (keyword == "minLength" && text.size() < value.get<std::size_t>()) {
		context.fail("is too short");
	}
}

void SchemaValidator::checkRange(const std::string& keyword, const json& value, const json& instance, Context& context)
{
	if (!instance.is_number()) {
		return;
	}
	const bool below = instance.get<double>() < value.get<double>();
	const bool above = instance.get<double>() > value.get<double>();
	if ((keyword == "minimum" && below) || (keyword == "maximum" && above)) {
		context.fail("is out of range");
	}
}

// "<file>#<pointer>", "<file>" or "#<pointer>"; files are relative to the
// schema folder.
void SchemaValidator::checkRef(const std::string& ref, const json& instance, Context& context)
{
	const auto hash = ref.find('#');
	const auto file = ref.substr(0, hash);
	const auto pointer = hash == std::string::npos ? std::string() : ref.substr(hash + 1);
	Context    target{ file.empty() ? context.file : file, context.path, context.errors };
	check(document(target.file).at(json::json_pointer(pointer)), instance, target);
}

std::size_t SchemaValidator::matches(const json& options, const json& instance, const Context& context)
{
	return static_cast<std::size_t>(std::ranges::count_if(options, [&](const json& option) {
		std::vector<std::string> errors;
		Context                  attempt{ context.file, context.path, &errors };
		check(option, instance, attempt);
		return errors.empty();
	}));
}

}
