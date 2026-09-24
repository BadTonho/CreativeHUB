#pragma once

#include "image_document_store.h"

#include <QImage>
#include <QString>
#include <QVector>

namespace image_editor {

class ImageDocumentSession final {
public:
    [[nodiscard]] bool createCanvas(const QSize& size,
                                    const QColor& background,
                                    QString* error = nullptr);
    [[nodiscard]] bool openImage(const QString& source_path, QString* error = nullptr);
    [[nodiscard]] bool openDocument(const QString& document_path, QString* error = nullptr);
    [[nodiscard]] bool restoreRecovery(const QString& recovery_path, QString* error = nullptr);
    [[nodiscard]] bool relinkSource(const QString& source_path, QString* error = nullptr);

    [[nodiscard]] bool saveDocument(QString document_path = {}, QString* error = nullptr);
    [[nodiscard]] bool exportImage(const QString& output_path, QString* error = nullptr) const;

    [[nodiscard]] bool applyCrop(const QRect& crop, QString* error = nullptr);
    [[nodiscard]] bool applyPaintStroke(const QVector<QPointF>& points,
                                        const QColor& color,
                                        int diameter,
                                        QString* error = nullptr);
    [[nodiscard]] QString addLayer();
    [[nodiscard]] bool deleteLayer(const QString& layer_id);
    [[nodiscard]] bool renameLayer(const QString& layer_id,
                                   const QString& name,
                                   QString* error = nullptr);
    [[nodiscard]] bool moveLayer(const QString& layer_id, int direction);
    [[nodiscard]] bool setLayerVisible(const QString& layer_id, bool visible);
    [[nodiscard]] bool setLayerOpacity(const QString& layer_id, int opacity);
    void beginLayerOpacityEdit();
    void endLayerOpacityEdit();
    [[nodiscard]] bool selectLayer(const QString& layer_id);
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
    [[nodiscard]] bool hasDocument() const noexcept {
        return data_.base_kind == ImageBaseKind::Canvas || !data_.source_path.isEmpty();
    }
    [[nodiscard]] QString sourcePath() const { return data_.source_path; }
    [[nodiscard]] QString documentPath() const { return document_path_; }
    [[nodiscard]] QString recoveryTargetPath() const { return document_path_; }
    [[nodiscard]] QString recoverySessionId() const { return recovery_session_id_; }
    [[nodiscard]] const ImageDocumentData& data() const noexcept { return data_; }
    [[nodiscard]] QString selectedLayerId() const { return selected_layer_id_; }
    [[nodiscard]] bool selectedLayerIsEditable() const noexcept;

private:
    struct EditSnapshot {
        ImageDocumentData document;
        QString selected_layer_id;
    };

    void initializeDefaultLayers();
    [[nodiscard]] qsizetype layerIndex(const QString& layer_id) const noexcept;
    void pushEdit();
    void recordEditSnapshot(ImageDocumentData before, QString selected_layer_id);
    [[nodiscard]] bool loadSource(const QString& path, QImage* image, QString* error) const;
    [[nodiscard]] QSize renderedSize() const;

    ImageDocumentData data_;
    QImage source_image_;
    QString document_path_;
    QString recovery_session_id_;
    QString selected_layer_id_;
    QString baseline_source_path_;
    QSize baseline_source_size_;
    ImageBaseKind baseline_base_kind_ = ImageBaseKind::SourceImage;
    QColor baseline_canvas_background_ = QColor(0, 0, 0, 0);
    QVector<ImageOperation> baseline_operations_;
    QVector<ImageLayerData> baseline_layers_;
    bool force_dirty_ = false;
    QVector<EditSnapshot> undo_stack_;
    QVector<EditSnapshot> redo_stack_;
    ImageDocumentData opacity_edit_snapshot_;
    bool opacity_edit_active_ = false;
    static constexpr qsizetype kMaximumHistoryEntries = 100;
};

} // namespace image_editor
