#pragma once

#include "timeline_model.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>

namespace timeline {

struct EditState {
    TimelineModel::Snapshot timeline;
    std::optional<TrackId> active_track_id;
    std::optional<ClipId> active_clip_id;
    std::optional<std::filesystem::path> selected_source_path;
    std::optional<TransitionSelection> active_transition;
    std::int64_t playhead_frame = 0;
    std::optional<std::int64_t> preserved_playhead_frame;

    friend bool operator==(const EditState&, const EditState&) = default;
};

class TimelineHistory final {
public:
    static constexpr std::size_t max_states = 100;

    void recordBeforeEdit(EditState state);
    [[nodiscard]] std::optional<EditState> undo(const EditState& current);
    [[nodiscard]] std::optional<EditState> redo(const EditState& current);
    void clear() noexcept;

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] std::size_t undoCount() const noexcept;
    [[nodiscard]] std::size_t redoCount() const noexcept;

private:
    static void trimToLimit(std::deque<EditState>& states);

    std::deque<EditState> undo_states_;
    std::deque<EditState> redo_states_;
};

} // namespace timeline
