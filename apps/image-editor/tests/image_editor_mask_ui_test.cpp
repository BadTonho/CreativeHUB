#include "image_editor_window.h"
#include "image_canvas.h"

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>
#include <QTreeWidget>
#include <iostream>

bool testLayerMasksUi(const QString& directory) {
    const QString source = directory + "/mask-ui-source.png";
    const QString document = directory + "/mask-ui.cimg";
    const QString output = directory + "/mask-ui.png";
    QImage transparent(32, 32, QImage::Format_ARGB32);
    transparent.fill(Qt::transparent);
    if (!transparent.save(source)) return false;
    image_editor::ImageDocumentSession fixture;
    QString error;
    if (!fixture.openImage(source, &error) ||
        !fixture.applyPaintStroke({QPointF(16, 16)}, Qt::red, 100, &error) ||
        !fixture.saveDocument(document, &error)) return false;
    image_editor::ImageEditorWindow window;
    window.resize(1000, 700);
    window.show();
    if (!window.openLinkedImage(source, document, output)) return false;
    QCoreApplication::processEvents();
    auto* canvas = window.findChild<image_editor::ImageCanvas*>();
    auto* tree = window.findChild<QTreeWidget*>("imageLayerTree");
    auto* add = window.findChild<QAction*>("addLayerMaskAction");
    auto* remove = window.findChild<QAction*>("removeLayerMaskAction");
    auto* enable = window.findChild<QAction*>("enableLayerMaskAction");
    auto* paint = window.findChild<QAction*>("paintToolAction");
    auto* erase = window.findChild<QAction*>("eraserToolAction");
    auto* save = window.findChild<QAction*>("saveDocumentAction");
    auto* undo = window.findChild<QAction*>("undoAction");
    auto* redo = window.findChild<QAction*>("redoAction");
    auto* menu = window.findChild<QMenu*>("imageLayerContextMenu");
    if (canvas == nullptr || tree == nullptr || add == nullptr || remove == nullptr ||
        enable == nullptr || paint == nullptr || erase == nullptr || save == nullptr ||
        undo == nullptr || redo == nullptr || menu == nullptr) return false;
    const auto check_menu = [&](QTreeWidgetItem* item, bool allowed, bool has_mask, QAction* trigger = nullptr) {
        bool correct = false;
        const auto connection = QObject::connect(menu, &QMenu::aboutToShow, &window, [&]() {
            correct = add->isVisible() == (allowed && !has_mask) &&
                enable->isVisible() == (allowed && has_mask) &&
                remove->isVisible() == (allowed && has_mask);
            QTimer::singleShot(0, menu, [menu, trigger]() {
                menu->close();
                if (trigger != nullptr) trigger->trigger();
            });
        });
        emit tree->customContextMenuRequested(tree->visualItemRect(item).center());
        QObject::disconnect(connection);
        return correct;
    };
    if (!check_menu(tree->currentItem(), true, false, add)) return false;
    if (!canvas->maskEditing()) return false;
    if (!check_menu(tree->currentItem(), true, true)) return false;
    paint->trigger();
    canvas->setBrush(Qt::black, 6);
    QSignalSpy previews(canvas, &image_editor::ImageCanvas::maskPaintPreviewRequested);
    const QPoint center = canvas->rect().center();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, center);
    QTest::mouseMove(canvas, center + QPoint(2, 0));
    if (previews.count() < 1) return false;
    QTest::keyClick(canvas, Qt::Key_Escape);
    save->trigger();
    image_editor::ImageDocumentData data;
    if (!image_editor::ImageDocumentStore::loadDocument(document, &data, &error) ||
        !data.layers.back().mask.has_value() || !data.layers.back().mask->operations.isEmpty() ||
        QImage(output).pixelColor(16, 16).alpha() != 255) {
        std::cerr << "Cancelling mask painting committed a stroke.\n";
        return false;
    }
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, center);
    // Linked output remains unchanged until Save.
    if (QImage(output).pixelColor(16, 16).alpha() != 255) return false;
    save->trigger();
    if (!image_editor::ImageDocumentStore::loadDocument(document, &data, &error) ||
        data.layers.back().operations.size() != 1 || data.layers.back().mask->operations.size() != 1 ||
        QImage(output).pixelColor(16, 16).alpha() != 0) {
        std::cerr << "Mask painting changed content or was omitted from linked publication.\n";
        return false;
    }
    undo->trigger();
    save->trigger();
    if (QImage(output).pixelColor(16, 16).alpha() != 255) return false;
    redo->trigger();
    save->trigger();
    if (QImage(output).pixelColor(16, 16).alpha() != 0) return false;
    canvas->setBrush(Qt::white, 6);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, center);
    save->trigger();
    if (QImage(output).pixelColor(16, 16).alpha() != 255) return false;
    erase->trigger();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, center);
    save->trigger();
    if (QImage(output).pixelColor(16, 16).alpha() != 0) return false;

    // Clicking either thumbnail changes the editing target on the same row.
    auto row = tree->visualItemRect(tree->currentItem());
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(row.left() + 30, row.center().y()));
    if (canvas->maskEditing()) return false;
    row = tree->visualItemRect(tree->currentItem());
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(row.left() + 78, row.center().y()));
    if (!canvas->maskEditing()) return false;
    enable->setChecked(true);
    enable->trigger();
    save->trigger();
    if (QImage(output).pixelColor(16, 16).alpha() != 255) return false;
    enable->trigger();
    save->trigger();
    if (QImage(output).pixelColor(16, 16).alpha() != 0) return false;
    remove->trigger();
    if (canvas->maskEditing()) return false;
    save->trigger();
    if (QImage(output).pixelColor(16, 16).alpha() != 255) return false;
    if (undo == nullptr) return false;
    undo->trigger();
    save->trigger();
    if (!image_editor::ImageDocumentStore::loadDocument(document, &data, &error) ||
        !data.layers.back().mask.has_value()) return false;

    // Right-clicking another row while a mask is active must safely keep that row selected.
    row = tree->visualItemRect(tree->currentItem());
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(row.left() + 78, row.center().y()));
    auto* background_item = tree->topLevelItem(tree->topLevelItemCount() - 1);
    if (!check_menu(background_item, false, false) || tree->currentItem() != background_item ||
        canvas->maskEditing()) return false;
    add->trigger();
    save->trigger();
    if (!image_editor::ImageDocumentStore::loadDocument(document, &data, &error) ||
        data.layers.front().mask.has_value()) return false;
    QFile unchanged(source);
    return unchanged.open(QIODevice::ReadOnly) && QImage(source) == transparent;
}
