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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>
#include <limits>
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

void testCanvasCreationPersistenceAndRecovery(const QString& root) {
    image_editor::ImageDocumentSession session;
    QString error;
    const QColor custom_background(24, 96, 180, 128);
    require(session.createCanvas(QSize(32, 20), custom_background, &error), error);
    require(session.hasDocument() && session.hasSource() && !session.sourceIsMissing() &&
                session.sourcePath().isEmpty(),
            QStringLiteral("A new canvas was not represented as a self-contained document."));
    require(session.renderedImage().size() == QSize(32, 20) &&
                session.renderedImage().pixelColor(5, 5) == custom_background,
            QStringLiteral("The new canvas dimensions or custom background are incorrect."));
    require(session.isDirty(), QStringLiteral("A new, unsaved canvas should be dirty."));

    session.rotateRight();
    require(session.renderedImage().size() == QSize(20, 32),
            QStringLiteral("Rotation did not apply to a new canvas."));
    require(session.undo() && session.renderedImage().size() == QSize(32, 20),
            QStringLiteral("Undo did not restore the canvas dimensions."));
    require(session.redo() && session.renderedImage().size() == QSize(20, 32),
            QStringLiteral("Redo did not reapply the canvas rotation."));
    require(session.applyCrop(QRect(0, 0, 12, 18), &error), error);
    require(session.renderedImage().size() == QSize(12, 18),
            QStringLiteral("Crop did not apply to a new canvas."));
    require(session.undo() && session.undo() && session.renderedImage().size() == QSize(32, 20),
            QStringLiteral("Undo did not restore canvas edits in order."));

    const QString recovery_directory = root + QStringLiteral("/canvas-recovery");
    image_editor::RecoveryStore recovery(recovery_directory);
    require(recovery.save(session, &error), error);
    const auto snapshots = recovery.snapshots();
    require(snapshots.size() == 1,
            QStringLiteral("An unsaved canvas was not included in recovery."));
    const QString recovery_path = recovery.pathFor(session);
    image_editor::ImageDocumentSession restored;
    require(restored.restoreRecovery(snapshots.front(), &error), error);
    require(restored.data() == session.data() && restored.renderedImage() == session.renderedImage() &&
                restored.isDirty() && !restored.sourceIsMissing() &&
                recovery.pathFor(restored) == recovery_path,
            QStringLiteral("Canvas recovery did not preserve pixels, edits, or its recovery identity."));
    require(recovery.remove(snapshots.front()),
            QStringLiteral("The canvas recovery snapshot could not be removed."));

    const QString document_path = root + QStringLiteral("/canvas.cimg");
    require(session.saveDocument(document_path, &error), error);
    require(!session.isDirty(), QStringLiteral("Saving a canvas did not clear its dirty state."));
    session.rotateLeft();
    require(session.isDirty() && session.undo() && !session.isDirty(),
            QStringLiteral("Undo did not return a saved canvas to its clean baseline."));
    session.rotateRight();
    require(session.isDirty() && session.undo() && !session.isDirty(),
            QStringLiteral("Canvas redo history or its saved baseline is inconsistent."));
    QFile document_file(document_path);
    require(document_file.open(QIODevice::ReadOnly),
            QStringLiteral("The saved canvas document could not be read."));
    const auto document_json = QJsonDocument::fromJson(document_file.readAll()).object();
    require(document_json.value("version").toInt() == 3 &&
                document_json.value("base").toObject().value("kind").toString() == "canvas",
            QStringLiteral("Canvas save did not use the version 3 canvas representation."));

    image_editor::ImageDocumentSession reopened;
    require(reopened.openDocument(document_path, &error), error);
    require(reopened.data() == session.data() &&
                reopened.renderedImage() == session.renderedImage() && !reopened.isDirty(),
            QStringLiteral("A saved canvas did not round-trip through the document format."));

    image_editor::ImageDocumentSession transparent;
    require(transparent.createCanvas(QSize(10, 8), QColor(0, 0, 0, 0), &error), error);
    require(transparent.renderedImage().pixelColor(2, 2).alpha() == 0,
            QStringLiteral("A transparent canvas was not transparent."));
    const QString png_path = root + QStringLiteral("/transparent-canvas.png");
    require(transparent.exportImage(png_path, &error), error);
    QImage png(png_path);
    require(!png.isNull() && png.pixelColor(2, 2).alpha() == 0,
            QStringLiteral("PNG export did not preserve transparent canvas pixels."));
    const QString jpeg_path = root + QStringLiteral("/transparent-canvas.jpg");
    require(transparent.exportImage(jpeg_path, &error), error);
    QImage jpeg(jpeg_path);
    require(!jpeg.isNull() && jpeg.pixelColor(2, 2).red() > 245 &&
                jpeg.pixelColor(2, 2).green() > 245 && jpeg.pixelColor(2, 2).blue() > 245,
            QStringLiteral("JPEG export did not flatten transparent canvas pixels over white."));

    image_editor::ImageDocumentSession white;
    require(white.createCanvas(QSize(5, 5), Qt::white, &error), error);
    require(white.renderedImage().pixelColor(0, 0) == QColor(Qt::white),
            QStringLiteral("A white canvas background was not preserved."));

    image_editor::ImageDocumentSession translucent_paint;
    require(translucent_paint.createCanvas(QSize(8, 8), QColor(0, 0, 0, 0), &error), error);
    require(translucent_paint.applyPaintStroke(
                {QPointF(4.0, 4.0)}, QColor(20, 80, 240, 128), 4, &error), error);
    const QColor translucent_pixel = translucent_paint.renderedImage().pixelColor(4, 4);
    require(translucent_pixel.alpha() >= 120 && translucent_pixel.alpha() <= 136 &&
                translucent_pixel.blue() > 230,
            QStringLiteral("Brush alpha was not composited over a transparent canvas: a=%1, b=%2.")
                .arg(translucent_pixel.alpha()).arg(translucent_pixel.blue()));
    const QString translucent_export_path = root + QStringLiteral("/translucent-paint.png");
    require(translucent_paint.exportImage(translucent_export_path, &error), error);
    const QImage translucent_export(translucent_export_path);
    require(translucent_export.pixelColor(4, 4).alpha() == translucent_pixel.alpha(),
            QStringLiteral("PNG export did not preserve painted alpha."));

    const QImage original = session.renderedImage();
    require(!session.createCanvas(QSize(0, 20), Qt::white, &error) && !error.isEmpty(),
            QStringLiteral("Invalid canvas dimensions were accepted."));
    require(!session.createCanvas(QSize(32768, 32768), Qt::white, &error),
            QStringLiteral("Canvas dimensions above the pixel budget were accepted."));
    require(session.renderedImage() == original,
            QStringLiteral("A rejected canvas creation replaced the current document."));
}

void testLegacyVersionOneDocument(const QString& root) {
    const QString source_path = root + QStringLiteral("/legacy-source.png");
    require(writeImage(source_path, sampleImage()),
            QStringLiteral("Could not create the version 1 source image."));
    const QString path = root + QStringLiteral("/legacy.cimg");
    QJsonObject source;
    source.insert("path", QFileInfo(source_path).fileName());
    source.insert("width", 4);
    source.insert("height", 3);
    QJsonObject root_object;
    root_object.insert("format", "creative-suite-image-document");
    root_object.insert("version", 1);
    root_object.insert("source", source);
    root_object.insert("operations", QJsonArray{});
    QFile file(path);
    require(file.open(QIODevice::WriteOnly),
            QStringLiteral("Could not create the version 1 document fixture."));
    file.write(QJsonDocument(root_object).toJson());
    file.close();

    image_editor::ImageDocumentSession session;
    QString error;
    require(session.openDocument(path, &error), error);
    require(session.data().base_kind == image_editor::ImageBaseKind::SourceImage &&
                session.renderedImage() == sampleImage() && !session.isDirty(),
            QStringLiteral("A version 1 source-image document did not remain compatible."));
    require(session.saveDocument({}, &error), error);
    QFile upgraded(path);
    require(upgraded.open(QIODevice::ReadOnly),
            QStringLiteral("The upgraded version 1 document could not be read."));
    require(QJsonDocument::fromJson(upgraded.readAll()).object().value("version").toInt() == 3,
            QStringLiteral("Saving a version 1 document did not upgrade it to version 3."));
}

void testVersionTwoDocumentCompatibility(const QString& root) {
    const QString source_path = root + QStringLiteral("/version-two-source.png");
    require(writeImage(source_path, sampleImage()),
            QStringLiteral("Could not create the version 2 source image."));
    const QString path = root + QStringLiteral("/version-two.cimg");
    QJsonObject base;
    base.insert("kind", "source_image");
    base.insert("path", QFileInfo(source_path).fileName());
    base.insert("width", 4);
    base.insert("height", 3);
    QJsonObject root_object;
    root_object.insert("format", "creative-suite-image-document");
    root_object.insert("version", 2);
    root_object.insert("base", base);
    root_object.insert("operations", QJsonArray{});
    QFile file(path);
    require(file.open(QIODevice::WriteOnly),
            QStringLiteral("Could not create the version 2 document fixture."));
    file.write(QJsonDocument(root_object).toJson());
    file.close();

    image_editor::ImageDocumentSession session;
    QString error;
    require(session.openDocument(path, &error), error);
    require(session.renderedImage() == sampleImage() && !session.isDirty(),
            QStringLiteral("A version 2 source-image document did not remain compatible."));
    require(session.saveDocument({}, &error), error);
    QFile upgraded(path);
    require(upgraded.open(QIODevice::ReadOnly),
            QStringLiteral("The upgraded version 2 document could not be read."));
    require(QJsonDocument::fromJson(upgraded.readAll()).object().value("version").toInt() == 3,
            QStringLiteral("Saving a version 2 document did not upgrade it to version 3."));

    base.remove("path");
    base.insert("kind", "canvas");
    base.insert("width", 3);
    base.insert("height", 2);
    base.insert("background", "#80402010");
    root_object.insert("base", base);
    const QString canvas_path = root + QStringLiteral("/version-two-canvas.cimg");
    QFile canvas_file(canvas_path);
    require(canvas_file.open(QIODevice::WriteOnly),
            QStringLiteral("Could not create the version 2 canvas fixture."));
    canvas_file.write(QJsonDocument(root_object).toJson());
    canvas_file.close();
    image_editor::ImageDocumentSession version_two_canvas;
    require(version_two_canvas.openDocument(canvas_path, &error), error);
    require(version_two_canvas.renderedImage().size() == QSize(3, 2) &&
                version_two_canvas.renderedImage().pixelColor(0, 0) == QColor(64, 32, 16, 128),
            QStringLiteral("A version 2 self-contained canvas did not remain compatible."));
}

void testPaintStrokesPersistenceUndoRedoAndValidation(const QString& root) {
    const QString source_path = root + QStringLiteral("/paint-source.png");
    QImage source(16, 12, QImage::Format_ARGB32);
    source.fill(Qt::white);
    require(writeImage(source_path, source),
            QStringLiteral("Could not create the paint source image."));
    QFile original_file(source_path);
    require(original_file.open(QIODevice::ReadOnly),
            QStringLiteral("Could not read the original paint source."));
    const QByteArray original_bytes = original_file.readAll();
    original_file.close();

    image_editor::ImageDocumentSession session;
    QString error;
    require(session.openImage(source_path, &error), error);
    const QVector<QPointF> points{QPointF(3.0, 6.0), QPointF(12.0, 6.0)};
    require(session.applyPaintStroke(points, QColor(220, 20, 40), 3, &error), error);
    const QImage painted = session.renderedImage();
    require(painted.pixelColor(7, 6) == QColor(220, 20, 40) && session.isDirty(),
            QStringLiteral("The paint stroke did not render at its image-space position."));
    require(session.data().operations.size() == 1 &&
                session.data().operations.front().kind == image_editor::OperationKind::PaintStroke,
            QStringLiteral("The paint gesture was not stored as one document operation."));

    image_editor::ImageDocumentSession ordered_edits;
    require(ordered_edits.openImage(source_path, &error), error);
    require(ordered_edits.applyCrop(QRect(2, 1, 10, 8), &error), error);
    require(ordered_edits.applyPaintStroke(
                {QPointF(4.0, 3.0)}, QColor(Qt::blue), 4, &error), error);
    const QImage cropped_and_painted = ordered_edits.renderedImage();
    require(cropped_and_painted.size() == QSize(10, 8) &&
                cropped_and_painted.pixelColor(4, 3) == QColor(Qt::blue),
            QStringLiteral("Paint coordinates did not use the current cropped image bounds."));
    ordered_edits.rotateRight();
    require(ordered_edits.renderedImage().size() == QSize(8, 10) &&
                ordered_edits.undo() && ordered_edits.renderedImage() == cropped_and_painted &&
                ordered_edits.undo() &&
                ordered_edits.renderedImage() == source.copy(QRect(2, 1, 10, 8)),
            QStringLiteral("Paint and geometry operations were not ordered in history."));

    const int operation_count = session.data().operations.size();
    const QVector<QPointF> invalid_points{QPointF(-1.0, 0.0)};
    require(!session.applyPaintStroke({}, Qt::black, 12, &error) && !error.isEmpty(),
            QStringLiteral("An empty paint stroke was accepted."));
    require(!session.applyPaintStroke(invalid_points, Qt::black, 12, &error) && !error.isEmpty(),
            QStringLiteral("An out-of-bounds paint point was accepted."));
    const QVector<QPointF> non_finite_points{
        QPointF(std::numeric_limits<double>::quiet_NaN(), 2.0)};
    require(!session.applyPaintStroke(non_finite_points, Qt::black, 12, &error) && !error.isEmpty(),
            QStringLiteral("A non-finite paint point was accepted."));
    require(!session.applyPaintStroke(points, QColor{}, 12, &error) && !error.isEmpty(),
            QStringLiteral("An invalid paint color was accepted."));
    require(!session.applyPaintStroke(points, Qt::black, 0, &error) && !error.isEmpty(),
            QStringLiteral("An invalid paint diameter was accepted."));
    require(!session.applyPaintStroke(points, Qt::black, 513, &error) && !error.isEmpty(),
            QStringLiteral("A paint diameter above the supported range was accepted."));
    error = QStringLiteral("previous failure");
    require(!session.applyPaintStroke(points, QColor(0, 0, 0, 0), 12, &error) && error.isEmpty(),
            QStringLiteral("A fully transparent paint stroke should have no effect."));
    require(session.data().operations.size() == operation_count &&
                session.renderedImage() == painted && session.canUndo(),
            QStringLiteral("A rejected or invisible stroke changed the document or history."));

    require(session.undo() && session.renderedImage() == source && !session.isDirty(),
            QStringLiteral("Undo did not remove the complete paint stroke."));
    require(session.redo() && session.renderedImage() == painted && session.isDirty(),
            QStringLiteral("Redo did not restore the complete paint stroke."));

    const QString document_path = root + QStringLiteral("/painted.cimg");
    require(session.saveDocument(document_path, &error), error);
    QFile document_file(document_path);
    require(document_file.open(QIODevice::ReadOnly),
            QStringLiteral("The painted document could not be read."));
    const QJsonObject saved_json = QJsonDocument::fromJson(document_file.readAll()).object();
    require(saved_json.value("version").toInt() == 3 &&
                saved_json.value("operations").toArray().at(0).toObject()
                    .value("kind").toString() == "paint_stroke",
            QStringLiteral("Paint was not serialized using the version 3 stroke operation."));

    image_editor::ImageDocumentSession reopened;
    require(reopened.openDocument(document_path, &error), error);
    require(reopened.data() == session.data() && reopened.renderedImage() == painted &&
                !reopened.isDirty(),
            QStringLiteral("The saved paint stroke did not round-trip."));

    const QString recovery_directory = root + QStringLiteral("/paint-recovery");
    image_editor::RecoveryStore recovery(recovery_directory);
    reopened.rotateRight();
    require(recovery.save(reopened, &error), error);
    const auto snapshots = recovery.snapshots();
    require(snapshots.size() == 1,
            QStringLiteral("The painted document was not included in recovery."));
    image_editor::ImageDocumentSession restored;
    require(restored.restoreRecovery(snapshots.front(), &error), error);
    require(restored.renderedImage() == reopened.renderedImage() && restored.isDirty(),
            QStringLiteral("Recovery did not preserve the painted operation."));

    const QString export_path = root + QStringLiteral("/painted.png");
    require(reopened.exportImage(export_path, &error), error);
    QImage exported(export_path);
    bool exported_stroke_found = false;
    for (int y = 0; y < exported.height(); ++y) {
        for (int x = 0; x < exported.width(); ++x) {
            const QColor pixel = exported.pixelColor(x, y);
            if (pixel.red() > 180 && pixel.green() < 80 && pixel.blue() < 90) {
                exported_stroke_found = true;
            }
        }
    }
    require(!exported.isNull() && exported.size() == reopened.renderedImage().size() &&
                exported_stroke_found,
            QStringLiteral("PNG export did not include the paint stroke."));

    QFile unchanged_source(source_path);
    require(unchanged_source.open(QIODevice::ReadOnly) &&
                unchanged_source.readAll() == original_bytes,
            QStringLiteral("Painting modified the original source image."));
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

    QJsonObject base;
    base.insert("kind", "canvas");
    base.insert("width", 4);
    base.insert("height", 3);
    base.insert("background", "#FFFFFFFF");
    QJsonObject invalid_point;
    invalid_point.insert("x", 4.0);
    invalid_point.insert("y", 1.0);
    QJsonArray points;
    points.append(invalid_point);
    QJsonObject invalid_stroke;
    invalid_stroke.insert("kind", "paint_stroke");
    invalid_stroke.insert("color", "#FF000000");
    invalid_stroke.insert("diameter", 12);
    invalid_stroke.insert("points", points);
    QJsonArray operations;
    operations.append(invalid_stroke);
    QJsonObject invalid_paint_document;
    invalid_paint_document.insert("format", "creative-suite-image-document");
    invalid_paint_document.insert("version", 3);
    invalid_paint_document.insert("base", base);
    invalid_paint_document.insert("operations", operations);
    const QString invalid_stroke_path = root + QStringLiteral("/invalid-stroke.cimg");
    QFile invalid_stroke_file(invalid_stroke_path);
    require(invalid_stroke_file.open(QIODevice::WriteOnly),
            QStringLiteral("Could not create an invalid paint document."));
    invalid_stroke_file.write(QJsonDocument(invalid_paint_document).toJson());
    invalid_stroke_file.close();
    require(!session.openDocument(invalid_stroke_path, &error) && !error.isEmpty(),
            QStringLiteral("A version 3 paint stroke outside the image bounds was accepted."));
    require(session.sourcePath() == QFileInfo(source_path).absoluteFilePath() &&
                session.renderedImage() == original_render,
            QStringLiteral("An invalid paint document replaced the current image."));
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
        testCanvasCreationPersistenceAndRecovery(root);
        testLegacyVersionOneDocument(root);
        testVersionTwoDocumentCompatibility(root);
        testPaintStrokesPersistenceUndoRedoAndValidation(root);
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
