#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../timeline/timeline_model.h"

namespace project {

inline constexpr int current_format_version = 4;
inline constexpr int previous_format_version = 3;
inline constexpr int older_format_version = 2;
inline constexpr int legacy_format_version = 1;
inline constexpr const char* format_identifier = "creative-suite.main-editor";

struct ProjectClip {
    std::filesystem::path source_path;
    std::int64_t timeline_start_frame = 0;
    std::int64_t source_start_frame = 0;
    std::int64_t duration_frames = 0;
    double audio_gain = 1.0;
    bool audio_muted = false;
    timeline::Transform2D transform;
    timeline::TransformKeyframes keyframes;
    timeline::ClipKind kind = timeline::ClipKind::Video;
    timeline::TextStyle text;

    friend bool operator==(const ProjectClip&, const ProjectClip&) = default;
};

struct ProjectTrack {
    std::string name;
    double audio_gain = 1.0;
    bool audio_muted = false;
    std::vector<ProjectClip> clips;

    friend bool operator==(const ProjectTrack&, const ProjectTrack&) = default;
};

struct ProjectMedia {
    std::filesystem::path source_path;
    std::string display_name;
    std::string bin_path = "Unsorted";
    bool offline = false;

    friend bool operator==(const ProjectMedia&, const ProjectMedia&) = default;
};

struct ProjectDocument {
    int canvas_width = 1920;
    int canvas_height = 1080;
    std::vector<ProjectMedia> media;
    std::vector<std::string> bins;
    std::vector<ProjectTrack> timeline_tracks;
    // Compatibility view for callers still being migrated to timeline_tracks.
    std::vector<ProjectClip> timeline_clips;

    friend bool operator==(const ProjectDocument&, const ProjectDocument&) = default;
};

enum class ProjectErrorCode {
    Io,
    InvalidFormat,
    UnsupportedVersion,
    MissingField,
    InvalidValue,
    MediaUnavailable,
    InvalidTimeline,
};

class ProjectError final : public std::runtime_error {
public:
    ProjectError(
        ProjectErrorCode code,
        std::string message,
        std::optional<int> system_error = std::nullopt,
        std::filesystem::path related_path = {})
        : std::runtime_error(std::move(message)),
          code_(code),
          system_error_(system_error),
          related_path_(std::move(related_path)) {}

    [[nodiscard]] ProjectErrorCode code() const noexcept { return code_; }
    [[nodiscard]] const std::optional<int>& system_error() const noexcept {
        return system_error_;
    }
    [[nodiscard]] const std::filesystem::path& related_path() const noexcept {
        return related_path_;
    }

private:
    ProjectErrorCode code_;
    std::optional<int> system_error_;
    std::filesystem::path related_path_;
};

} // namespace project
