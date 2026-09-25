#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../timeline/timeline_model.h"
#include "../timeline/timeline_layout.h"

namespace project {

inline constexpr int current_format_version = 10;
inline constexpr int stable_ids_format_version = 9;
inline constexpr int linked_image_format_version = 10;
inline constexpr int media_kind_format_version = 8;
inline constexpr int timeline_zoom_format_version = 6;
inline constexpr int timeline_row_height_format_version = 7;
inline constexpr int transitions_format_version = 5;
inline constexpr int clip_kind_format_version = 4;
inline constexpr int canvas_format_version = 3;
inline constexpr int legacy_v2_format_version = 2;
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
    timeline::ClipId clip_id = 0;
    std::optional<media::LinkedImageReference> image_editor_variant;

    friend bool operator==(const ProjectClip&, const ProjectClip&) = default;
};

struct ProjectTransition {
    std::size_t from_clip_index = 0;
    std::size_t to_clip_index = 0;
    timeline::TransitionKind kind = timeline::TransitionKind::CrossDissolve;
    std::int64_t duration_frames = 15;

    friend bool operator==(const ProjectTransition&, const ProjectTransition&) = default;
};

struct ProjectTrack {
    std::string name;
    double audio_gain = 1.0;
    bool audio_muted = false;
    std::vector<ProjectClip> clips;
    std::vector<ProjectTransition> transitions;
    timeline::TrackId track_id = 0;

    friend bool operator==(const ProjectTrack&, const ProjectTrack&) = default;
};

struct ProjectMedia {
    std::filesystem::path source_path;
    std::string display_name;
    std::string bin_path = "Unsorted";
    bool offline = false;
    media::MediaKind kind = media::MediaKind::Video;
    std::optional<media::LinkedImageReference> image_editor_link;

    friend bool operator==(const ProjectMedia&, const ProjectMedia&) = default;
};

struct ProjectDocument {
    int canvas_width = 1920;
    int canvas_height = 1080;
    double timeline_zoom = 1.0;
    double timeline_row_height = timeline::kDefaultTrackRowHeight;
    std::vector<ProjectMedia> media;
    std::vector<std::string> bins;
    std::vector<ProjectTrack> timeline_tracks;

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
