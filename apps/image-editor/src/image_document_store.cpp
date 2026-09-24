#include "image_document_store.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>

#include <cmath>
#include <limits>

namespace image_editor {
namespace {

constexpr int kDocumentVersion = 1;
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

QJsonObject encodeDocument(const ImageDocumentData& document,
                           const QString& document_path) {
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

    QJsonObject source;
    source.insert("path", stored_source);
    source.insert("width", document.source_size.width());
    source.insert("height", document.source_size.height());

    QJsonArray operations;
    for (const auto& operation : document.operations) {
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
        }
        operations.append(encoded);
    }

    QJsonObject root;
    root.insert("format", kDocumentFormat);
    root.insert("version", kDocumentVersion);
    root.insert("source", source);
    root.insert("operations", operations);
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
    if (!isInteger(root.value("version"), &version) || version != kDocumentVersion) {
        assignError(error, QStringLiteral("This Image Editor document version is not supported."));
        return false;
    }

    const auto source = root.value("source").toObject();
    const QString path = source.value("path").toString();
    int source_width = 0;
    int source_height = 0;
    if (path.isEmpty() || !isInteger(source.value("width"), &source_width) ||
        !isInteger(source.value("height"), &source_height) ||
        source_width <= 0 || source_height <= 0) {
        assignError(error, QStringLiteral("The document source reference is invalid."));
        return false;
    }

    ImageDocumentData decoded;
    decoded.source_size = QSize(source_width, source_height);
    decoded.source_path = QDir::isAbsolutePath(path)
        ? QFileInfo(path).absoluteFilePath()
        : QFileInfo(QDir(QFileInfo(document_path).absolutePath()).filePath(path))
              .absoluteFilePath();

    const auto operations_value = root.value("operations");
    if (!operations_value.isArray()) {
        assignError(error, QStringLiteral("The document edit list is invalid."));
        return false;
    }

    QSize current_size = decoded.source_size;
    const auto operations = operations_value.toArray();
    decoded.operations.reserve(operations.size());
    for (const auto& value : operations) {
        if (!value.isObject()) {
            assignError(error, QStringLiteral("The document contains an invalid edit."));
            return false;
        }
        const auto object = value.toObject();
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
                x > current_size.width() - width || y > current_size.height() - height) {
                assignError(error, QStringLiteral("The document contains an invalid crop."));
                return false;
            }
            operation.kind = OperationKind::Crop;
            operation.crop = QRect(x, y, width, height);
            current_size = operation.crop.size();
        } else if (kind == "rotate") {
            int turns = 0;
            if (!isInteger(object.value("quarter_turns"), &turns) ||
                (turns != -1 && turns != 1)) {
                assignError(error, QStringLiteral("The document contains an invalid rotation."));
                return false;
            }
            operation.kind = OperationKind::Rotate;
            operation.quarter_turns = turns;
            current_size.transpose();
        } else if (kind == "flip_horizontal") {
            operation.kind = OperationKind::FlipHorizontal;
        } else if (kind == "flip_vertical") {
            operation.kind = OperationKind::FlipVertical;
        } else {
            assignError(error, QStringLiteral("The document contains an unsupported edit."));
            return false;
        }
        decoded.operations.append(operation);
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
    if (document_path.isEmpty() || document.source_path.isEmpty() ||
        !document.source_size.isValid() || document.source_size.isEmpty()) {
        assignError(error, QStringLiteral("The document path or image source is invalid."));
        return false;
    }
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
    if (recovery_path.isEmpty() || recovery.document.source_path.isEmpty() ||
        !recovery.document.source_size.isValid() || recovery.document.source_size.isEmpty()) {
        assignError(error, QStringLiteral("The recovery document is invalid."));
        return false;
    }
    QJsonObject root;
    root.insert("format", kRecoveryFormat);
    root.insert("version", kDocumentVersion);
    root.insert("target_document_path", recovery.target_document_path);
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
        !isInteger(root.value("version"), &version) || version != kDocumentVersion ||
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
    const QString target = root.value("target_document_path").toString();
    recovery->target_document_path = target.isEmpty()
        ? QString{}
        : QFileInfo(target).absoluteFilePath();
    return true;
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
