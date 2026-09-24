#include "image_document_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include <cmath>
#include <limits>

namespace image_editor {
namespace {

constexpr int kLegacyDocumentVersion = 1;
constexpr int kCanvasDocumentVersion = 2;
constexpr int kPaintDocumentVersion = 3;
constexpr int kDocumentVersion = 4;
constexpr int kRecoveryVersion = 1;
constexpr auto kDocumentFormat = "creative-suite-image-document";
constexpr auto kRecoveryFormat = "creative-suite-image-recovery";

void assignError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

bool isInteger(const QJsonValue& value, int* result) {
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
        return false;
    }
    *result = static_cast<int>(number);
    return true;
}

bool isArgbHexColor(const QString& value) {
    if (value.size() != 9 || value.front() != QLatin1Char('#')) return false;
    for (qsizetype i = 1; i < value.size(); ++i) {
        const QChar character = value.at(i);
        const bool digit = character >= QLatin1Char('0') && character <= QLatin1Char('9');
        const bool lower_hex = character >= QLatin1Char('a') && character <= QLatin1Char('f');
        const bool upper_hex = character >= QLatin1Char('A') && character <= QLatin1Char('F');
        if (!digit && !lower_hex && !upper_hex) return false;
    }
    return true;
}

QJsonObject encodeOperation(const ImageOperation& operation) {
    QJsonObject encoded;
    switch (operation.kind) {
    case OperationKind::Crop:
        encoded.insert("kind", "crop");
        encoded.insert("x", operation.crop.x());
        encoded.insert("y", operation.crop.y());
        encoded.insert("width", operation.crop.width());
        encoded.insert("height", operation.crop.height());
        break;
    case OperationKind::Rotate:
        encoded.insert("kind", "rotate");
        encoded.insert("quarter_turns", operation.quarter_turns);
        break;
    case OperationKind::FlipHorizontal:
        encoded.insert("kind", "flip_horizontal");
        break;
    case OperationKind::FlipVertical:
        encoded.insert("kind", "flip_vertical");
        break;
    case OperationKind::PaintStroke: {
        encoded.insert("kind", "paint_stroke");
        encoded.insert("color", operation.paint_stroke.color.name(QColor::HexArgb));
        encoded.insert("diameter", operation.paint_stroke.diameter);
        QJsonArray points;
        for (const auto& point : operation.paint_stroke.points) {
            QJsonObject encoded_point;
            encoded_point.insert("x", point.x());
            encoded_point.insert("y", point.y());
            points.append(encoded_point);
        }
        encoded.insert("points", points);
        break;
    }
    }
    return encoded;
}

QJsonArray encodeOperations(const QVector<ImageOperation>& operations) {
    QJsonArray encoded;
    for (const auto& operation : operations) encoded.append(encodeOperation(operation));
    return encoded;
}

bool decodeOperations(const QJsonValue& value,
                      int version,
                      QSize* current_size,
                      bool fixed_canvas,
                      QVector<ImageOperation>* decoded,
                      QString* error) {
    if (!value.isArray() || current_size == nullptr || decoded == nullptr) {
        assignError(error, QStringLiteral("The document edit list is invalid."));
        return false;
    }
    const auto operations = value.toArray();
    if (operations.size() > ImageDocumentStore::kMaximumOperations) {
        assignError(error, QStringLiteral("The document has too many edit operations."));
        return false;
    }
    decoded->reserve(operations.size());
    for (const auto& item : operations) {
        if (!item.isObject()) {
            assignError(error, QStringLiteral("The document contains an invalid edit."));
            return false;
        }
        const auto object = item.toObject();
        const QString kind = object.value("kind").toString();
        ImageOperation operation;
        if (kind == "crop") {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            if (!isInteger(object.value("x"), &x) || !isInteger(object.value("y"), &y) ||
                !isInteger(object.value("width"), &width) ||
                !isInteger(object.value("height"), &height) ||
                x < 0 || y < 0 || width <= 0 || height <= 0 ||
                x > current_size->width() - width || y > current_size->height() - height) {
                assignError(error, QStringLiteral("The document contains an invalid crop."));
                return false;
            }
            operation.kind = OperationKind::Crop;
            operation.crop = QRect(x, y, width, height);
            if (!fixed_canvas) *current_size = operation.crop.size();
        } else if (kind == "rotate") {
            int turns = 0;
            if (!isInteger(object.value("quarter_turns"), &turns) ||
                (turns != -1 && turns != 1)) {
                assignError(error, QStringLiteral("The document contains an invalid rotation."));
                return false;
            }
            operation.kind = OperationKind::Rotate;
            operation.quarter_turns = turns;
            if (!fixed_canvas) current_size->transpose();
        } else if (kind == "flip_horizontal") {
            operation.kind = OperationKind::FlipHorizontal;
        } else if (kind == "flip_vertical") {
            operation.kind = OperationKind::FlipVertical;
        } else if (kind == "paint_stroke" && version >= kPaintDocumentVersion) {
            const auto encoded_points = object.value("points").toArray();
            const QString encoded_color = object.value("color").toString();
            const QColor color(encoded_color);
            int diameter = 0;
            if (encoded_points.isEmpty() ||
                encoded_points.size() > ImageDocumentStore::kMaximumPaintStrokePoints ||
                !isArgbHexColor(encoded_color) || !color.isValid() ||
                !isInteger(object.value("diameter"), &diameter) ||
                diameter < 1 || diameter > ImageDocumentStore::kMaximumPaintBrushDiameter) {
                assignError(error, QStringLiteral("The document contains an invalid paint stroke."));
                return false;
            }
            operation.kind = OperationKind::PaintStroke;
            operation.paint_stroke.color = color;
            operation.paint_stroke.diameter = diameter;
            operation.paint_stroke.points.reserve(encoded_points.size());
            for (const auto& encoded_point_value : encoded_points) {
                if (!encoded_point_value.isObject()) {
                    assignError(error, QStringLiteral("The document contains an invalid paint stroke point."));
                    return false;
                }
                const auto encoded_point = encoded_point_value.toObject();
                const auto x_value = encoded_point.value("x");
                const auto y_value = encoded_point.value("y");
                if (!x_value.isDouble() || !y_value.isDouble()) {
                    assignError(error, QStringLiteral("The document contains an invalid paint stroke point."));
                    return false;
                }
                const double x = x_value.toDouble();
                const double y = y_value.toDouble();
                if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0 || y < 0.0 ||
                    x >= current_size->width() || y >= current_size->height()) {
                    assignError(error, QStringLiteral("The document contains an out-of-bounds paint stroke point."));
                    return false;
                }
                operation.paint_stroke.points.append(QPointF(x, y));
            }
        } else {
            assignError(error, QStringLiteral("The document contains an unsupported edit."));
            return false;
        }
        decoded->append(std::move(operation));
    }
    return true;
}

QSize sizeAfterOperations(QSize size, const QVector<ImageOperation>& operations) {
    for (const auto& operation : operations) {
        if (operation.kind == OperationKind::Crop) size = operation.crop.size();
        else if (operation.kind == OperationKind::Rotate) size.transpose();
    }
    return size;
}

bool isValidLayerId(const QString& id) {
    const QUuid uuid(id);
    return !uuid.isNull() &&
        uuid.toString(QUuid::WithoutBraces).compare(id, Qt::CaseInsensitive) == 0;
}

bool validateLayers(const ImageDocumentData& document, QString* error) {
    if (document.layers.isEmpty() ||
        document.layers.size() > ImageDocumentStore::kMaximumLayers ||
        !document.layers.front().background) {
        assignError(error, QStringLiteral("The document layer stack is invalid."));
        return false;
    }
    QSet<QString> ids;
    const QSize canvas_size = sizeAfterOperations(document.source_size, document.operations);
    qsizetype background_count = 0;
    for (qsizetype index = 0; index < document.layers.size(); ++index) {
        const auto& layer = document.layers.at(index);
        const QString normalized_id = layer.id.toLower();
        if (!isValidLayerId(layer.id) || ids.contains(normalized_id)) {
            assignError(error, QStringLiteral("The document contains an invalid or duplicate layer ID."));
            return false;
        }
        ids.insert(normalized_id);
        if (layer.background) {
            ++background_count;
            if (index != 0 || layer.name != QStringLiteral("Background") ||
                layer.opacity != 100 || !layer.operations.isEmpty()) {
                assignError(error, QStringLiteral("The Background layer is invalid."));
                return false;
            }
            continue;
        }
        if (layer.name.trimmed().isEmpty() ||
            layer.name.size() > ImageDocumentStore::kMaximumLayerNameLength ||
            layer.opacity < 0 || layer.opacity > 100) {
            assignError(error, QStringLiteral("A document layer has invalid properties."));
            return false;
        }
        QSize layer_size = canvas_size;
        QVector<ImageOperation> validated;
        QJsonArray ops = encodeOperations(layer.operations);
        if (!decodeOperations(ops, kDocumentVersion, &layer_size, true, &validated, error)) {
            return false;
        }
    }
    if (background_count != 1) {
        assignError(error, QStringLiteral("The document must contain exactly one Background layer."));
        return false;
    }
    return true;
}

bool validateDocument(const ImageDocumentData& document, QString* error) {
    const bool valid_source = document.base_kind == ImageBaseKind::SourceImage &&
        !document.source_path.isEmpty() && document.source_size.isValid() &&
        !document.source_size.isEmpty();
    const bool valid_canvas = document.base_kind == ImageBaseKind::Canvas &&
        document.source_path.isEmpty() && ImageDocumentStore::isValidCanvasSize(document.source_size) &&
        document.canvas_background.isValid();
    if ((!valid_source && !valid_canvas) ||
        document.operations.size() > ImageDocumentStore::kMaximumOperations) {
        assignError(error, QStringLiteral("The document path or image base is invalid."));
        return false;
    }
    QSize base_size = document.source_size;
    QVector<ImageOperation> checked_operations;
    if (!decodeOperations(encodeOperations(document.operations), kDocumentVersion,
                          &base_size, false, &checked_operations, error)) {
        return false;
    }
    return validateLayers(document, error);
}

QJsonObject encodeDocument(const ImageDocumentData& document,
                           const QString& document_path) {
    QJsonObject base;
    if (document.base_kind == ImageBaseKind::Canvas) {
        base.insert("kind", "canvas");
        base.insert("width", document.source_size.width());
        base.insert("height", document.source_size.height());
        base.insert("background", document.canvas_background.name(QColor::HexArgb));
    } else {
        const QFileInfo source_info(document.source_path);
        const QString source_absolute = source_info.absoluteFilePath();
        QString stored_source = source_absolute;
        const QString relative = QDir(QFileInfo(document_path).absolutePath())
                                     .relativeFilePath(source_absolute);
        const QString clean_relative = QDir::cleanPath(relative);
        if (!QDir::isAbsolutePath(relative) && clean_relative != ".." &&
            !clean_relative.startsWith("../") && !clean_relative.startsWith("..\\")) {
            stored_source = QDir::fromNativeSeparators(clean_relative);
        }

        base.insert("kind", "source_image");
        base.insert("path", stored_source);
        base.insert("width", document.source_size.width());
        base.insert("height", document.source_size.height());
    }

    QJsonObject root;
    root.insert("format", kDocumentFormat);
    root.insert("version", kDocumentVersion);
    root.insert("base", base);
    root.insert("operations", encodeOperations(document.operations));
    QJsonArray layers;
    for (const auto& layer : document.layers) {
        QJsonObject encoded;
        encoded.insert("id", layer.id);
        encoded.insert("name", layer.name);
        encoded.insert("kind", layer.background ? "background" : "raster");
        encoded.insert("visible", layer.visible);
        encoded.insert("opacity", layer.opacity);
        encoded.insert("operations", encodeOperations(layer.operations));
        layers.append(encoded);
    }
    root.insert("layers", layers);
    return root;
}

bool decodeDocument(const QJsonObject& root,
                    const QString& document_path,
                    ImageDocumentData* document,
                    QString* error) {
    if (root.value("format").toString() != kDocumentFormat) {
        assignError(error, QStringLiteral("This is not a supported Image Editor document."));
        return false;
    }
    int version = 0;
    if (!isInteger(root.value("version"), &version) ||
        (version < kLegacyDocumentVersion || version > kDocumentVersion)) {
        assignError(error, QStringLiteral("This Image Editor document version is not supported."));
        return false;
    }

    const auto source = version == kLegacyDocumentVersion
        ? root.value("source").toObject()
        : QJsonObject{};
    const auto base = version >= kCanvasDocumentVersion
        ? root.value("base").toObject()
        : QJsonObject{};
    const QString base_kind = version == kLegacyDocumentVersion
        ? QStringLiteral("source_image") : base.value("kind").toString();
    const auto base_data = version == kLegacyDocumentVersion ? source : base;
    const QString path = base_data.value("path").toString();
    int source_width = 0;
    int source_height = 0;
    if (!isInteger(base_data.value("width"), &source_width) ||
        !isInteger(base_data.value("height"), &source_height) ||
        source_width <= 0 || source_height <= 0) {
        assignError(error, QStringLiteral("The document base dimensions are invalid."));
        return false;
    }

    ImageDocumentData decoded;
    decoded.source_size = QSize(source_width, source_height);
    if (base_kind == "source_image") {
        if (path.isEmpty()) {
            assignError(error, QStringLiteral("The document source reference is invalid."));
            return false;
        }
        decoded.base_kind = ImageBaseKind::SourceImage;
        decoded.source_path = QDir::isAbsolutePath(path)
            ? QFileInfo(path).absoluteFilePath()
            : QFileInfo(QDir(QFileInfo(document_path).absolutePath()).filePath(path))
                  .absoluteFilePath();
    } else if (base_kind == "canvas" && version >= kCanvasDocumentVersion) {
        const QColor background(base.value("background").toString());
        if (!ImageDocumentStore::isValidCanvasSize(decoded.source_size) || !background.isValid()) {
            assignError(error, QStringLiteral("The canvas base is invalid or too large."));
            return false;
        }
        decoded.base_kind = ImageBaseKind::Canvas;
        decoded.canvas_background = background;
    } else {
        assignError(error, QStringLiteral("The document base type is not supported."));
        return false;
    }

    QSize current_size = decoded.source_size;
    if (!decodeOperations(root.value("operations"), version, &current_size, false,
                          &decoded.operations, error)) {
        return false;
    }

    if (version == kDocumentVersion) {
        const auto encoded_layers = root.value("layers");
        if (!encoded_layers.isArray() || encoded_layers.toArray().isEmpty() ||
            encoded_layers.toArray().size() > ImageDocumentStore::kMaximumLayers) {
            assignError(error, QStringLiteral("The document layer stack is invalid."));
            return false;
        }
        const QSize layer_canvas_size = current_size;
        const auto layer_array = encoded_layers.toArray();
        decoded.layers.reserve(layer_array.size());
        for (qsizetype index = 0; index < layer_array.size(); ++index) {
            const auto encoded_value = layer_array.at(index);
            if (!encoded_value.isObject()) {
                assignError(error, QStringLiteral("The document contains an invalid layer."));
                return false;
            }
            const auto encoded = encoded_value.toObject();
            const QString kind = encoded.value("kind").toString();
            int opacity = -1;
            if (!isInteger(encoded.value("opacity"), &opacity) ||
                !encoded.value("visible").isBool()) {
                assignError(error, QStringLiteral("A document layer has invalid properties."));
                return false;
            }
            ImageLayerData layer;
            layer.id = encoded.value("id").toString();
            layer.name = encoded.value("name").toString();
            layer.background = kind == QStringLiteral("background");
            layer.visible = encoded.value("visible").toBool();
            layer.opacity = opacity;
            if (kind != QStringLiteral("background") && kind != QStringLiteral("raster")) {
                assignError(error, QStringLiteral("The document contains an unsupported layer type."));
                return false;
            }
            QSize layer_size = layer_canvas_size;
            if (!decodeOperations(encoded.value("operations"), kDocumentVersion,
                                  &layer_size, true, &layer.operations, error)) {
                return false;
            }
            decoded.layers.append(std::move(layer));
        }
        if (!validateLayers(decoded, error)) return false;
    } else {
        ImageLayerData background;
        background.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        background.name = QStringLiteral("Background");
        background.background = true;
        ImageLayerData first_layer;
        first_layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        first_layer.name = QStringLiteral("Layer 1");
        decoded.layers = {background, first_layer};
    }

    *document = std::move(decoded);
    return true;
}

bool readJson(const QString& file_path, QJsonObject* object, QString* error) {
    QFile file(file_path);
    if (!file.open(QIODevice::ReadOnly)) {
        assignError(error, file.errorString());
        return false;
    }
    QJsonParseError parse_error;
    const auto json = QJsonDocument::fromJson(file.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !json.isObject()) {
        assignError(error, parse_error.errorString().isEmpty()
            ? QStringLiteral("The document content is invalid.")
            : parse_error.errorString());
        return false;
    }
    *object = json.object();
    return true;
}

bool writeJson(const QString& file_path, const QJsonObject& object, QString* error) {
    QSaveFile file(file_path);
    if (!file.open(QIODevice::WriteOnly)) {
        assignError(error, file.errorString());
        return false;
    }
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        assignError(error, file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        assignError(error, file.errorString());
        return false;
    }
    return true;
}

} // namespace

bool ImageDocumentStore::saveDocument(const QString& document_path,
                                      const ImageDocumentData& document,
                                      QString* error) {
    if (document_path.isEmpty()) {
        assignError(error, QStringLiteral("A document path is required."));
        return false;
    }
    if (!validateDocument(document, error)) return false;
    return writeJson(document_path, encodeDocument(document, document_path), error);
}

bool ImageDocumentStore::loadDocument(const QString& document_path,
                                      ImageDocumentData* document,
                                      QString* error) {
    if (document == nullptr || document_path.isEmpty()) {
        assignError(error, QStringLiteral("A document path is required."));
        return false;
    }
    QJsonObject root;
    if (!readJson(document_path, &root, error)) return false;
    return decodeDocument(root, document_path, document, error);
}

bool ImageDocumentStore::saveRecovery(const QString& recovery_path,
                                      const RecoveryDocumentData& recovery,
                                      QString* error) {
    if (recovery_path.isEmpty()) {
        assignError(error, QStringLiteral("The recovery document is invalid."));
        return false;
    }
    if (!validateDocument(recovery.document, error)) return false;
    QJsonObject root;
    root.insert("format", kRecoveryFormat);
    root.insert("version", kRecoveryVersion);
    root.insert("target_document_path", recovery.target_document_path);
    if (!recovery.session_id.isEmpty()) root.insert("session_id", recovery.session_id);
    root.insert("document", encodeDocument(recovery.document, recovery_path));
    return writeJson(recovery_path, root, error);
}

bool ImageDocumentStore::loadRecovery(const QString& recovery_path,
                                      RecoveryDocumentData* recovery,
                                      QString* error) {
    if (recovery == nullptr || recovery_path.isEmpty()) {
        assignError(error, QStringLiteral("A recovery path is required."));
        return false;
    }
    QJsonObject root;
    if (!readJson(recovery_path, &root, error)) return false;
    int version = 0;
    if (root.value("format").toString() != kRecoveryFormat ||
        !isInteger(root.value("version"), &version) || version != kRecoveryVersion ||
        !root.value("document").isObject()) {
        assignError(error, QStringLiteral("This recovery snapshot is not supported."));
        return false;
    }
    ImageDocumentData document;
    if (!decodeDocument(root.value("document").toObject(), recovery_path,
                        &document, error)) {
        return false;
    }
    recovery->document = std::move(document);
    recovery->session_id = root.value("session_id").toString();
    const QString target = root.value("target_document_path").toString();
    recovery->target_document_path = target.isEmpty()
        ? QString{}
        : QFileInfo(target).absoluteFilePath();
    return true;
}

bool ImageDocumentStore::isValidCanvasSize(const QSize& size) noexcept {
    if (size.width() <= 0 || size.height() <= 0 ||
        size.width() > 32768 || size.height() > 32768) {
        return false;
    }
    const qint64 pixel_count = static_cast<qint64>(size.width()) * size.height();
    return pixel_count <= kMaximumCanvasPixels;
}

QStringList ImageDocumentStore::supportedImageExtensions() {
    return {QStringLiteral("*.png"), QStringLiteral("*.PNG"),
            QStringLiteral("*.jpg"), QStringLiteral("*.JPG"),
            QStringLiteral("*.jpeg"), QStringLiteral("*.JPEG"),
            QStringLiteral("*.bmp"), QStringLiteral("*.BMP"),
            QStringLiteral("*.webp"), QStringLiteral("*.WEBP"),
            QStringLiteral("*.tif"), QStringLiteral("*.TIF"),
            QStringLiteral("*.tiff"), QStringLiteral("*.TIFF")};
}

} // namespace image_editor
