#include "tom/stats/core/table_merge.h"

#include <algorithm>
#include <cctype>
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

// Plugin and file names are case-insensitive in the game (Windows file system).
char foldCase(char c)
{
	return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool equalsIgnoreCase(std::string_view a, std::string_view b)
{
	return std::ranges::equal(a, b, {}, foldCase, foldCase);
}

bool lessIgnoreCase(std::string_view a, std::string_view b)
{
	return std::ranges::lexicographical_compare(a, b, {}, foldCase, foldCase);
}

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

// A parsed file and where it goes in the merge order.
struct OrderedFile
{
	// Default file first, then unnamed files, then plugin-named files.
	enum class Group
	{
		Default,
		Unnamed,
		Plugin,
	};

	Group       group;
	std::size_t loadIndex = 0;  // index in the active plugins, for Group::Plugin
	std::string name;
	json        content;
};

bool mergesBefore(const OrderedFile& a, const OrderedFile& b)
{
	if (a.group != b.group) {
		return a.group < b.group;
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

class FileOrder
{
public:
	FileOrder(const PluginQuery& plugins, std::vector<Diagnostic>& diagnostics) :
		plugins_(plugins), active_(plugins.activePlugins()), diagnostics_(diagnostics) {}

	void add(const TableSource& source);

	[[nodiscard]] std::vector<OrderedFile> sorted() &&
	{
		std::ranges::stable_sort(files_, mergesBefore);
		return std::move(files_);
	}

private:
	[[nodiscard]] std::optional<OrderedFile> classify(const TableSource& source, std::string_view stem) const;
	[[nodiscard]] std::optional<std::size_t> lastActive(const TableSource& source, std::string_view stem) const;
	void                                     skip(const TableSource& source, Diagnostic::Kind kind, std::string detail) const;

	const PluginQuery&       plugins_;
	std::vector<PluginInfo>  active_;
	std::vector<Diagnostic>& diagnostics_;
	std::vector<OrderedFile> files_;
};

void FileOrder::add(const TableSource& source)
{
	const auto stem = jsonStem(source.name);
	if (!stem) {
		skip(source, Diagnostic::Kind::InvalidFile, "not a .json file");
		return;
	}
	auto file = classify(source, *stem);
	if (!file) {
		return;
	}
	try {
		file->content = json::parse(source.text);
	} catch (const json::parse_error& error) {
		skip(source, Diagnostic::Kind::InvalidFile, error.what());
		return;
	}
	if (!file->content.is_object()) {
		skip(source, Diagnostic::Kind::InvalidFile, "top level is not an object");
		return;
	}
	files_.push_back(std::move(*file));
}

std::optional<OrderedFile> FileOrder::classify(const TableSource& source, std::string_view stem) const
{
	if (equalsIgnoreCase(source.name, kDefaultFileName)) {
		return OrderedFile{ OrderedFile::Group::Default, 0, source.name, {} };
	}
	if (const auto index = lastActive(source, stem)) {
		return OrderedFile{ OrderedFile::Group::Plugin, *index, source.name, {} };
	}
	if (plugins_.isInstalled(stem)) {
		skip(source, Diagnostic::Kind::InactivePlugin, "");
		return std::nullopt;
	}
	return OrderedFile{ OrderedFile::Group::Unnamed, 0, source.name, {} };
}

// Load-order index of the last active plugin named `stem`. Warns when several
// active plugins share that name.
std::optional<std::size_t> FileOrder::lastActive(const TableSource& source, std::string_view stem) const
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
		diagnostics_.push_back({ Diagnostic::Kind::SameNamePlugins, source.name, "", joined(matches) });
	}
	return last;
}

void FileOrder::skip(const TableSource& source, Diagnostic::Kind kind, std::string detail) const
{
	diagnostics_.push_back({ kind, source.name, "", std::move(detail) });
}

// One file (or the registered entries) being merged. Keys from the default
// layers may be overridden freely; keys from patch files are tracked.
struct Layer
{
	std::string file;
	bool        isPatch = false;
};

// Applies layers on top of each other and remembers which patch file set which
// key, so that a patch overriding another patch is reported.
class Merger
{
public:
	explicit Merger(MergeResult& result) :
		result_(result) {}

	void apply(const json& content, const Layer& layer) { mergeObject(result_.merged, content, json::json_pointer{}, layer); }

private:
	void mergeObject(json& target, const json& patch, const json::json_pointer& path, const Layer& layer);
	void mergeKey(json& target, const std::string& key, const json& value, const json::json_pointer& path, const Layer& layer);
	void removeKey(json& target, const std::string& key, const json::json_pointer& path, const Layer& layer);
	void claim(const json::json_pointer& path, const Layer& layer);
	void release(const std::string& path, const Layer& layer);
	void releaseChildren(const std::string& path, const Layer& layer);
	void reportConflict(const std::string& path, const std::string& earlierFile, const Layer& layer);

	MergeResult&                       result_;
	std::map<std::string, std::string> owners_;  // JSON pointer of a key a patch set -> that patch file
};

void Merger::mergeObject(json& target, const json& patch, const json::json_pointer& path, const Layer& layer)
{
	for (const auto& [key, value] : patch.items()) {
		mergeKey(target, key, value, path / key, layer);
	}
}

void Merger::mergeKey(json& target, const std::string& key, const json& value, const json::json_pointer& path, const Layer& layer)
{
	if (value.is_null()) {
		removeKey(target, key, path, layer);
		return;
	}
	if (!value.is_object()) {
		claim(path, layer);
		target[key] = value;
		return;
	}
	auto& slot = target[key];
	if (!slot.is_object()) {
		// Turning a missing or non-object key into an object sets the key
		// itself; the keys inside belong to whichever layer sets them.
		release(path.to_string(), layer);
		slot = json::object();
	}
	mergeObject(slot, value, path, layer);
}

void Merger::removeKey(json& target, const std::string& key, const json::json_pointer& path, const Layer& layer)
{
	claim(path, layer);
	if (target.erase(key) == 0) {
		return;
	}
	result_.diagnostics.push_back({ Diagnostic::Kind::Removed, layer.file, path.to_string(), "" });
}

// Records that `layer` set the key at `path`, replacing everything below it.
void Merger::claim(const json::json_pointer& path, const Layer& layer)
{
	if (!layer.isPatch) {
		return;
	}
	auto key = path.to_string();
	release(key, layer);
	releaseChildren(key, layer);
	owners_.emplace(std::move(key), layer.file);
}

void Merger::release(const std::string& path, const Layer& layer)
{
	const auto it = owners_.find(path);
	if (it == owners_.end()) {
		return;
	}
	reportConflict(it->first, it->second, layer);
	owners_.erase(it);
}

void Merger::releaseChildren(const std::string& path, const Layer& layer)
{
	const auto prefix = path + "/";
	auto       it = owners_.lower_bound(prefix);
	while (it != owners_.end() && it->first.starts_with(prefix)) {
		reportConflict(it->first, it->second, layer);
		it = owners_.erase(it);
	}
}

void Merger::reportConflict(const std::string& path, const std::string& earlierFile, const Layer& layer)
{
	result_.diagnostics.push_back({ Diagnostic::Kind::Conflict, layer.file, path, earlierFile });
}

}

MergeResult mergeTable(std::span<const TableSource> sources, const PluginQuery& plugins, const nlohmann::json& registered)
{
	MergeResult result;
	FileOrder   order(plugins, result.diagnostics);
	for (const auto& source : sources) {
		order.add(source);
	}

	Merger merger(result);
	if (registered.is_object()) {
		merger.apply(registered, Layer{});
	}
	for (const auto& file : std::move(order).sorted()) {
		result.diagnostics.push_back({ Diagnostic::Kind::Loaded, file.name, "", "" });
		merger.apply(file.content, Layer{ file.name, file.group != OrderedFile::Group::Default });
	}
	return result;
}

}
