#include "rendering/image_bucket_fill.h"
#include "image_document_session.h"
#include "image_document_store.h"

#include <QFile>
#include <QGuiApplication>
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

void testExactFourWayFillAndTolerance() {
    QImage diagonal(2, 2, QImage::Format_ARGB32);
    diagonal.fill(Qt::transparent);
    diagonal.setPixelColor(1, 0, Qt::red);
    diagonal.setPixelColor(0, 1, Qt::red);
    ImageBucketFillData fill;
    fill.seed = QPoint(0, 0);
    fill.color = QColor(20, 120, 220, 255);
    bool changed = false;
    QString error;
    require(ImageBucketFill::apply(&diagonal, fill, &changed, &error) && changed,
            "An exact fill should change the seed pixel.");
    require(diagonal.pixelColor(0, 0) == fill.color &&
            diagonal.pixelColor(1, 1).alpha() == 0 &&
            diagonal.pixelColor(1, 0) == QColor(Qt::red),
            "Flood fill must use four-way connectivity and preserve diagonal regions.");

    QImage tolerance(3, 1, QImage::Format_ARGB32);
    tolerance.setPixelColor(0, 0, QColor(40, 40, 40, 200));
    tolerance.setPixelColor(1, 0, QColor(45, 45, 45, 195));
    tolerance.setPixelColor(2, 0, QColor(45, 45, 45, 194));
    fill.seed = QPoint(0, 0);
    fill.color = QColor(200, 30, 80, 255);
    fill.tolerance = 5;
    require(ImageBucketFill::apply(&tolerance, fill, &changed, &error) && changed,
            "A tolerance fill should render successfully.");
    const auto first = tolerance.pixelColor(0, 0);
    const auto second = tolerance.pixelColor(1, 0);
    const auto third = tolerance.pixelColor(2, 0);
    if (first != fill.color || second != fill.color || third.alpha() != 194 ||
        third.red() < 44 || third.red() > 46) {
        std::cerr << "Tolerance colors after fill: " << first.name(QColor::HexArgb).toStdString()
                  << ", " << second.name(QColor::HexArgb).toStdString()
                  << ", " << third.name(QColor::HexArgb).toStdString() << '\n';
        throw std::runtime_error("Tolerance must include the inclusive per-channel RGBA boundary.");
    }
}

void testSelectionClipsFill() {
    QImage image(4, 1, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    ImageBucketFillData fill;
    fill.seed = QPoint(0, 0);
    fill.color = Qt::green;
    QPainterPath selection;
    selection.addRect(QRectF(0, 0, 2, 1));
    fill.clipping_path = selection;
    bool changed = false;
    require(ImageBucketFill::apply(&image, fill, &changed) && changed &&
            image.pixelColor(0, 0) == QColor(Qt::green) &&
            image.pixelColor(1, 0) == QColor(Qt::green) &&
            image.pixelColor(2, 0).alpha() == 0 &&
            image.pixelColor(3, 0).alpha() == 0,
            "The selection must bound both flood traversal and fill output.");
}

void testSessionUndoMaskAndV14RoundTrip(const QString& root) {
    ImageDocumentSession session;
    QString error;
    require(session.createCanvas(QSize(4, 3), Qt::transparent, &error),
            "Could not create the synthetic fill canvas.");
    const QColor paint_color(25, 130, 220, 255);
    require(session.applyBucketFill(QPoint(1, 1), paint_color, 0, &error),
            "A fill on the editable layer should create an operation.");
    const QImage painted = session.renderedImage();
    require(painted.pixelColor(3, 2) == paint_color,
            "The layer fill should cover the connected transparent canvas.");

    require(!session.applyBucketFill(QPoint(1, 1), paint_color, 0, &error) && error.isEmpty(),
            "A same-color fill should not add a history edit or report an error.");
    require(session.undo() && session.renderedImage().pixelColor(1, 1).alpha() == 0,
            "Undo should remove the single bucket-fill operation.");
    require(session.redo() && session.renderedImage() == painted,
            "Redo should restore the filled layer pixels.");

    const QString layer_id = session.selectedLayerId();
    require(session.addLayerMask(layer_id), "Could not add a mask to the editable layer.");
    const QColor mask_input(0, 255, 0, 255);
    const int mask_gray = qGray(mask_input.rgb());
    require(session.applyBucketFill(QPoint(1, 1), mask_input, 0, &error, {}, true),
            "A fill on the selected mask should create an operation.");
    const auto& mask = session.data().layers.at(1).mask;
    require(mask.has_value() && mask->operations.size() == 1 &&
            mask->operations.front().kind == OperationKind::BucketFill &&
            qRed(mask->operations.front().bucket_fill.color.rgb()) ==
                qGreen(mask->operations.front().bucket_fill.color.rgb()) &&
            qGreen(mask->operations.front().bucket_fill.color.rgb()) ==
                qBlue(mask->operations.front().bucket_fill.color.rgb()),
            "Mask fill color must be stored as grayscale.");
    require(session.renderedImage().pixelColor(1, 1).alpha() == mask_gray,
            "A colored mask fill should apply its grayscale value to layer alpha.");
    require(session.undo() && session.renderedImage() == painted,
            "Undo should restore the layer after a mask fill.");
    require(session.redo() && session.renderedImage().pixelColor(1, 1).alpha() == mask_gray,
            "Redo should restore the mask fill.");

    const QString path = root + QStringLiteral("/bucket-fill-v16.cimg");
    require(session.saveDocument(path, &error), "Could not save the bucket-fill document.");
    QFile file(path);
    require(file.open(QIODevice::ReadOnly), "Could not inspect the saved document.");
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    require(json.value("version").toInt() == ImageDocumentStore::kCurrentDocumentVersion,
            "Saving a bucket fill must write the current .cimg version.");
    ImageDocumentSession reopened;
    require(reopened.openDocument(path, &error), "Could not reopen the v16 fill document.");
    require(reopened.renderedImage() == session.renderedImage() &&
            reopened.data().layers.at(1).mask->operations.front().kind == OperationKind::BucketFill,
            "The layer and mask fill operations must survive saving and reopening.");
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
        testExactFourWayFillAndTolerance();
        testSelectionClipsFill();
        testSessionUndoMaskAndV14RoundTrip(temporary.path());
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
