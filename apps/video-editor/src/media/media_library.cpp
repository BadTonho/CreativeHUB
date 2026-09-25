#include "media_library.h"

#include "video_frame.h"

#include <algorithm>
#include <cctype>
#include <system_error>

namespace media {
namespace {

bool hasPrefix(std::string_view value, std::string_view prefix) {
    return value.size() > prefix.size() &&
        value.substr(0, prefix.size()) == prefix &&
        value[prefix.size()] == '/';
}

} // namespace

std::filesystem::path MediaLibrary::canonicalPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) return canonical;
    const auto absolute = std::filesystem::absolute(path, error);
    if (!error) return absolute.lexically_normal();
    return path.lexically_normal();
}

bool MediaLibrary::validBinPath(std::string_view path) noexcept {
    if (path.empty() || path.front() == '/' || path.back() == '/') return false;
    if (path.find('\\') != std::string_view::npos) return false;
    std::size_t start = 0;
    while (start < path.size()) {
        const auto separator = path.find('/', start);
        const auto component = path.substr(
            start,
            separator == std::string_view::npos ? path.size() - start : separator - start);
        if (component.empty() || component == "." || component == "..") return false;
        start = separator == std::string_view::npos ? path.size() : separator + 1;
    }
    return true;
}

std::string MediaLibrary::normalizeBinPath(std::string_view path) {
    return std::string(path);
}

std::string MediaLibrary::defaultDisplayName(const std::filesystem::path& path) {
    const auto name = path.filename().u8string();
    return std::string(reinterpret_cast<const char*>(name.data()), name.size());
}

void MediaLibrary::ensureBinPath(std::string_view path) {
    std::string current;
    const std::string normalized = normalizeBinPath(path);
    std::size_t start = 0;
    while (start < normalized.size()) {
        const auto separator = normalized.find('/', start);
        const auto component = normalized.substr(
            start,
            separator == std::string::npos ? normalized.size() - start : separator - start);
        if (!current.empty()) current += '/';
        current += component;
        if (std::find(bins_.begin(), bins_.end(), current) == bins_.end()) {
            bins_.push_back(current);
        }
        start = separator == std::string::npos ? normalized.size() : separator + 1;
    }
}

bool MediaLibrary::contains(const std::filesystem::path& path) const {
    return indexForPath(path) != items_.size();
}

std::size_t MediaLibrary::indexForPath(const std::filesystem::path& path) const {
    const auto canonical = canonicalPath(path);
    const auto found = path_index_.find(canonical);
    return found == path_index_.end() ? items_.size() : found->second;
}

bool MediaLibrary::isInBin(std::size_t index, std::string_view selected_bin) const {
    if (index >= items_.size()) return false;
    if (selected_bin.empty()) return true;
    const auto& item_bin = items_[index].bin_path;
    return item_bin == selected_bin || hasPrefix(item_bin, selected_bin);
}

MediaMutationResult MediaLibrary::addOnline(
    VideoMetadata metadata,
    VideoFrame first_frame,
    std::string display_name,
    std::string bin_path) {
    metadata.source_path = canonicalPath(metadata.source_path);
    if (contains(metadata.source_path)) return MediaMutationResult::Duplicate;
    if (!validBinPath(bin_path)) return MediaMutationResult::InvalidBin;
    if (display_name.empty()) display_name = metadata.display_name;
    if (display_name.empty()) display_name = defaultDisplayName(metadata.source_path);
    ensureBinPath(bin_path);
    metadata.display_name = display_name;
    items_.push_back({std::move(metadata), std::move(first_frame),
                      std::move(display_name), std::move(bin_path), false});
    path_index_.emplace(items_.back().metadata.source_path, items_.size() - 1);
    return MediaMutationResult::Changed;
}

MediaMutationResult MediaLibrary::addOffline(
    std::filesystem::path source_path,
    std::string display_name,
    std::string bin_path,
    MediaKind kind) {
    source_path = canonicalPath(source_path);
    if (contains(source_path)) return MediaMutationResult::Duplicate;
    if (!validBinPath(bin_path)) return MediaMutationResult::InvalidBin;
    if (display_name.empty()) display_name = defaultDisplayName(source_path);
    ensureBinPath(bin_path);
    VideoMetadata metadata;
    metadata.kind = kind;
    metadata.source_path = source_path;
    metadata.display_name = display_name;
    items_.push_back({std::move(metadata), {}, std::move(display_name),
                      std::move(bin_path), true});
    path_index_.emplace(items_.back().metadata.source_path, items_.size() - 1);
    return MediaMutationResult::Changed;
}

MediaMutationResult MediaLibrary::restore(
    std::size_t index,
    VideoMetadata metadata,
    VideoFrame first_frame) {
    if (index >= items_.size()) return MediaMutationResult::InvalidIndex;
    if (!items_[index].offline) return MediaMutationResult::NoChange;
    metadata.source_path = canonicalPath(metadata.source_path);
    metadata.display_name = items_[index].display_name;
    items_[index].metadata = std::move(metadata);
    items_[index].first_frame = std::move(first_frame);
    items_[index].offline = false;
    return MediaMutationResult::Changed;
}

MediaMutationResult MediaLibrary::rename(std::size_t index, std::string display_name) {
    if (index >= items_.size()) return MediaMutationResult::InvalidIndex;
    if (display_name.empty()) return MediaMutationResult::InvalidName;
    if (items_[index].display_name == display_name) return MediaMutationResult::NoChange;
    items_[index].display_name = std::move(display_name);
    items_[index].metadata.display_name = items_[index].display_name;
    return MediaMutationResult::Changed;
}

MediaMutationResult MediaLibrary::moveToBin(std::size_t index, std::string bin_path) {
    if (index >= items_.size()) return MediaMutationResult::InvalidIndex;
    if (!validBinPath(bin_path)) return MediaMutationResult::InvalidBin;
    if (items_[index].bin_path == bin_path) return MediaMutationResult::NoChange;
    ensureBinPath(bin_path);
    items_[index].bin_path = std::move(bin_path);
    return MediaMutationResult::Changed;
}

MediaMutationResult MediaLibrary::markOffline(std::size_t index) {
    if (index >= items_.size()) return MediaMutationResult::InvalidIndex;
    if (items_[index].offline) return MediaMutationResult::NoChange;
    items_[index].offline = true;
    items_[index].first_frame = {};
    return MediaMutationResult::Changed;
}

MediaMutationResult MediaLibrary::setImageEditorLink(
    const std::filesystem::path& path,
    std::optional<LinkedImageReference> link) {
    const auto index = indexForPath(path);
    if (index >= items_.size() || items_[index].metadata.kind != MediaKind::Image) {
        return MediaMutationResult::InvalidIndex;
    }
    if (items_[index].image_editor_link == link) return MediaMutationResult::NoChange;
    items_[index].image_editor_link = std::move(link);
    return MediaMutationResult::Changed;
}

MediaMutationResult MediaLibrary::refreshImagePresentation(
    const std::filesystem::path& path,
    VideoMetadata metadata,
    VideoFrame first_frame) {
    const auto index = indexForPath(path);
    if (index >= items_.size() || items_[index].metadata.kind != MediaKind::Image ||
        first_frame.width <= 0 || first_frame.height <= 0 ||
        first_frame.rgba_pixels.empty()) {
        return MediaMutationResult::InvalidIndex;
    }
    auto& item = items_[index];
    metadata.kind = MediaKind::Image;
    metadata.source_path = item.metadata.source_path;
    metadata.display_name = item.metadata.display_name;
    const bool unchanged = item.first_frame.width == first_frame.width &&
        item.first_frame.height == first_frame.height &&
        item.first_frame.rgba_pixels == first_frame.rgba_pixels &&
        item.metadata.width == metadata.width && item.metadata.height == metadata.height;
    if (unchanged) return MediaMutationResult::NoChange;
    item.metadata = std::move(metadata);
    item.first_frame = std::move(first_frame);
    item.offline = false;
    return MediaMutationResult::Changed;
}

MediaMutationResult MediaLibrary::createBin(std::string bin_path) {
    if (!validBinPath(bin_path)) return MediaMutationResult::InvalidBin;
    if (std::find(bins_.begin(), bins_.end(), bin_path) != bins_.end()) {
        return MediaMutationResult::NoChange;
    }
    ensureBinPath(bin_path);
    return MediaMutationResult::Changed;
}

MediaMutationResult MediaLibrary::renameBin(std::string old_path, std::string new_path) {
    return moveBin(std::move(old_path), std::move(new_path));
}

MediaMutationResult MediaLibrary::moveBin(
    std::string old_path,
    std::string new_path) {
    if (old_path.empty() || old_path == default_bin ||
        !validBinPath(new_path)) {
        return MediaMutationResult::InvalidBin;
    }
    if (std::find(bins_.begin(), bins_.end(), old_path) == bins_.end()) {
        return MediaMutationResult::InvalidBin;
    }
    if (old_path == new_path) return MediaMutationResult::NoChange;
    if (hasPrefix(new_path, old_path)) return MediaMutationResult::InvalidBin;
    for (const auto& bin : bins_) {
        if (bin == new_path || hasPrefix(bin, new_path)) {
            return MediaMutationResult::InvalidBin;
        }
    }

    const auto separator = new_path.rfind('/');
    if (separator != std::string::npos) {
        ensureBinPath(new_path.substr(0, separator));
    }
    for (auto& bin : bins_) {
        if (bin == old_path) bin = new_path;
        else if (hasPrefix(bin, old_path)) bin = new_path + bin.substr(old_path.size());
    }
    for (auto& item : items_) {
        if (item.bin_path == old_path) item.bin_path = new_path;
        else if (hasPrefix(item.bin_path, old_path)) {
            item.bin_path = new_path + item.bin_path.substr(old_path.size());
        }
    }
    return MediaMutationResult::Changed;
}

void MediaLibrary::clear() noexcept {
    items_.clear();
    path_index_.clear();
    bins_.clear();
    bins_.emplace_back(default_bin);
}

} // namespace media
