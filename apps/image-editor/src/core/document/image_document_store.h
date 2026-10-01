#pragma once

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QSize>
#include <QVector>

namespace image_editor {

enum class ImageBaseKind {
    SourceImage,
    Canvas,
};

enum class OperationKind {
    Crop,
    Rotate,
    FlipHorizontal,
    FlipVertical,
    PaintStroke,
    EraseStroke,
    Shape,
    Text,
};

enum class ImageShapeKind {
    Line,
    Rectangle,
    Ellipse,
};

struct ImagePaintStroke {
    QString id;
    QVector<QPointF> points;
    QColor color = Qt::black;
    int diameter = 12;

    bool operator==(const ImagePaintStroke&) const = default;
};

struct ImageEraseStroke {
    QString id;
    QVector<QPointF> points;
    int diameter = 12;

    bool operator==(const ImageEraseStroke&) const = default;
};

struct ImageShapeData {
    QString id;
    ImageShapeKind kind = ImageShapeKind::Rectangle;
    QPointF start;
    QPointF end;
    bool stroke_enabled = true;
    QColor stroke_color = Qt::black;
    int stroke_width = 2;
    bool fill_enabled = true;
    QColor fill_color = Qt::black;

    bool operator==(const ImageShapeData&) const = default;
};

enum class ImageTextAlignment {
    Left,
    Center,
    Right,
};

struct ImageTextData {
    QString id;
    QString content;
    QString font_family = QStringLiteral("Sans Serif");
    int font_pixel_size = 48;
    QColor color = Qt::black;
    ImageTextAlignment alignment = ImageTextAlignment::Left;
    QPointF position;
    qreal box_width = 240.0;

    bool operator==(const ImageTextData&) const = default;
};

struct ImageShapePlacement {
    ImageShapeData shape;
    QString layer_id;
    int layer_opacity = 100;
};

struct ImageOperation {
    OperationKind kind = OperationKind::Crop;
    QRect crop;
    int quarter_turns = 0;
    ImagePaintStroke paint_stroke;
    ImageEraseStroke erase_stroke;
    ImageShapeData shape;
    ImageTextData text;

    bool operator==(const ImageOperation&) const = default;
};

struct ImageObjectPlacement {
    ImageOperation operation;
    QString layer_id;
    int layer_opacity = 100;

    bool operator==(const ImageObjectPlacement&) const = default;
};

struct ImageLayerData {
    QString id;
    QString name;
    QString parent_group_id;
    bool background = false;
    bool visible = true;
    int opacity = 100;
    QVector<ImageOperation> operations;

    bool operator==(const ImageLayerData&) const = default;
};

struct ImageGroupData {
    QString id;
    QString name;
    bool visible = true;
    int opacity = 100;
    QVector<ImageOperation> operations;
    // Child layer IDs are ordered bottom-to-top. Groups cannot contain groups.
    QStringList layer_ids;

    bool operator==(const ImageGroupData&) const = default;
};

struct ImageStackItemData {
    QString id;
    bool group = false;

    bool operator==(const ImageStackItemData&) const = default;
};

struct ImageDocumentData {
    ImageBaseKind base_kind = ImageBaseKind::SourceImage;
    QString source_path;
    QSize source_size;
    QColor canvas_background = QColor(0, 0, 0, 0);
    // Version 1-3 edits remain in this sequence and render as Background content.
    QVector<ImageOperation> operations;
    // Raster layers are stored bottom-to-top in flattened tree order. Background
    // is always first; the hierarchy and root stacking order live below.
    QVector<ImageLayerData> layers;
    QVector<ImageGroupData> groups;
    // Root stack ordered bottom-to-top. Group children are stored by group ID.
    QVector<ImageStackItemData> root_stack;

    bool operator==(const ImageDocumentData&) const = default;
};

struct RecoveryDocumentData {
    ImageDocumentData document;
    QString target_document_path;
    QString session_id;
};

class ImageDocumentStore final {
public:
    static constexpr qint64 kMaximumCanvasPixels = 64LL * 1024LL * 1024LL;
    static constexpr qsizetype kMaximumPaintStrokePoints = 100'000;
    static constexpr int kMaximumPaintBrushDiameter = 1024;
    static constexpr int kMaximumShapeStrokeWidth = 1024;
    static constexpr int kMaximumTextFontPixelSize = 1024;
    static constexpr qsizetype kMaximumTextLength = 16'384;
    static constexpr qsizetype kMaximumFontFamilyLength = 256;
    static constexpr qsizetype kMaximumLayers = 512;
    static constexpr qsizetype kMaximumLayerNameLength = 128;
    static constexpr qsizetype kMaximumOperations = 100'000;

    [[nodiscard]] static bool isValidCanvasSize(const QSize& size) noexcept;
    [[nodiscard]] static bool isValidShape(const ImageShapeData& shape,
                                           const QSize& canvas_size,
                                           QString* error = nullptr);
    [[nodiscard]] static bool isValidText(const ImageTextData& text,
                                          const QSize& canvas_size,
                                          QString* error = nullptr);

    [[nodiscard]] static bool saveDocument(
        const QString& document_path,
        const ImageDocumentData& document,
        QString* error = nullptr);

    [[nodiscard]] static bool loadDocument(
        const QString& document_path,
        ImageDocumentData* document,
        QString* error = nullptr);

    [[nodiscard]] static bool saveRecovery(
        const QString& recovery_path,
        const RecoveryDocumentData& recovery,
        QString* error = nullptr);

    [[nodiscard]] static bool loadRecovery(
        const QString& recovery_path,
        RecoveryDocumentData* recovery,
        QString* error = nullptr);

    [[nodiscard]] static QStringList supportedImageExtensions();
};

[[nodiscard]] QRectF imageTextBounds(const ImageTextData& text);

} // namespace image_editor
