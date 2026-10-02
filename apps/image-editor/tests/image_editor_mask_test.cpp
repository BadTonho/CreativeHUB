#include "image_document_session.h"
#include "recovery_store.h"

#include <QGuiApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace image_editor;

namespace {
void require(bool condition, const QString& message) {
    if (!condition) throw std::runtime_error(message.toStdString());
}

QString makeLayer(ImageDocumentSession& session, const QColor& color) {
    ImageShapeData shape;
    shape.start = QPointF(0, 0);
    shape.end = QPointF(15, 15);
    shape.stroke_enabled = false;
    shape.fill_color = color;
    QString error;
    require(!session.addShape(shape, &error).isEmpty(), error);
    return session.selectedLayerId();
}

QJsonObject readJson(const QString& path) {
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), file.errorString());
    return QJsonDocument::fromJson(file.readAll()).object();
}

void writeJson(const QString& path, const QJsonObject& document) {
    QFile file(path);
    require(file.open(QIODevice::WriteOnly), file.errorString());
    require(file.write(QJsonDocument(document).toJson()) > 0, file.errorString());
}

void testMasks(const QString& root) {
    ImageDocumentSession session;
    QString error;
    require(session.createCanvas(QSize(16, 16), Qt::transparent, &error), error);
    const QString red = makeLayer(session, Qt::red);
    const QImage original = session.renderedImage();
    require(!session.addLayerMask(session.data().layers.front().id), "Background accepted a mask.");
    require(session.addLayerMask(red), "Could not add a mask.");
    require(session.undo() && !session.data().layers.back().mask.has_value(), "Mask creation undo failed.");
    require(session.redo() && session.data().layers.back().mask.has_value(), "Mask creation redo failed.");
    require(!session.addLayerMask(red), "Duplicate mask creation succeeded.");
    require(session.renderedImage() == original, "The initial white mask changed the layer.");
    require(session.renderedLayerMaskThumbnails(QSize(16, 16)).value(red).pixelColor(4, 6) == QColor(Qt::white),
            "The initial mask thumbnail is not white.");
    const QVector<QPointF> points{QPointF(4, 6)};
    require(session.applyLayerMaskStroke(points, Qt::black, 4, &error), error);
    require(session.renderedImage().pixelColor(4, 6).alpha() == 0, "Black did not hide layer pixels.");
    const auto masked = session.renderedImage();
    require(session.undo() && session.renderedImage() == original, "Mask stroke undo failed.");
    require(session.redo() && session.renderedImage() == masked, "Mask stroke redo failed.");
    require(session.applyLayerMaskStroke(points, Qt::white, 4, &error), error);
    require(session.renderedImage().pixelColor(4, 6).alpha() == 255, "White did not restore pixels.");
    require(session.applyLayerMaskStroke(points, QColor(128, 128, 128), 4, &error), error);
    require(std::abs(session.renderedImage().pixelColor(4, 6).alpha() - 128) <= 1,
            "Gray did not produce partial alpha.");
    require(session.applyLayerMaskStroke(points, QColor(0, 0, 0, 128), 4, &error), error);
    require(std::abs(session.renderedImage().pixelColor(4, 6).alpha() - 64) <= 1,
            "Paint alpha did not control mask strength.");
    require(session.applyLayerMaskStroke(points, QColor(240, 60, 10), 4, &error), error);
    const auto& gray_stroke = session.data().layers.back().mask->operations.last().paint_stroke;
    require(gray_stroke.color.red() == qGray(QColor(240, 60, 10).rgb()) &&
            gray_stroke.color.red() == gray_stroke.color.green(), "Mask paint was not grayscale.");
    const auto before_preview = session.data();
    const auto preview = session.renderedImageWithMaskStroke(points, Qt::black, 4);
    require(preview.pixelColor(4, 6).alpha() == 0 && session.data() == before_preview,
            "Mask preview edited the document or did not mask pixels.");
    require(session.applyLayerMaskEraseStroke(points, 4, &error), error);
    require(session.renderedImage().pixelColor(4, 6).alpha() == 0, "Mask eraser did not write black.");
    require(session.undo() && session.data() == before_preview, "Mask eraser undo changed layer content.");
    require(session.redo() && session.renderedImage().pixelColor(4, 6).alpha() == 0, "Mask eraser redo failed.");
    require(!session.applyLayerMaskStroke({}, Qt::black, 4, &error) && !error.isEmpty(),
            "Empty mask stroke was accepted.");
    require(!session.applyLayerMaskStroke({QPointF(16, 1)}, Qt::black, 4, &error),
            "Out-of-bounds mask stroke was accepted.");
    require(!session.applyLayerMaskStroke(points, Qt::black, 0, &error), "Zero brush size was accepted.");
    require(!session.applyLayerMaskStroke(points, QColor(), 4, &error), "Invalid mask color was accepted.");
    const auto no_op_data = session.data();
    require(!session.applyLayerMaskStroke(points, Qt::transparent, 4, &error) &&
            error.isEmpty() && session.data() == no_op_data, "Transparent mask paint was not a no-op.");
    const auto thumbnail = session.renderedLayerMaskThumbnails(QSize(16, 16)).value(red);
    require(thumbnail.pixelColor(4, 6) == QColor(Qt::black), "Mask thumbnail did not show black paint.");
    require(session.renderedLayerThumbnails(QSize(16, 16)).value(red).pixelColor(4, 6).alpha() == 0,
            "Layer thumbnail ignored its mask.");
    require(session.renderedLayerMaskThumbnails({}).isEmpty(), "Invalid thumbnail dimensions were accepted.");
    require(session.setLayerMaskEnabled(red, false) && session.renderedImage() == original,
            "Disabling a mask did not reveal original content.");
    require(session.undo() && session.data().layers.back().mask->enabled, "Mask toggle undo failed.");
    require(session.redo() && !session.data().layers.back().mask->enabled, "Mask toggle redo failed.");
    require(session.setLayerMaskEnabled(red, true), "Could not enable mask.");
    require(session.removeLayerMask(red) && session.renderedImage() == original,
            "Removing a mask damaged content.");
    require(session.undo() && session.data().layers.back().mask.has_value(), "Mask removal undo failed.");

    const auto before_flip = session.renderedImage();
    session.flipHorizontal();
    require(session.renderedImage() == before_flip.mirrored(true, false), "Horizontal flip left mask behind.");
    const auto after_flip = session.data();
    require(session.undo() && session.renderedImage() == before_flip, "Layer transform undo left the mask transformed.");
    require(session.redo() && session.data() == after_flip, "Layer transform redo lost mask history.");
    const auto before_vertical = session.renderedImage();
    session.flipVertical();
    require(session.renderedImage() == before_vertical.mirrored(false, true), "Vertical flip left mask behind.");
    const auto before_rotate = session.renderedImage();
    session.rotateRight();
    session.rotateLeft();
    require(session.renderedImage() == before_rotate, "Rotation did not preserve mask alignment.");
    require(session.applyCrop(QRect(2, 2, 10, 10), &error), error);
    require(session.renderedImage().pixelColor(0, 0).alpha() == 0 &&
            session.data().layers.back().mask->operations.last().kind == OperationKind::Crop,
            "Crop did not apply to content and mask.");

    const QString document_path = root + "/masked.cimg";
    require(session.saveDocument(document_path, &error), error);
    require(readJson(document_path).value("version").toInt() == 11, "Mask document is not v11.");
    ImageDocumentSession reopened;
    require(reopened.openDocument(document_path, &error) && reopened.data() == session.data() &&
            reopened.renderedImage() == session.renderedImage(), "Mask round-trip lost edits.");
    RecoveryStore recovery(root + "/recovery");
    require(session.applyLayerMaskStroke({QPointF(8, 8)}, Qt::black, 3, &error), error);
    require(recovery.save(session, &error), error);
    ImageDocumentSession restored;
    require(restored.restoreRecovery(recovery.pathFor(session), &error) &&
            restored.data() == session.data() && restored.isDirty(), "Recovery lost the mask.");
    const auto recovery_json = readJson(recovery.pathFor(session));
    require(recovery_json.value("version").toInt() == 1 &&
            recovery_json.value("document").toObject().value("version").toInt() == 11,
            "Recovery changed its envelope or omitted v10.");

    const QString png_path = root + "/masked.png";
    require(session.exportImage(png_path, &error) && QImage(png_path) == session.renderedImage(),
            "Composite PNG export ignored masks.");
    ImageExportOptions quick;
    quick.scope = ImageExportScope::SelectedLayer;
    require(session.exportImage(root + "/quick.png", quick, &error), error);
    require(QImage(root + "/quick.png").pixelColor(8, 8).alpha() == 0,
            "Quick Export ignored its selected layer's mask.");
    quick.jpeg_background = Qt::green;
    require(session.exportImage(root + "/quick.jpg", quick, &error), error);
    require(QImage(root + "/quick.jpg").pixelColor(8, 8).green() > 120, "JPEG did not flatten masked alpha.");

    const QString blue = makeLayer(session, Qt::blue);
    require(session.selectLayer(red), "Could not reselect masked layer.");
    require(session.applyLayerMaskEraseStroke({QPointF(8, 8)}, 4, &error), error);
    // The unmasked upper layer must be unaffected by the lower layer's mask.
    require(session.renderedImage().pixelColor(8, 8) == QColor(Qt::blue), "Mask affected another layer.");
    const QString group = session.groupLayers({red, blue}, &error);
    require(!group.isEmpty(), error);
    require(!session.addLayerMask(group), "A group accepted a mask.");
    require(session.setGroupOpacity(group, 50), "Could not set group opacity.");
    require(std::abs(session.renderedImage().pixelColor(8, 8).alpha() - 127) <= 1,
            "Group opacity was applied incorrectly to masked children.");
    require(session.selectedGroupId() == group || session.selectGroup(group), "Could not select group.");
    quick.scope = ImageExportScope::SelectedGroup;
    require(session.exportImage(root + "/group.png", quick, &error), error);
    require(QImage(root + "/group.png") == session.renderedImage(), "Group Quick Export ignored child masks.");
    require(session.setLayerVisible(red, false), "Could not hide lower group child.");
    session.flipHorizontal();
    require(session.selectLayer(blue) && session.addLayerMask(blue), "Could not mask transformed group child.");
    require(session.applyLayerMaskEraseStroke({QPointF(2, 5)}, 3, &error), error);
    require(session.renderedImage().pixelColor(2, 5).alpha() == 0 &&
            session.data().layers.back().mask->operations.last().erase_stroke.points.first() == QPointF(13, 5),
            "Mask brush did not invert the parent group's transform.");
    require(session.selectGroup(group) && session.exportImage(root + "/masked-group.png", quick, &error), error);
    require(QImage(root + "/masked-group.png").pixelColor(2, 5).alpha() == 0,
            "Group Quick Export lost a child's transformed mask.");
    require(session.saveDocument(root + "/masked-group.cimg", &error), error);
    ImageDocumentSession grouped_session;
    require(grouped_session.openDocument(root + "/masked-group.cimg", &error) &&
            grouped_session.data() == session.data() && grouped_session.renderedImage() == session.renderedImage(),
            "A mask on a group child did not round-trip.");
    auto group_json = readJson(root + "/masked-group.cimg");
    auto group_layers = group_json.value("layers").toArray();
    auto group_object = group_layers.last().toObject();
    group_object.insert("mask", QJsonObject{{"enabled", true}, {"operations", QJsonArray{}}});
    group_layers.replace(group_layers.size() - 1, group_object);
    group_json.insert("layers", group_layers);
    writeJson(root + "/invalid-group.cimg", group_json);
    require(!grouped_session.openDocument(root + "/invalid-group.cimg", &error), "A persisted group accepted a mask.");

    // Mask-bearing v10 data must be rejected rather than silently dropped in old envelopes.
    auto json = readJson(document_path);
    const auto initial_layers = json.value("layers").toArray();
    const QString bad_path = root + "/bad-mask.cimg";
    auto expect_invalid = [&](QJsonObject candidate) {
        writeJson(bad_path, candidate);
        const auto previous = reopened.data();
        require(!reopened.openDocument(bad_path, &error) && !error.isEmpty() && reopened.data() == previous,
                "Invalid mask data replaced the open document.");
    };
    auto change_mask = [&](const QJsonValue& mask) {
        auto layers = initial_layers;
        auto layer = layers.last().toObject();
        layer.insert("mask", mask);
        layers.replace(layers.size() - 1, layer);
        auto candidate = json;
        candidate.insert("layers", layers);
        return candidate;
    };
    expect_invalid(change_mask(true));
    expect_invalid(change_mask(QJsonObject{{"enabled", true}}));
    expect_invalid(change_mask(QJsonObject{{"enabled", "true"}, {"operations", QJsonArray{}}}));
    auto unsupported_mask = initial_layers.last().toObject().value("mask").toObject();
    auto mask_ops = unsupported_mask.value("operations").toArray();
    auto stroke = mask_ops.first().toObject();
    stroke.insert("color", "#FFFF0000");
    mask_ops.replace(0, stroke);
    unsupported_mask.insert("operations", mask_ops);
    expect_invalid(change_mask(unsupported_mask));
    auto invalid_stroke = initial_layers.last().toObject().value("mask").toObject();
    auto invalid_ops = invalid_stroke.value("operations").toArray();
    auto duplicate = invalid_ops.first().toObject();
    duplicate.insert("id", initial_layers.last().toObject().value("operations").toArray().first().toObject().value("id"));
    invalid_ops.replace(0, duplicate);
    invalid_stroke.insert("operations", invalid_ops);
    expect_invalid(change_mask(invalid_stroke));
    invalid_stroke.insert("operations", QJsonArray{initial_layers.last().toObject().value("operations").toArray().first()});
    expect_invalid(change_mask(invalid_stroke));
    auto background_layers = initial_layers;
    auto background = background_layers.first().toObject();
    background.insert("mask", QJsonObject{{"enabled", true}, {"operations", QJsonArray{}}});
    background_layers.replace(0, background);
    auto invalid_background = json;
    invalid_background.insert("layers", background_layers);
    expect_invalid(invalid_background);
    auto old_with_mask = json;
    old_with_mask.insert("version", 9);
    expect_invalid(old_with_mask);

    // A supported v9 file without masks retains pixels and upgrades to v11.
    auto legacy_layers = initial_layers;
    auto legacy_layer = legacy_layers.last().toObject();
    legacy_layer.remove("mask");
    legacy_layers.replace(legacy_layers.size() - 1, legacy_layer);
    auto legacy = json;
    legacy.insert("version", 9);
    legacy.insert("layers", legacy_layers);
    writeJson(root + "/legacy-v9.cimg", legacy);
    ImageDocumentSession legacy_session;
    require(legacy_session.openDocument(root + "/legacy-v9.cimg", &error), error);
    require(!legacy_session.data().layers.back().mask.has_value(), "v9 invented a mask.");
    const auto legacy_pixels = legacy_session.renderedImage();
    require(legacy_session.saveDocument({}, &error), error);
    require(readJson(root + "/legacy-v9.cimg").value("version").toInt() == 11 &&
            legacy_session.renderedImage() == legacy_pixels, "v9 migration changed pixels.");

    // Mirrored transforms must not make a valid mask exceed persisted limits.
    QJsonArray maximum_ops;
    for (int index = 0; index < ImageDocumentStore::kMaximumOperations; ++index) {
        maximum_ops.append(QJsonObject{{"kind", "flip_horizontal"}});
    }
    writeJson(root + "/full-mask.cimg", change_mask(QJsonObject{{"enabled", true}, {"operations", maximum_ops}}));
    ImageDocumentSession full_mask;
    require(full_mask.openDocument(root + "/full-mask.cimg", &error), error);
    const auto full_data = full_mask.data();
    require(!full_mask.applyLayerMaskStroke(points, Qt::black, 4, &error) && !error.isEmpty(),
            "A full mask accepted another stroke.");
    require(!full_mask.applyCrop(QRect(2, 2, 10, 10), &error) && !error.isEmpty(),
            "Crop exceeded the mask operation limit.");
    full_mask.rotateLeft();
    full_mask.rotateRight();
    full_mask.flipHorizontal();
    full_mask.flipVertical();
    require(full_mask.data() == full_data && !full_mask.canUndo() && !full_mask.isDirty(),
            "Transforms exceeded mask limits or changed history after rejection.");
}
} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    QTemporaryDir temporary;
    try {
        require(temporary.isValid(), "Could not create mask test directory.");
        testMasks(temporary.path());
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
