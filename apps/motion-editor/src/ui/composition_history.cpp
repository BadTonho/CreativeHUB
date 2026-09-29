#include "composition_history.h"

#include <utility>

namespace motion::ui {

void CompositionHistory::recordBeforeEdit(CompositionEditState state)
{
    undo_states_.push_back(std::move(state));
    trimToLimit(undo_states_);
    redo_states_.clear();
}

void CompositionHistory::beginCoalescedEdit(CompositionEditState state)
{
    if (!coalesced_before_.has_value()) coalesced_before_ = std::move(state);
}

void CompositionHistory::finishCoalescedEdit(const CompositionEditState& current)
{
    if (!coalesced_before_.has_value()) return;
    if (!sameComposition(coalesced_before_->document, current.document)) {
        undo_states_.push_back(std::move(*coalesced_before_));
        trimToLimit(undo_states_);
        redo_states_.clear();
    }
    coalesced_before_.reset();
}

std::optional<CompositionEditState> CompositionHistory::undo(
    const CompositionEditState& current)
{
    finishCoalescedEdit(current);
    if (undo_states_.empty()) return std::nullopt;

    redo_states_.push_back(current);
    trimToLimit(redo_states_);
    auto previous = std::move(undo_states_.back());
    undo_states_.pop_back();
    return previous;
}

std::optional<CompositionEditState> CompositionHistory::redo(
    const CompositionEditState& current)
{
    finishCoalescedEdit(current);
    if (redo_states_.empty()) return std::nullopt;

    undo_states_.push_back(current);
    trimToLimit(undo_states_);
    auto next = std::move(redo_states_.back());
    redo_states_.pop_back();
    return next;
}

void CompositionHistory::clear() noexcept
{
    undo_states_.clear();
    redo_states_.clear();
    coalesced_before_.reset();
}

bool CompositionHistory::canUndo() const noexcept
{
    return !undo_states_.empty() || coalesced_before_.has_value();
}

bool CompositionHistory::canRedo() const noexcept
{
    return !coalesced_before_.has_value() && !redo_states_.empty();
}

std::size_t CompositionHistory::undoCount() const noexcept
{
    return undo_states_.size() + (coalesced_before_.has_value() ? 1U : 0U);
}

std::size_t CompositionHistory::redoCount() const noexcept
{
    return canRedo() ? redo_states_.size() : 0U;
}

bool CompositionHistory::sameComposition(
    const model::CompositionDocument& left,
    const model::CompositionDocument& right) noexcept
{
    return left.canvasSize() == right.canvasSize() &&
        left.frameRate() == right.frameRate() &&
        left.layers() == right.layers();
}

void CompositionHistory::trimToLimit(std::deque<CompositionEditState>& states)
{
    while (states.size() > max_states) states.pop_front();
}

} // namespace motion::ui
