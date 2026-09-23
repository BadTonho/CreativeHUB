#include "playback_transition_plan.h"

#include <algorithm>

namespace playback::detail {

void applyTransitionRequests(
    std::vector<CompositionFrameRequest>& requests,
    std::span<const CompositionSessionRef> sessions,
    std::span<const CompositionTransitionSpec> transitions,
    std::int64_t global_frame) {
    // The worker sorts references for painting, but endpoint lookup has always
    // used session insertion order when duplicate identifiers are present.
    const auto find_session = [sessions](qint64 track_index,
                                         qint64 clip_index) -> const CompositionSessionRef* {
        const CompositionSessionRef* first = nullptr;
        for (const auto& session : sessions) {
            if (session.spec != nullptr &&
                session.spec->track_index == track_index &&
                session.spec->clip_index == clip_index &&
                (first == nullptr || session.session_index < first->session_index)) {
                first = &session;
            }
        }
        return first;
    };
    const auto remove_requests_for = [&requests](std::size_t session_index) {
        requests.erase(
            std::remove_if(
                requests.begin(),
                requests.end(),
                [session_index](const CompositionFrameRequest& request) {
                    return request.session_index == session_index;
                }),
            requests.end());
    };
    const auto append_transition_request = [&requests](
        std::size_t session_index,
        std::int64_t local_frame,
        double opacity_multiplier,
        bool allow_forward_decode = true) {
        if (opacity_multiplier > 0.0) {
            requests.push_back(CompositionFrameRequest{
                session_index,
                local_frame,
                std::clamp(opacity_multiplier, 0.0, 1.0),
                allow_forward_decode});
        }
    };

    for (const auto& transition : transitions) {
        if (transition.duration_frames <= 0) continue;
        const auto from = find_session(
            transition.track_index, transition.from_clip_index);
        const auto to = find_session(
            transition.track_index, transition.to_clip_index);
        if (from == nullptr || to == nullptr) continue;

        const auto boundary = transition.boundary_frame;
        const auto duration = transition.duration_frames;
        if (transition.kind == timeline::TransitionKind::CrossDissolve) {
            if (global_frame < boundary || global_frame >= boundary + duration) {
                continue;
            }
            const auto offset = global_frame - boundary;
            const auto from_local = std::max<std::int64_t>(
                0, from->spec->segment_frame_count - 1);
            const auto to_local = offset;
            const double blend = duration == 1
                ? 1.0
                : static_cast<double>(offset + 1) / static_cast<double>(duration);
            remove_requests_for(from->session_index);
            remove_requests_for(to->session_index);
            // FrameCompositor starts from an opaque black canvas. The outgoing
            // layer stays opaque; only incoming alpha controls the blend.
            append_transition_request(from->session_index, from_local, 1.0, false);
            append_transition_request(to->session_index, to_local, blend);
        } else if (transition.kind == timeline::TransitionKind::FadeToBlack) {
            if (global_frame >= boundary - duration && global_frame < boundary) {
                const auto offset = global_frame - (boundary - duration);
                const double fade = static_cast<double>(offset + 1) /
                    static_cast<double>(duration);
                remove_requests_for(from->session_index);
                append_transition_request(
                    from->session_index,
                    global_frame - from->spec->timeline_start_frame,
                    1.0 - fade);
            } else if (global_frame >= boundary && global_frame < boundary + duration) {
                const auto offset = global_frame - boundary;
                const double fade = duration == 1
                    ? 0.0
                    : static_cast<double>(offset) /
                        static_cast<double>(duration - 1);
                remove_requests_for(to->session_index);
                append_transition_request(to->session_index, offset, fade);
            }
        }
    }
}

} // namespace playback::detail
