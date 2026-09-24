#pragma once

#include <QColor>
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
};

struct ImageOperation {
    OperationKind kind = OperationKind::Crop;
    QRect crop;
    int quarter_turns = 0;

    bool operator==(const ImageOperation&) const = default;
};

struct ImageDocumentData {
    ImageBaseKind base_kind = ImageBaseKind::SourceImage;
    QString source_path;
    QSize source_size;
    QColor canvas_background = QColor(0, 0, 0, 0);
    QVector<ImageOperation> operations;

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
