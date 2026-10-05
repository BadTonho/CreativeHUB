#include "image_document_codec.h"
#include "image_document_store.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <cmath>
#include <limits>
#include <utility>

namespace image_editor {
namespace {

constexpr int kRecoveryVersion = 1;
constexpr auto kRecoveryFormat = "creative-suite-image-recovery";

void assignError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

bool isInteger(const QJsonValue& value, int* result) {
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number > static_cast<double>(std::numeric_limits<int>::max())) return false;
    *result = static_cast<int>(number);
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
    QJsonObject encoded;
    if (!ImageDocumentCodec::encodeDocument(document, document_path, &encoded, error)) {
        return false;
    }
    return writeJson(document_path, encoded, error);
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
    return ImageDocumentCodec::decodeDocument(root, document_path, document, error);
}

bool ImageDocumentStore::saveRecovery(const QString& recovery_path,
                                      const RecoveryDocumentData& recovery,
                                      QString* error) {
    if (recovery_path.isEmpty()) {
        assignError(error, QStringLiteral("The recovery document is invalid."));
        return false;
    }
    QJsonObject encoded_document;
    if (!ImageDocumentCodec::encodeDocument(
            recovery.document, recovery_path, &encoded_document, error)) return false;
    QJsonObject root;
    root.insert("format", kRecoveryFormat);
    root.insert("version", kRecoveryVersion);
    root.insert("target_document_path", recovery.target_document_path);
    if (!recovery.session_id.isEmpty()) root.insert("session_id", recovery.session_id);
    root.insert("document", encoded_document);
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
    if (!ImageDocumentCodec::decodeDocument(
            root.value("document").toObject(), recovery_path, &document, error)) return false;
    recovery->document = std::move(document);
    recovery->session_id = root.value("session_id").toString();
    const QString target = root.value("target_document_path").toString();
    recovery->target_document_path = target.isEmpty()
        ? QString{}
        : QFileInfo(target).absoluteFilePath();
    return true;
}

bool ImageDocumentStore::isValidRaster(const ImageRasterData& raster, QString* error) {
    return ImageDocumentCodec::isValidRaster(raster, error);
}

bool ImageDocumentStore::isValidCanvasSize(const QSize& size) noexcept {
    return ImageDocumentCodec::isValidCanvasSize(size);
}

bool ImageDocumentStore::isValidShape(const ImageShapeData& shape,
                                      const QSize& canvas_size,
                                      QString* error) {
    return ImageDocumentCodec::isValidShape(shape, canvas_size, error);
}

bool ImageDocumentStore::isValidText(const ImageTextData& text,
                                     const QSize& canvas_size,
                                     QString* error) {
    return ImageDocumentCodec::isValidText(text, canvas_size, error);
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
