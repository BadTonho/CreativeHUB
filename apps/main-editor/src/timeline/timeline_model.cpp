#include "timeline_model.h"

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

TimelineClip makeClip(const media::VideoMetadata& metadata) {
    return TimelineClip{
        canonicalPath(metadata.source_path),
        metadata.display_name,
        metadata.duration_seconds,
        metadata.frame_rate,
        metadata.frame_count};
}

} // namespace

AddClipResult TimelineModel::addClip(const media::VideoMetadata& metadata) {
    auto candidate = makeClip(metadata);
    if (clip_.has_value() && clip_->source_path == candidate.source_path) {
        return AddClipResult::AlreadyPresent;
    }
    if (clip_.has_value()) return AddClipResult::Occupied;

    clip_ = std::move(candidate);
    return AddClipResult::Added;
}

void TimelineModel::clear() noexcept {
    clip_.reset();
}

bool TimelineModel::hasClip() const noexcept {
    return clip_.has_value();
}

const TimelineClip* TimelineModel::clip() const noexcept {
    return clip_.has_value() ? &*clip_ : nullptr;
}

} // namespace timeline
