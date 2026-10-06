#include "editing/image_document_history.h"

#include <utility>

namespace image_editor {

void ImageDocumentHistory::clear() noexcept {
    undo_stack_.clear();
    redo_stack_.clear();
}

void ImageDocumentHistory::record(Snapshot snapshot) {
    undo_stack_.append(std::move(snapshot));
    if (undo_stack_.size() > kMaximumHistoryEntries) undo_stack_.removeFirst();
    redo_stack_.clear();
}

std::optional<ImageDocumentHistory::Snapshot> ImageDocumentHistory::undo(
    Snapshot current) {
    if (undo_stack_.isEmpty()) return std::nullopt;
    redo_stack_.append(std::move(current));
    return undo_stack_.takeLast();
}

std::optional<ImageDocumentHistory::Snapshot> ImageDocumentHistory::redo(
    Snapshot current) {
    if (redo_stack_.isEmpty()) return std::nullopt;
    undo_stack_.append(std::move(current));
    return redo_stack_.takeLast();
}

} // namespace image_editor
