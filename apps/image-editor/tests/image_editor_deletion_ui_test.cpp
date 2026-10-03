#include "image_editor_window.h"
#include "image_canvas.h"
#include "layer_panel.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QFile>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QSettings>
#include <QSpinBox>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <iostream>

using namespace image_editor;

bool testDeletionUi(const QString& root) {
    const auto check = [](bool ok, const char* cause) {
        if (!ok) std::cerr << "Deletion UI: " << cause << '\n';
        return ok;
    };
    const QString background = root + "/delete-ui-background.png";
    const QString first_path = root + "/delete-ui-first.png";
    const QString second_path = root + "/delete-ui-second.png";
    const QString doc = root + "/delete-ui.cimg";
    const QString output = root + "/delete-ui-output.png";
    QImage bg(64, 64, QImage::Format_ARGB32); bg.fill(Qt::transparent);
    QImage photo(32, 32, QImage::Format_ARGB32); photo.fill(Qt::red);
    if (!bg.save(background) || !photo.save(first_path)) return false;
    photo.fill(Qt::green);
    if (!photo.save(second_path)) return false;
    const auto bytes = [](const QString& path) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return QByteArray{};
        return file.readAll();
    };
    const QByteArray original = bytes(second_path);
    ImageDocumentSession fixture;
    if (!fixture.openImage(background) ||
        !fixture.importRasterImages(prepareRasterImport({first_path, second_path}).images)) return false;
    const QString second_id = fixture.selectedLayerId();
    if (!fixture.addLayerMask(second_id) ||
        !fixture.applyPaintStroke({QPointF(3, 3)}, Qt::blue, 2)) return false;
    const QString group = fixture.groupLayers({fixture.data().layers[2].id, second_id});
    if (group.isEmpty() || !fixture.selectLayer(second_id) || !fixture.saveDocument(doc)) return false;
    const auto baseline = fixture.data();

    ImageEditorWindow window;
    window.resize(1100, 800);
    window.show();
    window.activateWindow();
    auto* contextual = window.findChild<QAction*>("deleteSelectedShapeAction");
    auto* objects = window.findChild<QAction*>("deleteSelectedObjectsAction");
    auto* button = window.findChild<QPushButton*>("deleteSelectedShapeButton");
    auto* layers_button = window.findChild<QToolButton*>("deleteImageLayerButton");
    auto* tree = window.findChild<QTreeWidget*>("imageLayerTree");
    auto* canvas = window.findChild<ImageCanvas*>();
    auto* selection = window.findChild<QAction*>("selectShapesToolAction");
    auto* save = window.findChild<QAction*>("saveDocumentAction");
    auto* undo = window.findChild<QAction*>("undoAction");
    auto* redo = window.findChild<QAction*>("redoAction");
    auto* menu = window.findChild<QMenu*>("imageLayerContextMenu");
    if (!check(contextual && !contextual->isEnabled() &&
        contextual->shortcut() == QKeySequence(Qt::Key_Delete) && objects && button &&
        layers_button && layers_button->text() == "Delete" && !layers_button->accessibleName().isEmpty() &&
        tree && canvas == nullptr && selection && save && undo && redo && menu, "Missing deletion controls/defaults.")) return false;
    if (!window.openLinkedImage(background, doc, output)) return false;
    canvas = window.findChild<ImageCanvas*>();
    if (!canvas) return false;
    selection->setChecked(true);
    QCoreApplication::processEvents();
    const auto row = [&](const QString& name) -> QTreeWidgetItem* {
        for (QTreeWidgetItemIterator it(tree); *it; ++it) if ((*it)->text(0) == name) return *it;
        return nullptr;
    };
    const auto choose_image = [&]() {
        auto* item = row("delete-ui-second.png");
        if (!item) return false;
        tree->scrollToItem(item);
        const QRect rect = tree->visualItemRect(item);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
            QPoint(rect.left() + 16, rect.center().y()));
        canvas->setFocus();
        QCoreApplication::processEvents();
        return objects->isEnabled() && contextual->isEnabled();
    };
    ImageDocumentSession persisted;
    const auto saved_matches = [&](const ImageDocumentData& expected) {
        save->trigger();
        return persisted.openDocument(doc) && persisted.data() == expected &&
            QImage(output) == persisted.renderedImage();
    };
    auto without_image = baseline;
    without_image.layers[3].operations.removeFirst();
    if (!check(choose_image() && button->isVisible() && button->isEnabled(),
        "Imported image has no visible delete button.")) return false;
    // Delete during a drag must cancel its preview and prevent a release commit.
    const auto point = [&](QPointF p) {
        return (QPointF(canvas->rect().center()) + (p - QPointF(32, 32)) * canvas->zoomFactor()).toPoint();
    };
    QSignalSpy geometry(canvas, &ImageCanvas::objectsGeometryChanged);
    QSignalSpy preview(canvas, &ImageCanvas::objectsPreviewRequested);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, point({32, 32}));
    QTest::mouseMove(canvas, point({36, 32}));
    QTest::keyClick(canvas, Qt::Key_Delete);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, point({36, 32}));
    if (!check(preview.count() > 0 && geometry.isEmpty() && !objects->isEnabled() &&
        row("delete-ui-second.png") && saved_matches(without_image),
        "Canvas Delete lost the layer/mask, left a preview, or committed the drag.")) return false;
    undo->trigger();
    if (!check(saved_matches(baseline), "Canvas deletion undo failed.")) return false;
    redo->trigger();
    if (!check(saved_matches(without_image), "Canvas deletion redo failed.")) return false;
    undo->trigger();
    if (!choose_image()) return false;
    button->click();
    if (!check(saved_matches(without_image), "Selection delete button failed.")) return false;
    undo->trigger();
    if (!choose_image()) return false;
    objects->trigger();
    if (!check(saved_matches(without_image), "Edit menu object command failed.")) return false;
    undo->trigger();

    // Delete in fields must edit characters, including a panel rename editor.
    auto* rename = window.findChild<QToolButton*>("renameImageLayerButton");
    if (!choose_image() || !rename) return false;
    rename->click();
    QCoreApplication::processEvents();
    auto* editor = qobject_cast<QLineEdit*>(QApplication::focusWidget());
    if (!check(editor && !contextual->isEnabled(), "Rename did not protect Delete.")) return false;
    editor->setText("Layer"); editor->setCursorPosition(0);
    QTest::keyClick(editor, Qt::Key_Delete);
    if (!check(editor->text() == "ayer" && tree->topLevelItemCount() == 3,
        "Delete removed a layer during rename.")) return false;
    QTest::keyClick(editor, Qt::Key_Escape);
    QCoreApplication::processEvents();
    auto* paint = window.findChild<QAction*>("paintToolAction");
    paint->setChecked(true);
    auto* size = window.findChild<QSpinBox*>("paintBrushSizeSpinBox");
    if (!check(size != nullptr, "Brush field missing.")) return false;
    size->setFocus();
    size->selectAll();
    const int count = tree->topLevelItemCount();
    QTest::keyClick(size, Qt::Key_Delete);
    if (!check(!contextual->isEnabled() && tree->topLevelItemCount() == count,
        "Delete removed content from a numeric field.")) return false;
    selection->setChecked(true);
    ImageTextData text;
    text.content = "AB"; text.position = {10, 10};
    canvas->beginTextEditing(text, false);
    auto* text_editor = canvas->findChild<QPlainTextEdit*>();
    if (!text_editor) return false;
    text_editor->selectAll();
    QTest::keyClick(text_editor, Qt::Key_Delete);
    if (!check(text_editor->toPlainText().isEmpty() && !contextual->isEnabled() &&
        tree->topLevelItemCount() == count, "Delete removed a layer during text editing.")) return false;
    QTest::keyClick(text_editor, Qt::Key_Escape);

    // A panel Delete removes the whole selected layer, not just its raster.
    if (!choose_image()) return false;
    tree->setFocus(); QCoreApplication::processEvents();
    QTest::keyClick(tree, Qt::Key_Delete);
    if (!check(!row("delete-ui-second.png") && undo->isEnabled(), "Panel Delete did not remove its layer.")) return false;
    undo->trigger();
    if (!check(row("delete-ui-second.png") && saved_matches(baseline), "Panel layer undo failed.")) return false;
    if (!choose_image()) return false;
    layers_button->click();
    if (!check(!row("delete-ui-second.png"), "Panel delete button failed.")) return false;
    undo->trigger();
    if (!choose_image()) return false;
    bool opened = false;
    const auto menu_connection = QObject::connect(menu, &QMenu::aboutToShow, &window, [&]() {
        auto* action = menu->findChild<QAction*>("deleteLayerContextAction");
        opened = action && action->isVisible() && action->isEnabled() && action->text() == "Delete Layer";
        QTimer::singleShot(0, menu, [menu, action]() { if (action) action->trigger(); menu->close(); });
    });
    auto* item = row("delete-ui-second.png");
    const QPoint pos = tree->visualItemRect(item).center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, pos, tree->viewport()->mapToGlobal(pos));
    QApplication::sendEvent(tree->viewport(), &event);
    QObject::disconnect(menu_connection);
    if (!check(opened && !row("delete-ui-second.png"), "Delete Layer context menu failed.")) return false;
    // The offscreen platform does not reactivate the owner after QMenu::exec.
    window.activateWindow();
    QCoreApplication::processEvents();
    undo->trigger();
    // Group + child + Background selection: delete once, keep Background.
    tree->clearSelection();
    auto* group_row = row("Group 1");
    tree->setCurrentItem(group_row, 0, QItemSelectionModel::ClearAndSelect);
    row("delete-ui-second.png")->setSelected(true);
    row("Background")->setSelected(true);
    tree->setFocus(); QCoreApplication::processEvents();
    if (!check(tree->selectedItems().size() == 3 && contextual->isEnabled(),
        "Mixed selection/shortcut was not ready.")) {
        std::cerr << "Selected=" << tree->selectedItems().size() << " enabled=" << contextual->isEnabled()
            << " focus=" << (QApplication::focusWidget() ? QApplication::focusWidget()->objectName().toStdString() : "none") << '\n';
        for (auto* selected : tree->selectedItems()) std::cerr << selected->text(0).toStdString() << '\n';
        return false;
    }
    QTest::keyClick(tree, Qt::Key_Delete);
    if (!check(tree->topLevelItemCount() == 2 && !row("Group 1") && row("Background"),
        "Mixed deletion removed Background or retained group children.")) {
        std::cerr << "Rows=" << tree->topLevelItemCount() << " group=" << bool(row("Group 1"))
            << " background=" << bool(row("Background")) << " selected=" << tree->selectedItems().size()
            << " focus=" << (QApplication::focusWidget() ? QApplication::focusWidget()->objectName().toStdString() : "none") << '\n';
        return false;
    }
    undo->trigger();
    if (!check(row("Group 1") && row("Group 1")->childCount() == 2 && saved_matches(baseline),
        "Mixed deletion undo was not atomic.")) return false;
    tree->setCurrentItem(row("Background"), 0, QItemSelectionModel::ClearAndSelect);
    tree->setFocus(); QCoreApplication::processEvents();
    if (!check(!contextual->isEnabled() && !layers_button->isEnabled(), "Background deletion enabled.")) return false;
    QTest::keyClick(tree, Qt::Key_Delete);
    tree->clearSelection(); QCoreApplication::processEvents();
    if (!check(!contextual->isEnabled() && !layers_button->isEnabled() && tree->topLevelItemCount() == 3 &&
        bytes(second_path) == original, "Empty deletion or original file preservation failed.")) return false;

    // Unavailable imported files can still be removed through the panel.
    if (!QFile::remove(second_path) || !window.openLinkedImage(background, doc, output)) return false;
    item = row("delete-ui-second.png");
    if (!check(item && item->toolTip(0).contains("Relink Image"), "Missing source fixture failed.")) return false;
    tree->setCurrentItem(item, 0, QItemSelectionModel::ClearAndSelect);
    tree->setFocus(); QCoreApplication::processEvents();
    QTest::keyClick(tree, Qt::Key_Delete);
    save->trigger();
    if (!check(persisted.openDocument(doc) && persisted.rasterSourceProblems().isEmpty() &&
        QImage(output) == persisted.renderedImage(), "Missing layer deletion/save/publication failed.")) return false;
    // Preserve an earlier Delete assignment when introducing the new default.
    QSettings settings;
    settings.beginGroup("ImageEditor/KeyboardShortcuts");
    settings.setValue("paintToolAction", QKeySequence(Qt::Key_Delete).toString(QKeySequence::PortableText));
    settings.remove("deleteSelectedShapeAction");
    settings.endGroup(); settings.sync();
    {
        ImageEditorWindow reopened;
        auto* kept = reopened.findChild<QAction*>("paintToolAction");
        auto* migrated = reopened.findChild<QAction*>("deleteSelectedShapeAction");
        if (!check(kept->shortcut() == QKeySequence(Qt::Key_Delete) && migrated->shortcut().isEmpty() &&
            migrated->property("defaultShortcut").toString() == QKeySequence(Qt::Key_Delete).toString(QKeySequence::PortableText),
            "New Delete default displaced an existing preference.")) return false;
    }
    settings.beginGroup("ImageEditor/KeyboardShortcuts");
    settings.remove("paintToolAction");
    settings.setValue("deleteSelectedShapeAction", QString{});
    settings.endGroup(); settings.sync();
    {
        ImageEditorWindow reopened;
        if (!check(reopened.findChild<QAction*>("deleteSelectedShapeAction")->shortcut().isEmpty(),
            "Explicitly cleared Delete preference was lost.")) return false;
    }
    settings.beginGroup("ImageEditor/KeyboardShortcuts");
    settings.remove("deleteSelectedShapeAction");
    settings.endGroup(); settings.sync();
    return true;
}
