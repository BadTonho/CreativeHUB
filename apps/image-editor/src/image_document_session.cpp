#include "image_document_session.h"

#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>
#include <QTransform>
#include <QDir>

namespace image_editor {
namespace {

void assignError(QString* error, const QString& message) {
    if (error != nullptr) *error = message;
}

QString absoluteCleanPath(const QString& path) {
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

} // namespace

bool ImageDocumentSession::loadSource(const QString& source_path,
                                      QImage* image,
                                      QString* error) const {
    if (source_path.isEmpty()) {
        assignError(error, QStringLiteral("An image source path is required."));
        return false;
    }
    QImageReader reader(source_path);
    reader.setAutoTransform(true);
    reader.setDecideFormatFromContent(true);
    auto decoded = reader.read();
    if (decoded.isNull()) {
        assignError(error, reader.errorString().isEmpty()
            ? QStringLiteral("The image could not be decoded.")
            : reader.errorString());
        return false;
    }
    *image = std::move(decoded);
    return true;
}

bool ImageDocumentSession::openImage(const QString& source_path, QString* error) {
    QImage decoded;
    if (!loadSource(source_path, &decoded, error)) return false;

    data_ = {};
    data_.source_path = absoluteCleanPath(source_path);
    data_.source_size = decoded.size();
    source_image_ = std::move(decoded);
    document_path_.clear();
    baseline_source_path_ = data_.source_path;
    baseline_operations_.clear();
    force_dirty_ = false;
    undo_stack_.clear();
    redo_stack_.clear();
    return true;
}

bool ImageDocumentSession::openDocument(const QString& document_path, QString* error) {
    ImageDocumentData candidate;
    if (!ImageDocumentStore::loadDocument(document_path, &candidate, error)) return false;

    QImage decoded;
    const bool missing = !QFileInfo::exists(candidate.source_path);
    if (!missing) {
        if (!loadSource(candidate.source_path, &decoded, error)) return false;
        if (decoded.size() != candidate.source_size) {
            assignError(error, QStringLiteral("The source image dimensions no longer match the document."));
            return false;
        }
    }

    data_ = std::move(candidate);
    source_image_ = std::move(decoded);
    document_path_ = absoluteCleanPath(document_path);
    baseline_source_path_ = data_.source_path;
    baseline_operations_ = data_.operations;
    force_dirty_ = false;
    undo_stack_.clear();
    redo_stack_.clear();
    return true;
}

bool ImageDocumentSession::restoreRecovery(const QString& recovery_path, QString* error) {
    RecoveryDocumentData recovery;
    if (!ImageDocumentStore::loadRecovery(recovery_path, &recovery, error)) return false;

    QImage decoded;
    const bool missing = !QFileInfo::exists(recovery.document.source_path);
    if (!missing) {
        if (!loadSource(recovery.document.source_path, &decoded, error)) return false;
        if (decoded.size() != recovery.document.source_size) {
            assignError(error, QStringLiteral("The recovery source dimensions no longer match."));
            return false;
        }
    }

    data_ = std::move(recovery.document);
    source_image_ = std::move(decoded);
    document_path_ = recovery.target_document_path;
    baseline_source_path_ = data_.source_path;
    baseline_operations_.clear();
    force_dirty_ = true;
    undo_stack_.clear();
    redo_stack_.clear();
    return true;
}

bool ImageDocumentSession::relinkSource(const QString& source_path, QString* error) {
    QImage decoded;
    if (!loadSource(source_path, &decoded, error)) return false;
    if (decoded.size() != data_.source_size) {
        assignError(error, QStringLiteral("Choose an image with the original dimensions."));
        return false;
    }

    if (absoluteCleanPath(source_path) == data_.source_path && hasSource()) return true;
    data_.source_path = absoluteCleanPath(source_path);
    source_image_ = std::move(decoded);
    return true;
}

bool ImageDocumentSession::saveDocument(QString document_path, QString* error) {
    if (!hasSource()) {
        assignError(error, QStringLiteral("Relink the source image before saving the document."));
        return false;
    }
    if (document_path.isEmpty()) document_path = document_path_;
    if (document_path.isEmpty()) {
        assignError(error, QStringLiteral("Choose a path for the editable document."));
        return false;
    }
    if (!ImageDocumentStore::saveDocument(document_path, data_, error)) return false;
    document_path_ = absoluteCleanPath(document_path);
    baseline_source_path_ = data_.source_path;
    baseline_operations_ = data_.operations;
    force_dirty_ = false;
    return true;
}

bool ImageDocumentSession::exportImage(const QString& output_path, QString* error) const {
    if (!hasSource()) {
        assignError(error, QStringLiteral("Open or relink an image before exporting."));
        return false;
    }
    const QString suffix = QFileInfo(output_path).suffix().toLower();
    QByteArray format;
    if (suffix == "png") format = "png";
    else if (suffix == "jpg" || suffix == "jpeg") format = "jpeg";
    else {
        assignError(error, QStringLiteral("Export supports PNG and JPEG files."));
        return false;
    }

    QImage rendered = renderedImage();
    if (format == "jpeg") {
        QImage flattened(rendered.size(), QImage::Format_RGB32);
        flattened.fill(Qt::white);
        QPainter painter(&flattened);
        painter.drawImage(0, 0, rendered);
        painter.end();
        rendered = std::move(flattened);
    }

    QSaveFile output(output_path);
    if (!output.open(QIODevice::WriteOnly)) {
        assignError(error, output.errorString());
        return false;
    }
    QImageWriter writer(&output, format);
    if (format == "jpeg") writer.setQuality(95);
    if (!writer.write(rendered)) {
        assignError(error, writer.errorString());
        output.cancelWriting();
        return false;
    }
    if (!output.commit()) {
        assignError(error, output.errorString());
        return false;
    }
    return true;
}

QSize ImageDocumentSession::renderedSize() const {
    QSize size = data_.source_size;
    for (const auto& operation : data_.operations) {
        if (operation.kind == OperationKind::Crop) size = operation.crop.size();
        else if (operation.kind == OperationKind::Rotate) size.transpose();
    }
    return size;
}

QImage ImageDocumentSession::renderedImage() const {
    if (!hasSource()) return {};
    QImage result = source_image_;
    for (const auto& operation : data_.operations) {
        switch (operation.kind) {
        case OperationKind::Crop:
            result = result.copy(operation.crop);
            break;
        case OperationKind::Rotate: {
            QTransform transform;
            transform.rotate(operation.quarter_turns * 90.0);
            result = result.transformed(transform, Qt::FastTransformation);
            break;
        }
        case OperationKind::FlipHorizontal:
            result = result.mirrored(true, false);
            break;
        case OperationKind::FlipVertical:
            result = result.mirrored(false, true);
            break;
        }
    }
    return result;
}

bool ImageDocumentSession::applyCrop(const QRect& crop, QString* error) {
    if (!hasSource()) {
        assignError(error, QStringLiteral("Open or relink an image before cropping."));
        return false;
    }
    const QSize size = renderedSize();
    const QRect valid = crop.normalized().intersected(QRect(QPoint(0, 0), size));
    if (valid.isEmpty()) {
        assignError(error, QStringLiteral("The crop area is empty."));
        return false;
    }
    if (valid == QRect(QPoint(0, 0), size)) return false;
    pushEdit();
    ImageOperation operation;
    operation.kind = OperationKind::Crop;
    operation.crop = valid;
    data_.operations.append(operation);
    return true;
}

void ImageDocumentSession::pushEdit() {
    undo_stack_.append({data_.operations});
    if (undo_stack_.size() > kMaximumHistoryEntries) undo_stack_.removeFirst();
    redo_stack_.clear();
}

void ImageDocumentSession::rotateLeft() {
    pushEdit();
    data_.operations.append({OperationKind::Rotate, {}, -1});
}

void ImageDocumentSession::rotateRight() {
    pushEdit();
    data_.operations.append({OperationKind::Rotate, {}, 1});
}

void ImageDocumentSession::flipHorizontal() {
    pushEdit();
    data_.operations.append({OperationKind::FlipHorizontal, {}, 0});
}

void ImageDocumentSession::flipVertical() {
    pushEdit();
    data_.operations.append({OperationKind::FlipVertical, {}, 0});
}

bool ImageDocumentSession::undo() {
    if (undo_stack_.isEmpty()) return false;
    redo_stack_.append({data_.operations});
    const auto previous = undo_stack_.takeLast();
    data_.operations = previous.operations;
    return true;
}

bool ImageDocumentSession::redo() {
    if (redo_stack_.isEmpty()) return false;
    undo_stack_.append({data_.operations});
    const auto next = redo_stack_.takeLast();
    data_.operations = next.operations;
    return true;
}

bool ImageDocumentSession::isDirty() const noexcept {
    return force_dirty_ || data_.source_path != baseline_source_path_ ||
        data_.operations != baseline_operations_;
}

} // namespace image_editor
