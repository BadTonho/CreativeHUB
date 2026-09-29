#pragma once

#include "../model/composition_document.h"

#include <cstddef>
#include <deque>
#include <optional>

namespace motion::ui {

struct CompositionEditState {
    model::CompositionDocument document;
    model::LayerId selected_layer_id = 0;
};

// Bounded, application-owned history for composition edits. UI navigation and
// Media Pool state are intentionally kept outside these snapshots.
class CompositionHistory final {
public:
    static constexpr std::size_t max_states = 100;

    void recordBeforeEdit(CompositionEditState state);
    void beginCoalescedEdit(CompositionEditState state);
    void finishCoalescedEdit(const CompositionEditState& current);
    [[nodiscard]] std::optional<CompositionEditState> undo(
        const CompositionEditState& current);
    [[nodiscard]] std::optional<CompositionEditState> redo(
        const CompositionEditState& current);
    void clear() noexcept;

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;
    [[nodiscard]] std::size_t undoCount() const noexcept;
    [[nodiscard]] std::size_t redoCount() const noexcept;

private:
    static bool sameComposition(const model::CompositionDocument& left,
                                const model::CompositionDocument& right) noexcept;
    static void trimToLimit(std::deque<CompositionEditState>& states);

    std::deque<CompositionEditState> undo_states_;
    std::deque<CompositionEditState> redo_states_;
    std::optional<CompositionEditState> coalesced_before_;
};

} // namespace motion::ui
