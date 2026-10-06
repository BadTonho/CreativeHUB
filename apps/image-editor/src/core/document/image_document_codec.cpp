#include "image_document_codec.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonObject>
#include <QPolygonF>
#include <QSet>
#include <QTextLayout>
#include <QTextOption>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>

namespace image_editor {
namespace {

constexpr int kLegacyDocumentVersion = 1;
constexpr int kCanvasDocumentVersion = 2;
constexpr int kPaintDocumentVersion = 3;
constexpr int kLayerDocumentVersion = 4;
constexpr int kEraseDocumentVersion = 5;
constexpr int kShapeDocumentVersion = 6;
constexpr int kObjectIdentityDocumentVersion = 7;
constexpr int kLayerGroupsDocumentVersion = 8;
constexpr int kEditableTextDocumentVersion = 9;
constexpr int kLayerMaskDocumentVersion = 10;
constexpr int kLinkedRasterDocumentVersion = 11;
constexpr int kCanvasSizeDocumentVersion = 12;
constexpr int kStrokeClipDocumentVersion = 13;
constexpr int kBucketFillDocumentVersion = 14;
constexpr int kDocumentVersion = ImageDocumentStore::kCurrentDocumentVersion;
constexpr auto kDocumentFormat = "creative-suite-image-document";
constexpr int kMaximumStoredCoordinate = 1'000'000;

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

bool isCanonicalUuid(const QString& value) {
    const QUuid uuid(value);
    return !uuid.isNull() &&
        uuid.toString(QUuid::WithoutBraces).compare(value, Qt::CaseInsensitive) == 0;
}

QString objectId(const ImageOperation& operation) {
    switch (operation.kind) {
    case OperationKind::PaintStroke: return operation.paint_stroke.id;
    case OperationKind::EraseStroke: return operation.erase_stroke.id;
    case OperationKind::Shape: return operation.shape.id;
    case OperationKind::Text: return operation.text.id;
    case OperationKind::RasterImage: return operation.raster.id;
    default: return {};
    }
}

void ensureObjectIds(ImageDocumentData* document) {
    if (document == nullptr) return;
    const auto ensure = [](QVector<ImageOperation>* operations) {
        if (operations == nullptr) return;
        for (auto& operation : *operations) {
            if (operation.kind == OperationKind::PaintStroke &&
                operation.paint_stroke.id.isEmpty()) {
                operation.paint_stroke.id =
                    QUuid::createUuid().toString(QUuid::WithoutBraces);
            } else if (operation.kind == OperationKind::EraseStroke &&
                       operation.erase_stroke.id.isEmpty()) {
                operation.erase_stroke.id =
                    QUuid::createUuid().toString(QUuid::WithoutBraces);
            }
        }
    };
    ensure(&document->operations);
    for (auto& layer : document->layers) {
        ensure(&layer.operations);
        if (layer.mask.has_value()) ensure(&layer.mask->operations);
    }
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

QString storedPath(const QString& path, const QString& document_path) {
    if (document_path.isEmpty()) return path;
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const QString relative = QDir::cleanPath(QDir(QFileInfo(document_path).absolutePath())
        .relativeFilePath(absolute));
    return !QDir::isAbsolutePath(relative) && relative != ".." &&
        !relative.startsWith("../") && !relative.startsWith("..\\")
        ? QDir::fromNativeSeparators(relative) : absolute;
}

QJsonArray encodeClipPath(const QPainterPath& path) {
    QJsonArray encoded;
    for (int index = 0; index < path.elementCount(); ++index) {
        const auto element = path.elementAt(index);
        encoded.append(QJsonObject{{"type", static_cast<int>(element.type)},
                                   {"x", element.x}, {"y", element.y}});
    }
    return encoded;
}

bool decodeClipPath(const QJsonValue& value, Qt::FillRule fill_rule,
                    QPainterPath* path) {
    if (!value.isArray() || path == nullptr) return false;
    const auto elements = value.toArray();
    if (elements.isEmpty() ||
        elements.size() > ImageDocumentStore::kMaximumStrokeClipPathElements) return false;

    QPainterPath decoded;
    const auto coordinate = [](const QJsonObject& object, const char* name, qreal* result) {
        const QJsonValue value = object.value(QLatin1String(name));
        if (!value.isDouble() || !std::isfinite(value.toDouble()) ||
            std::abs(value.toDouble()) > kMaximumStoredCoordinate) return false;
        *result = value.toDouble();
        return true;
    };
    for (qsizetype index = 0; index < elements.size(); ++index) {
        if (!elements.at(index).isObject()) return false;
        const QJsonObject object = elements.at(index).toObject();
        int type = -1;
        qreal x = 0.0;
        qreal y = 0.0;
        if (!isInteger(object.value("type"), &type) ||
            !coordinate(object, "x", &x) || !coordinate(object, "y", &y)) return false;
        switch (static_cast<QPainterPath::ElementType>(type)) {
        case QPainterPath::MoveToElement:
            decoded.moveTo(x, y);
            break;
        case QPainterPath::LineToElement:
            decoded.lineTo(x, y);
            break;
        case QPainterPath::CurveToElement: {
            if (index + 2 >= elements.size() || !elements.at(index + 1).isObject() ||
                !elements.at(index + 2).isObject()) return false;
            const QJsonObject control2 = elements.at(index + 1).toObject();
            const QJsonObject end = elements.at(index + 2).toObject();
            int control2_type = -1;
            int end_type = -1;
            qreal control2_x = 0.0, control2_y = 0.0, end_x = 0.0, end_y = 0.0;
            if (!isInteger(control2.value("type"), &control2_type) ||
                control2_type != QPainterPath::CurveToDataElement ||
                !isInteger(end.value("type"), &end_type) ||
                end_type != QPainterPath::CurveToDataElement ||
                !coordinate(control2, "x", &control2_x) ||
                !coordinate(control2, "y", &control2_y) ||
                !coordinate(end, "x", &end_x) || !coordinate(end, "y", &end_y)) return false;
            decoded.cubicTo(QPointF(x, y), QPointF(control2_x, control2_y),
                            QPointF(end_x, end_y));
            index += 2;
            break;
        }
        case QPainterPath::CurveToDataElement:
        default:
            return false;
        }
    }
    if (decoded.isEmpty() || decoded.elementCount() >
            ImageDocumentStore::kMaximumStrokeClipPathElements) return false;
    decoded.setFillRule(fill_rule);
    *path = std::move(decoded);
    return true;
}

bool decodeStrokeClip(const QJsonObject& object, int version,
                      std::optional<QPainterPath>* clipping_path) {
    if (clipping_path == nullptr || version < kStrokeClipDocumentVersion ||
        !object.contains("clip_path")) return clipping_path != nullptr;
    int fill_rule = -1;
    if (!isInteger(object.value("clip_rule"), &fill_rule) ||
        (fill_rule != static_cast<int>(Qt::OddEvenFill) &&
         fill_rule != static_cast<int>(Qt::WindingFill))) return false;
    QPainterPath path;
    if (!decodeClipPath(object.value("clip_path"),
                        static_cast<Qt::FillRule>(fill_rule), &path)) return false;
    *clipping_path = std::move(path);
    return true;
}

QJsonObject encodeOperation(const ImageOperation& operation, const QString& document_path = {}) {
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
        if (operation.transform_bounds.isValid() && !operation.transform_bounds.isEmpty()) {
            encoded.insert("bounds_x", operation.transform_bounds.x());
            encoded.insert("bounds_y", operation.transform_bounds.y());
            encoded.insert("bounds_width", operation.transform_bounds.width());
            encoded.insert("bounds_height", operation.transform_bounds.height());
        }
        break;
    case OperationKind::FlipHorizontal:
        encoded.insert("kind", "flip_horizontal");
        if (operation.transform_bounds.isValid() && !operation.transform_bounds.isEmpty()) {
            encoded.insert("bounds_x", operation.transform_bounds.x());
            encoded.insert("bounds_y", operation.transform_bounds.y());
            encoded.insert("bounds_width", operation.transform_bounds.width());
            encoded.insert("bounds_height", operation.transform_bounds.height());
        }
        break;
    case OperationKind::FlipVertical:
        encoded.insert("kind", "flip_vertical");
        if (operation.transform_bounds.isValid() && !operation.transform_bounds.isEmpty()) {
            encoded.insert("bounds_x", operation.transform_bounds.x());
            encoded.insert("bounds_y", operation.transform_bounds.y());
            encoded.insert("bounds_width", operation.transform_bounds.width());
            encoded.insert("bounds_height", operation.transform_bounds.height());
        }
        break;
    case OperationKind::PaintStroke: {
        encoded.insert("kind", "paint_stroke");
        encoded.insert("id", operation.paint_stroke.id);
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
        if (operation.paint_stroke.clipping_path.has_value()) {
            encoded.insert("clip_path", encodeClipPath(*operation.paint_stroke.clipping_path));
            encoded.insert("clip_rule", static_cast<int>(
                operation.paint_stroke.clipping_path->fillRule()));
        }
        break;
    }
    case OperationKind::EraseStroke: {
        encoded.insert("kind", "erase_stroke");
        encoded.insert("id", operation.erase_stroke.id);
        encoded.insert("diameter", operation.erase_stroke.diameter);
        QJsonArray points;
        for (const auto& point : operation.erase_stroke.points) {
            QJsonObject encoded_point;
            encoded_point.insert("x", point.x());
            encoded_point.insert("y", point.y());
            points.append(encoded_point);
        }
        encoded.insert("points", points);
        if (operation.erase_stroke.clipping_path.has_value()) {
            encoded.insert("clip_path", encodeClipPath(*operation.erase_stroke.clipping_path));
            encoded.insert("clip_rule", static_cast<int>(
                operation.erase_stroke.clipping_path->fillRule()));
        }
        break;
    }
    case OperationKind::BucketFill: {
        const auto& fill = operation.bucket_fill;
        encoded.insert("kind", "bucket_fill");
        encoded.insert("seed_x", fill.seed.x());
        encoded.insert("seed_y", fill.seed.y());
        encoded.insert("color", fill.color.name(QColor::HexArgb));
        encoded.insert("tolerance", fill.tolerance);
        if (fill.clipping_path.has_value()) {
            encoded.insert("clip_path", encodeClipPath(*fill.clipping_path));
            encoded.insert("clip_rule", static_cast<int>(fill.clipping_path->fillRule()));
        }
        break;
    }
    case OperationKind::Shape: {
        const auto& shape = operation.shape;
        encoded.insert("kind", "shape");
        encoded.insert("id", shape.id);
        encoded.insert("shape_type", shape.kind == ImageShapeKind::Line ? "line" :
            (shape.kind == ImageShapeKind::Ellipse ? "ellipse" : "rectangle"));
        encoded.insert("start_x", shape.start.x());
        encoded.insert("start_y", shape.start.y());
        encoded.insert("end_x", shape.end.x());
        encoded.insert("end_y", shape.end.y());
        encoded.insert("stroke_enabled", shape.stroke_enabled);
        encoded.insert("stroke_color", shape.stroke_color.name(QColor::HexArgb));
        encoded.insert("stroke_width", shape.stroke_width);
        encoded.insert("fill_enabled", shape.fill_enabled);
        encoded.insert("fill_color", shape.fill_color.name(QColor::HexArgb));
        break;
    }
    case OperationKind::RasterImage: {
        const auto& raster = operation.raster;
        encoded.insert("kind", "raster_image");
        encoded.insert("id", raster.id);
        encoded.insert("path", storedPath(raster.source_path, document_path));
        encoded.insert("width", raster.source_size.width());
        encoded.insert("height", raster.source_size.height());
        const auto& t = raster.transform;
        encoded.insert("transform", QJsonArray{t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()});
        break;
    }
    case OperationKind::Text: {
        const auto& text = operation.text;
        encoded.insert("kind", "text");
        encoded.insert("id", text.id);
        encoded.insert("content", text.content);
        encoded.insert("font_family", text.font_family);
        encoded.insert("font_pixel_size", text.font_pixel_size);
        encoded.insert("color", text.color.name(QColor::HexArgb));
        encoded.insert("alignment", text.alignment == ImageTextAlignment::Center
            ? "center" : (text.alignment == ImageTextAlignment::Right ? "right" : "left"));
        encoded.insert("x", text.position.x());
        encoded.insert("y", text.position.y());
        encoded.insert("box_width", text.box_width);
        break;
    }
    }
    return encoded;
}

QJsonArray encodeOperations(const QVector<ImageOperation>& operations, const QString& document_path = {}) {
    QJsonArray encoded;
    for (const auto& operation : operations) encoded.append(encodeOperation(operation, document_path));
    return encoded;
}

bool decodeOperations(const QJsonValue& value,
                      int version,
                      QSize* current_size,
                      bool fixed_canvas,
                      QVector<ImageOperation>* decoded,
                      QString* error, const QString& document_path = {}) {
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
        const auto read_transform_bounds = [&]() {
            if (!fixed_canvas) return true;
            const bool has_bounds = object.contains("bounds_x") || object.contains("bounds_y") ||
                object.contains("bounds_width") || object.contains("bounds_height");
            if (!has_bounds) {
                operation.transform_bounds = QRect(QPoint(), *current_size);
                return true;
            }
            int x = 0, y = 0, width = 0, height = 0;
            if (!isInteger(object.value("bounds_x"), &x) ||
                !isInteger(object.value("bounds_y"), &y) ||
                !isInteger(object.value("bounds_width"), &width) ||
                !isInteger(object.value("bounds_height"), &height) ||
                width <= 0 || height <= 0 ||
                std::abs(static_cast<qint64>(x)) > kMaximumStoredCoordinate ||
                std::abs(static_cast<qint64>(y)) > kMaximumStoredCoordinate ||
                width > 32768 || height > 32768 ||
                static_cast<qint64>(x) + width > kMaximumStoredCoordinate ||
                static_cast<qint64>(y) + height > kMaximumStoredCoordinate) {
                return false;
            }
            operation.transform_bounds = QRect(x, y, width, height);
            return true;
        };
        if (kind == "crop") {
            int x = 0;
            int y = 0;
            int width = 0;
            int height = 0;
            if (!isInteger(object.value("x"), &x) || !isInteger(object.value("y"), &y) ||
                !isInteger(object.value("width"), &width) ||
                !isInteger(object.value("height"), &height) ||
                width <= 0 || height <= 0 || width > 32768 || height > 32768 ||
                ((version < kCanvasSizeDocumentVersion || !fixed_canvas) &&
                    (x < 0 || y < 0 || x > current_size->width() - width ||
                     y > current_size->height() - height)) ||
                (version >= kCanvasSizeDocumentVersion && fixed_canvas &&
                    (std::abs(static_cast<qint64>(x)) > kMaximumStoredCoordinate ||
                     std::abs(static_cast<qint64>(y)) > kMaximumStoredCoordinate ||
                     static_cast<qint64>(x) + width > kMaximumStoredCoordinate ||
                     static_cast<qint64>(y) + height > kMaximumStoredCoordinate))) {
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
            if (!read_transform_bounds()) {
                assignError(error, QStringLiteral("The document contains invalid transform bounds."));
                return false;
            }
            if (!fixed_canvas) current_size->transpose();
        } else if (kind == "flip_horizontal") {
            operation.kind = OperationKind::FlipHorizontal;
            if (!read_transform_bounds()) {
                assignError(error, QStringLiteral("The document contains invalid transform bounds."));
                return false;
            }
        } else if (kind == "flip_vertical") {
            operation.kind = OperationKind::FlipVertical;
            if (!read_transform_bounds()) {
                assignError(error, QStringLiteral("The document contains invalid transform bounds."));
                return false;
            }
        } else if (kind == "paint_stroke" && version >= kPaintDocumentVersion) {
            const auto encoded_points = object.value("points").toArray();
            const QString encoded_color = object.value("color").toString();
            const QString id = object.value("id").toString();
            const QColor color(encoded_color);
            int diameter = 0;
            if (encoded_points.isEmpty() ||
                encoded_points.size() > ImageDocumentStore::kMaximumPaintStrokePoints ||
                !isArgbHexColor(encoded_color) || !color.isValid() ||
                !isInteger(object.value("diameter"), &diameter) ||
                diameter < 1 || diameter > ImageDocumentStore::kMaximumPaintBrushDiameter ||
                (version >= kObjectIdentityDocumentVersion && !isCanonicalUuid(id)) ||
                !decodeStrokeClip(object, version,
                                  &operation.paint_stroke.clipping_path)) {
                assignError(error, QStringLiteral("The document contains an invalid paint stroke."));
                return false;
            }
            operation.kind = OperationKind::PaintStroke;
            operation.paint_stroke.id = version >= kObjectIdentityDocumentVersion
                ? id : QUuid::createUuid().toString(QUuid::WithoutBraces);
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
                if (!std::isfinite(x) || !std::isfinite(y) ||
                    (version < kCanvasSizeDocumentVersion &&
                     (x < 0.0 || y < 0.0 || x >= current_size->width() || y >= current_size->height())) ||
                    (version >= kCanvasSizeDocumentVersion &&
                     (std::abs(x) > kMaximumStoredCoordinate || std::abs(y) > kMaximumStoredCoordinate))) {
                    assignError(error, QStringLiteral("The document contains an out-of-bounds paint stroke point."));
                    return false;
                }
                operation.paint_stroke.points.append(QPointF(x, y));
            }
        } else if (kind == "erase_stroke" && version >= kEraseDocumentVersion && fixed_canvas) {
            const auto encoded_points = object.value("points").toArray();
            const QString id = object.value("id").toString();
            int diameter = 0;
            if (encoded_points.isEmpty() ||
                encoded_points.size() > ImageDocumentStore::kMaximumPaintStrokePoints ||
                !isInteger(object.value("diameter"), &diameter) || diameter < 1 ||
                diameter > ImageDocumentStore::kMaximumPaintBrushDiameter ||
                (version >= kObjectIdentityDocumentVersion && !isCanonicalUuid(id)) ||
                !decodeStrokeClip(object, version,
                                  &operation.erase_stroke.clipping_path)) {
                assignError(error, QStringLiteral("The document contains an invalid erase stroke."));
                return false;
            }
            operation.kind = OperationKind::EraseStroke;
            operation.erase_stroke.id = version >= kObjectIdentityDocumentVersion
                ? id : QUuid::createUuid().toString(QUuid::WithoutBraces);
            operation.erase_stroke.diameter = diameter;
            operation.erase_stroke.points.reserve(encoded_points.size());
            for (const auto& encoded_point_value : encoded_points) {
                if (!encoded_point_value.isObject()) {
                    assignError(error, QStringLiteral("The document contains an invalid erase stroke point."));
                    return false;
                }
                const auto encoded_point = encoded_point_value.toObject();
                const auto x_value = encoded_point.value("x");
                const auto y_value = encoded_point.value("y");
                if (!x_value.isDouble() || !y_value.isDouble()) {
                    assignError(error, QStringLiteral("The document contains an invalid erase stroke point."));
                    return false;
                }
                const double x = x_value.toDouble();
                const double y = y_value.toDouble();
                if (!std::isfinite(x) || !std::isfinite(y) ||
                    (version < kCanvasSizeDocumentVersion &&
                     (x < 0.0 || y < 0.0 || x >= current_size->width() || y >= current_size->height())) ||
                    (version >= kCanvasSizeDocumentVersion &&
                     (std::abs(x) > kMaximumStoredCoordinate || std::abs(y) > kMaximumStoredCoordinate))) {
                    assignError(error, QStringLiteral("The document contains an out-of-bounds erase stroke point."));
                    return false;
                }
                operation.erase_stroke.points.append(QPointF(x, y));
            }
        } else if (kind == "bucket_fill" && version >= kBucketFillDocumentVersion && fixed_canvas) {
            const QString color_text = object.value("color").toString();
            const QColor color(color_text);
            int seed_x = 0;
            int seed_y = 0;
            int tolerance = -1;
            if (!isInteger(object.value("seed_x"), &seed_x) ||
                !isInteger(object.value("seed_y"), &seed_y) ||
                std::abs(static_cast<qint64>(seed_x)) > kMaximumStoredCoordinate ||
                std::abs(static_cast<qint64>(seed_y)) > kMaximumStoredCoordinate ||
                !isArgbHexColor(color_text) || !color.isValid() ||
                !isInteger(object.value("tolerance"), &tolerance) ||
                tolerance < 0 || tolerance > 255 ||
                !decodeStrokeClip(object, version, &operation.bucket_fill.clipping_path)) {
                assignError(error, QStringLiteral("The document contains an invalid bucket fill."));
                return false;
            }
            operation.kind = OperationKind::BucketFill;
            operation.bucket_fill.seed = QPoint(seed_x, seed_y);
            operation.bucket_fill.color = color;
            operation.bucket_fill.tolerance = tolerance;
        } else if (kind == "shape" && version >= kShapeDocumentVersion && fixed_canvas) {
            const QString id = object.value("id").toString();
            const QString shape_type = object.value("shape_type").toString();
            const QString stroke_color_text = object.value("stroke_color").toString();
            const QString fill_color_text = object.value("fill_color").toString();
            const QColor stroke_color(stroke_color_text);
            const QColor fill_color(fill_color_text);
            int stroke_width = 0;
            const auto start_x = object.value("start_x");
            const auto start_y = object.value("start_y");
            const auto end_x = object.value("end_x");
            const auto end_y = object.value("end_y");
            if (!object.value("stroke_enabled").isBool() ||
                !object.value("fill_enabled").isBool() ||
                !isInteger(object.value("stroke_width"), &stroke_width) ||
                !isArgbHexColor(stroke_color_text) || !stroke_color.isValid() ||
                !isArgbHexColor(fill_color_text) || !fill_color.isValid() ||
                !start_x.isDouble() || !start_y.isDouble() ||
                !end_x.isDouble() || !end_y.isDouble()) {
                assignError(error, QStringLiteral("The document contains an invalid shape."));
                return false;
            }
            operation.kind = OperationKind::Shape;
            operation.shape.id = id;
            operation.shape.kind = shape_type == "line" ? ImageShapeKind::Line :
                (shape_type == "ellipse" ? ImageShapeKind::Ellipse : ImageShapeKind::Rectangle);
            if (shape_type != "line" && shape_type != "rectangle" && shape_type != "ellipse") {
                assignError(error, QStringLiteral("The document contains an unsupported shape type."));
                return false;
            }
            operation.shape.start = QPointF(start_x.toDouble(), start_y.toDouble());
            operation.shape.end = QPointF(end_x.toDouble(), end_y.toDouble());
            operation.shape.stroke_enabled = object.value("stroke_enabled").toBool();
            operation.shape.stroke_color = stroke_color;
            operation.shape.stroke_width = stroke_width;
            operation.shape.fill_enabled = object.value("fill_enabled").toBool();
            operation.shape.fill_color = fill_color;
            if (version >= kCanvasSizeDocumentVersion) {
                ImageShapeData shifted = operation.shape;
                shifted.start += QPointF(kMaximumStoredCoordinate, kMaximumStoredCoordinate);
                shifted.end += QPointF(kMaximumStoredCoordinate, kMaximumStoredCoordinate);
                if (!ImageDocumentCodec::isValidShape(
                        shifted, QSize(kMaximumStoredCoordinate * 2 + 32768,
                                       kMaximumStoredCoordinate * 2 + 32768), error)) return false;
            } else if (!ImageDocumentCodec::isValidShape(operation.shape, *current_size, error)) {
                return false;
            }
        } else if (kind == "text" && version >= kEditableTextDocumentVersion && fixed_canvas) {
            const QString alignment = object.value("alignment").toString();
            const QString color_text = object.value("color").toString();
            const QColor color(color_text);
            int font_pixel_size = 0;
            const auto x = object.value("x");
            const auto y = object.value("y");
            const auto box_width = object.value("box_width");
            if (!object.value("content").isString() ||
                !object.value("font_family").isString() ||
                !isInteger(object.value("font_pixel_size"), &font_pixel_size) ||
                !isArgbHexColor(color_text) || !color.isValid() ||
                !x.isDouble() || !y.isDouble() || !box_width.isDouble() ||
                (alignment != "left" && alignment != "center" && alignment != "right")) {
                assignError(error, QStringLiteral("The document contains invalid text data."));
                return false;
            }
            operation.kind = OperationKind::Text;
            operation.text.id = object.value("id").toString();
            operation.text.content = object.value("content").toString();
            operation.text.font_family = object.value("font_family").toString();
            operation.text.font_pixel_size = font_pixel_size;
            operation.text.color = color;
            operation.text.alignment = alignment == "center" ? ImageTextAlignment::Center
                : (alignment == "right" ? ImageTextAlignment::Right
                                         : ImageTextAlignment::Left);
            operation.text.position = QPointF(x.toDouble(), y.toDouble());
            operation.text.box_width = box_width.toDouble();
            if (version >= kCanvasSizeDocumentVersion) {
                ImageTextData shifted = operation.text;
                shifted.position += QPointF(kMaximumStoredCoordinate, kMaximumStoredCoordinate);
                if (!ImageDocumentCodec::isValidText(
                        shifted, QSize(kMaximumStoredCoordinate * 2 + 32768,
                                       kMaximumStoredCoordinate * 2 + 32768), error)) return false;
            } else if (!ImageDocumentCodec::isValidText(operation.text, *current_size, error)) {
                return false;
            }
        } else if (kind == "raster_image" && version >= kLinkedRasterDocumentVersion && fixed_canvas) {
            int width = 0, height = 0;
            const auto matrix = object.value("transform").toArray();
            if (!isInteger(object.value("width"), &width) ||
                !isInteger(object.value("height"), &height) || matrix.size() != 6 ||
                std::any_of(matrix.begin(), matrix.end(), [](const QJsonValue& v) {
                    return !v.isDouble() || !std::isfinite(v.toDouble());
                })) {
                assignError(error, QStringLiteral("The imported image geometry is invalid."));
                return false;
            }
            operation.kind = OperationKind::RasterImage;
            auto& raster = operation.raster;
            raster.id = object.value("id").toString();
            raster.source_path = object.value("path").toString();
            if (!raster.source_path.isEmpty() && !document_path.isEmpty())
                raster.source_path = QDir::cleanPath(QDir::isAbsolutePath(raster.source_path)
                    ? raster.source_path : QDir(QFileInfo(document_path).absolutePath()).filePath(raster.source_path));
            raster.source_size = QSize(width, height);
            raster.transform = QTransform(matrix[0].toDouble(), matrix[1].toDouble(),
                matrix[2].toDouble(), matrix[3].toDouble(), matrix[4].toDouble(), matrix[5].toDouble());
            if (!ImageDocumentCodec::isValidRaster(raster, error)) return false;
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
    return isCanonicalUuid(id);
}

bool validateLayers(const ImageDocumentData& document,
                    QSet<QString>& object_ids,
                    QString* error) {
    const qsizetype item_count = document.layers.size() + document.groups.size();
    if (document.layers.isEmpty() ||
        item_count > ImageDocumentStore::kMaximumLayers ||
        !document.layers.front().background || document.root_stack.isEmpty()) {
        assignError(error, QStringLiteral("The document layer stack is invalid."));
        return false;
    }
    QSet<QString> ids;
    QSet<QString> group_ids;
    const QSize canvas_size = document.canvas_size.isValid() && !document.canvas_size.isEmpty()
        ? document.canvas_size : sizeAfterOperations(document.source_size, document.operations);
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
                layer.opacity != 100 || !layer.operations.isEmpty() || layer.mask.has_value()) {
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
        for (const auto& operation : validated) {
            const QString id = objectId(operation).toLower();
            if (id.isEmpty()) continue;
            if (!isCanonicalUuid(objectId(operation)) || object_ids.contains(id)) {
                assignError(error, QStringLiteral("The document contains an invalid or duplicate object ID."));
                return false;
            }
            object_ids.insert(id);
        }
        if (layer.mask.has_value()) {
            QSize mask_size = canvas_size;
            QVector<ImageOperation> mask_operations;
            if (!decodeOperations(encodeOperations(layer.mask->operations), kDocumentVersion,
                                  &mask_size, true, &mask_operations, error)) return false;
            for (const auto& operation : mask_operations) {
                if (operation.kind == OperationKind::RasterImage || operation.kind == OperationKind::Shape || operation.kind == OperationKind::Text ||
                    (operation.kind == OperationKind::BucketFill &&
                     (qRed(operation.bucket_fill.color.rgb()) != qGreen(operation.bucket_fill.color.rgb()) ||
                      qRed(operation.bucket_fill.color.rgb()) != qBlue(operation.bucket_fill.color.rgb()))) ||
                    (operation.kind == OperationKind::PaintStroke &&
                     (operation.paint_stroke.color.red() != operation.paint_stroke.color.green() ||
                      operation.paint_stroke.color.red() != operation.paint_stroke.color.blue()))) {
                    assignError(error, QStringLiteral("A layer mask contains an unsupported operation or non-grayscale color."));
                    return false;
                }
                const QString id = objectId(operation).toLower();
                if (!id.isEmpty()) {
                    if (object_ids.contains(id)) {
                        assignError(error, QStringLiteral("The document contains a duplicate mask stroke ID."));
                        return false;
                    }
                    object_ids.insert(id);
                }
            }
        }
    }
    for (const auto& group : document.groups) {
        const QString normalized_id = group.id.toLower();
        if (!isValidLayerId(group.id) || ids.contains(normalized_id)) {
            assignError(error, QStringLiteral("The document contains an invalid or duplicate group ID."));
            return false;
        }
        ids.insert(normalized_id);
        group_ids.insert(normalized_id);
    }
    if (background_count != 1) {
        assignError(error, QStringLiteral("The document must contain exactly one Background layer."));
        return false;
    }

    QSet<QString> rooted;
    for (qsizetype index = 0; index < document.root_stack.size(); ++index) {
        const auto& item = document.root_stack.at(index);
        const QString normalized_id = item.id.toLower();
        if (item.group) {
            if (!group_ids.contains(normalized_id) || rooted.contains(normalized_id)) {
                assignError(error, QStringLiteral("The document root stack is invalid."));
                return false;
            }
        } else {
            const auto layer = std::find_if(document.layers.cbegin(), document.layers.cend(),
                [&normalized_id](const ImageLayerData& candidate) {
                    return candidate.id.toLower() == normalized_id;
                });
            if (layer == document.layers.cend() || !layer->parent_group_id.isEmpty() ||
                rooted.contains(normalized_id) || (index == 0) != layer->background) {
                assignError(error, QStringLiteral("The document root stack is invalid."));
                return false;
            }
        }
        rooted.insert(normalized_id);
    }
    if (document.root_stack.front().group ||
        document.root_stack.front().id != document.layers.front().id ||
        rooted.size() != document.groups.size() +
            std::count_if(document.layers.cbegin(), document.layers.cend(),
                [](const ImageLayerData& layer) { return layer.parent_group_id.isEmpty(); })) {
        assignError(error, QStringLiteral("The document root stack is incomplete."));
        return false;
    }

    QSet<QString> child_ids;
    for (const auto& group : document.groups) {
        const QString normalized_id = group.id.toLower();
        if (group.name.trimmed().isEmpty() ||
            group.name.size() > ImageDocumentStore::kMaximumLayerNameLength ||
            group.opacity < 0 || group.opacity > 100) {
            assignError(error, QStringLiteral("A document group has invalid properties."));
            return false;
        }
        if (group.operations.size() > ImageDocumentStore::kMaximumOperations) {
            assignError(error, QStringLiteral("A document group has too many operations."));
            return false;
        }
        for (const auto& operation : group.operations) {
            if (operation.kind != OperationKind::Crop && operation.kind != OperationKind::Rotate &&
                operation.kind != OperationKind::FlipHorizontal &&
                operation.kind != OperationKind::FlipVertical) {
                assignError(error, QStringLiteral("A group contains an unsupported operation."));
                return false;
            }
        }
        QSize group_size = canvas_size;
        QVector<ImageOperation> validated_group_operations;
        if (!decodeOperations(encodeOperations(group.operations), kDocumentVersion,
                             &group_size, true, &validated_group_operations, error)) {
            return false;
        }
        for (const QString& child_id : group.layer_ids) {
            const QString child_key = child_id.toLower();
            const auto child = std::find_if(document.layers.cbegin(), document.layers.cend(),
                [&child_key](const ImageLayerData& layer) {
                    return layer.id.toLower() == child_key;
                });
            if (child == document.layers.cend() || child->background ||
                child->parent_group_id.toLower() != normalized_id ||
                child_ids.contains(child_key)) {
                assignError(error, QStringLiteral("A group contains an invalid child layer."));
                return false;
            }
            child_ids.insert(child_key);
        }
    }
    for (const auto& layer : document.layers) {
        if (layer.background) continue;
        if (!layer.parent_group_id.isEmpty() &&
            (!group_ids.contains(layer.parent_group_id.toLower()) ||
             !child_ids.contains(layer.id.toLower()))) {
            assignError(error, QStringLiteral("A layer has an invalid group parent."));
            return false;
        }
    }
    return true;
}

bool validateDocument(const ImageDocumentData& document, QString* error) {
    const QSize legacy_canvas_size = sizeAfterOperations(document.source_size, document.operations);
    const QSize canvas_size = document.canvas_size.isValid() && !document.canvas_size.isEmpty()
        ? document.canvas_size : legacy_canvas_size;
    const bool valid_source = document.base_kind == ImageBaseKind::SourceImage &&
        !document.source_path.isEmpty() && document.source_size.isValid() &&
        !document.source_size.isEmpty() && ImageDocumentCodec::isValidCanvasSize(canvas_size);
    const bool valid_canvas = document.base_kind == ImageBaseKind::Canvas &&
        document.source_path.isEmpty() && ImageDocumentCodec::isValidCanvasSize(document.source_size) &&
        ImageDocumentCodec::isValidCanvasSize(canvas_size) && document.canvas_background.isValid();
    const QPoint base_offset = document.canvas_base_offset;
    if ((!valid_source && !valid_canvas) ||
        document.operations.size() > ImageDocumentStore::kMaximumOperations ||
        std::abs(static_cast<qint64>(base_offset.x())) > kMaximumStoredCoordinate ||
        std::abs(static_cast<qint64>(base_offset.y())) > kMaximumStoredCoordinate) {
        assignError(error, QStringLiteral("The document path or image base is invalid."));
        return false;
    }
    QSize base_size = document.source_size;
    QVector<ImageOperation> checked_operations;
    if (!decodeOperations(encodeOperations(document.operations), kDocumentVersion,
                          &base_size, false, &checked_operations, error)) {
        return false;
    }
    QSet<QString> object_ids;
    const auto validate_ids = [&object_ids, error](const QVector<ImageOperation>& operations) {
        for (const auto& operation : operations) {
            const QString id = objectId(operation);
            if (id.isEmpty()) continue;
            if (!isCanonicalUuid(id) || object_ids.contains(id.toLower())) {
                assignError(error, QStringLiteral("The document contains an invalid or duplicate object ID."));
                return false;
            }
            object_ids.insert(id.toLower());
        }
        return true;
    };
    return validate_ids(checked_operations) && validateLayers(document, object_ids, error);
}

QJsonObject encodeDocumentJson(const ImageDocumentData& document,
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
    const QSize canvas_size = document.canvas_size.isValid() && !document.canvas_size.isEmpty()
        ? document.canvas_size : sizeAfterOperations(document.source_size, document.operations);
    root.insert("canvas", QJsonObject{
        {"width", canvas_size.width()}, {"height", canvas_size.height()},
        {"base_offset_x", document.canvas_base_offset.x()},
        {"base_offset_y", document.canvas_base_offset.y()}});
    root.insert("operations", encodeOperations(document.operations));
    const auto encode_layer = [&document_path](const ImageLayerData& layer) {
        QJsonObject encoded;
        encoded.insert("id", layer.id);
        encoded.insert("name", layer.name);
        encoded.insert("kind", layer.background ? "background" : "raster");
        encoded.insert("visible", layer.visible);
        encoded.insert("opacity", layer.opacity);
        encoded.insert("operations", encodeOperations(layer.operations, document_path));
        if (layer.mask.has_value()) {
            QJsonObject mask;
            mask.insert("enabled", layer.mask->enabled);
            mask.insert("operations", encodeOperations(layer.mask->operations));
            encoded.insert("mask", mask);
        }
        return encoded;
    };
    QJsonArray layers;
    for (const auto& item : document.root_stack) {
        if (!item.group) {
            const auto layer = std::find_if(document.layers.cbegin(), document.layers.cend(),
                [&item](const ImageLayerData& value) { return value.id == item.id; });
            if (layer != document.layers.cend()) layers.append(encode_layer(*layer));
            continue;
        }
        const auto group = std::find_if(document.groups.cbegin(), document.groups.cend(),
            [&item](const ImageGroupData& value) { return value.id == item.id; });
        if (group == document.groups.cend()) continue;
        QJsonObject encoded_group;
        encoded_group.insert("id", group->id);
        encoded_group.insert("name", group->name);
        encoded_group.insert("kind", "group");
        encoded_group.insert("visible", group->visible);
        encoded_group.insert("opacity", group->opacity);
        encoded_group.insert("operations", encodeOperations(group->operations));
        QJsonArray children;
        for (const auto& child_id : group->layer_ids) {
            const auto child = std::find_if(document.layers.cbegin(), document.layers.cend(),
                [&child_id](const ImageLayerData& value) { return value.id == child_id; });
            if (child != document.layers.cend()) children.append(encode_layer(*child));
        }
        encoded_group.insert("children", children);
        layers.append(encoded_group);
    }
    root.insert("layers", layers);
    return root;
}

bool decodeDocumentJson(const QJsonObject& root,
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
        if (!ImageDocumentCodec::isValidCanvasSize(decoded.source_size) || !background.isValid()) {
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
    if (version >= kCanvasSizeDocumentVersion) {
        const auto canvas = root.value("canvas").toObject();
        int width = 0, height = 0, offset_x = 0, offset_y = 0;
        if (!isInteger(canvas.value("width"), &width) ||
            !isInteger(canvas.value("height"), &height) ||
            !isInteger(canvas.value("base_offset_x"), &offset_x) ||
            !isInteger(canvas.value("base_offset_y"), &offset_y) ||
            !ImageDocumentCodec::isValidCanvasSize(QSize(width, height)) ||
            std::abs(static_cast<qint64>(offset_x)) > kMaximumStoredCoordinate ||
            std::abs(static_cast<qint64>(offset_y)) > kMaximumStoredCoordinate) {
            assignError(error, QStringLiteral("The document canvas dimensions or base offset are invalid."));
            return false;
        }
        decoded.canvas_size = QSize(width, height);
        decoded.canvas_base_offset = QPoint(offset_x, offset_y);
    } else {
        decoded.canvas_size = current_size;
        decoded.canvas_base_offset = {};
    }

    if (version >= kLayerDocumentVersion) {
        const auto encoded_layers = root.value("layers");
        if (!encoded_layers.isArray() || encoded_layers.toArray().isEmpty() ||
            encoded_layers.toArray().size() > ImageDocumentStore::kMaximumLayers) {
            assignError(error, QStringLiteral("The document layer stack is invalid."));
            return false;
        }
        const QSize layer_canvas_size = decoded.canvas_size;
        const auto layer_array = encoded_layers.toArray();
        decoded.layers.reserve(layer_array.size());
        qsizetype stack_item_count = 0;
        const auto decode_raster_layer = [&layer_canvas_size, version, error, &document_path](
            const QJsonValue& encoded_value,
            const QString& parent_group_id,
            ImageLayerData* layer) {
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
            layer->id = encoded.value("id").toString();
            layer->name = encoded.value("name").toString();
            layer->parent_group_id = parent_group_id;
            layer->background = kind == QStringLiteral("background");
            layer->visible = encoded.value("visible").toBool();
            layer->opacity = opacity;
            if (kind != QStringLiteral("background") && kind != QStringLiteral("raster")) {
                assignError(error, QStringLiteral("The document contains an unsupported layer type."));
                return false;
            }
            QSize layer_size = layer_canvas_size;
            if (!decodeOperations(encoded.value("operations"), version,
                                  &layer_size, true, &layer->operations, error, document_path)) {
                return false;
            }
            if (encoded.contains("mask")) {
                if (version < kLayerMaskDocumentVersion || layer->background ||
                    !encoded.value("mask").isObject()) {
                    assignError(error, QStringLiteral("This layer mask is not supported."));
                    return false;
                }
                const auto mask = encoded.value("mask").toObject();
                if (!mask.value("enabled").isBool()) {
                    assignError(error, QStringLiteral("A layer mask has invalid properties."));
                    return false;
                }
                ImageLayerMaskData decoded_mask;
                decoded_mask.enabled = mask.value("enabled").toBool();
                QSize mask_size = layer_canvas_size;
                if (!decodeOperations(mask.value("operations"), version, &mask_size, true,
                                      &decoded_mask.operations, error)) return false;
                layer->mask = std::move(decoded_mask);
            }
            return true;
        };
        for (const auto& encoded_value : layer_array) {
            if (++stack_item_count > ImageDocumentStore::kMaximumLayers ||
                !encoded_value.isObject()) {
                assignError(error, QStringLiteral("The document layer stack is invalid."));
                return false;
            }
            const auto encoded = encoded_value.toObject();
            const QString kind = encoded.value("kind").toString();
            if (version >= kLayerGroupsDocumentVersion && kind == QStringLiteral("group")) {
                if (encoded.contains("mask")) {
                    assignError(error, QStringLiteral("Group masks are not supported."));
                    return false;
                }
                int opacity = -1;
                if (!isInteger(encoded.value("opacity"), &opacity) ||
                    !encoded.value("visible").isBool() ||
                    !encoded.value("children").isArray()) {
                    assignError(error, QStringLiteral("A document group has invalid properties."));
                    return false;
                }
                ImageGroupData group;
                group.id = encoded.value("id").toString();
                group.name = encoded.value("name").toString();
                group.visible = encoded.value("visible").toBool();
                group.opacity = opacity;
                QSize group_size = layer_canvas_size;
                if (!decodeOperations(encoded.value("operations"), version,
                                      &group_size, true, &group.operations, error)) {
                    return false;
                }
                const auto children = encoded.value("children").toArray();
                for (const auto& child_value : children) {
                    if (++stack_item_count > ImageDocumentStore::kMaximumLayers) {
                        assignError(error, QStringLiteral("The document exceeds the 512-item layer limit."));
                        return false;
                    }
                    ImageLayerData child;
                    if (!decode_raster_layer(child_value, group.id, &child) || child.background) {
                        if (error != nullptr && error->isEmpty()) {
                            assignError(error, QStringLiteral("Groups can contain editable raster layers only."));
                        }
                        return false;
                    }
                    group.layer_ids.append(child.id);
                    decoded.layers.append(std::move(child));
                }
                decoded.root_stack.append({group.id, true});
                decoded.groups.append(std::move(group));
            } else {
                ImageLayerData layer;
                if (!decode_raster_layer(encoded_value, {}, &layer)) return false;
                decoded.root_stack.append({layer.id, false});
                decoded.layers.append(std::move(layer));
            }
        }
    } else {
        ImageLayerData background;
        background.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        background.name = QStringLiteral("Background");
        background.background = true;
        ImageLayerData first_layer;
        first_layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        first_layer.name = QStringLiteral("Layer 1");
        decoded.layers = {background, first_layer};
        decoded.root_stack = {{background.id, false}, {first_layer.id, false}};
    }

    if (!validateDocument(decoded, error)) return false;
    *document = std::move(decoded);
    return true;
}


} // namespace

bool ImageDocumentCodec::encodeDocument(const ImageDocumentData& document,
                                        const QString& document_path,
                                        QJsonObject* encoded,
                                        QString* error) {
    if (encoded == nullptr) {
        assignError(error, QStringLiteral("The encoded document output is invalid."));
        return false;
    }
    ImageDocumentData normalized = document;
    ensureObjectIds(&normalized);
    if (!validateDocument(normalized, error)) return false;
    *encoded = encodeDocumentJson(normalized, document_path);
    return true;
}

bool ImageDocumentCodec::decodeDocument(const QJsonObject& encoded,
                                        const QString& document_path,
                                        ImageDocumentData* document,
                                        QString* error) {
    if (document == nullptr) {
        assignError(error, QStringLiteral("The decoded document output is invalid."));
        return false;
    }
    return decodeDocumentJson(encoded, document_path, document, error);
}

bool ImageDocumentCodec::isValidRaster(const ImageRasterData& raster, QString* error) {
    const auto& t = raster.transform;
    const double values[] = {t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()};
    bool valid = isCanonicalUuid(raster.id) && !raster.source_path.trimmed().isEmpty() &&
        !raster.source_path.contains(QChar::Null) && isValidCanvasSize(raster.source_size) &&
        t.isAffine() && t.isInvertible();
    for (double value : values) valid = valid && std::isfinite(value);
    const auto corners = t.map(QPolygonF(QRectF(QPointF(), QSizeF(raster.source_size))));
    for (const auto& corner : corners)
        valid = valid && std::isfinite(corner.x()) && std::isfinite(corner.y());
    if (!valid) assignError(error, QStringLiteral("The imported image reference or affine geometry is invalid."));
    return valid;
}

bool ImageDocumentCodec::isValidCanvasSize(const QSize& size) noexcept {
    if (size.width() <= 0 || size.height() <= 0 ||
        size.width() > 32768 || size.height() > 32768) {
        return false;
    }
    const qint64 pixel_count = static_cast<qint64>(size.width()) * size.height();
    return pixel_count <= ImageDocumentStore::kMaximumCanvasPixels;
}

bool ImageDocumentCodec::isValidShape(const ImageShapeData& shape,
                                      const QSize& canvas_size,
                                      QString* error) {
    const QUuid uuid(shape.id);
    const bool valid_id = !uuid.isNull() &&
        uuid.toString(QUuid::WithoutBraces).compare(shape.id, Qt::CaseInsensitive) == 0;
    const auto valid_point = [&canvas_size](const QPointF& point) {
        return std::isfinite(point.x()) && std::isfinite(point.y()) &&
            point.x() >= 0.0 && point.y() >= 0.0 &&
            point.x() < canvas_size.width() && point.y() < canvas_size.height();
    };
    const bool valid_kind = shape.kind == ImageShapeKind::Line ||
        shape.kind == ImageShapeKind::Rectangle || shape.kind == ImageShapeKind::Ellipse;
    const bool line = shape.kind == ImageShapeKind::Line;
    const bool non_degenerate = line
        ? shape.start != shape.end
        : shape.start.x() != shape.end.x() && shape.start.y() != shape.end.y();
    if (!valid_id || !valid_kind || !valid_point(shape.start) ||
        !valid_point(shape.end) || !non_degenerate ||
        shape.stroke_width < 1 || shape.stroke_width > ImageDocumentStore::kMaximumShapeStrokeWidth ||
        !shape.stroke_color.isValid() || !shape.fill_color.isValid() ||
        (!shape.stroke_enabled && !shape.fill_enabled) || (line && shape.fill_enabled)) {
        assignError(error, QStringLiteral("The shape geometry or style is invalid."));
        return false;
    }
    return true;
}

bool ImageDocumentCodec::isValidText(const ImageTextData& text,
                                     const QSize& canvas_size,
                                     QString* error) {
    const QUuid uuid(text.id);
    const bool valid_id = !uuid.isNull() &&
        uuid.toString(QUuid::WithoutBraces).compare(text.id, Qt::CaseInsensitive) == 0;
    const auto valid_alignment = text.alignment == ImageTextAlignment::Left ||
        text.alignment == ImageTextAlignment::Center ||
        text.alignment == ImageTextAlignment::Right;
    if (!valid_id || text.content.isEmpty() || text.content.size() > ImageDocumentStore::kMaximumTextLength ||
        text.content.contains(QChar::Null) || text.font_family.trimmed().isEmpty() ||
        text.font_family.size() > ImageDocumentStore::kMaximumFontFamilyLength ||
        text.font_pixel_size < 1 || text.font_pixel_size > ImageDocumentStore::kMaximumTextFontPixelSize ||
        !text.color.isValid() || !valid_alignment ||
        !std::isfinite(text.position.x()) || !std::isfinite(text.position.y()) ||
        !std::isfinite(text.box_width) || text.position.x() < 0.0 || text.position.y() < 0.0 ||
        text.position.x() >= canvas_size.width() || text.position.y() >= canvas_size.height() ||
        text.box_width < 1.0 || text.box_width > canvas_size.width() - text.position.x()) {
        assignError(error, QStringLiteral("The text content, layout, or style is invalid."));
        return false;
    }
    const QRectF bounds = imageTextBounds(text);
    if (bounds.height() <= 0.0 || bounds.bottom() > canvas_size.height() + 0.01) {
        assignError(error, QStringLiteral("The text layout extends beyond the canvas."));
        return false;
    }
    return true;
}


QRectF imageTextBounds(const ImageTextData& text) {
    QFont font(text.font_family);
    font.setPixelSize(std::clamp(text.font_pixel_size, 1,
        ImageDocumentStore::kMaximumTextFontPixelSize));
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setAlignment(text.alignment == ImageTextAlignment::Center
        ? Qt::AlignHCenter : (text.alignment == ImageTextAlignment::Right
            ? Qt::AlignRight : Qt::AlignLeft));
    QTextLayout layout(text.content, font);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal height = 0.0;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(std::max<qreal>(1.0, text.box_width));
        line.setPosition(QPointF(0.0, height));
        height += line.height();
    }
    layout.endLayout();
    return QRectF(text.position, QSizeF(text.box_width, height));
}


} // namespace image_editor
