#pragma once

#include "import_export/image_exporter.h"
#include "editing/image_document_history.h"
#include "image_document_store.h"
#include "import_export/image_raster_import.h"
#include "rendering/image_layer_raster_cache.h"

#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>

namespace image_editor {

enum class CanvasAnchor {
    TopLeft, Top, TopRight,
    Left, Center, Right,
    BottomLeft, Bottom, BottomRight,
};

class ImageDocumentSession final {
public:
    [[nodiscard]] bool importRasterImages(const QVector<PreparedRasterImage>& images,
        std::optional<QPointF> center = {}, QString* error = nullptr);
    [[nodiscard]] bool findRaster(const QString& id, ImageRasterData* raster,
        QString* layer_id = nullptr) const;
    [[nodiscard]] bool relinkRaster(const QString& id, const PreparedRasterImage& image,
        QString* error = nullptr);
    // Problems are keyed by object UUID; cached pixels remain stable for this session.
    [[nodiscard]] QHash<QString, QString> rasterSourceProblems() const;
    [[nodiscard]] QImage renderedImageWithObjects(
        const QVector<ImageObjectPlacement>& objects) const;
    [[nodiscard]] bool createCanvas(const QSize& size,
                                    const QColor& background,
                                    QString* error = nullptr);
    [[nodiscard]] bool openImage(const QString& source_path, QString* error = nullptr);
    [[nodiscard]] bool openDocument(const QString& document_path, QString* error = nullptr);
    [[nodiscard]] bool restoreRecovery(const QString& recovery_path, QString* error = nullptr);
    [[nodiscard]] bool relinkSource(const QString& source_path, QString* error = nullptr);
    [[nodiscard]] bool resizeCanvas(const QSize& size,
                                    CanvasAnchor anchor = CanvasAnchor::Center,
                                    QString* error = nullptr);

    [[nodiscard]] bool saveDocument(QString document_path = {}, QString* error = nullptr);
    [[nodiscard]] bool exportImage(const QString& output_path, QString* error = nullptr) const;
    [[nodiscard]] bool exportImage(const QString& output_path,
                                   const ImageExportOptions& options,
                                   QString* error = nullptr) const;
    [[nodiscard]] ImageExportSnapshot exportSnapshot() const;

    [[nodiscard]] bool applyCrop(const QRect& crop, QString* error = nullptr);
    [[nodiscard]] bool applyPaintStroke(const QVector<QPointF>& points,
                                        const QColor& color,
                                        int diameter,
                                        QString* error = nullptr,
                                        std::optional<QPainterPath> clipping_path = {});
    [[nodiscard]] bool applyEraseStroke(const QVector<QPointF>& points,
                                        int diameter,
                                        QString* error = nullptr,
                                        std::optional<QPainterPath> clipping_path = {});
    [[nodiscard]] bool applyBucketFill(const QPoint& seed,
                                      const QColor& color,
                                      int tolerance,
                                      QString* error = nullptr,
                                      std::optional<QPainterPath> clipping_path = {},
                                      bool mask_target = false);
    [[nodiscard]] bool applyLinearGradient(
        const QPointF& start, const QPointF& end, const QColor& color,
        QString* error = nullptr,
        std::optional<QPainterPath> clipping_path = {},
        bool mask_target = false);
    [[nodiscard]] QString addShape(ImageShapeData shape, QString* error = nullptr);
    [[nodiscard]] bool updateShape(const ImageShapeData& shape,
                                   QString* error = nullptr);
    [[nodiscard]] QString addText(ImageTextData text, QString* error = nullptr);
    [[nodiscard]] bool updateText(const ImageTextData& text, QString* error = nullptr);
    [[nodiscard]] bool findText(const QString& text_id,
                                ImageTextData* text,
                                QString* layer_id = nullptr) const;
    [[nodiscard]] bool updateShapeRendered(const ImageShapeData& shape,
                                           QString* error = nullptr);
    [[nodiscard]] bool deleteShape(const QString& shape_id);
    [[nodiscard]] bool findShape(const QString& shape_id,
                                 ImageShapeData* shape,
                                 QString* layer_id = nullptr) const;
    [[nodiscard]] QImage renderedImageWithoutShape(const QString& shape_id) const;
    [[nodiscard]] QVector<ImageShapePlacement> visibleShapes() const;
    [[nodiscard]] QVector<ImageObjectPlacement> visibleObjects() const;
    [[nodiscard]] QImage renderedImageWithoutObjects(const QStringList& object_ids) const;
    [[nodiscard]] bool updateObjectsRendered(
        const QVector<ImageObjectPlacement>& objects, QString* error = nullptr);
    [[nodiscard]] bool updateShapeStyles(const QStringList& shape_ids,
                                         const ImageShapeData& style,
                                         QString* error = nullptr);
    [[nodiscard]] bool deleteObjects(const QStringList& object_ids);
    [[nodiscard]] QString addLayer();
    [[nodiscard]] bool deleteLayer(const QString& layer_id);
    // Removes valid editable items and group children in one history edit.
    // Background, duplicate IDs, and unknown items are ignored.
    [[nodiscard]] bool deleteStackItems(const QVector<ImageStackItemData>& items);
    [[nodiscard]] bool renameLayer(const QString& layer_id,
                                   const QString& name,
                                   QString* error = nullptr);
    [[nodiscard]] bool moveLayer(const QString& layer_id, int direction);
    [[nodiscard]] bool setLayerVisible(const QString& layer_id, bool visible);
    [[nodiscard]] bool setLayerOpacity(const QString& layer_id, int opacity);
    [[nodiscard]] bool addLayerMask(const QString& layer_id);
    [[nodiscard]] bool removeLayerMask(const QString& layer_id);
    [[nodiscard]] bool setLayerMaskEnabled(const QString& layer_id, bool enabled);
    [[nodiscard]] bool applyLayerMaskStroke(const QVector<QPointF>& points,
                                            const QColor& color, int diameter,
                                            QString* error = nullptr,
                                            std::optional<QPainterPath> clipping_path = {});
    [[nodiscard]] bool applyLayerMaskEraseStroke(const QVector<QPointF>& points,
                                                 int diameter,
                                                 QString* error = nullptr,
                                                 std::optional<QPainterPath> clipping_path = {});
    [[nodiscard]] QImage renderedImageWithMaskStroke(
        const QVector<QPointF>& points, const QColor& color, int diameter,
        std::optional<QPainterPath> clipping_path = {}) const;
    [[nodiscard]] QHash<QString, QImage> renderedLayerMaskThumbnails(
        const QSize& maximum_size) const;
    [[nodiscard]] QString addGroup(QString* error = nullptr);
    [[nodiscard]] QString groupLayers(const QStringList& layer_ids,
                                      QString* error = nullptr);
    [[nodiscard]] bool ungroup(const QString& group_id);
    [[nodiscard]] bool deleteGroup(const QString& group_id);
    [[nodiscard]] bool renameGroup(const QString& group_id,
                                   const QString& name,
                                   QString* error = nullptr);
    [[nodiscard]] bool setGroupVisible(const QString& group_id, bool visible);
    [[nodiscard]] bool setGroupOpacity(const QString& group_id, int opacity);
    [[nodiscard]] bool moveStackItem(const QString& item_id,
                                     bool is_group,
                                     const QString& target_group_id,
                                     qsizetype insertion_index);
    [[nodiscard]] bool moveStackItemBy(const QString& item_id, bool is_group, int direction);
    [[nodiscard]] bool selectGroup(const QString& group_id);
    [[nodiscard]] bool selectedGroupIsActive() const noexcept;
    [[nodiscard]] bool applySelectedGroupTransform(const ImageOperation& operation,
                                                   QString* error = nullptr);
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
    [[nodiscard]] std::uint64_t cachedRasterLayerBytes() const noexcept {
        return layer_raster_cache_.retainedBytes();
    }
    [[nodiscard]] QImage renderedImageWithEraseStroke(
        const QVector<QPointF>& points, int diameter,
        std::optional<QPainterPath> clipping_path = {}) const;
    [[nodiscard]] QImage renderedImageWithLinearGradient(
        const QPointF& start, const QPointF& end, const QColor& color,
        std::optional<QPainterPath> clipping_path = {},
        bool mask_target = false) const;
    [[nodiscard]] QHash<QString, QImage> renderedLayerThumbnails(
        const QSize& maximum_size) const;
    [[nodiscard]] bool hasSource() const noexcept { return !source_image_.isNull(); }
    [[nodiscard]] bool sourceIsMissing() const noexcept { return !hasSource() && !data_.source_path.isEmpty(); }
    [[nodiscard]] bool isDirty() const noexcept;
    [[nodiscard]] bool canUndo() const noexcept { return history_.canUndo(); }
    [[nodiscard]] bool canRedo() const noexcept { return history_.canRedo(); }
    [[nodiscard]] bool hasDocument() const noexcept {
        return data_.base_kind == ImageBaseKind::Canvas || !data_.source_path.isEmpty();
    }
    [[nodiscard]] QString sourcePath() const { return data_.source_path; }
    [[nodiscard]] QString documentPath() const { return document_path_; }
    [[nodiscard]] QString recoveryTargetPath() const { return document_path_; }
    [[nodiscard]] QString recoverySessionId() const { return recovery_session_id_; }
    [[nodiscard]] const ImageDocumentData& data() const noexcept { return data_; }
    [[nodiscard]] QString selectedLayerId() const { return selected_layer_id_; }
    [[nodiscard]] QString selectedGroupId() const { return selected_group_id_; }
    [[nodiscard]] bool selectedLayerIsEditable() const noexcept;

private:
    struct LayerThumbnailCacheEntry {
        QVector<ImageOperation> operations;
        QSize source_size;
        QSize maximum_size;
        qint64 source_cache_key = 0;
        bool background = false;
        std::optional<ImageLayerMaskData> mask;
        QImage thumbnail;
    };

    struct GroupThumbnailCacheEntry {
        QSize maximum_size;
        QImage thumbnail;
    };

    void initializeDefaultLayers();
    void invalidateGroupThumbnailCache() const noexcept;
    [[nodiscard]] qsizetype layerIndex(const QString& layer_id) const noexcept;
    void pushEdit();
    void recordEditSnapshot(ImageDocumentData before,
                            QString selected_layer_id,
                            QString selected_group_id = {});
    void restoreHistorySnapshot(ImageDocumentHistory::Snapshot snapshot);
    void commitDocumentEdit(ImageDocumentData document,
                            QString selected_layer_id,
                            QString selected_group_id);
    [[nodiscard]] qsizetype groupIndex(const QString& group_id) const noexcept;
    [[nodiscard]] QString parentGroupForLayer(const QString& layer_id) const;
    [[nodiscard]] bool effectiveLayerVisible(const ImageLayerData& layer) const;
    [[nodiscard]] bool applyLayerMaskStrokeInternal(
        const QVector<QPointF>& points, const QColor& color, int diameter,
        QString* error, std::optional<QPainterPath> clipping_path,
        bool erase);
    [[nodiscard]] std::optional<ImageOperation> prepareLinearGradientOperation(
        const QPointF& start, const QPointF& end, const QColor& color,
        std::optional<QPainterPath> clipping_path, bool mask_target,
        QString* error) const;
    [[nodiscard]] bool loadSource(const QString& path, QImage* image, QString* error) const;
    [[nodiscard]] QSize renderedSize() const;
    void loadRasterSources();

    ImageDocumentData data_;
    QImage source_image_;
    QHash<QString, QImage> raster_images_;
    QHash<QString, QString> raster_errors_;
    QString document_path_;
    QString recovery_session_id_;
    QString selected_layer_id_;
    QString selected_group_id_;
    QString baseline_source_path_;
    QSize baseline_source_size_;
    QSize baseline_canvas_size_;
    QPoint baseline_canvas_base_offset_;
    ImageBaseKind baseline_base_kind_ = ImageBaseKind::SourceImage;
    QColor baseline_canvas_background_ = QColor(0, 0, 0, 0);
    QVector<ImageOperation> baseline_operations_;
    QVector<ImageLayerData> baseline_layers_;
    QVector<ImageGroupData> baseline_groups_;
    QVector<ImageStackItemData> baseline_root_stack_;
    bool force_dirty_ = false;
    ImageDocumentHistory history_;
    mutable QHash<QString, LayerThumbnailCacheEntry> layer_thumbnail_cache_;
    mutable QHash<QString, GroupThumbnailCacheEntry> group_thumbnail_cache_;
    mutable ImageLayerRasterCache layer_raster_cache_;
    ImageDocumentData opacity_edit_snapshot_;
    bool opacity_edit_active_ = false;
};

} // namespace image_editor
