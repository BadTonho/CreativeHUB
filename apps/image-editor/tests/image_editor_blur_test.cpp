#include "rendering/image_blur_stroke.h"
#include "image_document_session.h"

#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainterPath>
#include <QTemporaryDir>

#include <iostream>
#include <stdexcept>

using namespace image_editor;

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void testBlurPixelsAlphaAndSelection() {
    QImage split(17, 9, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < split.height(); ++y) {
        for (int x = 0; x < split.width(); ++x)
            split.setPixelColor(x, y, x < 8 ? QColor(240, 20, 30) : QColor(20, 40, 230));
    }
    const QImage original = split;
    ImageBlurStrokeData stroke;
    stroke.points = {QPointF(8.5, 4.5)};
    stroke.diameter = 7;
    stroke.radius = 2;
    bool changed = false;
    QString error;
    require(ImageBlurStrokeRenderer::apply(&split, stroke, &changed, &error) && changed,
            "A blur stroke across an edge must change its target pixels.");
    require(split.pixelColor(8, 4) != original.pixelColor(8, 4) &&
            split.pixelColor(0, 0) == original.pixelColor(0, 0),
            "Blur must soften the edge while leaving pixels outside the brush untouched.");

    QImage transparent(9, 9, QImage::Format_ARGB32_Premultiplied);
    transparent.fill(Qt::transparent);
    transparent.setPixelColor(4, 4, QColor(255, 30, 10, 255));
    stroke.points = {QPointF(4.5, 4.5)};
    stroke.diameter = 9;
    stroke.radius = 1;
    require(ImageBlurStrokeRenderer::apply(&transparent, stroke, &changed, &error) && changed,
            "Blurring a semitransparent edge must change RGBA pixels.");
    const auto* blurred_row = reinterpret_cast<const QRgb*>(transparent.constScanLine(4));
    const QRgb blurred = blurred_row[4];
    require(qAlpha(blurred) > 0 && qAlpha(blurred) < 255 &&
            qRed(blurred) >= qAlpha(blurred) - 1 &&
            qRed(blurred) <= qAlpha(blurred) &&
            qGreen(blurred) <= qRed(blurred) / 4 &&
            qBlue(blurred) <= qRed(blurred) / 8,
            "Premultiplied filtering must preserve edge color without transparent RGB halos.");

    QImage unchanged(9, 9, QImage::Format_ARGB32_Premultiplied);
    unchanged.fill(QColor(40, 80, 120, 255));
    stroke.radius = 0;
    const QImage before_zero_radius = unchanged;
    require(ImageBlurStrokeRenderer::apply(
                &unchanged, stroke, &changed, &error) && !changed &&
            unchanged == before_zero_radius,
            "A zero radius must be a successful no-op.");

    QImage clipped = original;
    stroke.points = {QPointF(8.5, 4.5)};
    stroke.diameter = 17;
    stroke.radius = 2;
    QPainterPath selection;
    selection.addRect(QRectF(0, 0, 8, clipped.height()));
    stroke.clipping_path = selection;
    require(ImageBlurStrokeRenderer::apply(&clipped, stroke, &changed, &error) && changed &&
            clipped.pixelColor(8, 4) == original.pixelColor(8, 4) &&
            clipped.pixelColor(3, 4) != original.pixelColor(3, 4),
            "Area Selection must clip blur writes while still blurring within its boundary.");
}

void testSessionPreviewHistoryMaskAndPersistence(const QString& root) {
    ImageDocumentSession session;
    QString error;
    require(session.createCanvas(QSize(16, 8), Qt::transparent, &error),
            "Could not create the synthetic blur canvas.");
    require(session.applyPaintStroke(
                {QPointF(1.5, 3.5), QPointF(6.0, 3.5)}, QColor(230, 35, 25), 5, &error) &&
            session.applyPaintStroke(
                {QPointF(10.0, 3.5), QPointF(14.0, 3.5)}, QColor(20, 45, 225), 5, &error),
            "Could not prepare the synthetic layer edge.");
    const QImage before = session.renderedImage();
    const QVector<QPointF> points{QPointF(8.0, 3.5)};
    const QImage preview = session.renderedImageWithBlurStroke(points, 7, 2);
    require(!preview.isNull() && preview != before && session.renderedImage() == before,
            "Blur preview must show a changed composition without modifying the document.");
    require(!session.applyBlurStroke(points, 7, 0, &error) && error.isEmpty() &&
            session.renderedImage() == before,
            "A zero-radius blur must not create an operation or history edit.");
    require(session.applyBlurStroke(points, 7, 2, &error),
            "Applying blur to the active layer should create one operation.");
    const QImage applied = session.renderedImage();
    const qsizetype layer_index = 1;
    require(applied == preview &&
            session.data().layers.at(layer_index).operations.back().kind == OperationKind::BlurStroke,
            "The preview and committed blur pixels must match and persist as one operation.");
    require(session.undo() && session.renderedImage() == before &&
            session.redo() && session.renderedImage() == applied,
            "Undo and redo must remove and restore the blur operation.");

    const QString saved_path = root + QStringLiteral("/blur-v16.cimg");
    require(session.saveDocument(saved_path, &error), "Could not save the blur document.");
    QFile saved(saved_path);
    require(saved.open(QIODevice::ReadOnly), "Could not inspect the saved blur document.");
    QJsonObject saved_json = QJsonDocument::fromJson(saved.readAll()).object();
    saved.close();
    require(saved_json.value("version").toInt() == 16,
            "A saved blur document must use .cimg version 16.");
    const auto encoded_operation = saved_json.value("layers").toArray().at(layer_index)
        .toObject().value("operations").toArray().last().toObject();
    require(encoded_operation.value("kind").toString() == QStringLiteral("blur_stroke") &&
            encoded_operation.value("diameter").toInt() == 7 &&
            encoded_operation.value("radius").toInt() == 2,
            "Version 16 must serialize blur settings and its ordered operation.");
    ImageDocumentSession reopened;
    require(reopened.openDocument(saved_path, &error) &&
            reopened.data() == session.data() && reopened.renderedImage() == applied,
            "Version 16 must preserve blur operations and pixels when reopened.");

    require(session.addLayerMask(session.selectedLayerId()),
            "Could not add a synthetic layer mask.");
    require(session.applyLayerMaskStroke(
                {QPointF(5.5, 3.5)}, Qt::black, 7, &error),
            "Could not prepare grayscale mask pixels.");
    require(session.applyBlurStroke({QPointF(5.5, 3.5)}, 7, 2, &error, {}, true),
            "Blur should be applicable to the selected layer mask.");
    const auto& mask_operations = session.data().layers.at(layer_index).mask->operations;
    require(mask_operations.back().kind == OperationKind::BlurStroke,
            "Mask blur must be recorded as an ordered blur operation.");
    const QImage mask_preview = session.renderedLayerMaskThumbnails(QSize(32, 32))
        .value(session.selectedLayerId());
    require(!mask_preview.isNull(), "The edited mask must render a thumbnail.");
    for (int y = 0; y < mask_preview.height(); ++y) {
        for (int x = 0; x < mask_preview.width(); ++x) {
            const QColor pixel = mask_preview.pixelColor(x, y);
            if (pixel.alpha() > 0)
                require(pixel.red() == pixel.green() && pixel.green() == pixel.blue(),
                        "Blurred mask pixels must remain grayscale.");
        }
    }

    ImageDocumentSession legacy;
    require(legacy.createCanvas(QSize(3, 2), Qt::transparent, &error) &&
            legacy.saveDocument(root + QStringLiteral("/legacy-v15-temp.cimg"), &error),
            "Could not create a legacy document fixture.");
    QFile legacy_file(root + QStringLiteral("/legacy-v15-temp.cimg"));
    require(legacy_file.open(QIODevice::ReadOnly), "Could not read the legacy fixture.");
    QJsonObject legacy_json = QJsonDocument::fromJson(legacy_file.readAll()).object();
    legacy_file.close();
    legacy_json.insert(QStringLiteral("version"), 15);
    const QString legacy_path = root + QStringLiteral("/legacy-v15.cimg");
    QFile v15(legacy_path);
    require(v15.open(QIODevice::WriteOnly) &&
            v15.write(QJsonDocument(legacy_json).toJson()) >= 0,
            "Could not write the version 15 compatibility fixture.");
    v15.close();
    ImageDocumentSession legacy_reopened;
    require(legacy_reopened.openDocument(legacy_path, &error) &&
            legacy_reopened.data().canvas_size == QSize(3, 2),
            "The editor must continue reading version 15 documents.");
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    try {
        testBlurPixelsAlphaAndSelection();
        testSessionPreviewHistoryMaskAndPersistence(temporary.path());
    } catch (const std::exception& error) {
        std::cerr << "Image Editor blur regression failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
