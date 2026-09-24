#pragma once

#include "image_document_store.h"

#include <QImage>
#include <QString>

namespace image_editor {

class ImageDocumentSession final {
public:
    [[nodiscard]] bool openImage(const QString& source_path, QString* error = nullptr);
    [[nodiscard]] bool openDocument(const QString& document_path, QString* error = nullptr);
    [[nodiscard]] bool restoreRecovery(const QString& recovery_path, QString* error = nullptr);
    [[nodiscard]] bool relinkSource(const QString& source_path, QString* error = nullptr);

    [[nodiscard]] bool saveDocument(QString document_path = {}, QString* error = nullptr);
    [[nodiscard]] bool exportImage(const QString& output_path, QString* error = nullptr) const;

    [[nodiscard]] bool applyCrop(const QRect& crop, QString* error = nullptr);
    void rotateLeft();
    void rotateRight();
    void flipHorizontal();
    void flipVertical();
    [[nodiscard]] bool undo();
    [[nodiscard]] bool redo();

    [[nodiscard]] QImage renderedImage() const;
    [[nodiscard]] bool hasSource() const noexcept { return !source_image_.isNull(); }
    [[nodiscard]] bool sourceIsMissing() const noexcept { return !hasSource() && !data_.source_path.isEmpty(); }
    [[nodiscard]] bool isDirty() const noexcept;
    [[nodiscard]] bool canUndo() const noexcept { return !undo_stack_.isEmpty(); }
    [[nodiscard]] bool canRedo() const noexcept { return !redo_stack_.isEmpty(); }
    [[nodiscard]] bool hasDocument() const noexcept { return !data_.source_path.isEmpty(); }
    [[nodiscard]] QString sourcePath() const { return data_.source_path; }
    [[nodiscard]] QString documentPath() const { return document_path_; }
    [[nodiscard]] QString recoveryTargetPath() const { return document_path_; }
    [[nodiscard]] const ImageDocumentData& data() const noexcept { return data_; }

private:
    struct EditSnapshot {
        QVector<ImageOperation> operations;
    };

    void pushEdit();
    [[nodiscard]] bool loadSource(const QString& path, QImage* image, QString* error) const;
    [[nodiscard]] QSize renderedSize() const;

    ImageDocumentData data_;
    QImage source_image_;
    QString document_path_;
    QString baseline_source_path_;
    QVector<ImageOperation> baseline_operations_;
    bool force_dirty_ = false;
    QVector<EditSnapshot> undo_stack_;
    QVector<EditSnapshot> redo_stack_;
    static constexpr qsizetype kMaximumHistoryEntries = 100;
};

} // namespace image_editor
