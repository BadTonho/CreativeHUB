#include "image_document_session.h"
#include "image_document_renderer.h"
#include "image_document_store.h"
#include "image_editor_logger.h"
#include "image_layer_stack_editor.h"
#include "recovery_store.h"

#include <QGuiApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <utility>

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

bool visuallyEquivalent(const QImage& left, const QImage& right, int tolerance = 2) {
    if (left.size() != right.size()) return false;
    for (int y = 0; y < left.height(); ++y) {
        for (int x = 0; x < left.width(); ++x) {
            const QColor a = left.pixelColor(x, y);
            const QColor b = right.pixelColor(x, y);
            if (std::abs(a.red() - b.red()) > tolerance ||
                std::abs(a.green() - b.green()) > tolerance ||
                std::abs(a.blue() - b.blue()) > tolerance ||
                std::abs(a.alpha() - b.alpha()) > tolerance) return false;
        }
    }
    return true;
}

void testStatelessDocumentRenderer() {
    using namespace image_editor;

    const QString background_id = QStringLiteral("background");
    const QString layer_id = QStringLiteral("paint-layer");
    const QString group_id = QStringLiteral("paint-group");
    const QString paint_id = QStringLiteral("green-mark");

    QImage source(QSize(4, 3), QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::blue);

    ImageDocumentData document;
    document.source_size = source.size();
    document.canvas_size = source.size();
    ImageLayerData background;
    background.id = background_id;
    background.name = QStringLiteral("Background");
    background.background = true;
    document.layers.append(background);

    ImageLayerData paint_layer;
    paint_layer.id = layer_id;
    paint_layer.name = QStringLiteral("Paint");
    paint_layer.parent_group_id = group_id;
    ImageOperation paint;
    paint.kind = OperationKind::PaintStroke;
    paint.paint_stroke.id = paint_id;
    paint.paint_stroke.points = {QPointF(1.5, 1.5)};
    paint.paint_stroke.color = Qt::green;
    paint.paint_stroke.diameter = 2;
    paint_layer.operations.append(paint);
    document.layers.append(paint_layer);

    ImageGroupData group;
    group.id = group_id;
    group.name = QStringLiteral("Paint group");
    group.layer_ids.append(layer_id);
    document.groups.append(group);
    document.root_stack = {{background_id, false}, {group_id, true}};

    const QHash<QString, QImage> raster_images;
    require(ImageDocumentRenderer::documentSize(document, source.size()) == source.size(),
            QStringLiteral("The renderer should preserve the document bounds."));

    const QImage composite = ImageDocumentRenderer::composite(
        document, source, raster_images);
    require(!composite.isNull() && composite.size() == source.size() &&
                composite.pixelColor(1, 1) == QColor(Qt::green),
            QStringLiteral("The stateless renderer should composite document operations."));

    const QImage without_mark = ImageDocumentRenderer::composite(
        document, source, raster_images, {paint_id});
    require(!without_mark.isNull() && without_mark.pixelColor(1, 1) == QColor(Qt::blue),
            QStringLiteral("The renderer should exclude requested object IDs from previews."));

    const QImage selected_layer = ImageDocumentRenderer::selectedLayer(
        document, source, raster_images, layer_id);
    require(!selected_layer.isNull() &&
                selected_layer.pixelColor(1, 1) == QColor(Qt::green) &&
                selected_layer.pixelColor(3, 2).alpha() == 0,
            QStringLiteral("The renderer should isolate a selected raster layer (center %1, edge %2).")
                .arg(selected_layer.pixelColor(1, 1).name(QColor::HexArgb),
                     selected_layer.pixelColor(3, 2).name(QColor::HexArgb)));

    const QImage selected_group = ImageDocumentRenderer::selectedGroup(
        document, source, raster_images, group_id);
    require(!selected_group.isNull() &&
                selected_group.pixelColor(1, 1) == QColor(Qt::green) &&
                selected_group.pixelColor(3, 2).alpha() == 0,
            QStringLiteral("The renderer should isolate a selected group."));

    std::atomic_bool cancelled{true};
    require(ImageDocumentRenderer::composite(
                document, source, raster_images, {}, &cancelled).isNull(),
            QStringLiteral("The renderer should stop when cancellation is requested."));
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
    require(document_json.value("version").toInt() == 13 &&
                document_json.value("base").toObject().value("kind").toString() == "canvas",
            QStringLiteral("Canvas save did not use the version 13 grouped-layer and canvas representation."));

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

void testCanvasResizingAnchorsPersistenceAndHistory(const QString& root) {
    constexpr std::array<image_editor::CanvasAnchor, 9> anchors = {
        image_editor::CanvasAnchor::TopLeft, image_editor::CanvasAnchor::Top,
        image_editor::CanvasAnchor::TopRight, image_editor::CanvasAnchor::Left,
        image_editor::CanvasAnchor::Center, image_editor::CanvasAnchor::Right,
        image_editor::CanvasAnchor::BottomLeft, image_editor::CanvasAnchor::Bottom,
        image_editor::CanvasAnchor::BottomRight};
    const auto offset_for = [](const QSize& from, const QSize& to,
                               image_editor::CanvasAnchor anchor) {
        int column = 1;
        int row = 1;
        switch (anchor) {
        case image_editor::CanvasAnchor::TopLeft:
        case image_editor::CanvasAnchor::Left:
        case image_editor::CanvasAnchor::BottomLeft: column = 0; break;
        case image_editor::CanvasAnchor::TopRight:
        case image_editor::CanvasAnchor::Right:
        case image_editor::CanvasAnchor::BottomRight: column = 2; break;
        default: break;
        }
        switch (anchor) {
        case image_editor::CanvasAnchor::TopLeft:
        case image_editor::CanvasAnchor::Top:
        case image_editor::CanvasAnchor::TopRight: row = 0; break;
        case image_editor::CanvasAnchor::BottomLeft:
        case image_editor::CanvasAnchor::Bottom:
        case image_editor::CanvasAnchor::BottomRight: row = 2; break;
        default: break;
        }
        const auto component = [](int difference, int alignment) {
            return alignment == 0 ? 0 : (alignment == 2 ? difference : difference / 2);
        };
        return QPoint(component(to.width() - from.width(), column),
                      component(to.height() - from.height(), row));
    };

    const QString source_path = root + QStringLiteral("/resize-source.png");
    const QImage small_source = sampleImage();
    require(writeImage(source_path, small_source),
            QStringLiteral("Could not create the canvas resize source image."));
    const QSize expanded_size(10, 8);
    for (const auto anchor : anchors) {
        image_editor::ImageDocumentSession session;
        QString error;
        require(session.openImage(source_path, &error), error);
        const QImage original = session.renderedImage();
        const QPoint offset = offset_for(original.size(), expanded_size, anchor);
        require(session.resizeCanvas(expanded_size, anchor, &error), error);
        const QImage expanded = session.renderedImage();
        require(expanded.size() == expanded_size &&
                    session.data().source_size == original.size() &&
                    session.data().canvas_base_offset == offset,
                QStringLiteral("Canvas expansion did not retain the source size and anchor offset."));
        for (int y = 0; y < original.height(); ++y) {
            for (int x = 0; x < original.width(); ++x) {
                require(expanded.pixelColor(x + offset.x(), y + offset.y()) == original.pixelColor(x, y),
                        QStringLiteral("Canvas expansion scaled or misplaced base pixels."));
            }
        }
        const int empty_x = offset.x() > 0 ? 0 : expanded.width() - 1;
        const int empty_y = offset.y() > 0 ? 0 : expanded.height() - 1;
        require(expanded.pixelColor(empty_x, empty_y).alpha() == 0,
                QStringLiteral("A source-image canvas extension was not transparent."));
        require(session.undo() && session.renderedImage() == original && !session.isDirty(),
                QStringLiteral("Canvas resizing was not a single undoable edit."));
        require(session.redo() && session.renderedImage() == expanded,
                QStringLiteral("Canvas resize Redo did not restore the expanded pixels."));
    }

    QImage large_source(10, 8, QImage::Format_ARGB32);
    for (int y = 0; y < large_source.height(); ++y)
        for (int x = 0; x < large_source.width(); ++x)
            large_source.setPixelColor(x, y, QColor(x * 17, y * 23, x + y, 255));
    const QString large_source_path = root + QStringLiteral("/resize-large-source.png");
    require(writeImage(large_source_path, large_source),
            QStringLiteral("Could not create the canvas reduction source image."));
    const QSize reduced_size(4, 3);
    for (const auto anchor : anchors) {
        image_editor::ImageDocumentSession session;
        QString error;
        require(session.openImage(large_source_path, &error), error);
        const QPoint offset = offset_for(large_source.size(), reduced_size, anchor);
        require(session.resizeCanvas(reduced_size, anchor, &error), error);
        const QImage reduced = session.renderedImage();
        require(reduced.size() == reduced_size,
                QStringLiteral("Canvas reduction did not apply its requested dimensions."));
        for (int y = 0; y < reduced.height(); ++y) {
            for (int x = 0; x < reduced.width(); ++x) {
                const QPoint source_point(x - offset.x(), y - offset.y());
                const QColor expected = QRect(QPoint(), large_source.size()).contains(source_point)
                    ? large_source.pixelColor(source_point) : QColor(0, 0, 0, 0);
                require(reduced.pixelColor(x, y) == expected,
                        QStringLiteral("Canvas reduction did not crop the source at its selected anchor."));
            }
        }
        require(session.undo() && session.renderedImage() == large_source,
                QStringLiteral("Undo did not restore the full canvas after reduction."));
    }

    image_editor::ImageDocumentSession canvas_document;
    QString error;
    const QColor background(31, 81, 141, 177);
    require(canvas_document.createCanvas(QSize(4, 3), background, &error), error);
    const QImage canvas_before_resize = canvas_document.renderedImage();
    require(canvas_document.resizeCanvas(QSize(8, 7), image_editor::CanvasAnchor::Center, &error), error);
    require(canvas_document.renderedImage().pixelColor(0, 0) == background &&
                canvas_document.renderedImage().pixelColor(2, 2) ==
                    canvas_before_resize.pixelColor(0, 0),
            QStringLiteral("Canvas resizing did not fill the new area with the configured background."));

    image_editor::ImageDocumentSession structured;
    require(structured.openImage(source_path, &error), error);
    const QString first_layer = structured.addLayer();
    const QString second_layer = structured.addLayer();
    require(!first_layer.isEmpty() && !second_layer.isEmpty(),
            QStringLiteral("Could not create editable layers for resize coverage."));
    require(structured.selectLayer(first_layer) &&
                structured.applyPaintStroke({QPointF(1, 1)}, Qt::red, 2, &error), error);
    require(structured.selectLayer(second_layer) && structured.addLayerMask(second_layer) &&
                structured.applyLayerMaskStroke({QPointF(2, 1)}, Qt::white, 1, &error), error);
    const QString group_id = structured.groupLayers({first_layer, second_layer}, &error);
    require(!group_id.isEmpty(), error);
    structured.rotateRight();
    require(structured.resizeCanvas(QSize(10, 8), image_editor::CanvasAnchor::Center, &error), error);
    const QPoint offset(3, 2);
    const auto first_after = std::find_if(structured.data().layers.cbegin(), structured.data().layers.cend(),
        [&first_layer](const auto& layer) { return layer.id == first_layer; });
    const auto second_after = std::find_if(structured.data().layers.cbegin(), structured.data().layers.cend(),
        [&second_layer](const auto& layer) { return layer.id == second_layer; });
    require(first_after != structured.data().layers.cend() &&
                second_after != structured.data().layers.cend() &&
                first_after->operations.front().paint_stroke.points.front() == QPointF(1, 1) + offset &&
                second_after->mask.has_value() &&
                second_after->mask->operations.front().paint_stroke.points.front() == QPointF(2, 1) + offset &&
                structured.data().groups.front().operations.front().transform_bounds == QRect(3, 2, 4, 3),
            QStringLiteral("Resize did not shift layer content, masks, and group transform bounds together."));
    const QString resized_document = root + QStringLiteral("/resized-structured.cimg");
    require(structured.saveDocument(resized_document, &error), error);
    QFile resized_file(resized_document);
    require(resized_file.open(QIODevice::ReadOnly),
            QStringLiteral("The resized document could not be read."));
    const auto resized_json = QJsonDocument::fromJson(resized_file.readAll()).object();
    require(resized_json.value("version").toInt() == 13 &&
                resized_json.value("canvas").toObject().value("width").toInt() == 10 &&
                resized_json.value("base").toObject().value("width").toInt() == 4,
            QStringLiteral("The v13 document did not distinguish the resized canvas from its source."));
    image_editor::ImageDocumentSession reopened;
    require(reopened.openDocument(resized_document, &error), error);
    require(reopened.data() == structured.data() &&
                reopened.renderedImage() == structured.renderedImage(),
            QStringLiteral("A resized v13 document did not preserve its editable rendering."));
    const QString exported = root + QStringLiteral("/resized-export.png");
    require(reopened.exportImage(exported, &error), error);
    require(QImage(exported).size() == QSize(10, 8),
            QStringLiteral("Full export did not use the resized document dimensions."));

    const QString old_format = root + QStringLiteral("/v11-resize-compat.cimg");
    image_editor::ImageDocumentSession old_session;
    require(old_session.openImage(source_path, &error) && old_session.saveDocument(old_format, &error), error);
    QFile old_file(old_format);
    require(old_file.open(QIODevice::ReadOnly), QStringLiteral("Could not read the v13 compatibility fixture."));
    QJsonObject old_json = QJsonDocument::fromJson(old_file.readAll()).object();
    old_file.close();
    old_json.insert("version", 11);
    old_json.remove("canvas");
    QFile old_writer(old_format);
    require(old_writer.open(QIODevice::WriteOnly | QIODevice::Truncate),
            QStringLiteral("Could not write the v11 compatibility fixture."));
    old_writer.write(QJsonDocument(old_json).toJson());
    old_writer.close();
    image_editor::ImageDocumentSession migrated;
    require(migrated.openDocument(old_format, &error) &&
                migrated.renderedImage() == small_source &&
                migrated.renderedImage().size() == small_source.size(), error);
    require(migrated.saveDocument({}, &error), error);
    QFile migrated_file(old_format);
    require(migrated_file.open(QIODevice::ReadOnly) &&
                QJsonDocument::fromJson(migrated_file.readAll()).object().value("version").toInt() == 13,
            QStringLiteral("Saving a v11 document did not migrate it to v13."));

    const QString missing_document = root + QStringLiteral("/missing-source-resized.cimg");
    QJsonObject missing_json;
    QFile migrated_source_file(resized_document);
    require(migrated_source_file.open(QIODevice::ReadOnly),
            QStringLiteral("Could not read the saved v13 relink fixture."));
    missing_json = QJsonDocument::fromJson(migrated_source_file.readAll()).object();
    migrated_source_file.close();
    QJsonObject missing_base = missing_json.value("base").toObject();
    missing_base.insert("path", QStringLiteral("missing-original.png"));
    missing_json.insert("base", missing_base);
    QFile missing_writer(missing_document);
    require(missing_writer.open(QIODevice::WriteOnly),
            QStringLiteral("Could not write the missing-source v13 fixture."));
    missing_writer.write(QJsonDocument(missing_json).toJson());
    missing_writer.close();
    image_editor::ImageDocumentSession offline;
    require(offline.openDocument(missing_document, &error) && offline.sourceIsMissing(), error);
    const QString wrong_size_source = root + QStringLiteral("/wrong-size-source.png");
    QImage wrong_size(10, 8, QImage::Format_ARGB32);
    wrong_size.fill(Qt::green);
    require(writeImage(wrong_size_source, wrong_size),
            QStringLiteral("Could not create the incompatible relink source."));
    require(!offline.relinkSource(wrong_size_source, &error) && offline.sourceIsMissing(),
            QStringLiteral("Relink accepted a replacement with the resized canvas dimensions instead of the original source dimensions."));
    require(offline.relinkSource(source_path, &error) && offline.renderedImage().size() == QSize(10, 8), error);
    const auto before_rejected = migrated.data();
    require(!migrated.resizeCanvas(QSize(32768, 32768), image_editor::CanvasAnchor::Center, &error) &&
                migrated.data() == before_rejected,
            QStringLiteral("An over-budget resize changed the document."));
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
    require(QJsonDocument::fromJson(upgraded.readAll()).object().value("version").toInt() == 13,
            QStringLiteral("Saving a version 1 document did not upgrade it to version 13."));
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
    require(QJsonDocument::fromJson(upgraded.readAll()).object().value("version").toInt() == 13,
            QStringLiteral("Saving a version 2 document did not upgrade it to version 13."));

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
                !session.data().operations.front().paint_stroke.id.isEmpty() &&
                session.data().layers.size() == 2 &&
                session.selectedLayerIsEditable(),
            QStringLiteral("A version 3 paint edit or its generated object ID was not preserved on migrated Background."));
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
    require(QJsonDocument::fromJson(upgraded.readAll()).object().value("version").toInt() == 13,
            QStringLiteral("Saving a version 3 document did not upgrade it to version 13."));
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
    require(maximum_brush_json.value("version").toInt() == 13 &&
                serialized_maximum_diameter ==
                    image_editor::ImageDocumentStore::kMaximumPaintBrushDiameter,
            QStringLiteral("The 1024 px paint diameter was not saved in version 13."));
    image_editor::ImageDocumentSession reopened_maximum_brush;
    require(reopened_maximum_brush.openDocument(maximum_brush_path, &error), error);
    require(reopened_maximum_brush.data() == maximum_brush_session.data() &&
                reopened_maximum_brush.data().layers.at(1).operations.front()
                        .paint_stroke.diameter ==
                    image_editor::ImageDocumentStore::kMaximumPaintBrushDiameter,
            QStringLiteral("A 1024 px paint stroke did not round-trip through version 7."));

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
    require(saved_json.value("version").toInt() == 13 &&
                saved_json.value("layers").toArray().at(1).toObject()
                    .value("operations").toArray().at(0).toObject()
                    .value("kind").toString() == "paint_stroke",
            QStringLiteral("Paint was not serialized in the version 13 layer operations."));

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
    require(v4_reopened.saveDocument(v4_path, &error), error);
    QFile migrated_v4_file(v4_path);
    require(migrated_v4_file.open(QIODevice::ReadOnly) &&
                QJsonDocument::fromJson(migrated_v4_file.readAll()).object()
                        .value("version").toInt() == 13,
            QStringLiteral("Saving a version 4 document did not migrate it to v13."));

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
    require(maximum_json.value("version").toInt() == 13 && operations.size() == 2 &&
                operations.at(1).toObject().value("kind").toString() == "erase_stroke" &&
                operations.at(1).toObject().value("diameter").toInt() == 1024,
            QStringLiteral("A maximum-size erase stroke was not serialized as version 13."));
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
    oversized_erase_document.insert("version", 5);
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

void testEditableShapesRenderingPersistenceAndHistory(const QString& root) {
    image_editor::ImageDocumentSession session;
    QString error;
    require(session.createCanvas(QSize(64, 48), QColor(0, 0, 0, 0), &error), error);

    image_editor::ImageShapeData rectangle;
    rectangle.kind = image_editor::ImageShapeKind::Rectangle;
    rectangle.start = QPointF(4, 4);
    rectangle.end = QPointF(22, 22);
    rectangle.stroke_color = Qt::red;
    rectangle.stroke_width = 2;
    rectangle.fill_color = QColor(10, 210, 40, 160);
    QString rectangle_id = session.addShape(rectangle, &error);
    require(!rectangle_id.isEmpty(), error);
    QString rectangle_layer;
    require(session.findShape(rectangle_id, nullptr, &rectangle_layer) &&
                session.data().layers.size() == 3 &&
                session.data().layers.at(2).name == QStringLiteral("Shape 1") &&
                session.data().layers.at(2).operations.size() == 1 &&
                session.selectedLayerId() == rectangle_layer,
            QStringLiteral("Creating a shape did not add and select its own Shape 1 layer."));
    require(session.renderedImage().pixelColor(12, 12) == QColor(10, 210, 40, 160),
            QStringLiteral("The rectangle fill was not rendered with its alpha."));
    const QString original_layer = session.data().layers.at(1).id;
    require(session.undo() && session.data().layers.size() == 2 &&
                session.selectedLayerId() == original_layer &&
                session.renderedImage().pixelColor(12, 12).alpha() == 0 &&
                session.redo() && session.data().layers.size() == 3 &&
                session.selectedLayerId() == rectangle_layer &&
                session.renderedImage().pixelColor(12, 12).alpha() == 160,
            QStringLiteral("Shape layer creation was not one undoable edit."));

    const QString lower_layer = session.selectedLayerId();
    const QString upper_layer = session.addLayer();
    require(!upper_layer.isEmpty(), QStringLiteral("Could not add an upper shape layer."));
    image_editor::ImageShapeData ellipse;
    ellipse.kind = image_editor::ImageShapeKind::Ellipse;
    ellipse.start = QPointF(34, 4);
    ellipse.end = QPointF(56, 26);
    ellipse.stroke_color = Qt::yellow;
    ellipse.fill_color = QColor(20, 60, 240, 210);
    const QString ellipse_id = session.addShape(ellipse, &error);
    require(!ellipse_id.isEmpty(), error);
    QString ellipse_layer;
    require(session.findShape(ellipse_id, nullptr, &ellipse_layer) &&
                ellipse_layer != rectangle_layer &&
                session.data().layers.at(4).name == QStringLiteral("Shape 2") &&
                session.selectedLayerId() == ellipse_layer,
            QStringLiteral("The ellipse did not receive its own numbered layer."));
    image_editor::ImageShapeData line;
    line.kind = image_editor::ImageShapeKind::Line;
    line.start = QPointF(4, 35);
    line.end = QPointF(22, 35);
    line.stroke_color = QColor(220, 30, 180);
    line.fill_enabled = false;
    const QString line_id = session.addShape(line, &error);
    require(!line_id.isEmpty(), error);
    QString line_layer;
    require(session.findShape(line_id, nullptr, &line_layer) &&
                line_layer != rectangle_layer && line_layer != ellipse_layer &&
                session.data().layers.at(5).name == QStringLiteral("Shape 3") &&
                session.selectedLayerId() == line_layer,
            QStringLiteral("The line did not receive a distinct selected layer."));
    const QImage rendered = session.renderedImage();
    require(rendered.size() == QSize(64, 48) &&
                rendered.pixelColor(45, 15).blue() > 200 &&
                rendered.pixelColor(12, 35).red() > 180 &&
                rendered.pixelColor(12, 35).blue() > 140,
            QStringLiteral("The ellipse fill or line stroke did not appear in the composite."));
    require(rendered.pixelColor(3, 35).alpha() > 0 &&
                rendered.pixelColor(3, 35).alpha() < 255,
            QStringLiteral("Shape edges were not antialiased."));

    const auto placements = session.visibleShapes();
    require(placements.size() == 3 && placements.front().shape.id == line_id &&
                placements.at(1).shape.id == ellipse_id &&
                placements.back().shape.id == rectangle_id &&
                placements.front().layer_id == line_layer &&
                placements.at(1).layer_id == ellipse_layer &&
                placements.back().layer_id == rectangle_layer,
            QStringLiteral("Visible shapes were not enumerated in top-to-bottom order."));
    require(session.setLayerOpacity(line_layer, 0) &&
                session.visibleShapes().size() == 2 &&
                session.undo() && session.visibleShapes().size() == 3,
            QStringLiteral("Zero-opacity shapes remained available for selection."));
    require(session.setLayerVisible(ellipse_layer, false),
            QStringLiteral("Could not hide the ellipse layer."));
    const auto shapes_with_hidden_ellipse = session.visibleShapes();
    require(shapes_with_hidden_ellipse.size() == 2 &&
                std::none_of(shapes_with_hidden_ellipse.cbegin(),
                    shapes_with_hidden_ellipse.cend(),
                    [&ellipse_id](const image_editor::ImageShapePlacement& placement) {
                        return placement.shape.id == ellipse_id;
                    }),
            QStringLiteral("Hidden-layer shapes remained available for selection."));
    require(session.undo() && session.visibleShapes().size() == 3,
            QStringLiteral("Undo did not restore shape visibility for selection."));

    image_editor::ImageShapeData changed;
    require(session.findShape(rectangle_id, &changed),
            QStringLiteral("The stored rectangle could not be found."));
    changed.fill_color = QColor(240, 80, 20, 255);
    const bool shape_style_changed = session.updateShape(changed, &error);
    const QColor styled_pixel = session.renderedImage().pixelColor(12, 12);
    require(shape_style_changed && styled_pixel == changed.fill_color,
            QStringLiteral("A shape style change was not applied (changed=%1, color=%2, pixel=%3, error=%4).")
                .arg(shape_style_changed).arg(changed.fill_color.name(QColor::HexArgb),
                    styled_pixel.name(QColor::HexArgb), error));
    require(session.undo() && session.renderedImage().pixelColor(12, 12) == rectangle.fill_color &&
                session.redo() && session.renderedImage().pixelColor(12, 12) == changed.fill_color,
            QStringLiteral("Shape style Undo/Redo did not restore the previous fill."));

    require(session.deleteShape(line_id) && !session.findShape(line_id, nullptr) &&
                session.undo() && session.findShape(line_id, nullptr) &&
                session.redo() && !session.findShape(line_id, nullptr),
            QStringLiteral("Shape deletion was not one undoable edit."));

    const QString composite_path = root + QStringLiteral("/editable-shapes.png");
    require(session.undo(), QStringLiteral("Could not restore the line before export."));
    require(session.exportImage(composite_path, &error), error);
    const QImage composite_export(composite_path);
    require(composite_export.size() == QSize(64, 48) &&
                composite_export.pixelColor(12, 12) == changed.fill_color &&
                composite_export.pixelColor(45, 15).blue() > 200,
            QStringLiteral("Composite PNG export omitted editable shapes."));

    image_editor::ImageExportOptions selected_options;
    selected_options.scope = image_editor::ImageExportScope::SelectedLayer;
    require(session.selectLayer(ellipse_layer),
            QStringLiteral("Could not select the ellipse layer for Quick Export."));
    const QString selected_path = root + QStringLiteral("/selected-shapes.png");
    require(session.exportImage(selected_path, selected_options, &error), error);
    const QImage selected_export(selected_path);
    require(selected_export.size() == QSize(64, 48) &&
                selected_export.pixelColor(12, 12).alpha() == 0 &&
                selected_export.pixelColor(45, 15).blue() > 200,
            QStringLiteral("Selected-layer PNG export included the lower layer or omitted its shapes."));

    const QString document_path = root + QStringLiteral("/editable-shapes.cimg");
    require(session.saveDocument(document_path, &error), error);
    QFile document_file(document_path);
    require(document_file.open(QIODevice::ReadOnly),
            QStringLiteral("The version 13 shape document could not be read."));
    QJsonObject document_json = QJsonDocument::fromJson(document_file.readAll()).object();
    document_file.close();
    const auto saved_layers = document_json.value("layers").toArray();
    const auto saved_rectangle_layer = std::find_if(
        saved_layers.cbegin(), saved_layers.cend(), [&rectangle_layer](const QJsonValue& value) {
            return value.toObject().value("id").toString() == rectangle_layer;
        });
    require(document_json.value("version").toInt() == 13 &&
                saved_rectangle_layer != saved_layers.cend() &&
                (*saved_rectangle_layer).toObject().value("name").toString() == "Shape 1" &&
                (*saved_rectangle_layer).toObject().value("operations").toArray().size() == 1 &&
                (*saved_rectangle_layer).toObject().value("operations").toArray().at(0)
                    .toObject().value("kind").toString() == "shape",
            QStringLiteral("Shapes were not stored in individual v9 layer operation sequences."));
    image_editor::ImageDocumentSession reopened;
    require(reopened.openDocument(document_path, &error) &&
                reopened.data() == session.data() && reopened.renderedImage() == session.renderedImage(),
            QStringLiteral("Editable shapes did not round-trip through the v9 document."));

    QJsonObject version_five = document_json;
    version_five.insert("version", 5);
    auto old_layers = version_five.value("layers").toArray();
    for (qsizetype index = 1; index < old_layers.size(); ++index) {
        auto layer = old_layers.at(index).toObject();
        layer.insert("operations", QJsonArray{});
        old_layers.replace(index, layer);
    }
    version_five.insert("layers", old_layers);
    const QString v5_path = root + QStringLiteral("/shapes-v5-compatible.cimg");
    QFile v5_file(v5_path);
    require(v5_file.open(QIODevice::WriteOnly),
            QStringLiteral("Could not create the version 5 compatibility fixture."));
    v5_file.write(QJsonDocument(version_five).toJson());
    v5_file.close();
    image_editor::ImageDocumentSession v5_reopened;
    require(v5_reopened.openDocument(v5_path, &error) &&
                v5_reopened.visibleShapes().isEmpty(),
            QStringLiteral("A v5 document did not load with its pre-shape appearance."));
    require(v5_reopened.saveDocument(v5_path, &error), error);
    QFile migrated_v5_file(v5_path);
    require(migrated_v5_file.open(QIODevice::ReadOnly) &&
                QJsonDocument::fromJson(migrated_v5_file.readAll()).object()
                        .value("version").toInt() == 13,
            QStringLiteral("Saving a version 5 document did not migrate it to v13."));

    image_editor::ImageDocumentSession background_shape_session;
    require(background_shape_session.createCanvas(
                QSize(16, 16), QColor(0, 0, 0, 0), &error), error);
    const QString background_id = background_shape_session.data().layers.front().id;
    require(background_shape_session.selectLayer(background_id),
            QStringLiteral("Could not select Background before creating a shape."));
    image_editor::ImageShapeData background_shape;
    background_shape.start = QPointF(3, 3);
    background_shape.end = QPointF(12, 12);
    const QString background_shape_id = background_shape_session.addShape(background_shape, &error);
    QString background_shape_layer;
    require(!background_shape_id.isEmpty() &&
                background_shape_session.findShape(
                    background_shape_id, nullptr, &background_shape_layer) &&
                background_shape_session.data().layers.size() == 3 &&
                background_shape_session.data().layers.at(1).name == QStringLiteral("Shape 1") &&
                background_shape_session.data().layers.at(1).operations.size() == 1 &&
                background_shape_session.selectedLayerId() == background_shape_layer &&
                background_shape_session.selectedLayerIsEditable(),
            QStringLiteral("A shape drawn with Background selected was not inserted above it."));
    require(background_shape_session.undo() &&
                background_shape_session.data().layers.size() == 2 &&
                background_shape_session.selectedLayerId() == background_id &&
                background_shape_session.redo() &&
                background_shape_session.data().layers.size() == 3 &&
                background_shape_session.selectedLayerId() == background_shape_layer,
            QStringLiteral("Undo/Redo did not restore Background-anchored shape layer creation."));

    image_editor::ImageDocumentSession name_collision_session;
    require(name_collision_session.createCanvas(QSize(16, 16), Qt::transparent, &error), error);
    const QString named_anchor = name_collision_session.selectedLayerId();
    require(name_collision_session.renameLayer(named_anchor, QStringLiteral("shape 1"), &error),
            error);
    image_editor::ImageShapeData collision_shape;
    collision_shape.start = QPointF(2, 2);
    collision_shape.end = QPointF(8, 8);
    const QString collision_shape_id = name_collision_session.addShape(collision_shape, &error);
    QString collision_shape_layer;
    require(!collision_shape_id.isEmpty() &&
                name_collision_session.findShape(
                    collision_shape_id, nullptr, &collision_shape_layer) &&
                name_collision_session.data().layers.at(2).name == QStringLiteral("Shape 2") &&
                name_collision_session.selectedLayerId() == collision_shape_layer,
            QStringLiteral("Shape-layer naming collided with an existing user layer."));

    image_editor::ImageDocumentSession full_layer_session;
    require(full_layer_session.createCanvas(QSize(16, 16), Qt::transparent, &error), error);
    for (qsizetype count = full_layer_session.data().layers.size();
         count < image_editor::ImageDocumentStore::kMaximumLayers; ++count) {
        require(!full_layer_session.addLayer().isEmpty(),
                QStringLiteral("Could not fill the document to the layer limit."));
    }
    const auto document_at_layer_limit = full_layer_session.data();
    const QString selection_at_layer_limit = full_layer_session.selectedLayerId();
    const bool can_undo_at_layer_limit = full_layer_session.canUndo();
    const bool can_redo_at_layer_limit = full_layer_session.canRedo();
    image_editor::ImageShapeData limit_shape;
    limit_shape.start = QPointF(2, 2);
    limit_shape.end = QPointF(8, 8);
    require(full_layer_session.addShape(limit_shape, &error).isEmpty() && !error.isEmpty() &&
                full_layer_session.data() == document_at_layer_limit &&
                full_layer_session.selectedLayerId() == selection_at_layer_limit &&
                full_layer_session.canUndo() == can_undo_at_layer_limit &&
                full_layer_session.canRedo() == can_redo_at_layer_limit,
            QStringLiteral("The layer limit changed the document or selection."));

    image_editor::RecoveryStore recovery(root + QStringLiteral("/shape-recovery"));
    require(session.setLayerOpacity(upper_layer, 75),
            QStringLiteral("Could not dirty the document before shape recovery."));
    require(recovery.save(session, &error), error);
    const QString snapshot_path = recovery.pathFor(session);
    image_editor::ImageDocumentSession recovered;
    require(recovered.restoreRecovery(snapshot_path, &error) &&
                recovered.data() == session.data() && recovered.renderedImage() == session.renderedImage(),
            QStringLiteral("Recovery did not preserve version 13 shape operations."));
    QFile recovery_file(snapshot_path);
    require(recovery_file.open(QIODevice::ReadOnly),
            QStringLiteral("The shape recovery wrapper could not be read."));
    const QJsonObject recovery_json = QJsonDocument::fromJson(recovery_file.readAll()).object();
    require(recovery_json.value("version").toInt() == 1 &&
                recovery_json.value("document").toObject().value("version").toInt() == 13,
            QStringLiteral("Shape recovery changed the recovery wrapper version."));

    image_editor::ImageDocumentSession transformed;
    require(transformed.createCanvas(QSize(64, 48), QColor(0, 0, 0, 0), &error), error);
    image_editor::ImageShapeData transform_shape;
    transform_shape.start = QPointF(5, 5);
    transform_shape.end = QPointF(15, 15);
    transform_shape.fill_color = Qt::red;
    const QString transformed_id = transformed.addShape(transform_shape, &error);
    require(!transformed_id.isEmpty(), error);
    transformed.flipHorizontal();
    const auto visual_shapes = transformed.visibleShapes();
    require(visual_shapes.size() == 1 && visual_shapes.front().shape.start.x() == 58,
            QStringLiteral("Shape selection geometry did not follow a later layer flip."));
    auto moved_visual = visual_shapes.front().shape;
    moved_visual.start.rx() -= 2;
    moved_visual.end.rx() -= 2;
    require(transformed.updateShapeRendered(moved_visual, &error) &&
                transformed.visibleShapes().front().shape.start.x() == 56 &&
                transformed.undo() && transformed.visibleShapes().front().shape.start.x() == 58 &&
                transformed.redo() && transformed.visibleShapes().front().shape.start.x() == 56,
            QStringLiteral("Shape movement through a later layer transform lost Undo/Redo geometry."));

    image_editor::ImageShapeData invalid = rectangle;
    invalid.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    invalid.end.setX(invalid.start.x());
    require(!image_editor::ImageDocumentStore::isValidShape(invalid, QSize(64, 48), &error) &&
                !error.isEmpty(),
            QStringLiteral("Degenerate shape bounds were accepted."));
    invalid = line;
    invalid.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    invalid.fill_enabled = true;
    require(!image_editor::ImageDocumentStore::isValidShape(invalid, QSize(64, 48), &error),
            QStringLiteral("A line with fill enabled was accepted."));
    invalid = rectangle;
    invalid.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    invalid.stroke_width = image_editor::ImageDocumentStore::kMaximumShapeStrokeWidth + 1;
    require(!image_editor::ImageDocumentStore::isValidShape(invalid, QSize(64, 48), &error),
            QStringLiteral("An unsupported shape stroke width was accepted."));

    image_editor::ImageDocumentSession ordered;
    require(ordered.createCanvas(QSize(16, 16), QColor(0, 0, 0, 0), &error), error);
    require(ordered.applyPaintStroke({QPointF(8, 8)}, Qt::red, 5, &error), error);
    image_editor::ImageShapeData cover;
    cover.start = QPointF(5, 5);
    cover.end = QPointF(11, 11);
    cover.stroke_enabled = false;
    cover.fill_color = Qt::blue;
    require(!ordered.addShape(cover, &error).isEmpty(), error);
    require(ordered.renderedImage().pixelColor(8, 8) == QColor(Qt::blue),
            QStringLiteral("A later shape did not render above an earlier paint stroke."));
    require(ordered.applyEraseStroke({QPointF(8, 8)}, 5, &error), error);
    require(ordered.renderedImage().pixelColor(8, 8).red() > 200,
            QStringLiteral("Erasing the shape layer did not reveal the lower paint layer."));
    image_editor::ImageShapeData top_mark;
    top_mark.start = QPointF(7, 7);
    top_mark.end = QPointF(9, 9);
    top_mark.stroke_enabled = false;
    top_mark.fill_color = Qt::yellow;
    require(!ordered.addShape(top_mark, &error).isEmpty(), error);
    require(ordered.renderedImage().pixelColor(8, 8) == QColor(Qt::yellow),
            QStringLiteral("A shape after an eraser was not preserved in operation order."));
    require(!session.selectedLayerId().isEmpty() && lower_layer != upper_layer,
            QStringLiteral("The shape test layer setup became invalid."));
}

void testEditableTextRenderingPersistenceAndHistory(const QString& root) {
    image_editor::ImageDocumentSession session;
    QString error;
    require(session.createCanvas(QSize(180, 400), QColor(0, 0, 0, 0), &error), error);

    const image_editor::ImageTextData default_text;
    require(default_text.font_family == QStringLiteral("Sans Serif") &&
                default_text.font_pixel_size == 48 && default_text.color == QColor(Qt::black) &&
                default_text.alignment == image_editor::ImageTextAlignment::Left,
            QStringLiteral("The default text style does not match the approved defaults."));

    image_editor::ImageTextData text;
    text.content = QStringLiteral("Editable text wraps automatically");
    text.font_family = QStringLiteral("Sans Serif");
    text.font_pixel_size = 18;
    text.color = QColor(12, 24, 220, 255);
    text.alignment = image_editor::ImageTextAlignment::Left;
    text.position = QPointF(8, 6);
    text.box_width = 60;
    const QString text_id = session.addText(text, &error);
    require(!text_id.isEmpty(), error);
    text.id = text_id;
    QString text_layer;
    image_editor::ImageTextData stored;
    require(session.findText(text_id, &stored, &text_layer) && stored == text &&
                session.data().layers.size() == 3 &&
                session.data().layers.back().name == QStringLiteral("Text 1") &&
                session.data().layers.back().operations.size() == 1 &&
                session.selectedLayerId() == text_layer,
            QStringLiteral("New text did not persist in its own selected Text 1 layer."));
    require(session.undo() && session.data().layers.size() == 2 &&
                session.visibleObjects().isEmpty() && session.redo() &&
                session.findText(text_id, &stored, &text_layer) && stored == text &&
                session.selectedLayerId() == text_layer,
            QStringLiteral("Creating a text layer was not reversible as one edit."));

    const QRectF wrapped_bounds = image_editor::imageTextBounds(text);
    auto wide_text = text;
    wide_text.box_width = 160;
    const QRectF wide_bounds = image_editor::imageTextBounds(wide_text);
    require(wrapped_bounds.height() > wide_bounds.height() &&
                wrapped_bounds.height() >= text.font_pixel_size * 2,
            QStringLiteral("Text wrapping did not grow its layout height."));
    const auto inkBounds = [](const QImage& image) {
        QRect bounds;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (image.pixelColor(x, y).alpha() == 0) continue;
                bounds = bounds.isNull() ? QRect(x, y, 1, 1) : bounds.united(QRect(x, y, 1, 1));
            }
        }
        return bounds;
    };
    const auto renderAlignment = [&](image_editor::ImageTextAlignment alignment) {
        image_editor::ImageDocumentSession aligned;
        QString aligned_error;
        if (!aligned.createCanvas(QSize(150, 60), Qt::transparent, &aligned_error)) return QImage{};
        image_editor::ImageTextData sample;
        sample.content = QStringLiteral("I");
        sample.font_pixel_size = 20;
        sample.color = Qt::black;
        sample.position = QPointF(5, 5);
        sample.box_width = 130;
        sample.alignment = alignment;
        if (aligned.addText(sample, &aligned_error).isEmpty()) return QImage{};
        return aligned.renderedImage();
    };
    const QRect left_ink = inkBounds(renderAlignment(image_editor::ImageTextAlignment::Left));
    const QRect center_ink = inkBounds(renderAlignment(image_editor::ImageTextAlignment::Center));
    const QRect right_ink = inkBounds(renderAlignment(image_editor::ImageTextAlignment::Right));
    require(!left_ink.isEmpty() && !center_ink.isEmpty() && !right_ink.isEmpty() &&
                left_ink.center().x() < center_ink.center().x() &&
                center_ink.center().x() < right_ink.center().x(),
            QStringLiteral("Left, center, and right text alignment did not affect rendering."));

    const QImage rendered = session.renderedImage();
    require(!rendered.isNull() && inkBounds(rendered).width() > 0 &&
                session.visibleObjects().size() == 1 &&
                session.visibleObjects().front().operation.kind ==
                    image_editor::OperationKind::Text &&
                !session.renderedLayerThumbnails(QSize(64, 64)).value(text_layer).isNull(),
            QStringLiteral("Text was missing from the composite, selection model, or layer thumbnail."));

    require(session.updateText(stored, &error) == false && error.isEmpty(),
            QStringLiteral("An unchanged text operation unexpectedly created an edit."));
    stored.content += QStringLiteral("\nSecond line");
    stored.font_family = QStringLiteral("CreativeSuiteMissingFontForFallbackTest");
    stored.color = QColor(200, 20, 30, 255);
    stored.alignment = image_editor::ImageTextAlignment::Center;
    require(session.updateText(stored, &error) && session.findText(text_id, &text) &&
                text == stored && !inkBounds(session.renderedImage()).isEmpty() &&
                session.undo() && session.findText(text_id, &text) &&
                text.content == QStringLiteral("Editable text wraps automatically") &&
                session.redo() && session.findText(text_id, &text) && text == stored,
            QStringLiteral("Text content, fallback font, and formatting did not round-trip through Undo/Redo."));

    auto placements = session.visibleObjects();
    require(placements.size() == 1, QStringLiteral("The editable text placement disappeared."));
    auto resized = placements.front();
    const int original_font_size = resized.operation.text.font_pixel_size;
    resized.operation.text.box_width = 80;
    resized.operation.text.position.setX(12);
    require(session.updateObjectsRendered({resized}, &error) &&
                session.visibleObjects().front().operation.text.box_width == 80 &&
                session.visibleObjects().front().operation.text.position.x() == 12 &&
                session.visibleObjects().front().operation.text.font_pixel_size == original_font_size &&
                session.undo() && session.visibleObjects().front().operation.text.box_width == 60 &&
                session.redo() && session.visibleObjects().front().operation.text.box_width == 80,
            QStringLiteral("Resizing text width changed its font or failed Undo/Redo."));

    const QString export_path = root + QStringLiteral("/editable-text.png");
    require(session.exportImage(export_path, &error), error);
    require(!inkBounds(QImage(export_path)).isEmpty(),
            QStringLiteral("Flattened export omitted editable text."));
    image_editor::ImageExportOptions selected_options;
    selected_options.scope = image_editor::ImageExportScope::SelectedLayer;
    require(session.selectedLayerId() == text_layer || session.selectLayer(text_layer),
            QStringLiteral("Could not select the text layer."));
    const QString quick_export_path = root + QStringLiteral("/selected-text.png");
    require(session.exportImage(quick_export_path, selected_options, &error), error);
    require(!inkBounds(QImage(quick_export_path)).isEmpty(),
            QStringLiteral("Selected-layer Quick Export omitted editable text."));

    const QString document_path = root + QStringLiteral("/editable-text.cimg");
    require(session.saveDocument(document_path, &error), error);
    QFile document_file(document_path);
    require(document_file.open(QIODevice::ReadOnly),
            QStringLiteral("Could not read the v9 text document."));
    QJsonObject document_json = QJsonDocument::fromJson(document_file.readAll()).object();
    document_file.close();
    const auto saved_text_layers = document_json.value("layers").toArray();
    const auto text_layer_json = std::find_if(
        saved_text_layers.cbegin(), saved_text_layers.cend(), [&text_layer](const QJsonValue& value) {
            return value.toObject().value("id").toString() == text_layer;
        });
    require(document_json.value("version").toInt() == 13 &&
                text_layer_json != saved_text_layers.cend() &&
                text_layer_json->toObject().value("operations").toArray().at(0)
                    .toObject().value("kind").toString() == "text",
            QStringLiteral("Editable text was not serialized as a version 13 operation."));

    QJsonObject invalid_text_document = document_json;
    auto invalid_text_layers = invalid_text_document.value("layers").toArray();
    bool corrupted_text = false;
    for (qsizetype index = 0; index < invalid_text_layers.size(); ++index) {
        auto layer = invalid_text_layers.at(index).toObject();
        if (layer.value("id").toString() != text_layer) continue;
        auto operations = layer.value("operations").toArray();
        auto text_operation = operations.at(0).toObject();
        text_operation.insert("font_pixel_size",
                              image_editor::ImageDocumentStore::kMaximumTextFontPixelSize + 1);
        operations.replace(0, text_operation);
        layer.insert("operations", operations);
        invalid_text_layers.replace(index, layer);
        corrupted_text = true;
        break;
    }
    require(corrupted_text, QStringLiteral("Could not create an invalid text document fixture."));
    invalid_text_document.insert("layers", invalid_text_layers);
    const QString invalid_text_path = root + QStringLiteral("/invalid-editable-text.cimg");
    QFile invalid_text_file(invalid_text_path);
    require(invalid_text_file.open(QIODevice::WriteOnly),
            QStringLiteral("Could not write an invalid text document fixture."));
    invalid_text_file.write(QJsonDocument(invalid_text_document).toJson());
    invalid_text_file.close();
    image_editor::ImageDocumentData rejected_text_document;
    require(!image_editor::ImageDocumentStore::loadDocument(
                invalid_text_path, &rejected_text_document, &error) && !error.isEmpty(),
            QStringLiteral("A v9 document with an out-of-range text size was accepted."));

    image_editor::ImageDocumentSession reopened;
    require(reopened.openDocument(document_path, &error) && reopened.data() == session.data() &&
                reopened.renderedImage() == session.renderedImage(),
            QStringLiteral("Text formatting or rendering changed after reopening v9."));

    image_editor::RecoveryStore recovery(root + QStringLiteral("/text-recovery"));
    require(session.setLayerOpacity(text_layer, 85),
            QStringLiteral("Could not create pending changes before the text recovery snapshot."));
    require(recovery.save(session, &error), error);
    const QString recovery_path = recovery.pathFor(session);
    image_editor::ImageDocumentSession restored;
    require(restored.restoreRecovery(recovery_path, &error) && restored.data() == session.data() &&
                restored.renderedImage() == session.renderedImage(),
            QStringLiteral("Recovery did not accept and restore an inner v9 text document."));
    QFile recovery_file(recovery_path);
    require(recovery_file.open(QIODevice::ReadOnly),
            QStringLiteral("Could not read the v9 recovery wrapper."));
    const QJsonObject recovery_json = QJsonDocument::fromJson(recovery_file.readAll()).object();
    require(recovery_json.value("version").toInt() == 1 &&
                recovery_json.value("document").toObject().value("version").toInt() == 13,
            QStringLiteral("Text recovery changed the wrapper version or omitted the v13 payload."));

    image_editor::ImageDocumentSession legacy;
    require(legacy.createCanvas(QSize(24, 24), Qt::transparent, &error), error);
    const QString legacy_path = root + QStringLiteral("/legacy-v8.cimg");
    require(legacy.saveDocument(legacy_path, &error), error);
    QFile legacy_file(legacy_path);
    require(legacy_file.open(QIODevice::ReadOnly),
            QStringLiteral("Could not read a no-text compatibility document."));
    QJsonObject legacy_json = QJsonDocument::fromJson(legacy_file.readAll()).object();
    legacy_file.close();
    legacy_json.insert("version", 8);
    require(legacy_file.open(QIODevice::WriteOnly | QIODevice::Truncate),
            QStringLiteral("Could not write the v8 compatibility fixture."));
    legacy_file.write(QJsonDocument(legacy_json).toJson());
    legacy_file.close();
    image_editor::ImageDocumentSession migrated;
    require(migrated.openDocument(legacy_path, &error) && migrated.saveDocument({}, &error), error);
    require(legacy_file.open(QIODevice::ReadOnly) &&
                QJsonDocument::fromJson(legacy_file.readAll()).object().value("version").toInt() == 13,
            QStringLiteral("Saving a version 8 document did not migrate its envelope to v13."));

    image_editor::ImageDocumentSession grouped_text_session;
    require(grouped_text_session.createCanvas(QSize(180, 90), Qt::transparent, &error), error);
    image_editor::ImageTextData grouped_text;
    grouped_text.content = QStringLiteral("Grouped text");
    grouped_text.font_pixel_size = 20;
    grouped_text.position = QPointF(8, 8);
    grouped_text.box_width = 160;
    const QString grouped_text_id = grouped_text_session.addText(grouped_text, &error);
    QString grouped_text_layer;
    require(!grouped_text_id.isEmpty() && grouped_text_session.findText(
                grouped_text_id, nullptr, &grouped_text_layer), error);
    const QString sibling_layer = grouped_text_session.addLayer();
    const QString group_id = grouped_text_session.groupLayers(
        {grouped_text_layer, sibling_layer}, &error);
    require(!group_id.isEmpty(), error);
    const QImage full_opacity_group = grouped_text_session.renderedImage();
    const QRect group_ink = inkBounds(full_opacity_group);
    require(!group_ink.isEmpty() && grouped_text_session.setGroupOpacity(group_id, 40),
            QStringLiteral("Text could not be rendered and grouped under opacity control."));
    QPoint group_sample;
    bool found_group_sample = false;
    for (int y = 0; y < full_opacity_group.height() && !found_group_sample; ++y) {
        for (int x = 0; x < full_opacity_group.width(); ++x) {
            if (full_opacity_group.pixelColor(x, y).alpha() == 0) continue;
            group_sample = QPoint(x, y);
            found_group_sample = true;
            break;
        }
    }
    require(found_group_sample,
            QStringLiteral("Could not find a visible sample pixel in the text group."));
    require(grouped_text_session.renderedImage().pixelColor(group_sample).alpha() <
                full_opacity_group.pixelColor(group_sample).alpha() &&
                grouped_text_session.renderedLayerThumbnails(QSize(64, 64)).contains(group_id),
            QStringLiteral("Group opacity or thumbnails omitted the text layer."));
    require(grouped_text_session.selectedGroupId() == group_id ||
                grouped_text_session.selectGroup(group_id),
            QStringLiteral("Could not select the text-containing group."));
    const QString grouped_export_path = root + QStringLiteral("/grouped-text.png");
    image_editor::ImageExportOptions group_options;
    group_options.scope = image_editor::ImageExportScope::SelectedGroup;
    require(grouped_text_session.exportImage(grouped_export_path, group_options, &error), error);
    require(!inkBounds(QImage(grouped_export_path)).isEmpty(),
            QStringLiteral("Selected-group Quick Export omitted editable text."));

    image_editor::ImageTextData invalid = stored;
    invalid.font_pixel_size = image_editor::ImageDocumentStore::kMaximumTextFontPixelSize + 1;
    require(!image_editor::ImageDocumentStore::isValidText(invalid, QSize(180, 400), &error) &&
                !error.isEmpty(),
            QStringLiteral("An out-of-range text font size was accepted."));
    invalid = stored;
    invalid.content = QString(image_editor::ImageDocumentStore::kMaximumTextLength + 1, QLatin1Char('x'));
    require(!image_editor::ImageDocumentStore::isValidText(invalid, QSize(180, 400), &error),
            QStringLiteral("Text larger than the safe content limit was accepted."));

    placements = session.visibleObjects();
    require(session.deleteObjects({text_id}) && session.visibleObjects().isEmpty() &&
                session.undo() && session.visibleObjects().size() == 1 &&
                session.redo() && session.visibleObjects().isEmpty(),
            QStringLiteral("Deleting text was not a reversible object edit."));
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

void testGeneralObjectOperations(const QString& root) {
    image_editor::ImageDocumentSession session;
    QString error;
    require(session.createCanvas(QSize(64, 48), QColor(0, 0, 0, 0), &error), error);
    require(session.applyPaintStroke({QPointF(5, 12), QPointF(50, 12)}, Qt::red, 4, &error), error);
    require(session.applyEraseStroke({QPointF(10, 12), QPointF(15, 12)}, 4, &error), error);

    auto objects = session.visibleObjects();
    require(objects.size() == 2 &&
                objects.at(0).operation.kind == image_editor::OperationKind::EraseStroke &&
                objects.at(1).operation.kind == image_editor::OperationKind::PaintStroke &&
                !objects.at(0).operation.erase_stroke.id.isEmpty() &&
                !objects.at(1).operation.paint_stroke.id.isEmpty() &&
                objects.at(0).operation.erase_stroke.id != objects.at(1).operation.paint_stroke.id,
            QStringLiteral("Paint and erase strokes were not assigned distinct selectable IDs."));
    const QString erase_id = objects.at(0).operation.erase_stroke.id;
    const QString paint_id = objects.at(1).operation.paint_stroke.id;
    require(session.renderedImage().pixelColor(12, 12).alpha() == 0 &&
                session.renderedImage().pixelColor(30, 12).red() > 200,
            QStringLiteral("The test paint and erase operations did not render in order."));

    auto moved_eraser = objects.front();
    for (auto& point : moved_eraser.operation.erase_stroke.points) point.rx() += 18;
    require(session.updateObjectsRendered({moved_eraser}, &error), error);
    require(session.renderedImage().pixelColor(12, 12).red() > 200 &&
                session.renderedImage().pixelColor(30, 12).alpha() == 0,
            QStringLiteral("Moving an eraser did not move the erased region on its layer."));
    require(session.undo() && session.renderedImage().pixelColor(12, 12).alpha() == 0 &&
                session.redo() && session.renderedImage().pixelColor(12, 12).red() > 200,
            QStringLiteral("Moving an eraser was not a single undoable edit."));

    objects = session.visibleObjects();
    auto moved_paint = objects.back();
    require(moved_paint.operation.kind == image_editor::OperationKind::PaintStroke,
            QStringLiteral("The paint operation was not present in object selection."));
    for (auto& point : moved_paint.operation.paint_stroke.points) point.ry() += 10;
    moved_paint.operation.paint_stroke.diameter = 8;
    require(session.updateObjectsRendered({moved_paint}, &error), error);
    require(session.renderedImage().pixelColor(30, 22).red() > 200 &&
                session.renderedImage().pixelColor(30, 12).alpha() == 0,
            QStringLiteral("Moving and resizing a paint stroke did not update its rendered geometry."));
    require(session.undo() && session.redo(),
            QStringLiteral("Paint stroke transforms were not undoable and redoable."));

    image_editor::ImageShapeData first_shape;
    first_shape.start = QPointF(5, 30);
    first_shape.end = QPointF(16, 41);
    first_shape.fill_color = Qt::blue;
    const QString first_shape_id = session.addShape(first_shape, &error);
    require(!first_shape_id.isEmpty(), error);
    QString first_shape_layer;
    require(session.findShape(first_shape_id, nullptr, &first_shape_layer),
            QStringLiteral("The first object-selection shape layer was not created."));
    image_editor::ImageShapeData second_shape = first_shape;
    second_shape.start = QPointF(35, 30);
    second_shape.end = QPointF(46, 41);
    const QString second_shape_id = session.addShape(second_shape, &error);
    require(!second_shape_id.isEmpty(), error);
    QString second_shape_layer;
    require(session.findShape(second_shape_id, nullptr, &second_shape_layer) &&
                second_shape_layer != first_shape_layer,
            QStringLiteral("The second object-selection shape did not receive its own layer."));
    image_editor::ImageShapeData line_shape;
    line_shape.kind = image_editor::ImageShapeKind::Line;
    line_shape.start = QPointF(20, 30);
    line_shape.end = QPointF(30, 40);
    line_shape.fill_enabled = false;
    const QString line_shape_id = session.addShape(line_shape, &error);
    require(!line_shape_id.isEmpty(), error);
    QString line_shape_layer;
    require(session.findShape(line_shape_id, nullptr, &line_shape_layer) &&
                line_shape_layer != first_shape_layer &&
                line_shape_layer != second_shape_layer,
            QStringLiteral("The line object-selection shape did not receive its own layer."));
    image_editor::ImageShapeData style = first_shape;
    style.stroke_color = Qt::green;
    style.fill_color = QColor(20, 220, 80);
    style.stroke_width = 5;
    require(session.updateShapeStyles(
                {first_shape_id, second_shape_id, line_shape_id}, style, &error), error);
    const auto styled_objects = session.visibleObjects();
    const auto styled_line = std::find_if(styled_objects.cbegin(), styled_objects.cend(),
        [&line_shape_id](const image_editor::ImageObjectPlacement& placement) {
            return placement.operation.kind == image_editor::OperationKind::Shape &&
                placement.operation.shape.id == line_shape_id;
        });
    require(session.renderedImage().pixelColor(10, 35) == style.fill_color &&
                session.renderedImage().pixelColor(40, 35) == style.fill_color &&
                styled_line != styled_objects.cend() &&
                styled_line->operation.shape.stroke_color == style.stroke_color &&
                styled_line->operation.shape.stroke_width == style.stroke_width &&
                styled_line->operation.shape.stroke_enabled &&
                !styled_line->operation.shape.fill_enabled,
            QStringLiteral("Shape style controls did not update every selected shape (rect=%1 ellipse=%2 line=%3).")
                .arg(session.renderedImage().pixelColor(10, 35).name(QColor::HexArgb),
                     session.renderedImage().pixelColor(40, 35).name(QColor::HexArgb),
                     styled_line == styled_objects.cend()
                        ? QStringLiteral("missing")
                        : QStringLiteral("%1/%2/%3/%4")
                            .arg(styled_line->operation.shape.stroke_color.name(QColor::HexArgb))
                            .arg(styled_line->operation.shape.stroke_width)
                            .arg(styled_line->operation.shape.stroke_enabled)
                            .arg(styled_line->operation.shape.fill_enabled)));
    require(session.undo() && session.renderedImage().pixelColor(10, 35) == first_shape.fill_color &&
                session.redo(),
            QStringLiteral("Multi-shape style editing did not create one Undo/Redo entry."));

    require(session.setLayerVisible(second_shape_layer, false),
            QStringLiteral("Could not hide the second object layer."));
    const auto hidden_objects = session.visibleObjects();
    require(std::none_of(hidden_objects.cbegin(), hidden_objects.cend(),
                [&second_shape_id](const image_editor::ImageObjectPlacement& placement) {
                    return placement.operation.kind == image_editor::OperationKind::Shape &&
                        placement.operation.shape.id == second_shape_id;
                }),
            QStringLiteral("Objects from a hidden layer remained selectable."));
    require(session.undo(), QStringLiteral("Could not restore the visible object layer."));

    require(session.deleteObjects(
                {paint_id, erase_id, first_shape_id, second_shape_id, line_shape_id}) &&
                session.visibleObjects().isEmpty() && session.undo() &&
                session.visibleObjects().size() == 5 && session.redo() &&
                session.visibleObjects().isEmpty(),
            QStringLiteral("Deleting a mixed multi-selection was not one undoable edit."));
    require(session.undo(), QStringLiteral("Could not restore objects before v7 persistence."));

    const QString v8_path = root + QStringLiteral("/general-objects-v8.cimg");
    require(session.saveDocument(v8_path, &error), error);
    QFile v8_file(v8_path);
    require(v8_file.open(QIODevice::ReadOnly), QStringLiteral("Could not read the v8 object document."));
    const QJsonObject v8 = QJsonDocument::fromJson(v8_file.readAll()).object();
    v8_file.close();
    require(v8.value("version").toInt() == 13,
            QStringLiteral("The document did not migrate to cimg v10."));
    QJsonObject v7 = v8;
    v7.insert("version", 7);
    const QString v7_path = root + QStringLiteral("/general-objects-v7.cimg");
    QFile v7_file(v7_path);
    require(v7_file.open(QIODevice::WriteOnly), QStringLiteral("Could not create a v7 fixture."));
    v7_file.write(QJsonDocument(v7).toJson());
    v7_file.close();
    image_editor::ImageDocumentSession migrated_v7;
    require(migrated_v7.openDocument(v7_path, &error) &&
                migrated_v7.renderedImage() == session.renderedImage(), error);
    require(migrated_v7.saveDocument(v7_path, &error), error);
    QFile migrated_v7_file(v7_path);
    require(migrated_v7_file.open(QIODevice::ReadOnly) &&
                QJsonDocument::fromJson(migrated_v7_file.readAll()).object()
                        .value("version").toInt() == 13,
            QStringLiteral("Saving a version 7 document did not migrate it to v13."));
    const auto layer_array = v7.value("layers").toArray();
    const auto operation_array = layer_array.at(1).toObject().value("operations").toArray();
    bool paint_id_persisted = false;
    bool erase_id_persisted = false;
    for (const auto& value : operation_array) {
        const auto object = value.toObject();
        if (object.value("kind") == "paint_stroke") {
            paint_id_persisted = !object.value("id").toString().isEmpty();
        } else if (object.value("kind") == "erase_stroke") {
            erase_id_persisted = !object.value("id").toString().isEmpty();
        }
    }
    require(paint_id_persisted && erase_id_persisted,
            QStringLiteral("Cimg v7 compatibility did not preserve paint and eraser object IDs."));

    v7.insert("version", 6);
    auto migrated_layers = v7.value("layers").toArray();
    for (qsizetype index = 1; index < migrated_layers.size(); ++index) {
        auto layer = migrated_layers.at(index).toObject();
        auto operations = layer.value("operations").toArray();
        for (qsizetype operation_index = 0; operation_index < operations.size(); ++operation_index) {
            auto operation = operations.at(operation_index).toObject();
            if (operation.value("kind") == "paint_stroke" ||
                operation.value("kind") == "erase_stroke") operation.remove("id");
            operations.replace(operation_index, operation);
        }
        layer.insert("operations", operations);
        migrated_layers.replace(index, layer);
    }
    v7.insert("layers", migrated_layers);
    const QString v6_path = root + QStringLiteral("/general-objects-v6.cimg");
    QFile v6_file(v6_path);
    require(v6_file.open(QIODevice::WriteOnly), QStringLiteral("Could not create a v6 fixture."));
    v6_file.write(QJsonDocument(v7).toJson());
    v6_file.close();
    image_editor::ImageDocumentSession migrated;
    require(migrated.openDocument(v6_path, &error) &&
                migrated.renderedImage() == session.renderedImage(), error);
    const auto migrated_objects = migrated.visibleObjects();
    require(migrated_objects.size() == 5 &&
                std::all_of(migrated_objects.cbegin(), migrated_objects.cend(),
                    [](const image_editor::ImageObjectPlacement& placement) {
                        if (placement.operation.kind == image_editor::OperationKind::PaintStroke)
                            return !placement.operation.paint_stroke.id.isEmpty();
                        if (placement.operation.kind == image_editor::OperationKind::EraseStroke)
                            return !placement.operation.erase_stroke.id.isEmpty();
                        return placement.operation.kind == image_editor::OperationKind::Shape &&
                            !placement.operation.shape.id.isEmpty();
                    }),
            QStringLiteral("Loading v6 did not assign stable IDs to legacy stroke operations."));
    require(migrated.saveDocument(v6_path, &error), error);
    QFile resaved_v6(v6_path);
    require(resaved_v6.open(QIODevice::ReadOnly) &&
                QJsonDocument::fromJson(resaved_v6.readAll()).object().value("version").toInt() == 13,
                QStringLiteral("Saving a v6 document did not upgrade it to v13."));
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

    image_editor::ImageExportOptions custom_options;
    custom_options.jpeg_quality = 95;
    custom_options.jpeg_background = QColor(35, 120, 210);
    const QString custom_matte_path = root + QStringLiteral("/custom-matte.jpg");
    require(session.exportImage(custom_matte_path, custom_options, &error), error);
    const QImage custom_matte(custom_matte_path);
    const QColor matte_pixel = custom_matte.pixelColor(28, 28);
    require(!custom_matte.isNull() && std::abs(matte_pixel.red() - 35) < 10 &&
                std::abs(matte_pixel.green() - 120) < 10 &&
                std::abs(matte_pixel.blue() - 210) < 10,
            QStringLiteral("JPEG transparency was not flattened over the selected color."));

    custom_options.jpeg_quality = 100;
    const QString high_quality_path = root + QStringLiteral("/custom-matte-high.jpg");
    require(session.exportImage(high_quality_path, custom_options, &error), error);
    require(!QImage(high_quality_path).isNull(),
            QStringLiteral("JPEG quality 100 did not produce a decodable image."));

    custom_options.jpeg_quality = 0;
    const QString low_quality_path = root + QStringLiteral("/custom-matte-low-quality.jpg");
    require(session.exportImage(low_quality_path, custom_options, &error), error);
    require(!QImage(low_quality_path).isNull(),
            QStringLiteral("JPEG quality 0 did not produce a decodable image."));

    custom_options.jpeg_quality = -1;
    require(!session.exportImage(root + QStringLiteral("/invalid-quality.jpg"),
                                 custom_options, &error) && !error.isEmpty(),
            QStringLiteral("An out-of-range JPEG quality was accepted."));
    custom_options.jpeg_quality = 95;
    custom_options.jpeg_background = QColor(35, 120, 210, 128);
    require(!session.exportImage(root + QStringLiteral("/translucent-matte.jpg"),
                                 custom_options, &error) && !error.isEmpty(),
            QStringLiteral("A translucent JPEG background color was accepted."));

    auto snapshot = session.exportSnapshot();
    const QVector<QPointF> later_stroke{QPointF(20.0, 20.0)};
    require(session.applyPaintStroke(later_stroke, QColor(20, 80, 240), 6, &error), error);
    const QString snapshot_path = root + QStringLiteral("/captured-snapshot.png");
    const auto snapshot_result = image_editor::exportImageSnapshot(
        snapshot, snapshot_path);
    const QImage snapshot_image(snapshot_path);
    require(snapshot_result.status == image_editor::ImageExportStatus::Succeeded &&
                snapshot_image.pixelColor(20, 20).alpha() == 0 &&
                session.renderedImage().pixelColor(20, 20).alpha() > 0,
            QStringLiteral("An export snapshot changed when the live document was edited."));

    std::atomic_bool cancelled{false};
    const QString cancelled_render_path = root + QStringLiteral("/cancelled-render.png");
    const auto render_cancel = image_editor::exportImageSnapshot(
        snapshot, cancelled_render_path, {}, &cancelled,
        [&cancelled](image_editor::ImageExportPhase phase) {
            if (phase == image_editor::ImageExportPhase::Rendering) {
                cancelled.store(true, std::memory_order_relaxed);
            }
        });
    require(render_cancel.status == image_editor::ImageExportStatus::Cancelled &&
                !QFileInfo::exists(cancelled_render_path),
            QStringLiteral("Cancellation during rendering created an output file."));

    const QString preserved_path = root + QStringLiteral("/cancelled-finalize.jpg");
    QFile previous_output(preserved_path);
    require(previous_output.open(QIODevice::WriteOnly),
            QStringLiteral("Could not create the pre-existing export destination."));
    const QByteArray previous_bytes = QByteArrayLiteral("previous destination contents");
    require(previous_output.write(previous_bytes) == previous_bytes.size(),
            QStringLiteral("Could not seed the pre-existing export destination."));
    previous_output.close();
    cancelled.store(false, std::memory_order_relaxed);
    const auto finalize_cancel = image_editor::exportImageSnapshot(
        snapshot, preserved_path, {}, &cancelled,
        [&cancelled](image_editor::ImageExportPhase phase) {
            if (phase == image_editor::ImageExportPhase::Finalizing) {
                cancelled.store(true, std::memory_order_relaxed);
            }
        });
    QFile preserved_output(preserved_path);
    require(finalize_cancel.status == image_editor::ImageExportStatus::Cancelled &&
                preserved_output.open(QIODevice::ReadOnly) &&
                preserved_output.readAll() == previous_bytes,
            QStringLiteral("Cancellation before commit replaced an existing destination."));

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

void testImageLayerStackEditor() {
    using namespace image_editor;

    ImageDocumentData document;
    document.base_kind = ImageBaseKind::Canvas;
    document.canvas_size = QSize(12, 8);
    ImageLayerData background;
    background.id = QStringLiteral("background");
    background.name = QStringLiteral("Background");
    background.background = true;
    ImageLayerData first;
    first.id = QStringLiteral("first");
    first.name = QStringLiteral("First");
    ImageLayerData second;
    second.id = QStringLiteral("second");
    second.name = QStringLiteral("Second");
    ImageLayerData third;
    third.id = QStringLiteral("third");
    third.name = QStringLiteral("Third");
    document.layers = {background, first, second, third};
    document.root_stack = {{background.id, false}, {first.id, false},
                           {second.id, false}, {third.id, false}};
    const ImageDocumentData original = document;
    require(ImageLayerStackEditor::itemCount(document) == 4,
            QStringLiteral("Stack item count omitted layers."));

    ImageDocumentData unordered = document;
    std::reverse(unordered.layers.begin(), unordered.layers.end());
    ImageLayerStackEditor::rebuildLayerOrder(unordered);
    require(unordered.layers == document.layers,
            QStringLiteral("Flattened layer order did not follow the root stack."));

    QString error;
    const auto noncontiguous = ImageLayerStackEditor::groupLayers(
        document, {first.id, third.id}, {}, &error);
    require(!noncontiguous.has_value() && !error.isEmpty() && document == original,
            QStringLiteral("Rejected non-contiguous grouping changed its source document."));

    const auto added = ImageLayerStackEditor::addLayer(
        document, second.id, {});
    require(added.has_value() && document == original &&
            added->selected_group_id.isEmpty() &&
            added->document.root_stack.size() == 5 &&
            added->document.root_stack.at(3).id == added->selected_layer_id &&
            added->document.layers.at(3).id == added->selected_layer_id,
            QStringLiteral("Adding a layer did not prepare an inserted candidate and selection."));

    const auto moved_to_top = ImageLayerStackEditor::moveItem(
        document, first.id, false, {}, document.root_stack.size(), second.id, {});
    require(moved_to_top.has_value() && document == original &&
            moved_to_top->document.root_stack == QVector<ImageStackItemData>{
                {background.id, false}, {second.id, false}, {third.id, false},
                {first.id, false}} &&
            moved_to_top->document.layers.size() == document.layers.size(),
            QStringLiteral("Moving a root layer upward lost it from the stack."));
    const auto moved_to_bottom = ImageLayerStackEditor::moveItem(
        moved_to_top->document, first.id, false, {}, 1, second.id, {});
    require(moved_to_bottom.has_value() &&
            moved_to_bottom->document.root_stack == document.root_stack &&
            moved_to_bottom->document.layers == document.layers,
            QStringLiteral("Moving a root layer downward lost it from the stack."));

    const auto grouped = ImageLayerStackEditor::groupLayers(
        document, {second.id, first.id}, {}, &error);
    require(grouped.has_value() && error.isEmpty() && document == original &&
            grouped->selected_layer_id.isEmpty() &&
            grouped->selected_group_id == grouped->document.groups.front().id &&
            grouped->document.groups.front().layer_ids == QStringList{first.id, second.id} &&
            grouped->document.layers.at(1).parent_group_id == grouped->selected_group_id &&
            grouped->document.layers.at(2).parent_group_id == grouped->selected_group_id,
            QStringLiteral("Grouping did not preserve stack order or return the group selection."));

    const QString group_id = grouped->selected_group_id;
    const auto moved_into_root = ImageLayerStackEditor::moveItem(
        grouped->document, second.id, false, {},
        grouped->document.root_stack.size(), third.id, {});
    require(moved_into_root.has_value() &&
            moved_into_root->document.groups.front().layer_ids == QStringList{first.id} &&
            moved_into_root->document.layers.at(3).id == second.id &&
            moved_into_root->document.layers.at(3).parent_group_id.isEmpty() &&
            moved_into_root->selected_layer_id == third.id,
            QStringLiteral("Moving a child to the root lost membership or selection."));
    const auto moved_into_group = ImageLayerStackEditor::moveItem(
        moved_into_root->document, second.id, false, group_id, 1,
        third.id, {});
    require(moved_into_group.has_value() &&
            moved_into_group->document.groups.front().layer_ids ==
                QStringList{first.id, second.id},
            QStringLiteral("Moving a root layer into a group did not preserve child order."));

    const auto new_group = ImageLayerStackEditor::addGroup(
        grouped->document, first.id, {}, &error);
    require(new_group.has_value() && error.isEmpty() &&
            new_group->document.root_stack.size() == 4 &&
            new_group->document.root_stack.at(2).group &&
            new_group->selected_group_id == new_group->document.root_stack.at(2).id,
            QStringLiteral("Adding a group around a selected child created an invalid nesting."));
    const auto reordered_group = ImageLayerStackEditor::moveItemBy(
        new_group->document, group_id, true, 1,
        new_group->selected_layer_id, new_group->selected_group_id);
    require(reordered_group.has_value() &&
            reordered_group->document.root_stack.at(1).id == new_group->selected_group_id &&
            reordered_group->document.root_stack.at(2).id == group_id &&
            reordered_group->selected_group_id == new_group->selected_group_id,
            QStringLiteral("Moving a group changed its sibling order or selection incorrectly."));

    const auto ungrouped = ImageLayerStackEditor::ungroup(
        grouped->document, group_id, {}, group_id);
    require(ungrouped.has_value() && ungrouped->document.groups.isEmpty() &&
            ungrouped->document.root_stack.mid(1, 2) ==
                QVector<ImageStackItemData>{{first.id, false}, {second.id, false}} &&
            ungrouped->selected_layer_id == second.id &&
            ungrouped->selected_group_id.isEmpty(),
            QStringLiteral("Ungrouping did not restore child order and selection."));

    const auto deleted = ImageLayerStackEditor::deleteItems(
        grouped->document, {{group_id, true}}, first.id, {});
    require(deleted.has_value() && deleted->document.groups.isEmpty() &&
            deleted->document.layers == QVector<ImageLayerData>{background, third} &&
            deleted->document.root_stack ==
                QVector<ImageStackItemData>{{background.id, false}, {third.id, false}} &&
            deleted->selected_layer_id == third.id,
            QStringLiteral("Deleting a group did not remove its children or choose a valid selection."));
    require(!ImageLayerStackEditor::deleteItems(
                document, {{background.id, false}}, first.id, {}).has_value() &&
            !ImageLayerStackEditor::moveItemBy(
                document, background.id, false, -1, first.id, {}).has_value(),
            QStringLiteral("Background was allowed to be deleted or reordered."));

    ImageDocumentData full = document;
    while (ImageLayerStackEditor::itemCount(full) < ImageDocumentStore::kMaximumLayers) {
        ImageGroupData filler;
        filler.id = QStringLiteral("filler-%1").arg(full.groups.size());
        full.groups.append(std::move(filler));
    }
    const auto full_before = full;
    require(!ImageLayerStackEditor::addLayer(full, first.id, {}).has_value() &&
            !ImageLayerStackEditor::addGroup(full, first.id, {}, &error).has_value() &&
            !error.isEmpty() && full == full_before,
            QStringLiteral("Stack capacity rejection changed the candidate source."));
}

void testLayerGroups(const QString& root) {
    image_editor::ImageDocumentSession session;
    QString error;
    require(session.createCanvas(QSize(20, 16), QColor(0, 0, 0, 0), &error), error);
    const QString background_id = session.data().layers.front().id;
    const QString bottom_layer = session.selectedLayerId();
    require(session.applyPaintStroke(
                {QPointF(4, 4), QPointF(7, 7)}, QColor(240, 20, 10), 3, &error), error);
    const QString top_layer = session.addLayer();
    require(!top_layer.isEmpty() && session.applyPaintStroke(
                {QPointF(4, 4), QPointF(13, 7)}, QColor(15, 40, 240), 3, &error), error);
    const QImage before_grouping = session.renderedImage();

    const QString group_id = session.groupLayers({bottom_layer, top_layer}, &error);
    const QImage after_grouping = session.renderedImage();
    bool group_composition_equal = before_grouping.size() == after_grouping.size();
    int maximum_channel_difference = 0;
    for (int y = 0; group_composition_equal && y < before_grouping.height(); ++y) {
        for (int x = 0; x < before_grouping.width(); ++x) {
            const QColor before = before_grouping.pixelColor(x, y);
            const QColor after = after_grouping.pixelColor(x, y);
            const int difference = std::max({std::abs(before.red() - after.red()),
                std::abs(before.green() - after.green()),
                std::abs(before.blue() - after.blue()),
                std::abs(before.alpha() - after.alpha())});
            maximum_channel_difference = std::max(maximum_channel_difference, difference);
            if (difference > 2) group_composition_equal = false;
        }
    }
    require(!group_id.isEmpty() && session.data().root_stack.size() == 2 &&
                session.data().root_stack.at(0).id == background_id &&
                session.data().root_stack.at(1).group &&
                session.data().groups.size() == 1 &&
                session.data().groups.front().layer_ids == QStringList{bottom_layer, top_layer} &&
                session.data().layers.at(1).parent_group_id == group_id &&
                session.data().layers.at(2).parent_group_id == group_id &&
                session.selectedGroupId() == group_id && session.selectedGroupIsActive() &&
                group_composition_equal,
            QStringLiteral("Grouping contiguous sibling layers changed their order or composition "
                           "(root=%1, bg=%2, group=%3, children=%4, parents=%5/%6, selected=%7, "
                           "active=%8, render=%9).")
                .arg(session.data().root_stack.size())
                .arg(!session.data().root_stack.isEmpty() &&
                     session.data().root_stack.front().id == background_id)
                .arg(session.data().root_stack.size() > 1 &&
                     session.data().root_stack.at(1).group)
                .arg(session.data().groups.isEmpty() ? QStringLiteral("missing")
                    : session.data().groups.front().layer_ids.join(','))
                .arg(session.data().layers.size() > 1 &&
                     session.data().layers.at(1).parent_group_id == group_id)
                .arg(session.data().layers.size() > 2 &&
                     session.data().layers.at(2).parent_group_id == group_id)
                .arg(session.selectedGroupId() == group_id)
                .arg(session.selectedGroupIsActive())
                .arg(group_composition_equal)
                .append(QStringLiteral(" maximum channel delta=%1.")
                    .arg(maximum_channel_difference)));

    require(session.setGroupOpacity(group_id, 50),
            QStringLiteral("The group opacity could not be changed."));
    const QColor overlap = session.renderedImage().pixelColor(4, 4);
    require(overlap.alpha() >= 120 && overlap.alpha() <= 136 && overlap.blue() > overlap.red(),
            QStringLiteral("Group opacity was not applied once to the overlapping child composite."));
    require(session.setLayerVisible(bottom_layer, false) &&
                session.renderedImage().pixelColor(7, 7).alpha() == 0 &&
                session.renderedImage().pixelColor(13, 7).alpha() > 0,
            QStringLiteral("Child visibility did not affect only that layer."));
    require(session.setLayerVisible(bottom_layer, true),
            QStringLiteral("The hidden group child could not be restored."));

    image_editor::ImageExportOptions group_options;
    group_options.scope = image_editor::ImageExportScope::SelectedGroup;
    const QString group_export_path = root + QStringLiteral("/selected-group.png");
    require(session.exportImage(group_export_path, group_options, &error), error);
    const QImage group_export(group_export_path);
    require(group_export.size() == QSize(20, 16) &&
                group_export.pixelColor(4, 4).alpha() >= 120 &&
                group_export.pixelColor(7, 7).alpha() > 0 &&
                group_export.pixelColor(0, 0).alpha() == 0,
            QStringLiteral("Quick Export of a group omitted its children or included Background."));

    const QImage before_group_crop = session.renderedImage();
    require(session.applyCrop(QRect(0, 0, 9, 10), &error), error);
    require(session.renderedImage().size() == QSize(20, 16) &&
                session.renderedImage().pixelColor(13, 7).alpha() == 0,
            QStringLiteral("Group crop did not clip the composed children on the fixed canvas."));
    require(session.undo() && session.renderedImage() == before_group_crop,
            QStringLiteral("Group crop Undo failed."));

    // Exercise the other group transforms and their history against a saved visual baseline.
    const QImage before_transforms = session.renderedImage();
    session.rotateRight();
    require(session.renderedImage() != before_transforms &&
                session.renderedImage().size() == QSize(20, 16) && session.undo() &&
                session.renderedImage() == before_transforms,
            QStringLiteral("Group rotation did not transform and restore the combined content."));
    session.flipHorizontal();
    require(session.renderedImage() != before_transforms && session.undo() &&
                session.renderedImage() == before_transforms,
            QStringLiteral("Group flip did not transform and restore the combined content."));
    session.flipVertical();
    require(session.renderedImage() != before_transforms && session.undo() &&
                session.renderedImage() == before_transforms,
            QStringLiteral("Group vertical flip did not transform and restore the combined content."));

    require(session.setGroupVisible(group_id, false) &&
                session.renderedImage().pixelColor(4, 4).alpha() == 0 &&
                session.setGroupVisible(group_id, true),
            QStringLiteral("Group visibility did not hide and restore all its children."));
    require(session.undo() && session.data().groups.front().visible == false && session.redo() &&
                session.data().groups.front().visible == true,
            QStringLiteral("Group visibility did not participate in Undo/Redo."));

    // Clear group opacity and transforms before checking ungroup preservation.
    require(session.setGroupOpacity(group_id, 100),
            QStringLiteral("Group opacity could not be restored before ungrouping."));
    const QImage before_ungroup = session.renderedImage();
    require(session.ungroup(group_id) && session.data().groups.isEmpty() &&
                session.data().root_stack.size() == 3 &&
                session.data().root_stack.at(1).id == bottom_layer &&
                session.data().root_stack.at(2).id == top_layer &&
                visuallyEquivalent(session.renderedImage(), before_ungroup) &&
                session.undo() && session.data().groups.size() == 1 &&
                visuallyEquivalent(session.renderedImage(), before_ungroup),
            QStringLiteral("Ungroup did not preserve the child order, appearance, and history."));

    require(session.deleteGroup(group_id) && session.data().layers.size() == 1 &&
                session.undo() && session.data().layers.size() == 3 &&
                session.data().groups.size() == 1,
            QStringLiteral("Deleting a group and restoring it with Undo did not include its children."));

    image_editor::ImageDocumentSession structure;
    require(structure.createCanvas(QSize(12, 10), Qt::transparent, &error), error);
    const QString first = structure.selectedLayerId();
    const QString second = structure.addLayer();
    require(!second.isEmpty(), QStringLiteral("Could not create sibling layers for group validation."));
    const QString third = structure.addLayer();
    require(!third.isEmpty(), QStringLiteral("Could not create a non-contiguous sibling layer."));
    const auto before_rejected_grouping = structure.data();
    const bool undo_before_rejected_grouping = structure.canUndo();
    require(structure.groupLayers({first, third}, &error).isEmpty() &&
                structure.data() == before_rejected_grouping &&
                structure.canUndo() == undo_before_rejected_grouping,
            QStringLiteral("Non-contiguous Group Selected changed document or history."));
    const QString group = structure.groupLayers({first, second}, &error);
    require(!group.isEmpty(), error);
    require(structure.selectLayer(second), QStringLiteral("Could not select a child layer."));
    const QString child_added = structure.addLayer();
    require(!child_added.isEmpty() &&
                structure.data().groups.front().layer_ids == QStringList{first, second, child_added},
            QStringLiteral("Adding a layer while a group child was selected did not insert inside that group."));
    const QString new_group = structure.addGroup(&error);
    require(!new_group.isEmpty() && structure.data().root_stack.size() == 4 &&
                structure.data().root_stack.at(2).group &&
                structure.data().root_stack.at(2).id == new_group &&
                structure.data().root_stack.at(3).id == third,
            QStringLiteral("Creating a group while a child was selected created a nested group."));
    require(structure.selectGroup(group), QStringLiteral("Could not reselect the filled group."));
    const QString root_layer = structure.addLayer();
    require(!root_layer.isEmpty() && structure.data().layers.back().parent_group_id.isEmpty() &&
                structure.data().root_stack.at(2).id == root_layer,
            QStringLiteral("Adding a layer while a group was selected did not insert at the root."));

    const QString v8_path = root + QStringLiteral("/groups-v8.cimg");
    require(structure.saveDocument(v8_path, &error), error);
    QFile v8_file(v8_path);
    require(v8_file.open(QIODevice::ReadOnly), QStringLiteral("Could not read the v8 group document."));
    const QJsonObject v8_json = QJsonDocument::fromJson(v8_file.readAll()).object();
    require(v8_json.value("version").toInt() == 13 &&
                v8_json.value("layers").toArray().size() == structure.data().root_stack.size(),
            QStringLiteral("Group save did not write the v9 ordered stack."));
    image_editor::ImageDocumentSession reopened;
    require(reopened.openDocument(v8_path, &error) &&
                reopened.data() == structure.data() &&
                reopened.renderedImage() == structure.renderedImage(), error);

    image_editor::RecoveryStore recovery(root + QStringLiteral("/group-recovery"));
    require(structure.setGroupOpacity(group, 80),
            QStringLiteral("Could not dirty the group document before recovery."));
    require(recovery.save(structure, &error), error);
    QFile recovery_file(recovery.pathFor(structure));
    require(recovery_file.open(QIODevice::ReadOnly),
            QStringLiteral("The group recovery snapshot could not be read."));
    const QJsonObject recovery_json = QJsonDocument::fromJson(recovery_file.readAll()).object();
    require(recovery_json.value("version").toInt() == 1 &&
                recovery_json.value("document").toObject().value("version").toInt() == 13,
            QStringLiteral("Group recovery changed its wrapper version or lost the v13 payload."));

    image_editor::ImageDocumentData invalid = structure.data();
    invalid.groups.front().layer_ids.append(invalid.layers.front().id);
    require(!image_editor::ImageDocumentStore::saveDocument(
                root + QStringLiteral("/invalid-group.cimg"), invalid, &error) && !error.isEmpty(),
            QStringLiteral("A group containing Background was accepted by the document validator."));

    image_editor::ImageDocumentSession at_limit;
    require(at_limit.createCanvas(QSize(4, 4), Qt::transparent, &error), error);
    for (qsizetype index = at_limit.data().layers.size();
         index < image_editor::ImageDocumentStore::kMaximumLayers; ++index) {
        require(!at_limit.addGroup(&error).isEmpty(), error);
    }
    const auto full_stack = at_limit.data();
    const QString full_stack_selection = at_limit.selectedGroupId();
    require(at_limit.addGroup(&error).isEmpty() && !error.isEmpty() &&
                at_limit.data() == full_stack &&
                at_limit.selectedGroupId() == full_stack_selection,
            QStringLiteral("The 512-item group limit changed the document or selection."));
}

void testSelectedLayerExport(const QString& root) {
    image_editor::ImageDocumentSession session;
    QString error;
    require(session.createCanvas(QSize(12, 8), QColor(18, 28, 38), &error), error);
    const QString background_id = session.data().layers.front().id;
    const QString selected_id = session.selectedLayerId();
    require(session.applyPaintStroke({QPointF(4, 4)}, QColor(230, 35, 20), 3, &error), error);

    const QString composite_path = root + QStringLiteral("/quick-export-composite.png");
    require(session.exportImage(composite_path, &error), error);
    const QImage composite(composite_path);
    require(composite.size() == QSize(12, 8) &&
                composite.pixelColor(0, 0).alpha() == 255 &&
                composite.pixelColor(4, 4).red() > 200,
            QStringLiteral("The default export scope no longer composites all layers."));

    image_editor::ImageExportOptions options;
    options.scope = image_editor::ImageExportScope::SelectedLayer;
    const QString layer_path = root + QStringLiteral("/quick-export-selected.png");
    require(session.exportImage(layer_path, options, &error), error);
    const QImage selected(layer_path);
    require(selected.size() == QSize(12, 8) &&
                selected.pixelColor(4, 4).red() > 200 &&
                selected.pixelColor(0, 0).alpha() == 0,
            QStringLiteral("Selected-layer export included Background or changed the canvas bounds."));

    require(session.setLayerOpacity(selected_id, 50),
            QStringLiteral("The selected layer opacity could not be changed for export."));
    const QString opacity_path = root + QStringLiteral("/quick-export-opacity.png");
    require(session.exportImage(opacity_path, options, &error), error);
    const QColor half_opacity = QImage(opacity_path).pixelColor(4, 4);
    require(half_opacity.alpha() >= 120 && half_opacity.alpha() <= 136,
            QStringLiteral("Selected-layer export ignored layer opacity."));

    require(session.setLayerVisible(selected_id, false),
            QStringLiteral("The selected layer could not be hidden for export."));
    const QString hidden_path = root + QStringLiteral("/quick-export-hidden.png");
    require(session.exportImage(hidden_path, options, &error), error);
    const QImage hidden(hidden_path);
    require(hidden.size() == QSize(12, 8) && hidden.pixelColor(4, 4).alpha() == 0 &&
                hidden.pixelColor(0, 0).alpha() == 0,
            QStringLiteral("A hidden selected layer was not exported as a transparent canvas."));

    auto background_snapshot = session.exportSnapshot();
    background_snapshot.selected_layer_id = background_id;
    image_editor::ImageOperation base_mark;
    base_mark.kind = image_editor::OperationKind::PaintStroke;
    base_mark.paint_stroke.points = {QPointF(8, 2)};
    base_mark.paint_stroke.color = QColor(20, 210, 60);
    base_mark.paint_stroke.diameter = 3;
    background_snapshot.document.operations.append(base_mark);
    const QString background_path = root + QStringLiteral("/quick-export-background.png");
    const auto background_result = image_editor::exportImageSnapshot(
        background_snapshot, background_path, options);
    const QImage background(background_path);
    require(background_result.status == image_editor::ImageExportStatus::Succeeded &&
                background.size() == QSize(12, 8) &&
                background.pixelColor(0, 0).red() == 18 &&
                background.pixelColor(8, 2).green() > 180 &&
                background.pixelColor(4, 4).red() == 18,
            QStringLiteral("Background export omitted base operations or included another layer."));

    auto invalid_snapshot = session.exportSnapshot();
    invalid_snapshot.selected_layer_id = QStringLiteral("missing-layer");
    const auto invalid_result = image_editor::exportImageSnapshot(
        invalid_snapshot, root + QStringLiteral("/quick-export-invalid.png"), options);
    require(invalid_result.status == image_editor::ImageExportStatus::Failed &&
                !invalid_result.error.isEmpty(),
            QStringLiteral("Selected-layer export accepted a missing layer ID."));
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

void testAreaSelectionClipPersistenceAndRendering(const QString& root) {
    using namespace image_editor;
    ImageDocumentSession session;
    QString error;
    require(session.createCanvas(QSize(24, 16), QColor(0, 0, 0, 0), &error), error);
    const QPainterPath rectangle = [] {
        QPainterPath path;
        path.addRect(QRectF(5.0, 3.0, 7.0, 10.0));
        return path;
    }();
    require(session.applyPaintStroke({QPointF(2, 8), QPointF(21, 8)}, Qt::red, 2,
                                     &error, rectangle), error);
    const auto& layer = session.data().layers.back();
    require(layer.operations.back().paint_stroke.clipping_path == rectangle,
            QStringLiteral("Paint did not retain the active area-selection clip."));
    QImage rendered = session.renderedImage();
    require(rendered.pixelColor(3, 8).alpha() == 0 &&
                rendered.pixelColor(8, 8).red() > 200 &&
                rendered.pixelColor(14, 8).alpha() == 0,
            QStringLiteral("Paint escaped the selected area."));
    require(session.undo() && session.renderedImage().pixelColor(8, 8).alpha() == 0 &&
                session.redo() && session.renderedImage().pixelColor(8, 8).red() > 200,
            QStringLiteral("Undo/Redo did not preserve clipped paint."));

    const QString document_path = root + QStringLiteral("/area-selection-v13.cimg");
    require(session.saveDocument(document_path, &error), error);
    QFile saved_file(document_path);
    require(saved_file.open(QIODevice::ReadOnly),
            QStringLiteral("The clipped document could not be read."));
    QJsonObject saved_json = QJsonDocument::fromJson(saved_file.readAll()).object();
    saved_file.close();
    require(saved_json.value("version").toInt() == 13,
            QStringLiteral("A clipped stroke did not use .cimg v13."));
    ImageDocumentSession reopened;
    require(reopened.openDocument(document_path, &error), error);
    require(reopened.data() == session.data() &&
                reopened.renderedImage() == session.renderedImage(),
            QStringLiteral("The selection clip changed after saving and reopening."));
    const QString export_path = root + QStringLiteral("/area-selection.png");
    require(reopened.exportImage(export_path, &error), error);
    require(QImage(export_path) == reopened.renderedImage(),
            QStringLiteral("PNG export ignored the persisted stroke clip."));

    // A v12 document has no persisted selection clip; its stroke remains unrestricted.
    auto legacy_json = saved_json;
    legacy_json.insert("version", 12);
    auto layers = legacy_json.value("layers").toArray();
    auto editable_layer = layers.last().toObject();
    auto layer_operations = editable_layer.value("operations").toArray();
    auto stroke = layer_operations.last().toObject();
    stroke.remove("clip_path");
    stroke.remove("clip_rule");
    layer_operations.replace(layer_operations.size() - 1, stroke);
    editable_layer.insert("operations", layer_operations);
    layers.replace(layers.size() - 1, editable_layer);
    legacy_json.insert("layers", layers);
    const QString legacy_path = root + QStringLiteral("/area-selection-v12.cimg");
    QFile legacy_file(legacy_path);
    require(legacy_file.open(QIODevice::WriteOnly),
            QStringLiteral("The v12 compatibility fixture could not be written."));
    legacy_file.write(QJsonDocument(legacy_json).toJson());
    legacy_file.close();
    ImageDocumentSession legacy;
    require(legacy.openDocument(legacy_path, &error), error);
    require(!legacy.data().layers.back().operations.back().paint_stroke.clipping_path.has_value() &&
                legacy.renderedImage().pixelColor(3, 8).red() > 200,
            QStringLiteral("A v12 stroke did not retain its original unrestricted behavior."));

    auto invalid_document = session.data();
    QPainterPath oversized;
    oversized.moveTo(1.0, 1.0);
    for (qsizetype index = 0;
         index <= ImageDocumentStore::kMaximumStrokeClipPathElements; ++index) {
        oversized.lineTo(static_cast<qreal>(index + 1), 2.0);
    }
    invalid_document.layers.back().operations.back().paint_stroke.clipping_path = oversized;
    require(!ImageDocumentStore::saveDocument(
                root + QStringLiteral("/oversized-area-selection.cimg"),
                invalid_document, &error) && !error.isEmpty(),
            QStringLiteral("A stroke clip beyond the geometry limit was saved."));

    ImageDocumentSession mask_session;
    require(mask_session.createCanvas(QSize(24, 16), Qt::blue, &error), error);
    const QString mask_layer = mask_session.addLayer();
    require(!mask_layer.isEmpty() && mask_session.applyPaintStroke(
                {QPointF(2, 8), QPointF(21, 8)}, Qt::red, 4, &error), error);
    require(mask_session.addLayerMask(mask_layer),
            QStringLiteral("Could not create a mask for clipped painting."));
    require(mask_session.applyLayerMaskStroke(
                {QPointF(2, 8), QPointF(21, 8)}, Qt::black, 4, &error, rectangle), error);
    const QImage masked = mask_session.renderedImage();
    require(masked.pixelColor(3, 8).red() > 200 &&
                masked.pixelColor(8, 8).blue() > 200 &&
                masked.pixelColor(14, 8).red() > 200,
            QStringLiteral("Mask painting did not respect the selected area."));

    ImageDocumentSession grouped;
    require(grouped.createCanvas(QSize(24, 16), QColor(0, 0, 0, 0), &error), error);
    const QString child = grouped.addLayer();
    const QString second_child = grouped.addLayer();
    const QString group = grouped.groupLayers({child, second_child}, &error);
    require(!group.isEmpty(),
            QStringLiteral("Could not prepare a flipped layer group."));
    grouped.flipHorizontal();
    QPainterPath group_clip;
    group_clip.addRect(QRectF(15.0, 5.0, 5.0, 6.0));
    require(grouped.selectLayer(child) && grouped.applyPaintStroke(
                {QPointF(17, 8)}, Qt::green, 2, &error, group_clip), error);
    const QImage grouped_render = grouped.renderedImage();
    const auto grouped_child_layer = std::find_if(
        grouped.data().layers.cbegin(), grouped.data().layers.cend(),
        [&child](const ImageLayerData& layer) { return layer.id == child; });
    require(grouped_render.pixelColor(17, 8).green() > 150 &&
                grouped_render.pixelColor(6, 8).alpha() == 0 &&
                grouped_child_layer != grouped.data().layers.cend() &&
                grouped_child_layer->operations.back().paint_stroke.clipping_path.has_value(),
            QStringLiteral("A selected stroke in a transformed group used the wrong clip space."));
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
    QGuiApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        std::cerr << "Could not create a temporary test directory.\n";
        return 1;
    }
    const QString root = temporary.path();
    try {
        testDocumentEditingAndUndoRedo(root);
        testCanvasCreationPersistenceAndRecovery(root);
        testCanvasResizingAnchorsPersistenceAndHistory(root);
        testStatelessDocumentRenderer();
        testLegacyVersionOneDocument(root);
        testVersionTwoDocumentCompatibility(root);
        testVersionThreeMigrationToBackground(root);
        testPaintStrokesPersistenceUndoRedoAndValidation(root);
        testEraseStrokesPersistenceUndoRedoAndValidation(root);
        testEditableShapesRenderingPersistenceAndHistory(root);
        testEditableTextRenderingPersistenceAndHistory(root);
        testCropNoOpAndInvalidOperations(root);
        testGeneralObjectOperations(root);
        testLayerManagementTransformsAndOpacity(root);
        testImageLayerStackEditor();
        testLayerGroups(root);
        testAreaSelectionClipPersistenceAndRendering(root);
        testMissingSourceAndRelink(root);
        testExportAndFormatPlugins(root);
        testSelectedLayerExport(root);
        testRecoveryAndLogging(root);
        testInvalidDocument(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
