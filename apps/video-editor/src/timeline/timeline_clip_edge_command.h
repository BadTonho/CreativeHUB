#pragma once

#include "timeline_model.h"

#include <cstdint>
#include <optional>

namespace timeline {

struct ClipEdgeTrimSelection {
    ClipLocation location;
    std::int64_t playback_frame = 0;
    std::optional<std::int64_t> preserved_playhead_frame;
};

struct ClipEdgeTrimOutcome {
    TrimClipResult result = TrimClipResult::InvalidIndex;
    std::optional<ClipEdgeTrimSelection> selection;
};

// Applies the model edit and resolves the edited clip after the model sorts it.
// UI refresh, history recording, and playback commands remain with the caller.
[[nodiscard]] ClipEdgeTrimOutcome applyClipEdgeTrim(
    TimelineModel& model,
    ClipLocation location,
    ClipEdge edge,
    std::int64_t boundary_frame,
    ClipEdgeEditMode mode,
    std::int64_t playhead_before,
    std::int64_t playback_frame_before);

} // namespace timeline
