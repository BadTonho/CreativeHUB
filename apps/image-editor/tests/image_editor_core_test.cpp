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
    require(session.data().layers.size() == 2 &&
                session.data().layers.front().background &&
                session.selectedLayerIsEditable(),
            QStringLiteral("Opening an image did not create Background and an editable layer."));
    const QString selected_layer = session.selectedLayerId();
    require(session.applyCrop(QRect(1, 0, 3, 2), &error), error);
    session.rotateRight();
    session.flipHorizontal();
    require(session.renderedImage().size() == QSize(4, 3),
            QStringLiteral("Layer transforms changed the fixed document canvas dimensions."));
    session.rotateLeft();
    session.flipVertical();
    require(session.renderedImage().size() == QSize(4, 3) &&
                session.renderedImage().pixelColor(0, 0) == sampleImage().pixelColor(0, 0),
            QStringLiteral("Layer transforms changed the Background pixels or canvas size."));
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
    require(reopened.selectedLayerId() == selected_layer,
            QStringLiteral("Opening a layered document did not select its top editable layer."));

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
    require(session.renderedImage().size() == QSize(32, 20) &&
                session.renderedImage().pixelColor(5, 5) == custom_background,
            QStringLiteral("A layer rotation changed the canvas size or Background."));
    require(session.undo() && session.renderedImage().size() == QSize(32, 20),
            QStringLiteral("Undo did not restore the layer rotation."));
    require(session.redo() && session.renderedImage().size() == QSize(32, 20),
            QStringLiteral("Redo did not reapply the layer rotation."));
    require(session.applyCrop(QRect(0, 0, 12, 18), &error), error);
    require(session.renderedImage().size() == QSize(32, 20),
            QStringLiteral("A layer crop changed the fixed canvas dimensions."));
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
    require(document_json.value("version").toInt() == 5 &&
                document_json.value("base").toObject().value("kind").toString() == "canvas",
            QStringLiteral("Canvas save did not use the version 5 layered representation."));

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
    require(QJsonDocument::fromJson(upgraded.readAll()).object().value("version").toInt() == 5,
            QStringLiteral("Saving a version 1 document did not upgrade it to version 5."));
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
    require(QJsonDocument::fromJson(upgraded.readAll()).object().value("version").toInt() == 5,
            QStringLiteral("Saving a version 2 document did not upgrade it to version 5."));

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

void testVersionThreeMigrationToBackground(const QString& root) {
    QJsonObject base;
    base.insert("kind", "canvas");
    base.insert("width", 8);
    base.insert("height", 6);
    base.insert("background", "#00000000");
    QJsonObject point;
    point.insert("x", 3.0);
    point.insert("y", 2.0);
    QJsonObject stroke;
    stroke.insert("kind", "paint_stroke");
    stroke.insert("color", "#FFFF0000");
    stroke.insert("diameter", 2);
    stroke.insert("points", QJsonArray{point});
    QJsonObject root_object;
    root_object.insert("format", "creative-suite-image-document");
    root_object.insert("version", 3);
    root_object.insert("base", base);
    root_object.insert("operations", QJsonArray{stroke});
    const QString path = root + QStringLiteral("/version-three-painted.cimg");
    QFile file(path);
    require(file.open(QIODevice::WriteOnly),
            QStringLiteral("Could not create the version 3 compatibility fixture."));
    file.write(QJsonDocument(root_object).toJson());
    file.close();

    image_editor::ImageDocumentSession session;
    QString error;
    require(session.openDocument(path, &error), error);
    const QImage original_render = session.renderedImage();
    require(original_render.pixelColor(3, 2).red() > 240 &&
                session.data().operations.size() == 1 &&
                session.data().layers.size() == 2 &&
                session.selectedLayerIsEditable(),
            QStringLiteral("A version 3 paint edit was not preserved on migrated Background."));
    const auto layer_thumbnails = session.renderedLayerThumbnails(QSize(48, 36));
    require(layer_thumbnails.value(session.data().layers.front().id)
                    .pixelColor(18, 12).red() > 240 &&
                layer_thumbnails.value(session.selectedLayerId())
                    .pixelColor(24, 18).alpha() == 0,
            QStringLiteral("A migrated version 3 operation did not appear on its Background thumbnail."));
    require(session.setLayerVisible(session.data().layers.front().id, false) &&
                session.renderedImage().pixelColor(3, 2).alpha() == 0,
            QStringLiteral("Migrated version 3 content was not attached to Background."));
    require(session.undo() && session.renderedImage() == original_render,
            QStringLiteral("Undo did not restore the migrated Background visibility."));
    require(session.saveDocument({}, &error), error);
    QFile upgraded(path);
    require(upgraded.open(QIODevice::ReadOnly),
            QStringLiteral("The migrated version 3 document could not be reopened."));
    require(QJsonDocument::fromJson(upgraded.readAll()).object().value("version").toInt() == 5,
            QStringLiteral("Saving a version 3 document did not upgrade it to version 5."));
    image_editor::ImageDocumentSession reopened;
    require(reopened.openDocument(path, &error) && reopened.renderedImage() == original_render,
            QStringLiteral("Upgrading a version 3 document changed its visible pixels."));
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
    require(session.data().layers.at(1).operations.size() == 1 &&
                session.data().layers.at(1).operations.front().kind ==
                    image_editor::OperationKind::PaintStroke,
            QStringLiteral("The paint gesture was not stored on the selected layer."));

    image_editor::ImageDocumentSession ordered_edits;
    require(ordered_edits.openImage(source_path, &error), error);
    require(ordered_edits.applyCrop(QRect(2, 1, 10, 8), &error), error);
    require(ordered_edits.applyPaintStroke(
                {QPointF(4.0, 3.0)}, QColor(Qt::blue), 4, &error), error);
    const QImage cropped_and_painted = ordered_edits.renderedImage();
    require(cropped_and_painted.size() == QSize(16, 12) &&
                cropped_and_painted.pixelColor(4, 3) == QColor(Qt::blue) &&
                cropped_and_painted.pixelColor(0, 0) == QColor(Qt::white),
            QStringLiteral("Layer crop or paint did not preserve fixed canvas coordinates."));
    ordered_edits.rotateRight();
    require(ordered_edits.renderedImage().size() == QSize(16, 12) &&
                ordered_edits.undo() && ordered_edits.renderedImage() == cropped_and_painted &&
                ordered_edits.undo() &&
                ordered_edits.renderedImage() == source,
            QStringLiteral("Paint and geometry operations were not ordered in history."));

    const int operation_count = session.data().layers.at(1).operations.size();
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
    require(!session.applyPaintStroke(
                points, Qt::black,
                image_editor::ImageDocumentStore::kMaximumPaintBrushDiameter + 1, &error) &&
                !error.isEmpty(),
            QStringLiteral("A paint diameter above the supported range was accepted."));
    error = QStringLiteral("previous failure");
    require(!session.applyPaintStroke(points, QColor(0, 0, 0, 0), 12, &error) && error.isEmpty(),
            QStringLiteral("A fully transparent paint stroke should have no effect."));
    require(session.data().layers.at(1).operations.size() == operation_count &&
                session.renderedImage() == painted && session.canUndo(),
            QStringLiteral("A rejected or invisible stroke changed the document or history."));

    image_editor::ImageDocumentSession maximum_brush_session;
    require(maximum_brush_session.openImage(source_path, &error), error);
    require(maximum_brush_session.applyPaintStroke(
                {QPointF(8.0, 6.0)}, Qt::black,
                image_editor::ImageDocumentStore::kMaximumPaintBrushDiameter, &error), error);
    const QString maximum_brush_path = root + QStringLiteral("/maximum-brush.cimg");
    require(maximum_brush_session.saveDocument(maximum_brush_path, &error), error);
    QFile maximum_brush_file(maximum_brush_path);
    require(maximum_brush_file.open(QIODevice::ReadOnly),
            QStringLiteral("The maximum-brush document could not be read."));
    const QJsonObject maximum_brush_json =
        QJsonDocument::fromJson(maximum_brush_file.readAll()).object();
    const int serialized_maximum_diameter = maximum_brush_json.value("layers").toArray()
        .at(1).toObject().value("operations").toArray()
        .at(0).toObject().value("diameter").toInt();
    require(maximum_brush_json.value("version").toInt() == 5 &&
                serialized_maximum_diameter ==
                    image_editor::ImageDocumentStore::kMaximumPaintBrushDiameter,
            QStringLiteral("The 1024 px paint diameter was not saved in version 5."));
    image_editor::ImageDocumentSession reopened_maximum_brush;
    require(reopened_maximum_brush.openDocument(maximum_brush_path, &error), error);
    require(reopened_maximum_brush.data() == maximum_brush_session.data() &&
                reopened_maximum_brush.data().layers.at(1).operations.front()
                        .paint_stroke.diameter ==
                    image_editor::ImageDocumentStore::kMaximumPaintBrushDiameter,
            QStringLiteral("A 1024 px paint stroke did not round-trip through version 5."));

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
    require(saved_json.value("version").toInt() == 5 &&
                saved_json.value("layers").toArray().at(1).toObject()
                    .value("operations").toArray().at(0).toObject()
                    .value("kind").toString() == "paint_stroke",
            QStringLiteral("Paint was not serialized in the version 5 layer operations."));

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
    require(restored.data() == reopened.data() &&
                restored.renderedImage() == reopened.renderedImage() && restored.isDirty(),
            QStringLiteral("Recovery did not preserve the painted layer stack and operations."));

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

void testEraseStrokesPersistenceUndoRedoAndValidation(const QString& root) {
    const QString source_path = root + QStringLiteral("/erase-source.png");
    QImage source(16, 12, QImage::Format_ARGB32);
    source.fill(Qt::white);
    require(writeImage(source_path, source),
            QStringLiteral("Could not create the erase source image."));
    QFile original_file(source_path);
    require(original_file.open(QIODevice::ReadOnly),
            QStringLiteral("Could not read the original erase source."));
    const QByteArray original_bytes = original_file.readAll();
    original_file.close();

    image_editor::ImageDocumentSession session;
    QString error;
    require(session.openImage(source_path, &error), error);
    const QVector<QPointF> paint_points{QPointF(3, 6), QPointF(12, 6)};
    require(session.applyPaintStroke(paint_points, QColor(220, 20, 40), 5, &error), error);
    const QImage painted = session.renderedImage();
    require(painted.pixelColor(8, 6).red() > 200,
            QStringLiteral("The test paint stroke was not visible before erasing."));

    const QString v4_path = root + QStringLiteral("/erase-v4-compatible.cimg");
    const QString current_path = root + QStringLiteral("/erase-current.cimg");
    image_editor::ImageDocumentSession compatibility_session;
    require(compatibility_session.openImage(source_path, &error), error);
    require(compatibility_session.applyPaintStroke(
                paint_points, QColor(220, 20, 40), 5, &error), error);
    require(compatibility_session.saveDocument(current_path, &error), error);
    QFile current_file(current_path);
    require(current_file.open(QIODevice::ReadOnly),
            QStringLiteral("Could not read the current document fixture."));
    QJsonObject v4_document = QJsonDocument::fromJson(current_file.readAll()).object();
    current_file.close();
    v4_document.insert("version", 4);
    QFile v4_file(v4_path);
    require(v4_file.open(QIODevice::WriteOnly),
            QStringLiteral("Could not create the version 4 fixture."));
    v4_file.write(QJsonDocument(v4_document).toJson());
    v4_file.close();
    image_editor::ImageDocumentSession v4_reopened;
    require(v4_reopened.openDocument(v4_path, &error) &&
                v4_reopened.renderedImage() == painted,
            QStringLiteral("A version 4 layered document did not remain readable."));

    const QVector<QPointF> erase_points{QPointF(8, 6)};
    const auto before_preview = session.data();
    const bool undo_before_preview = session.canUndo();
    const QImage preview = session.renderedImageWithEraseStroke(erase_points, 5);
    require(preview.pixelColor(8, 6) == QColor(Qt::white) &&
                session.data() == before_preview && session.renderedImage() == painted &&
                session.canUndo() == undo_before_preview,
            QStringLiteral("The temporary erase preview changed the document or history."));

    require(session.applyEraseStroke(erase_points, 5, &error), error);
    const QImage erased = session.renderedImage();
    require(erased.pixelColor(8, 6) == QColor(Qt::white) && session.isDirty() &&
                session.data().layers.at(1).operations.size() == 2 &&
                session.data().layers.at(1).operations.back().kind ==
                    image_editor::OperationKind::EraseStroke,
            QStringLiteral("The eraser did not clear alpha from the selected layer."));
    require(session.undo() && session.renderedImage() == painted &&
                session.undo() && session.renderedImage() == source && !session.isDirty(),
            QStringLiteral("The erase gesture was not one undoable edit."));
    require(session.redo() && session.renderedImage() == painted &&
                session.redo() && session.renderedImage() == erased,
            QStringLiteral("Redo did not restore the complete erase stroke."));

    const int operation_count = session.data().layers.at(1).operations.size();
    const auto rejectErase = [&](const QVector<QPointF>& points, int diameter,
                                 const QString& message) {
        error.clear();
        require(!session.applyEraseStroke(points, diameter, &error) && !error.isEmpty(), message);
        require(session.data().layers.at(1).operations.size() == operation_count &&
                    session.renderedImage() == erased,
                QStringLiteral("A rejected erase stroke changed the document."));
    };
    rejectErase({}, 12, QStringLiteral("An empty erase stroke was accepted."));
    rejectErase({QPointF(-1, 0)}, 12,
                QStringLiteral("An out-of-bounds erase point was accepted."));
    rejectErase({QPointF(std::numeric_limits<double>::infinity(), 1)}, 12,
                QStringLiteral("A non-finite erase point was accepted."));
    rejectErase(erase_points, 0, QStringLiteral("A zero-diameter eraser was accepted."));
    rejectErase(erase_points,
                image_editor::ImageDocumentStore::kMaximumPaintBrushDiameter + 1,
                QStringLiteral("An oversized eraser was accepted."));

    const QString maximum_path = root + QStringLiteral("/maximum-eraser.cimg");
    image_editor::ImageDocumentSession maximum_eraser;
    require(maximum_eraser.openImage(source_path, &error), error);
    require(maximum_eraser.applyPaintStroke({QPointF(8, 6)}, Qt::red, 1024, &error), error);
    require(maximum_eraser.applyEraseStroke({QPointF(8, 6)}, 1024, &error), error);
    require(maximum_eraser.saveDocument(maximum_path, &error), error);
    QFile maximum_file(maximum_path);
    require(maximum_file.open(QIODevice::ReadOnly),
            QStringLiteral("The maximum eraser document could not be read."));
    const QJsonObject maximum_json = QJsonDocument::fromJson(maximum_file.readAll()).object();
    const auto operations = maximum_json.value("layers").toArray().at(1).toObject()
        .value("operations").toArray();
    require(maximum_json.value("version").toInt() == 5 && operations.size() == 2 &&
                operations.at(1).toObject().value("kind").toString() == "erase_stroke" &&
                operations.at(1).toObject().value("diameter").toInt() == 1024,
            QStringLiteral("A maximum-size erase stroke was not serialized as version 5."));
    QJsonObject version_four_with_erase = maximum_json;
    version_four_with_erase.insert("version", 4);
    const QString invalid_v4_path = root + QStringLiteral("/version-four-erase.cimg");
    QFile invalid_v4_file(invalid_v4_path);
    require(invalid_v4_file.open(QIODevice::WriteOnly),
            QStringLiteral("Could not create a version 4 eraser fixture."));
    invalid_v4_file.write(QJsonDocument(version_four_with_erase).toJson());
    invalid_v4_file.close();
    image_editor::ImageDocumentSession invalid_v4_session;
    require(!invalid_v4_session.openDocument(invalid_v4_path, &error) && !error.isEmpty(),
            QStringLiteral("A version 4 document incorrectly accepted an eraser operation."));

    auto invalid_erase_layers = maximum_json.value("layers").toArray();
    auto invalid_erase_layer = invalid_erase_layers.at(1).toObject();
    auto invalid_erase_operations = invalid_erase_layer.value("operations").toArray();
    auto invalid_erase = invalid_erase_operations.at(1).toObject();
    invalid_erase.insert("diameter", 1025);
    invalid_erase_operations.replace(1, invalid_erase);
    invalid_erase_layer.insert("operations", invalid_erase_operations);
    invalid_erase_layers.replace(1, invalid_erase_layer);
    QJsonObject oversized_erase_document = maximum_json;
    oversized_erase_document.insert("layers", invalid_erase_layers);
    const QString oversized_erase_path = root + QStringLiteral("/oversized-erase.cimg");
    QFile oversized_erase_file(oversized_erase_path);
    require(oversized_erase_file.open(QIODevice::WriteOnly),
            QStringLiteral("Could not create an oversized eraser fixture."));
    oversized_erase_file.write(QJsonDocument(oversized_erase_document).toJson());
    oversized_erase_file.close();
    image_editor::ImageDocumentSession oversized_erase_session;
    require(!oversized_erase_session.openDocument(oversized_erase_path, &error) &&
                !error.isEmpty(),
            QStringLiteral("A version 5 erase stroke above 1024 px was accepted."));

    image_editor::ImageDocumentSession maximum_reopened;
    require(maximum_reopened.openDocument(maximum_path, &error) &&
                maximum_reopened.data() == maximum_eraser.data() &&
                maximum_reopened.renderedImage() == maximum_eraser.renderedImage(), error);

    const QString document_path = root + QStringLiteral("/erased.cimg");
    require(session.saveDocument(document_path, &error), error);
    image_editor::ImageDocumentSession reopened;
    require(reopened.openDocument(document_path, &error) &&
                reopened.data() == session.data() && reopened.renderedImage() == erased,
            QStringLiteral("The paint and erase operations did not round-trip."));
    const QString recovery_directory = root + QStringLiteral("/erase-recovery");
    image_editor::RecoveryStore recovery(recovery_directory);
    reopened.rotateRight();
    require(recovery.save(reopened, &error), error);
    image_editor::ImageDocumentSession restored;
    const auto snapshots = recovery.snapshots();
    require(snapshots.size() == 1 && restored.restoreRecovery(snapshots.front(), &error) &&
                restored.data() == reopened.data() &&
                restored.renderedImage() == reopened.renderedImage(),
            QStringLiteral("Recovery did not preserve the eraser operation."));

    require(reopened.selectLayer(reopened.data().layers.front().id),
            QStringLiteral("Could not select Background for the eraser lock check."));
    require(!reopened.applyEraseStroke(erase_points, 5, &error) && !error.isEmpty(),
            QStringLiteral("The locked Background layer accepted an erase stroke."));
    QFile unchanged_source(source_path);
    require(unchanged_source.open(QIODevice::ReadOnly) &&
                unchanged_source.readAll() == original_bytes,
            QStringLiteral("Erasing modified the original source image."));
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
    require(session.renderedImage().size() == QSize(4, 3),
            QStringLiteral("The clipped layer crop changed the fixed canvas dimensions."));
}

void testLayerManagementTransformsAndOpacity(const QString& root) {
    image_editor::ImageDocumentSession session;
    QString error;
    require(session.createCanvas(QSize(12, 8), Qt::transparent, &error), error);
    const QString background_id = session.data().layers.front().id;
    const QString first_id = session.selectedLayerId();
    require(session.data().layers.size() == 2 &&
                session.data().layers.front().name == QStringLiteral("Background") &&
                session.data().layers.at(1).name == QStringLiteral("Layer 1"),
            QStringLiteral("A new canvas did not start with Background and Layer 1."));

    const QVector<QPointF> red_stroke{QPointF(2, 4), QPointF(6, 4)};
    require(session.applyPaintStroke(red_stroke, Qt::red, 2, &error), error);
    const QImage painted = session.renderedImage();
    require(painted.size() == QSize(12, 8) && painted.pixelColor(4, 4).red() > 240,
            QStringLiteral("Paint did not render into the selected transparent layer."));
    require(session.selectLayer(background_id) && !session.selectedLayerIsEditable(),
            QStringLiteral("The Background layer could not be selected as a locked layer."));
    require(!session.applyPaintStroke({QPointF(4, 4)}, Qt::blue, 2, &error) &&
                !error.isEmpty() && session.data().layers.front().operations.isEmpty(),
            QStringLiteral("Painting into Background was not rejected."));
    require(!session.deleteLayer(background_id) &&
                !session.setLayerOpacity(background_id, 50) &&
                !session.moveLayer(background_id, 1),
            QStringLiteral("A locked Background layer was modified."));
    require(session.setLayerVisible(background_id, false) &&
                session.renderedImage().pixelColor(4, 4).red() > 240,
            QStringLiteral("Hiding Background also hid a normal editable layer."));
    require(session.undo() && session.renderedImage().pixelColor(4, 4).red() > 240,
            QStringLiteral("Undo did not restore Background visibility."));
    require(session.selectLayer(first_id), QStringLiteral("The editable layer could not be reselected."));

    require(session.applyCrop(QRect(3, 0, 6, 8), &error), error);
    const QImage cropped = session.renderedImage();
    require(cropped.size() == QSize(12, 8) && cropped.pixelColor(2, 4).alpha() == 0 &&
                cropped.pixelColor(4, 4).red() > 240,
            QStringLiteral("Layer crop did not clear only pixels outside the selection."));
    session.rotateRight();
    const QImage rotated = session.renderedImage();
    require(rotated.size() == QSize(12, 8) && rotated != cropped,
            QStringLiteral("Layer rotation did not rotate content within fixed canvas bounds."));
    require(session.undo() && session.renderedImage() == cropped && session.redo() &&
                session.renderedImage() == rotated,
            QStringLiteral("Undo/redo did not restore the selected layer transform."));

    image_editor::ImageDocumentSession flips;
    require(flips.createCanvas(QSize(8, 8), Qt::transparent, &error), error);
    require(flips.applyPaintStroke({QPointF(1, 3)}, Qt::green, 2, &error), error);
    const QColor original_mark = flips.renderedImage().pixelColor(1, 3);
    flips.flipHorizontal();
    require(flips.renderedImage().size() == QSize(8, 8) &&
                flips.renderedImage().pixelColor(6, 3) == original_mark,
            QStringLiteral("Horizontal flip did not mirror selected-layer content."));
    flips.flipVertical();
    require(flips.renderedImage().size() == QSize(8, 8) &&
                flips.renderedImage().pixelColor(6, 4) == original_mark &&
                flips.undo() && flips.undo() && flips.renderedImage().pixelColor(1, 3) == original_mark,
            QStringLiteral("Vertical flip or undo changed canvas bounds or layer content."));

    const QString second_id = session.addLayer();
    require(!second_id.isEmpty() && session.selectedLayerId() == second_id &&
                session.data().layers.size() == 3 && session.data().layers.at(2).id == second_id,
            QStringLiteral("A new layer was not inserted above the selected layer."));
    require(session.renameLayer(second_id, QStringLiteral("Overlay"), &error), error);
    require(session.moveLayer(second_id, -1) && session.data().layers.at(1).id == second_id,
            QStringLiteral("Layer rename or reordering failed."));
    require(session.deleteLayer(second_id) && session.data().layers.size() == 2 &&
                session.selectedLayerId() == first_id,
            QStringLiteral("Deleting the selected layer did not select the adjacent layer."));
    require(session.deleteLayer(first_id) && session.data().layers.size() == 1 &&
                session.selectedLayerId() == background_id && !session.selectedLayerIsEditable(),
            QStringLiteral("Deleting the last editable layer did not select Background."));

    image_editor::ImageDocumentSession stacking;
    require(stacking.createCanvas(QSize(8, 8), Qt::transparent, &error), error);
    const QString red_layer = stacking.selectedLayerId();
    require(stacking.applyPaintStroke({QPointF(4, 4)}, Qt::red, 2, &error), error);
    const QString blue_layer = stacking.addLayer();
    require(stacking.applyPaintStroke({QPointF(4, 4)}, Qt::blue, 2, &error), error);
    require(stacking.renderedImage().pixelColor(4, 4).blue() >
                stacking.renderedImage().pixelColor(4, 4).red(),
            QStringLiteral("The top layer did not composite above lower layers: %1,%2,%3,%4.")
                .arg(stacking.renderedImage().pixelColor(4, 4).red())
                .arg(stacking.renderedImage().pixelColor(4, 4).green())
                .arg(stacking.renderedImage().pixelColor(4, 4).blue())
                .arg(stacking.renderedImage().pixelColor(4, 4).alpha()));
    require(stacking.moveLayer(blue_layer, -1) &&
                stacking.renderedImage().pixelColor(4, 4).red() >
                    stacking.renderedImage().pixelColor(4, 4).blue(),
            QStringLiteral("Reordering did not change the layer composite order."));
    require(stacking.setLayerVisible(red_layer, false) &&
                stacking.renderedImage().pixelColor(4, 4).blue() >
                    stacking.renderedImage().pixelColor(4, 4).red(),
            QStringLiteral("Layer visibility did not update the composite."));
    const int full_opacity_alpha = stacking.renderedImage().pixelColor(4, 4).alpha();
    require(stacking.setLayerOpacity(blue_layer, 50),
            QStringLiteral("Layer opacity could not be changed."));
    const QColor half_opacity = stacking.renderedImage().pixelColor(4, 4);
    require(stacking.data().layers.at(1).opacity == 50 &&
                half_opacity.alpha() >= full_opacity_alpha * 0.45 &&
                half_opacity.alpha() <= full_opacity_alpha * 0.55 &&
                half_opacity.blue() > half_opacity.red(),
            QStringLiteral("Layer opacity was not applied during compositing."));

    image_editor::ImageDocumentSession selection_source;
    require(selection_source.createCanvas(QSize(8, 8), Qt::transparent, &error), error);
    const QString bottom_layer = selection_source.selectedLayerId();
    const QString top_layer = selection_source.addLayer();
    const QString selection_path = root + QStringLiteral("/layer-selection.cimg");
    require(selection_source.saveDocument(selection_path, &error), error);
    image_editor::ImageDocumentSession selection;
    require(selection.openDocument(selection_path, &error), error);
    require(selection.selectLayer(bottom_layer) && !selection.isDirty() &&
                !selection.canUndo(),
            QStringLiteral("Changing the selected layer marked the document dirty or added history."));
    const QString inserted_layer = selection.addLayer();
    require(selection.selectedLayerId() == inserted_layer &&
                selection.data().layers.at(2).id == inserted_layer,
            QStringLiteral("A layer was not inserted above the selected middle layer."));
    require(selection.undo() && selection.selectedLayerId() == bottom_layer &&
                selection.redo() && selection.selectedLayerId() == inserted_layer &&
                selection.data().layers.at(3).id == top_layer,
            QStringLiteral("Undo/redo did not preserve the relevant layer selection."));

    image_editor::ImageDocumentSession opacity;
    require(opacity.createCanvas(QSize(8, 8), Qt::transparent, &error), error);
    const QString opacity_layer = opacity.selectedLayerId();
    const QString saved_path = root + QStringLiteral("/layer-opacity.cimg");
    require(opacity.saveDocument(saved_path, &error), error);
    opacity.beginLayerOpacityEdit();
    require(opacity.setLayerOpacity(opacity_layer, 75) &&
                opacity.setLayerOpacity(opacity_layer, 40),
            QStringLiteral("Layer opacity changes were not applied during the gesture."));
    opacity.endLayerOpacityEdit();
    require(opacity.canUndo() && opacity.data().layers.at(1).opacity == 40,
            QStringLiteral("Opacity drag did not create one undoable change."));
    require(opacity.undo() && !opacity.canUndo() && !opacity.isDirty() &&
                opacity.data().layers.at(1).opacity == 100,
            QStringLiteral("Undo did not restore the opacity baseline."));
    require(!opacity.setLayerOpacity(opacity_layer, 100) && !opacity.canUndo(),
            QStringLiteral("An opacity no-op created history."));
    require(opacity.redo() && opacity.data().layers.at(1).opacity == 40,
            QStringLiteral("Redo did not restore the grouped opacity change."));
    require(opacity.saveDocument(saved_path, &error), error);
    image_editor::ImageDocumentSession reopened;
    require(reopened.openDocument(saved_path, &error), error);
    require(reopened.data() == opacity.data() && reopened.selectedLayerId() == opacity_layer,
            QStringLiteral("Layer IDs or properties did not round-trip."));

    image_editor::ImageDocumentSession thumbnail_session;
    require(thumbnail_session.createCanvas(QSize(16, 8), Qt::transparent, &error), error);
    const QString thumbnail_layer = thumbnail_session.selectedLayerId();
    const QString thumbnail_background = thumbnail_session.data().layers.front().id;
    auto thumbnails = thumbnail_session.renderedLayerThumbnails(QSize(48, 36));
    require(thumbnails.contains(thumbnail_layer) &&
                thumbnails.value(thumbnail_layer).size() == QSize(48, 24) &&
                thumbnails.value(thumbnail_layer).pixelColor(24, 12).alpha() == 0 &&
                thumbnails.value(thumbnail_background).pixelColor(24, 12).alpha() == 0,
            QStringLiteral("Layer thumbnails did not preserve the canvas aspect ratio and alpha."));
    require(thumbnail_session.applyPaintStroke({QPointF(7, 2)}, Qt::red, 3, &error), error);
    thumbnails = thumbnail_session.renderedLayerThumbnails(QSize(48, 36));
    const QImage painted_thumbnail = thumbnails.value(thumbnail_layer);
    require(painted_thumbnail.pixelColor(21, 6).red() > 240 &&
                painted_thumbnail.pixelColor(21, 6).alpha() > 0,
            QStringLiteral("A painted layer thumbnail did not show its isolated content."));
    const qint64 painted_thumbnail_key = painted_thumbnail.cacheKey();
    require(thumbnail_session.setLayerVisible(thumbnail_layer, false) &&
                thumbnail_session.setLayerOpacity(thumbnail_layer, 0),
            QStringLiteral("Could not hide and fade the thumbnail test layer."));
    require(thumbnail_session.selectLayer(thumbnail_background) &&
                thumbnail_session.selectLayer(thumbnail_layer),
            QStringLiteral("Could not change selection in the thumbnail cache test."));
    require(thumbnail_session.renameLayer(thumbnail_layer, QStringLiteral("Painted"), &error),
            error);
    thumbnails = thumbnail_session.renderedLayerThumbnails(QSize(48, 36));
    require(thumbnails.value(thumbnail_layer).cacheKey() == painted_thumbnail_key &&
                thumbnails.value(thumbnail_layer).pixelColor(21, 6).red() > 240,
            QStringLiteral("Selection, name, visibility, or opacity incorrectly changed the isolated thumbnail."));
    require(thumbnail_session.undo() && thumbnail_session.undo() &&
                thumbnail_session.undo(),
            QStringLiteral("Could not undo the thumbnail name, visibility, and opacity edits."));
    thumbnail_session.rotateRight();
    const QImage transformed_thumbnail =
        thumbnail_session.renderedLayerThumbnails(QSize(48, 36)).value(thumbnail_layer);
    require(transformed_thumbnail != painted_thumbnail &&
                transformed_thumbnail.pixelColor(30, 9).red() > 240,
            QStringLiteral("A transform did not refresh the layer thumbnail."));
    require(thumbnail_session.undo(), QStringLiteral("Could not undo the thumbnail transform."));
    require(thumbnail_session.renderedLayerThumbnails(QSize(48, 36)).value(thumbnail_layer) ==
                painted_thumbnail,
            QStringLiteral("Undo did not restore the previous layer thumbnail."));
    require(thumbnail_session.createCanvas(QSize(16, 8), Qt::blue, &error), error);
    const auto replacement_thumbnails =
        thumbnail_session.renderedLayerThumbnails(QSize(48, 36));
    const QString replacement_background = thumbnail_session.data().layers.front().id;
    require(replacement_thumbnails.value(replacement_background).pixelColor(24, 12) == Qt::blue,
            QStringLiteral("Opening a replacement document returned a stale Background thumbnail."));
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

    QJsonObject valid_paint_point;
    valid_paint_point.insert("x", 1.0);
    valid_paint_point.insert("y", 1.0);
    QJsonArray valid_paint_points;
    valid_paint_points.append(valid_paint_point);
    QJsonObject oversized_stroke;
    oversized_stroke.insert("kind", "paint_stroke");
    oversized_stroke.insert("color", "#FF000000");
    oversized_stroke.insert("diameter",
                            image_editor::ImageDocumentStore::kMaximumPaintBrushDiameter + 1);
    oversized_stroke.insert("points", valid_paint_points);
    QJsonArray oversized_operations;
    oversized_operations.append(oversized_stroke);
    invalid_paint_document.insert("operations", oversized_operations);
    const QString oversized_stroke_path = root + QStringLiteral("/oversized-stroke.cimg");
    QFile oversized_stroke_file(oversized_stroke_path);
    require(oversized_stroke_file.open(QIODevice::WriteOnly),
            QStringLiteral("Could not create an oversized paint document."));
    oversized_stroke_file.write(QJsonDocument(invalid_paint_document).toJson());
    oversized_stroke_file.close();
    require(!session.openDocument(oversized_stroke_path, &error) && !error.isEmpty(),
            QStringLiteral("A paint stroke above 1024 px was accepted from a document."));
    require(session.sourcePath() == QFileInfo(source_path).absoluteFilePath() &&
                session.renderedImage() == original_render,
            QStringLiteral("An oversized paint stroke replaced the current image."));
    auto duplicate_layer_ids = session.data();
    duplicate_layer_ids.layers[1].id = duplicate_layer_ids.layers.front().id;
    require(!image_editor::ImageDocumentStore::saveDocument(
                root + QStringLiteral("/duplicate-layer-ids.cimg"), duplicate_layer_ids, &error) &&
                !error.isEmpty(),
            QStringLiteral("A document with duplicate layer IDs was saved."));
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
        testVersionThreeMigrationToBackground(root);
        testPaintStrokesPersistenceUndoRedoAndValidation(root);
        testEraseStrokesPersistenceUndoRedoAndValidation(root);
        testCropNoOpAndInvalidOperations(root);
        testLayerManagementTransformsAndOpacity(root);
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
