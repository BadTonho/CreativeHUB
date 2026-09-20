#pragma once

#include "video_frame.h"
#include "video_metadata.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace media {

inline constexpr std::string_view default_bin = "Unsorted";

struct MediaItem {
    VideoMetadata metadata;
    VideoFrame first_frame;
    std::string display_name;
    std::string bin_path{default_bin};
    bool offline = false;
};

enum class MediaMutationResult {
    Changed,
    Duplicate,
    InvalidIndex,
    InvalidName,
    InvalidBin,
    NoChange,
    NotOffline,
};

class MediaLibrary final {
public:
    [[nodiscard]] const std::vector<MediaItem>& items() const noexcept { return items_; }
    [[nodiscard]] std::vector<MediaItem>& items() noexcept { return items_; }
    [[nodiscard]] const std::vector<std::string>& bins() const noexcept { return bins_; }

    [[nodiscard]] std::size_t size() const noexcept { return items_.size(); }
    [[nodiscard]] bool empty() const noexcept { return items_.empty(); }
    [[nodiscard]] bool contains(const std::filesystem::path& path) const;
    [[nodiscard]] std::size_t indexForPath(const std::filesystem::path& path) const;
    [[nodiscard]] bool isInBin(std::size_t index, std::string_view selected_bin) const;

    MediaMutationResult addOnline(
        VideoMetadata metadata,
        VideoFrame first_frame,
        std::string display_name = {},
        std::string bin_path = std::string(default_bin));
    MediaMutationResult addOffline(
        std::filesystem::path source_path,
        std::string display_name = {},
        std::string bin_path = std::string(default_bin));
    MediaMutationResult restore(
        std::size_t index,
        VideoMetadata metadata,
        VideoFrame first_frame);
    MediaMutationResult rename(std::size_t index, std::string display_name);
    MediaMutationResult moveToBin(std::size_t index, std::string bin_path);
    MediaMutationResult markOffline(std::size_t index);
    MediaMutationResult createBin(std::string bin_path);
    MediaMutationResult renameBin(std::string old_path, std::string new_path);
    void clear() noexcept;

    static std::filesystem::path canonicalPath(const std::filesystem::path& path);
    static bool validBinPath(std::string_view path) noexcept;
    static std::string defaultDisplayName(const std::filesystem::path& path);

private:
    static std::string normalizeBinPath(std::string_view path);
    void ensureBinPath(std::string_view path);

    std::vector<MediaItem> items_;
    std::vector<std::string> bins_{std::string(default_bin)};
};

} // namespace media
