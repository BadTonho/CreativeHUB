#pragma once

#include "image_document_store.h"

#include <QHash>
#include <QImage>
#include <QString>
#include <QVector>

#include <optional>

namespace image_editor {

// Owns the in-memory snapshots used by a single ImageDocumentSession.
class ImageDocumentHistory final {
public:
    struct Snapshot {
        ImageDocumentData document;
        QString selected_layer_id;
        QString selected_group_id;
        QHash<QString, QImage> raster_images;
    };

    [[nodiscard]] bool canUndo() const noexcept { return !undo_stack_.isEmpty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_stack_.isEmpty(); }

    void clear() noexcept;
    void record(Snapshot snapshot);
    [[nodiscard]] std::optional<Snapshot> undo(Snapshot current);
    [[nodiscard]] std::optional<Snapshot> redo(Snapshot current);

private:
    static constexpr qsizetype kMaximumHistoryEntries = 100;

    QVector<Snapshot> undo_stack_;
    QVector<Snapshot> redo_stack_;
};

} // namespace image_editor
