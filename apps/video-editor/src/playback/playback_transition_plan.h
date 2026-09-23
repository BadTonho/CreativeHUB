#pragma once

#include "playback_worker.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace playback::detail {

// References a worker-owned composition session without copying media state.
struct CompositionSessionRef {
    const CompositionLayerSpec* spec = nullptr;
    std::size_t session_index = 0;
};

struct CompositionFrameRequest {
    std::size_t session_index = 0;
    std::int64_t local_frame = 0;
    double opacity_multiplier = 1.0;
    bool allow_forward_decode = true;
};

// Adjusts already-visible layer requests for transitions at global_frame.
// Decoding, final layer ordering, and composition remain in PlaybackWorker.
void applyTransitionRequests(
    std::vector<CompositionFrameRequest>& requests,
    std::span<const CompositionSessionRef> sessions,
    std::span<const CompositionTransitionSpec> transitions,
    std::int64_t global_frame);

} // namespace playback::detail
