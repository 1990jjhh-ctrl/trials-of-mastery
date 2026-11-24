#include "tom/stats/core/table_merge.h"

#include "ascii_case.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <optional>
#include <utility>

namespace tom::stats
{

namespace
{

using nlohmann::json;

constexpr std::string_view kJsonExtension = ".json";

std::string joined(const std::vector<std::string>& parts)
{
	std::string text;
	for (const auto& part : parts) {
		text += (text.empty() ? "" : ", ") + part;
	}
	return text;
}

std::optional<std::string_view> jsonStem(std::string_view fileName)
{
	if (fileName.size() <= kJsonExtension.size()) {
		return std::nullopt;
	}
	const auto stemSize = fileName.size() - kJsonExtension.size();
	if (!equalsIgnoreCase(fileName.substr(stemSize), kJsonExtension)) {
		return std::nullopt;
	}
	return fileName.substr(0, stemSize);
}

// Erases the entry for `path` and the entries for the keys below it.
template <class Value>
void eraseSubtree(std::map<std::string, Value>& map, const std::string& path)
{
	map.erase(path);
	const auto prefix = path + "/";
	auto       it = map.lower_bound(prefix);
	while (it != map.end() && it->first.starts_with(prefix)) {
		it = map.erase(it);
	}
}

// A parsed file and where it goes in the merge order.
struct OrderedFile
{
	Origin      origin;
	std::size_t loadIndex = 0;  // index in the active plugins, for Layer::ModPatch
	std::string name;           // file name without folder
	json        content;
};

bool mergesBefore(const OrderedFile& a, const OrderedFile& b)
{
	if (a.origin.layer != b.origin.layer) {
		return a.origin.layer < b.origin.layer;
	}
	if (a.loadIndex != b.loadIndex) {
		return a.loadIndex < b.loadIndex;
	}
	if (lessIgnoreCase(a.name, b.name)) {
		return true;
	}
	if (lessIgnoreCase(b.name, a.name)) {
		return false;
	}
	return a.name < b.name;
}

bool sameSlot(const OrderedFile& a, const OrderedFile& b)
{
	return a.origin.layer == b.origin.layer && a.loadIndex == b.loadIndex && equalsIgnoreCase(a.name, b.name);
}

class FileOrder
{
public:
	FileOrder(const PluginQuery& plugins, std::vector<Diagnostic>& diagnostics) :
		active_(plugins.activePlugins()), diagnostics_(diagnostics) {}

	void add(const TableSource& source);

	[[nodiscard]] std::vector<OrderedFile> sorted() &&;

private:
	[[nodiscard]] std::optional<OrderedFile> classify(const TableSource& source, const std::string& file, std::string_view stem) const;
	[[nodiscard]] std::optional<std::size_t> lastActive(const std::string& file, std::string_view stem) const;
	void                                     skip(const std::string& file, Diagnostic::Kind kind, std::string detail) const;

	std::vector<PluginInfo>  active_;
	std::vector<Diagnostic>& diagnostics_;
	std::vector<OrderedFile> files_;
};

void FileOrder::add(const TableSource& source)
{
	const auto file = source.path();
	const auto stem = jsonStem(source.name);
	if (!stem) {
		skip(file, Diagnostic::Kind::InvalidFile, "not a .json file");
		return;
	}
	auto ordered = classify(source, file, *stem);
	if (!ordered) {
		return;
	}
	try {
		ordered->content = json::parse(source.text);
	} catch (const json::exception& error) {
		// Not only parse_error: a number out of range throws out_of_range.
		skip(file, Diagnostic::Kind::InvalidFile, error.what());
		return;
	}
	if (!ordered->content.is_object()) {
		skip(file, Diagnostic::Kind::InvalidFile, "top level is not an object");
		return;
	}
	files_.push_back(std::move(*ordered));
}

// Sorts the files into merge order and warns about names that differ only in
// case, which the game's file system cannot hold side by side.
std::vector<OrderedFile> FileOrder::sorted() &&
{
	std::ranges::stable_sort(files_, mergesBefore);
	for (std::size_t i = 1; i < files_.size(); ++i) {
		if (sameSlot(files_[i - 1], files_[i])) {
			diagnostics_.push_back({ Diagnostic::Kind::SameNameFiles, files_[i].origin.file, "", files_[i - 1].origin.file });
		}
	}
	return std::move(files_);
}

std::optional<OrderedFile> FileOrder::classify(const TableSource& source, const std::string& file, std::string_view stem) const
{
	if (source.folder == TableSource::Folder::Table) {
		const auto layer = equalsIgnoreCase(source.name, kDefaultFileName) ? Layer::Default : Layer::Patch;
		return OrderedFile{ { layer, file }, 0, source.name, {} };
	}
	const auto index = lastActive(file, stem);
	if (!index) {
		skip(file, Diagnostic::Kind::InactivePlugin, "");
		return std::nullopt;
	}
	return OrderedFile{ { Layer::ModPatch, file }, *index, source.name, {} };
}

// Load-order index of the last active plugin named `stem`. Warns when several
// active plugins share that name.
std::optional<std::size_t> FileOrder::lastActive(const std::string& file, std::string_view stem) const
{
	std::optional<std::size_t> last;
	std::vector<std::string>   matches;
	for (std::size_t i = 0; i < active_.size(); ++i) {
		if (equalsIgnoreCase(active_[i].name, stem)) {
			matches.push_back(active_[i].name + active_[i].extension);
			last = i;
		}
	}
	if (matches.size() > 1) {
		diagnostics_.push_back({ Diagnostic::Kind::SameNamePlugins, file, "", joined(matches) });
	}
	return last;
}

void FileOrder::skip(const std::string& file, Diagnostic::Kind kind, std::string detail) const
{
	diagnostics_.push_back({ kind, file, "", std::move(detail) });
}

bool isPatch(const Origin& origin)
{
	return origin.layer == Layer::Patch || origin.layer == Layer::ModPatch;
}

// Applies layers on top of each other. Records the origin of every key, and
// which patch file set which key, so that a patch overriding another patch is
// reported. Keys of the lower layers may be overridden freely.
class Merger
{
public:
	explicit Merger(MergeResult& result) :
		result_(result) {}

	void apply(const json& content, const Origin& origin) { mergeObject(result_.merged, content, json::json_pointer{}, origin); }
	void finish();

private:
	void mergeObject(json& target, const json& patch, const json::json_pointer& path, const Origin& origin);
	void mergeKey(json& target, const std::string& key, const json& value, const json::json_pointer& path, const Origin& origin);
	void removeKey(json& target, const std::string& key, const json::json_pointer& path, const Origin& origin);
	void setOrigin(const json::json_pointer& path, const Origin& origin);
	void claim(const json::json_pointer& path, const Origin& origin);
	void release(const std::string& path, const Origin& origin);
	void releaseChildren(const std::string& path, const Origin& origin);
	void reportConflict(const std::string& path, const std::string& earlierFile, const Origin& origin);

	MergeResult&                       result_;
	std::map<std::string, std::string> owners_;  // JSON pointer of a key a patch set -> that patch file
};

// Keeps the origins of the leaves only; an object that received keys is
// described by the origins of its keys.
void Merger::finish()
{
	std::erase_if(result_.origins, [this](const auto& entry) {
		const auto& value = result_.merged.at(json::json_pointer(entry.first));
		return value.is_object() && !value.empty();
	});
}

void Merger::mergeObject(json& target, const json& patch, const json::json_pointer& path, const Origin& origin)
{
	for (const auto& [key, value] : patch.items()) {
		mergeKey(target, key, value, path / key, origin);
	}
}

void Merger::mergeKey(json& target, const std::string& key, const json& value, const json::json_pointer& path, const Origin& origin)
{
	if (value.is_null()) {
		removeKey(target, key, path, origin);
		return;
	}
	if (!value.is_object()) {
		claim(path, origin);
		target[key] = value;
		setOrigin(path, origin);
		return;
	}
	auto& slot = target[key];
	if (!slot.is_object()) {
		// Turning a missing or non-object key into an object sets the key itself.
		claim(path, origin);
		slot = json::object();
		setOrigin(path, origin);
	}
	if (!value.empty()) {
		// Keys merged into the object carry the claims from here on, so the
		// object itself no longer belongs to the patch that created it.
		owners_.erase(path.to_string());
	}
	mergeObject(slot, value, path, origin);
}

void Merger::removeKey(json& target, const std::string& key, const json::json_pointer& path, const Origin& origin)
{
	claim(path, origin);
	if (target.erase(key) == 0) {
		return;
	}
	eraseSubtree(result_.origins, path.to_string());
	result_.diagnostics.push_back({ Diagnostic::Kind::Removed, origin.file, path.to_string(), "" });
	if (target.empty() && !path.parent_pointer().empty()) {
		// The emptied object is now a leaf, left by this layer.
		setOrigin(path.parent_pointer(), origin);
	}
}

void Merger::setOrigin(const json::json_pointer& path, const Origin& origin)
{
	auto key = path.to_string();
	eraseSubtree(result_.origins, key);
	result_.origins.emplace(std::move(key), origin);
}

// Records that a patch set the key at `path`, replacing everything below it.
void Merger::claim(const json::json_pointer& path, const Origin& origin)
{
	if (!isPatch(origin)) {
		return;
	}
	auto key = path.to_string();
	release(key, origin);
	releaseChildren(key, origin);
	owners_.emplace(std::move(key), origin.file);
}

void Merger::release(const std::string& path, const Origin& origin)
{
	const auto it = owners_.find(path);
	if (it == owners_.end()) {
		return;
	}
	reportConflict(it->first, it->second, origin);
	owners_.erase(it);
}

void Merger::releaseChildren(const std::string& path, const Origin& origin)
{
	const auto prefix = path + "/";
	auto       it = owners_.lower_bound(prefix);
	while (it != owners_.end() && it->first.starts_with(prefix)) {
		reportConflict(it->first, it->second, origin);
		it = owners_.erase(it);
	}
}

void Merger::reportConflict(const std::string& path, const std::string& earlierFile, const Origin& origin)
{
	result_.diagnostics.push_back({ Diagnostic::Kind::Conflict, origin.file, path, earlierFile });
}

}

std::string TableSource::path() const
{
	if (folder == Folder::Plugins) {
		return std::string(kPluginsFolderName) + "/" + name;
	}
	return name;
}

MergeResult mergeTable(std::span<const TableSource> sources, const PluginQuery& plugins, const nlohmann::json& registered)
{
	MergeResult result;
	FileOrder   order(plugins, result.diagnostics);
	for (const auto& source : sources) {
		order.add(source);
	}
	const auto files = std::move(order).sorted();

	Merger merger(result);
	if (registered.is_object()) {
		merger.apply(registered, Origin{ Layer::Registered, "" });
	} else {
		result.diagnostics.push_back({ Diagnostic::Kind::InvalidRegistered, "", "",
			std::string("registered entries are ") + registered.type_name() + ", not an object" });
	}
	for (const auto& file : files) {
		result.diagnostics.push_back({ Diagnostic::Kind::Loaded, file.origin.file, "", "" });
		merger.apply(file.content, file.origin);
	}
	merger.finish();
	return result;
}

}
