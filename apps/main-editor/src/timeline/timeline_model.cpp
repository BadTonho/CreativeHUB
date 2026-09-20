#include "timeline_model.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <system_error>
#include <utility>

namespace timeline {
namespace {

std::filesystem::path canonicalPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (!error) return canonical;

    const auto absolute = std::filesystem::absolute(path, error);
    if (!error) return absolute.lexically_normal();
    return path.lexically_normal();
}

std::optional<std::int64_t> durationInFrames(
    const media::VideoMetadata& metadata) {
    if (metadata.frame_count.has_value() && *metadata.frame_count > 0) {
        return *metadata.frame_count;
    }

    if (!metadata.duration_seconds.has_value() ||
        !metadata.frame_rate.has_value() ||
        !std::isfinite(*metadata.duration_seconds) ||
        !std::isfinite(*metadata.frame_rate) ||
        *metadata.duration_seconds <= 0.0 ||
        *metadata.frame_rate <= 0.0) {
        return std::nullopt;
    }

    const double estimated_frames =
        *metadata.duration_seconds * *metadata.frame_rate;
    if (!std::isfinite(estimated_frames) ||
        estimated_frames > static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }

    return std::max<std::int64_t>(
        1,
        static_cast<std::int64_t>(std::ceil(estimated_frames)));
}

TimelineClip makeClip(
    const media::VideoMetadata& metadata,
    std::int64_t timeline_start_frame,
    std::int64_t timeline_duration_frames) {
    return TimelineClip{
        timeline_start_frame,
        timeline_duration_frames,
        canonicalPath(metadata.source_path),
        metadata.display_name,
        metadata.duration_seconds,
        metadata.frame_rate,
        metadata.frame_count};
}

} // namespace

AddClipResult TimelineModel::addClip(const media::VideoMetadata& metadata) {
    const auto duration_frames = durationInFrames(metadata);
    if (!duration_frames.has_value()) {
        return AddClipResult::InvalidTimingMetadata;
    }

    const auto timeline_start_frame = totalDurationFrames();
    if (*duration_frames > std::numeric_limits<std::int64_t>::max() -
            timeline_start_frame) {
        return AddClipResult::InvalidTimingMetadata;
    }

    clips_.push_back(makeClip(
        metadata,
        timeline_start_frame,
        *duration_frames));
    return AddClipResult::Added;
}

MoveClipResult TimelineModel::moveClip(
    std::size_t from_index,
    std::size_t to_index) {
    if (from_index >= clips_.size() || to_index >= clips_.size()) {
        return MoveClipResult::InvalidIndex;
    }
    if (from_index == to_index) {
        return MoveClipResult::NoChange;
    }

    if (from_index < to_index) {
        std::rotate(
            clips_.begin() + static_cast<std::ptrdiff_t>(from_index),
            clips_.begin() + static_cast<std::ptrdiff_t>(from_index + 1),
            clips_.begin() + static_cast<std::ptrdiff_t>(to_index + 1));
    } else {
        std::rotate(
            clips_.begin() + static_cast<std::ptrdiff_t>(to_index),
            clips_.begin() + static_cast<std::ptrdiff_t>(from_index),
            clips_.begin() + static_cast<std::ptrdiff_t>(from_index + 1));
    }

    std::int64_t timeline_start_frame = 0;
    for (auto& clip : clips_) {
        clip.timeline_start_frame = timeline_start_frame;
        timeline_start_frame += clip.timeline_duration_frames;
    }

    return MoveClipResult::Moved;
}

void TimelineModel::clear() noexcept {
    clips_.clear();
}

bool TimelineModel::hasClip() const noexcept {
    return !clips_.empty();
}

std::size_t TimelineModel::clipCount() const noexcept {
    return clips_.size();
}

std::int64_t TimelineModel::totalDurationFrames() const noexcept {
    if (clips_.empty()) return 0;

    const auto& last_clip = clips_.back();
    return last_clip.timeline_start_frame + last_clip.timeline_duration_frames;
}

const std::vector<TimelineClip>& TimelineModel::clips() const noexcept {
    return clips_;
}

std::optional<std::size_t> TimelineModel::firstClipIndexForSource(
    const std::filesystem::path& source_path) const {
    const auto canonical_source = canonicalPath(source_path);
    for (std::size_t index = 0; index < clips_.size(); ++index) {
        if (clips_[index].source_path == canonical_source) {
            return index;
        }
    }
    return std::nullopt;
}

} // namespace timeline
