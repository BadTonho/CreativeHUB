#include "image_linear_gradient.h"
#include "image_document_session.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

using namespace image_editor;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testGradientPixelsAndSelection() {
    QImage pixels(5, 1, QImage::Format_ARGB32);
    pixels.fill(Qt::transparent);
    ImageLinearGradientData gradient;
    gradient.start = QPointF(0.5, 0.5);
    gradient.end = QPointF(2.5, 0.5);
    gradient.color = QColor(220, 50, 30, 255);
    bool changed = false;
    QString error;
    require(ImageLinearGradient::apply(&pixels, gradient, &changed, &error) && changed,
            "A nonzero gradient must render and change pixels.");
    require(pixels.pixelColor(0, 0) == gradient.color &&
            pixels.pixelColor(1, 0).alpha() >= 120 &&
            pixels.pixelColor(1, 0).alpha() <= 136 &&
            pixels.pixelColor(1, 0).red() >= gradient.color.red() - 1 &&
            pixels.pixelColor(1, 0).red() <= gradient.color.red() &&
            pixels.pixelColor(2, 0).alpha() == 0 &&
            pixels.pixelColor(4, 0).alpha() == 0,
            "The linear ramp must use its opaque and transparent endpoints and pad beyond them.");

    QImage overlay(3, 1, QImage::Format_ARGB32);
    const QColor underlying(20, 40, 180, 255);
    overlay.fill(underlying);
    ImageLinearGradientData translucent_gradient;
    translucent_gradient.start = QPointF(0.5, 0.5);
    translucent_gradient.end = QPointF(2.5, 0.5);
    translucent_gradient.color = QColor(220, 80, 40, 128);
    require(ImageLinearGradient::apply(
                &overlay, translucent_gradient, &changed, &error) && changed &&
            overlay.pixelColor(0, 0).alpha() == 255 &&
            overlay.pixelColor(0, 0).red() > underlying.red() &&
            overlay.pixelColor(0, 0).blue() < underlying.blue() &&
            overlay.pixelColor(2, 0) == underlying,
            "The brush alpha must be interpolated in premultiplied RGBA, leaving the transparent end unchanged.");

    QImage clipped(5, 1, QImage::Format_ARGB32);
    clipped.fill(Qt::transparent);
    QPainterPath selection;
    selection.addRect(QRectF(0, 0, 2, 1));
    gradient.end = QPointF(4.5, 0.5);
    gradient.clipping_path = selection;
    require(ImageLinearGradient::apply(&clipped, gradient, &changed, &error) && changed &&
            clipped.pixelColor(0, 0).alpha() == 255 &&
            clipped.pixelColor(1, 0).alpha() < 255 &&
            clipped.pixelColor(2, 0).alpha() == 0,
            "The area selection must clip gradient output.");

    QImage unchanged(1, 1, QImage::Format_ARGB32);
    unchanged.fill(Qt::transparent);
    gradient.clipping_path.reset();
    gradient.start = gradient.end;
    require(ImageLinearGradient::apply(&unchanged, gradient, &changed, &error) && !changed,
            "A zero-length gradient must be a successful no-op.");
}

void testSessionPreviewHistoryMaskAndPersistence(const QString& root) {
    ImageDocumentSession session;
    QString error;
    require(session.createCanvas(QSize(5, 3), Qt::transparent, &error),
            "Could not create a synthetic gradient canvas.");
    const QPointF start(0.5, 1.5);
    const QPointF end(4.5, 1.5);
    const QColor color(30, 140, 230, 220);
    QPainterPath selection;
    selection.addRect(QRectF(0, 0, 5, 2));
    const QImage before = session.renderedImage();
    const QImage preview = session.renderedImageWithLinearGradient(
        start, end, color, selection);
    require(!preview.isNull() && session.renderedImage() == before && !session.canUndo(),
            "The live preview must not edit the document or history.");
    require(!session.applyLinearGradient(start, start, color, &error) &&
            error.isEmpty() && !session.canUndo(),
            "A zero-length gradient must not create an edit or history entry.");
    require(session.applyLinearGradient(start, end, color, &error, selection),
            "Applying a gradient to the active layer should create one operation.");
    const QImage applied = session.renderedImage();
    require(applied == preview && session.data().layers.at(1).operations.size() == 1 &&
            session.data().layers.at(1).operations.front().kind == OperationKind::LinearGradient &&
            session.data().layers.at(1).operations.front().linear_gradient.clipping_path.has_value() &&
            session.data().layers.at(1).operations.front().linear_gradient.clipping_path->contains(
                QPointF(1.0, 1.0)) &&
            !session.data().layers.at(1).operations.front().linear_gradient.clipping_path->contains(
                QPointF(1.0, 2.5)),
            "The preview and committed pixels must match, with one ordered operation.");
    require(session.undo() && session.renderedImage() == before &&
            session.redo() && session.renderedImage() == applied,
            "Undo and redo must remove and restore the gradient.");

    const QString saved_path = root + QStringLiteral("/gradient-v15.cimg");
    require(session.saveDocument(saved_path, &error), "Could not save the gradient document.");
    QFile saved(saved_path);
    require(saved.open(QIODevice::ReadOnly), "Could not inspect the saved gradient document.");
    const QJsonObject saved_json = QJsonDocument::fromJson(saved.readAll()).object();
    saved.close();
    const auto stored_operations = saved_json.value("layers").toArray().at(1)
        .toObject().value("operations").toArray();
    require(saved_json.value("version").toInt() == 15,
            "A saved gradient document must use .cimg version 15.");
    require(stored_operations.size() == 1 &&
            stored_operations.at(0).toObject().contains("clip_path"),
            "The v15 gradient operation must persist its optional selection clip.");
    ImageDocumentSession reopened;
    require(reopened.openDocument(saved_path, &error) &&
            reopened.data() == session.data() && reopened.renderedImage() == applied,
            "Version 15 must preserve the gradient pixels when reopened.");

    const QString v14_path = root + QStringLiteral("/legacy-v14.cimg");
    ImageDocumentSession legacy;
    require(legacy.createCanvas(QSize(3, 2), Qt::transparent, &error) &&
            legacy.applyBucketFill(QPoint(1, 1), QColor(10, 80, 160), 0, &error) &&
            legacy.saveDocument(v14_path, &error),
            "Could not create a version 14 compatibility fixture.");
    QFile legacy_file(v14_path);
    require(legacy_file.open(QIODevice::ReadOnly), "Could not read the compatibility fixture.");
    QJsonObject legacy_json = QJsonDocument::fromJson(legacy_file.readAll()).object();
    legacy_file.close();
    legacy_json.insert("version", 14);
    require(legacy_file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
            legacy_file.write(QJsonDocument(legacy_json).toJson()) >= 0,
            "Could not mark the compatibility fixture as version 14.");
    legacy_file.close();
    ImageDocumentSession v14_reopened;
    require(v14_reopened.openDocument(v14_path, &error) &&
            v14_reopened.renderedImage() == legacy.renderedImage(),
            "Version 15 must continue reading version 14 documents.");

    require(session.addLayerMask(session.selectedLayerId()),
            "Could not add a mask for the gradient test.");
    require(session.applyLinearGradient(
                start, end, QColor(255, 0, 0), &error, {}, true),
            "Applying a gradient to the active mask should create an operation.");
    const auto& mask_operations = session.data().layers.at(1).mask->operations;
    const QColor mask_color = mask_operations.back().linear_gradient.color;
    require(mask_operations.back().kind == OperationKind::LinearGradient &&
            mask_color.red() == mask_color.green() && mask_color.green() == mask_color.blue(),
            "Mask gradients must be stored in grayscale.");
    const QString mask_path = root + QStringLiteral("/gradient-mask-v15.cimg");
    require(session.saveDocument(mask_path, &error),
            "Could not save the mask gradient document.");
    ImageDocumentSession reopened_mask;
    require(reopened_mask.openDocument(mask_path, &error) &&
            reopened_mask.data() == session.data() &&
            reopened_mask.renderedImage() == session.renderedImage(),
            "A grayscale mask gradient must survive v15 save and reopen.");
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) {
        std::cerr << "Could not create a temporary test directory.\n";
        return 1;
    }
    try {
        testGradientPixelsAndSelection();
        testSessionPreviewHistoryMaskAndPersistence(temporary.path());
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
