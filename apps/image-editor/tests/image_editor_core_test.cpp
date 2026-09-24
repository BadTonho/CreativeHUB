#include "image_document_session.h"
#include "image_document_store.h"
#include "image_editor_logger.h"
#include "recovery_store.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

namespace {

void require(bool condition, const QString& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}

QImage sampleImage() {
    QImage image(4, 3, QImage::Format_ARGB32);
    image.setPixelColor(0, 0, Qt::red);
    image.setPixelColor(1, 0, Qt::green);
    image.setPixelColor(2, 0, Qt::blue);
    image.setPixelColor(3, 0, Qt::yellow);
    image.setPixelColor(0, 1, Qt::cyan);
    image.setPixelColor(1, 1, Qt::magenta);
    image.setPixelColor(2, 1, Qt::white);
    image.setPixelColor(3, 1, Qt::black);
    image.setPixelColor(0, 2, QColor(90, 0, 0));
    image.setPixelColor(1, 2, QColor(0, 90, 0));
    image.setPixelColor(2, 2, QColor(0, 0, 90));
    image.setPixelColor(3, 2, QColor(90, 90, 90));
    return image;
}

bool writeImage(const QString& path, const QImage& image, const QByteArray& format = "png") {
    QImageWriter writer(path, format);
    return writer.write(image);
}

void testDocumentEditingAndUndoRedo(const QString& root) {
    const QString media_dir = root + QStringLiteral("/media");
    require(QDir().mkpath(media_dir), QStringLiteral("Could not create the media test directory."));
    const QString source_path = media_dir + QStringLiteral("/source.png");
    require(writeImage(source_path, sampleImage()), QStringLiteral("Could not create the source image."));
    const QByteArray original_bytes = [&]() {
        QFile file(source_path);
        require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not read original source."));
        return file.readAll();
    }();

    image_editor::ImageDocumentSession session;
    QString error;
    require(session.openImage(source_path, &error), error);
    require(session.renderedImage() == sampleImage(), QStringLiteral("Initial image pixels changed."));
    require(session.applyCrop(QRect(1, 0, 3, 2), &error), error);
    session.rotateRight();
    session.flipHorizontal();
    require(session.renderedImage().size() == QSize(2, 3),
            QStringLiteral("Crop and rotation did not produce the expected dimensions."));
    session.rotateLeft();
    session.flipVertical();
    require(session.renderedImage().size() == QSize(3, 2),
            QStringLiteral("The second rotation did not restore the crop dimensions."));
    require(session.renderedImage() == sampleImage().copy(QRect(1, 0, 3, 2)),
            QStringLiteral("The ordered rotations and flips produced unexpected pixels."));
    require(session.isDirty(), QStringLiteral("Edits did not mark the document dirty."));

    const QString document_path = root + QStringLiteral("/image.cimg");
    require(session.saveDocument(document_path, &error), error);
    require(!session.isDirty(), QStringLiteral("Saving did not return to the clean baseline."));
    require(QFile::exists(document_path), QStringLiteral("The editable document was not created."));

    require(session.undo(), QStringLiteral("Undo was unavailable after an edit."));
    require(session.isDirty(), QStringLiteral("Undoing one saved edit did not mark the document dirty."));
    require(session.undo(), QStringLiteral("Second undo failed."));
    require(session.undo(), QStringLiteral("Third undo failed."));
    require(session.undo() && session.undo(), QStringLiteral("Undoing all edits failed."));
    require(session.isDirty(), QStringLiteral("Undoing edits changed the saved baseline."));
    require(!session.undo(), QStringLiteral("Undo succeeded with no history."));
    require(session.redo() && session.redo() && session.redo() &&
                session.redo() && session.redo(),
            QStringLiteral("Redo did not restore all edits."));
    require(!session.isDirty(), QStringLiteral("Redoing to the saved baseline did not clear dirty state."));

    image_editor::ImageDocumentSession reopened;
    require(reopened.openDocument(document_path, &error), error);
    require(reopened.data() == session.data(), QStringLiteral("Document data did not round-trip."));
    require(reopened.renderedImage() == session.renderedImage(),
            QStringLiteral("Rendered pixels changed after saving and reopening."));
    require(reopened.sourcePath() == QFileInfo(source_path).absoluteFilePath(),
            QStringLiteral("Relative source path did not resolve from the document."));

    const auto after_save = [&]() {
        QFile file(source_path);
        require(file.open(QIODevice::ReadOnly), QStringLiteral("Could not reread source image."));
        return file.readAll();
    }();
    require(after_save == original_bytes, QStringLiteral("Editing modified the original source file."));

    require(!reopened.canUndo() && !reopened.canRedo(),
            QStringLiteral("Undo history should remain in memory only."));
}

void testCropNoOpAndInvalidOperations(const QString& root) {
    const QString source_path = root + QStringLiteral("/crop-source.png");
    require(writeImage(source_path, sampleImage()), QStringLiteral("Could not create crop source."));
    image_editor::ImageDocumentSession session;
    QString error;
    require(session.openImage(source_path, &error), error);
    require(!session.applyCrop(QRect(0, 0, 4, 3), &error),
            QStringLiteral("A full-frame crop should be a no-op."));
    require(!session.isDirty() && !session.canUndo(),
            QStringLiteral("A no-op crop changed state or history."));
    require(!session.applyCrop(QRect(-100, -100, 2, 2), &error),
            QStringLiteral("An empty out-of-bounds crop was accepted."));
    require(session.applyCrop(QRect(-1, 0, 2, 2), &error),
            QStringLiteral("A partially intersecting crop was not clipped to the image."));
    require(session.renderedImage().size() == QSize(1, 2),
            QStringLiteral("The clipped crop dimensions are incorrect."));
}

void testMissingSourceAndRelink(const QString& root) {
    const QString source_path = root + QStringLiteral("/move-me.png");
    const QString moved_path = root + QStringLiteral("/restored/source.png");
    require(writeImage(source_path, sampleImage()), QStringLiteral("Could not create relink source."));
    image_editor::ImageDocumentSession session;
    QString error;
    require(session.openImage(source_path, &error), error);
    session.rotateRight();
    const QString document_path = root + QStringLiteral("/relink.cimg");
    require(session.saveDocument(document_path, &error), error);
    require(QDir().mkpath(QFileInfo(moved_path).absolutePath()),
            QStringLiteral("Could not create relink destination."));
    require(QFile::rename(source_path, moved_path), QStringLiteral("Could not move the source image."));

    image_editor::ImageDocumentSession reopened;
    require(reopened.openDocument(document_path, &error), error);
    require(reopened.sourceIsMissing(), QStringLiteral("A missing source was not represented as offline."));
    const QString wrong_size = root + QStringLiteral("/wrong-size.png");
    require(writeImage(wrong_size, QImage(2, 2, QImage::Format_ARGB32)),
            QStringLiteral("Could not create mismatched replacement."));
    require(!reopened.relinkSource(wrong_size, &error) && !error.isEmpty(),
            QStringLiteral("A source with different dimensions was accepted."));
    require(reopened.relinkSource(moved_path, &error), error);
    require(!reopened.sourceIsMissing() && reopened.isDirty(),
            QStringLiteral("Relinking did not restore the source or mark the document dirty."));
}

void testExportAndFormatPlugins(const QString& root) {
    const QString source_path = root + QStringLiteral("/alpha.png");
    QImage transparent(32, 32, QImage::Format_ARGB32);
    transparent.fill(Qt::transparent);
    transparent.setPixelColor(1, 1, QColor(255, 0, 0, 128));
    require(writeImage(source_path, transparent), QStringLiteral("Could not create transparent image."));

    image_editor::ImageDocumentSession session;
    QString error;
    require(session.openImage(source_path, &error), error);
    const QString png_path = root + QStringLiteral("/export.png");
    require(session.exportImage(png_path, &error), error);
    QImage png(png_path);
    require(!png.isNull() && png.pixelColor(30, 30).alpha() == 0,
            QStringLiteral("PNG export did not preserve transparency."));

    const QString jpeg_path = root + QStringLiteral("/export.jpg");
    require(session.exportImage(jpeg_path, &error), error);
    QImage jpeg(jpeg_path);
    require(!jpeg.isNull() && jpeg.pixelColor(30, 30).red() > 245 &&
                jpeg.pixelColor(30, 30).green() > 245 && jpeg.pixelColor(30, 30).blue() > 245,
            QStringLiteral("JPEG export did not flatten transparency over white."));
    require(!session.exportImage(root + QStringLiteral("/export.bmp"), &error),
            QStringLiteral("Unsupported export format was accepted."));
    require(!session.exportImage(root + QStringLiteral("/missing-dir/export.png"), &error),
            QStringLiteral("Export to a missing destination directory unexpectedly succeeded."));

    const auto formats = QImageReader::supportedImageFormats();
    for (const QByteArray required : {QByteArray("png"), QByteArray("jpeg"),
                                      QByteArray("bmp")}) {
        require(formats.contains(required),
                QStringLiteral("Qt image plugin is missing required format: %1")
                    .arg(QString::fromLatin1(required)));
    }
}

void testRecoveryAndLogging(const QString& root) {
    const QString source_path = root + QStringLiteral("/recover.png");
    require(writeImage(source_path, sampleImage()), QStringLiteral("Could not create recovery source."));
    image_editor::ImageDocumentSession session;
    QString error;
    require(session.openImage(source_path, &error), error);
    session.flipVertical();

    image_editor::RecoveryStore recovery(root + QStringLiteral("/app-data"));
    require(recovery.save(session, &error), error);
    const auto snapshots = recovery.snapshots();
    require(snapshots.size() == 1, QStringLiteral("Recovery snapshot was not discoverable."));

    image_editor::ImageDocumentSession restored;
    require(restored.restoreRecovery(snapshots.front(), &error), error);
    require(restored.isDirty() && restored.renderedImage() == session.renderedImage(),
            QStringLiteral("Recovery did not restore edited pixels as unsaved work."));

    image_editor::ImageEditorLogger logger(root + QStringLiteral("/logs"));
    logger.logError(QStringLiteral("test_operation"), QStringLiteral("test cause"), source_path, 17);
    QFile log_file(logger.logPath());
    require(log_file.open(QIODevice::ReadOnly), QStringLiteral("The error log was not created."));
    const auto entry = QJsonDocument::fromJson(log_file.readLine()).object();
    require(entry.value("severity").toString() == "error" &&
                entry.value("subsystem").toString() == "image_editor" &&
                entry.value("operation").toString() == "test_operation" &&
                entry.value("error_code").toInt() == 17,
            QStringLiteral("The structured error log entry is incomplete."));

    require(recovery.remove(snapshots.front()) && recovery.snapshots().isEmpty(),
            QStringLiteral("Recovery snapshot could not be removed."));
}

void testInvalidDocument(const QString& root) {
    const QString source_path = root + QStringLiteral("/known-good.png");
    require(writeImage(source_path, sampleImage()), QStringLiteral("Could not create valid image."));
    image_editor::ImageDocumentSession session;
    QString error;
    require(session.openImage(source_path, &error), error);
    const QImage original_render = session.renderedImage();

    const QString path = root + QStringLiteral("/invalid.cimg");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), QStringLiteral("Could not create invalid document."));
    file.write("{\"format\":\"creative-suite-image-document\",\"version\":999}");
    file.close();

    require(!session.openDocument(path, &error) && !error.isEmpty(),
            QStringLiteral("An unsupported document version was accepted."));
    require(session.sourcePath() == QFileInfo(source_path).absoluteFilePath() &&
                session.renderedImage() == original_render,
            QStringLiteral("A failed document open replaced the current image."));
    require(!session.openImage(root + QStringLiteral("/missing.png"), &error),
            QStringLiteral("A missing raster image was accepted."));
    require(session.sourcePath() == QFileInfo(source_path).absoluteFilePath() &&
                session.renderedImage() == original_render,
            QStringLiteral("A failed image open replaced the current image."));

    const QString corrupt_path = root + QStringLiteral("/corrupt.png");
    QFile corrupt(corrupt_path);
    require(corrupt.open(QIODevice::WriteOnly), QStringLiteral("Could not create corrupt image."));
    corrupt.write("not an image");
    corrupt.close();
    require(!session.openImage(corrupt_path, &error) && !error.isEmpty(),
            QStringLiteral("A corrupt raster image was accepted."));
    require(session.sourcePath() == QFileInfo(source_path).absoluteFilePath() &&
                session.renderedImage() == original_render,
            QStringLiteral("A corrupt image replaced the current image."));
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        std::cerr << "Could not create a temporary test directory.\n";
        return 1;
    }
    const QString root = temporary.path();
    try {
        testDocumentEditingAndUndoRedo(root);
        testCropNoOpAndInvalidOperations(root);
        testMissingSourceAndRelink(root);
        testExportAndFormatPlugins(root);
        testRecoveryAndLogging(root);
        testInvalidDocument(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
