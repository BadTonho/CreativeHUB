#pragma once

#include <QColor>
#include <QPointF>
#include <QRect>
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
};

struct ImagePaintStroke {
    QVector<QPointF> points;
    QColor color = Qt::black;
    int diameter = 12;

    bool operator==(const ImagePaintStroke&) const = default;
};

struct ImageOperation {
    OperationKind kind = OperationKind::Crop;
    QRect crop;
    int quarter_turns = 0;
    ImagePaintStroke paint_stroke;

    bool operator==(const ImageOperation&) const = default;
};

struct ImageLayerData {
    QString id;
    QString name;
    bool background = false;
    bool visible = true;
    int opacity = 100;
    QVector<ImageOperation> operations;

    bool operator==(const ImageLayerData&) const = default;
};

struct ImageDocumentData {
    ImageBaseKind base_kind = ImageBaseKind::SourceImage;
    QString source_path;
    QSize source_size;
    QColor canvas_background = QColor(0, 0, 0, 0);
    // Version 1-3 edits remain in this sequence and render as Background content.
    QVector<ImageOperation> operations;
    // Ordered bottom-to-top. The first entry is the locked Background layer.
    QVector<ImageLayerData> layers;

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
    static constexpr qsizetype kMaximumLayers = 512;
    static constexpr qsizetype kMaximumLayerNameLength = 128;
    static constexpr qsizetype kMaximumOperations = 100'000;

    [[nodiscard]] static bool isValidCanvasSize(const QSize& size) noexcept;

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

} // namespace image_editor
