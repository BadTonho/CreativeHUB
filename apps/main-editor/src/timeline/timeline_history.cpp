#include "timeline_history.h"

#include <utility>

namespace timeline {

void TimelineHistory::recordBeforeEdit(EditState state) {
    undo_states_.push_back(std::move(state));
    trimToLimit(undo_states_);
    redo_states_.clear();
}

std::optional<EditState> TimelineHistory::undo(const EditState& current) {
    if (undo_states_.empty()) return std::nullopt;

    redo_states_.push_back(current);
    trimToLimit(redo_states_);

    EditState previous = std::move(undo_states_.back());
    undo_states_.pop_back();
    return previous;
}

std::optional<EditState> TimelineHistory::redo(const EditState& current) {
    if (redo_states_.empty()) return std::nullopt;

    undo_states_.push_back(current);
    trimToLimit(undo_states_);

    EditState next = std::move(redo_states_.back());
    redo_states_.pop_back();
    return next;
}

void TimelineHistory::clear() noexcept {
    undo_states_.clear();
    redo_states_.clear();
}

bool TimelineHistory::canUndo() const noexcept {
    return !undo_states_.empty();
}

bool TimelineHistory::canRedo() const noexcept {
    return !redo_states_.empty();
}

std::size_t TimelineHistory::undoCount() const noexcept {
    return undo_states_.size();
}

std::size_t TimelineHistory::redoCount() const noexcept {
    return redo_states_.size();
}

void TimelineHistory::trimToLimit(std::deque<EditState>& states) {
    while (states.size() > max_states) states.pop_front();
}

} // namespace timeline
