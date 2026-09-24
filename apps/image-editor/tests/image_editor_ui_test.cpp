#include "image_canvas.h"
#include "image_editor_window.h"

#include <QAction>
#include <QApplication>
#include <QImage>
#include <QImageWriter>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QSignalSpy>
#include <QUuid>

#include <iostream>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Creative Suite"));
    QCoreApplication::setApplicationName(
        QStringLiteral("Image Editor UI Tests %1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;

    image_editor::ImageEditorWindow window;
    window.show();
    QCoreApplication::processEvents();

    auto* canvas = window.findChild<image_editor::ImageCanvas*>();
    if (canvas == nullptr || !window.isVisible()) {
        std::cerr << "The Image Editor window or canvas was not created.\n";
        return 1;
    }
    const QString source_path = temporary.filePath(QStringLiteral("ui-test.png"));
    QImage source(100, 80, QImage::Format_ARGB32);
    source.fill(Qt::blue);
    QImageWriter writer(source_path, "png");
    if (!writer.write(source) || !window.openImagePath(source_path)) {
        std::cerr << "The window could not load the integration test image.\n";
        return 1;
    }
    if (canvas->zoomFactor() <= 0.0) {
        std::cerr << "The image canvas did not calculate a fit scale.\n";
        return 1;
    }

    canvas->setCropMode(true);
    QSignalSpy crop_spy(canvas, &image_editor::ImageCanvas::cropSelected);
    const QPoint crop_center = canvas->rect().center();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, crop_center - QPoint(20, 20));
    QTest::mouseMove(canvas, crop_center + QPoint(20, 20));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, crop_center + QPoint(20, 20));
    if (crop_spy.size() != 1 || qvariant_cast<QRect>(crop_spy.takeFirst().at(0)).isEmpty()) {
        std::cerr << "The crop gesture did not emit a valid image-space rectangle.\n";
        return 1;
    }

    auto* rotate_action = window.findChild<QAction*>(QStringLiteral("rotateRightAction"));
    auto* undo_action = window.findChild<QAction*>(QStringLiteral("undoAction"));
    if (rotate_action == nullptr || undo_action == nullptr) {
        std::cerr << "Expected edit actions were not exposed by the window.\n";
        return 1;
    }
    rotate_action->trigger();
    if (!window.windowTitle().startsWith('*')) {
        std::cerr << "A user edit did not update the window's dirty projection.\n";
        return 1;
    }
    undo_action->trigger();
    if (!window.windowTitle().startsWith('*')) {
        std::cerr << "Undoing one action unexpectedly cleared earlier edits.\n";
        return 1;
    }
    undo_action->trigger();
    if (window.windowTitle().startsWith('*')) {
        std::cerr << "Undo did not project the clean baseline into the window.\n";
        return 1;
    }
    return 0;
}
