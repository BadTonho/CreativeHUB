#include "image_canvas.h"
#include "image_editor_window.h"
#include "layer_panel.h"
#include "tool_sidebar.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCursor>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QImage>
#include <QImageWriter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QRadioButton>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableWidget>
#include <QTreeWidget>
#include <QSlider>
#include <QSpinBox>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextLayout>
#include <QToolBar>
#include <QToolButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <QTest>
#include <QSignalSpy>
#include <QUuid>

#include <iostream>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

class ShortcutOverrideDeliveryProbe final : public QObject {
public:
    bool received = false;

protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::ShortcutOverride) received = true;
        return false;
    }
};

int layerRowCount(const QTreeWidget* tree) {
    return tree->topLevelItemCount();
}

QTreeWidgetItem* layerRowItem(QTreeWidget* tree, int row) {
    return tree->topLevelItem(row);
}

int darkPixelCount(const QImage& image) {
    const QImage pixels = image.convertToFormat(QImage::Format_ARGB32);
    int count = 0;
    for (int y = 0; y < pixels.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
        for (int x = 0; x < pixels.width(); ++x) {
            const QColor pixel = QColor::fromRgba(row[x]);
            if (pixel.alpha() > 200 && pixel.red() < 96 && pixel.green() < 96 &&
                pixel.blue() < 96) {
                ++count;
            }
        }
    }
    return count;
}

int selectionHighlightPixelCount(const QImage& image) {
    const QImage pixels = image.convertToFormat(QImage::Format_ARGB32);
    int count = 0;
    for (int y = 0; y < pixels.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(pixels.constScanLine(y));
        for (int x = 0; x < pixels.width(); ++x) {
            const QColor pixel = QColor::fromRgba(row[x]);
            if (pixel.alpha() > 200 && pixel.blue() > 140 && pixel.green() > 90 &&
                pixel.blue() > pixel.red() * 1.35 && pixel.green() > pixel.red() * 1.2) {
                ++count;
            }
        }
    }
    return count;
}

int currentLayerRow(const QTreeWidget* tree) {
    return tree->indexOfTopLevelItem(tree->currentItem());
}

void setCurrentLayerRow(QTreeWidget* tree, int row) {
    tree->setCurrentItem(tree->topLevelItem(row));
}

bool testLayerGroupsUi(const QString& directory) {
    const QString source_path = directory + QStringLiteral("/group-ui-source.png");
    QImage source(64, 48, QImage::Format_ARGB32);
    source.fill(Qt::transparent);
    if (!source.save(source_path, "PNG")) return false;

    image_editor::ImageEditorWindow window;
    window.show();
    if (!window.openImagePath(source_path)) return false;
    QCoreApplication::processEvents();
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("imageLayerTree"));
    auto* add_button = window.findChild<QToolButton*>(QStringLiteral("addImageLayerButton"));
    auto* group_button = window.findChild<QToolButton*>(
        QStringLiteral("groupSelectedLayersButton"));
    auto* delete_button = window.findChild<QToolButton*>(QStringLiteral("deleteImageLayerButton"));
    auto* ungroup_button = window.findChild<QToolButton*>(
        QStringLiteral("ungroupImageLayersButton"));
    auto* opacity = window.findChild<QSlider*>(QStringLiteral("imageLayerOpacitySlider"));
    auto* add_group_action = window.findChild<QAction*>(QStringLiteral("addImageGroupAction"));
    if (tree == nullptr || add_button == nullptr || group_button == nullptr ||
        delete_button == nullptr || ungroup_button == nullptr || opacity == nullptr ||
        add_group_action == nullptr || layerRowCount(tree) != 2) return false;

    add_group_action->trigger();
    QCoreApplication::processEvents();
    if (layerRowCount(tree) != 3 || tree->currentItem() == nullptr ||
        tree->currentItem()->text(0) != QStringLiteral("Group 1") ||
        !tree->currentItem()->data(0, Qt::UserRole + 2).toBool()) return false;

    add_button->click();
    add_button->click();
    QCoreApplication::processEvents();
    if (layerRowCount(tree) != 5 || layerRowItem(tree, 0)->text(0) != QStringLiteral("Layer 3") ||
        layerRowItem(tree, 1)->text(0) != QStringLiteral("Layer 2")) return false;

    const QRect top_row = tree->visualItemRect(layerRowItem(tree, 0));
    const QRect next_row = tree->visualItemRect(layerRowItem(tree, 1));
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(top_row.left() + 100, top_row.center().y()));
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      QPoint(next_row.left() + 100, next_row.center().y()));
    QCoreApplication::processEvents();
    if (!group_button->isEnabled() || tree->selectedItems().size() != 2) return false;
    group_button->click();
    QCoreApplication::processEvents();
    if (layerRowCount(tree) != 4 || layerRowItem(tree, 0)->text(0) != QStringLiteral("Group 2") ||
        layerRowItem(tree, 0)->childCount() != 2) return false;

    opacity->setValue(55);
    QCoreApplication::processEvents();
    if (opacity->value() != 55) return false;
    const QRect group_row = tree->visualItemRect(layerRowItem(tree, 0));
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(group_row.right() - 17, group_row.center().y()));
    QCoreApplication::processEvents();
    if (!layerRowItem(tree, 0)->data(0, Qt::AccessibleDescriptionRole).toString()
             .contains(QStringLiteral("Hidden"))) return false;

    setCurrentLayerRow(tree, 0);
    ungroup_button->click();
    QCoreApplication::processEvents();
    if (layerRowCount(tree) != 5 || layerRowItem(tree, 0)->text(0) != QStringLiteral("Layer 3") ||
        layerRowItem(tree, 1)->text(0) != QStringLiteral("Layer 2")) return false;

    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(tree->visualItemRect(layerRowItem(tree, 0)).left() + 100,
                             tree->visualItemRect(layerRowItem(tree, 0)).center().y()));
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::ControlModifier,
                      QPoint(tree->visualItemRect(layerRowItem(tree, 1)).left() + 100,
                             tree->visualItemRect(layerRowItem(tree, 1)).center().y()));
    QCoreApplication::processEvents();
    if (!group_button->isEnabled()) return false;
    group_button->click();
    QCoreApplication::processEvents();
    if (tree->currentItem() == nullptr || tree->currentItem()->text(0) != QStringLiteral("Group 2")) {
        return false;
    }
    delete_button->click();
    QCoreApplication::processEvents();
    return layerRowCount(tree) == 3 && layerRowItem(tree, 0)->text(0) == QStringLiteral("Group 1");
}

bool testLayerGroupContextMenu(const QString& directory) {
    const auto fail = [](const char* stage) {
        std::cerr << "Layer context menu check failed: " << stage << ".\n";
        return false;
    };
    const QString source_path = directory + QStringLiteral("/group-context-source.png");
    QImage source(64, 48, QImage::Format_ARGB32);
    source.fill(Qt::transparent);
    if (!source.save(source_path, "PNG")) return fail("create source image");

    image_editor::ImageEditorWindow window;
    window.show();
    if (!window.openImagePath(source_path)) return fail("open source image");
    QCoreApplication::processEvents();
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("imageLayerTree"));
    auto* add_button = window.findChild<QToolButton*>(QStringLiteral("addImageLayerButton"));
    auto* menu = window.findChild<QMenu*>(QStringLiteral("imageLayerContextMenu"));
    if (tree == nullptr || add_button == nullptr || menu == nullptr) {
        return fail("find panel controls");
    }
    auto* group_action = menu->findChild<QAction*>(
        QStringLiteral("groupSelectedLayersContextAction"));
    auto* ungroup_action = menu->findChild<QAction*>(
        QStringLiteral("ungroupLayerGroupContextAction"));
    auto* delete_group_action = menu->findChild<QAction*>(
        QStringLiteral("deleteLayerGroupContextAction"));
    if (group_action == nullptr || ungroup_action == nullptr || delete_group_action == nullptr) {
        return fail("find menu actions");
    }

    add_button->click();
    add_button->click();
    QCoreApplication::processEvents();
    if (layerRowCount(tree) != 4) return fail("create test layers");

    struct MenuState {
        bool group_visible = false;
        bool group_enabled = false;
        bool ungroup_visible = false;
        bool delete_group_visible = false;
        int selected_count = 0;
    };
    const auto invoke_context_menu = [&](int row,
                                        const QString& trigger_object_name,
                                        MenuState* state) {
        QTreeWidgetItem* item = layerRowItem(tree, row);
        if (item == nullptr) return fail("find context row");
        const QRect rect = tree->visualItemRect(item);
        if (!rect.isValid()) return fail("get context row geometry");
        bool opened = false;
        const auto connection = QObject::connect(menu, &QMenu::aboutToShow, &window,
            [&]() {
                opened = true;
                if (state != nullptr) {
                    state->group_visible = group_action->isVisible();
                    state->group_enabled = group_action->isEnabled();
                    state->ungroup_visible = ungroup_action->isVisible();
                    state->delete_group_visible = delete_group_action->isVisible();
                    state->selected_count = tree->selectedItems().size();
                }
                QAction* trigger = trigger_object_name.isEmpty()
                    ? nullptr : menu->findChild<QAction*>(trigger_object_name);
                QTimer::singleShot(0, menu, [menu, trigger]() {
                    if (trigger != nullptr && trigger->isVisible() && trigger->isEnabled()) {
                        trigger->trigger();
                    }
                    menu->close();
                });
            });
        const QPoint point(rect.left() + 100, rect.center().y());
        QContextMenuEvent context_event(QContextMenuEvent::Mouse, point,
                                        tree->viewport()->mapToGlobal(point));
        QApplication::sendEvent(tree->viewport(), &context_event);
        QObject::disconnect(connection);
        if (!opened) std::cerr << "Layer context menu did not open for row " << row << ".\n";
        return opened;
    };
    const auto click_row = [tree](int row, Qt::KeyboardModifiers modifiers) {
        QTreeWidgetItem* item = layerRowItem(tree, row);
        if (item == nullptr) return;
        const QRect rect = tree->visualItemRect(item);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, modifiers,
                          QPoint(rect.left() + 100, rect.center().y()));
    };

    click_row(0, Qt::NoModifier);
    click_row(3, Qt::ControlModifier);
    QCoreApplication::processEvents();
    MenuState background_selection;
    if (tree->selectedItems().size() != 2 ||
        !invoke_context_menu(0, {}, &background_selection) ||
        !background_selection.group_visible || background_selection.group_enabled ||
        background_selection.selected_count != 2) return fail("disable Background grouping");

    click_row(0, Qt::NoModifier);
    click_row(2, Qt::ControlModifier);
    QCoreApplication::processEvents();
    if (tree->selectedItems().size() != 2) return fail("create non-contiguous selection");
    MenuState non_contiguous;
    if (!invoke_context_menu(0, {}, &non_contiguous) ||
        !non_contiguous.group_visible || non_contiguous.group_enabled ||
        non_contiguous.selected_count != 2) return fail("disable non-contiguous grouping");

    MenuState right_clicked_unselected;
    if (!invoke_context_menu(1, {}, &right_clicked_unselected) ||
        !right_clicked_unselected.group_visible || right_clicked_unselected.group_enabled ||
        right_clicked_unselected.selected_count != 1 ||
        tree->currentItem() != layerRowItem(tree, 1)) return fail("select unselected context row");

    click_row(0, Qt::NoModifier);
    click_row(1, Qt::ControlModifier);
    QCoreApplication::processEvents();
    MenuState valid_selection;
    if (!invoke_context_menu(0, QStringLiteral("groupSelectedLayersContextAction"),
                             &valid_selection) ||
        !valid_selection.group_visible || !valid_selection.group_enabled ||
        valid_selection.selected_count != 2 || layerRowCount(tree) != 3 ||
        layerRowItem(tree, 0)->childCount() != 2) return fail("group from context menu");

    click_row(1, Qt::NoModifier);
    click_row(0, Qt::ControlModifier);
    MenuState mixed_selection;
    if (!invoke_context_menu(1, {}, &mixed_selection) ||
        !mixed_selection.group_visible || mixed_selection.group_enabled ||
        mixed_selection.selected_count != 2) return fail("disable grouping mixed layer/group selection");

    MenuState group_actions;
    if (!invoke_context_menu(0, QStringLiteral("ungroupLayerGroupContextAction"),
                             &group_actions) ||
        group_actions.group_visible || !group_actions.ungroup_visible ||
        !group_actions.delete_group_visible || layerRowCount(tree) != 4) {
        return fail("ungroup from context menu");
    }

    click_row(0, Qt::NoModifier);
    click_row(1, Qt::ControlModifier);
    MenuState regrouped;
    if (!invoke_context_menu(0, QStringLiteral("groupSelectedLayersContextAction"),
                             &regrouped) || layerRowCount(tree) != 3) {
        return fail("regroup from context menu");
    }
    MenuState deleted;
    if (!invoke_context_menu(0, QStringLiteral("deleteLayerGroupContextAction"), &deleted) ||
        !deleted.delete_group_visible || layerRowCount(tree) != 2) {
        return fail("delete group from context menu");
    }
    return true;
}

bool testTextEditorGrowthLayout() {
    image_editor::ImageCanvas canvas;
    canvas.resize(1584, 912);
    QImage image(1920, 1080, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    canvas.setImage(image);
    canvas.show();
    QCoreApplication::processEvents();
    auto* editor = canvas.findChild<QPlainTextEdit*>(
        QStringLiteral("imageCanvasTextEditor"));
    if (editor == nullptr) return false;
    const QString heading = QStringLiteral("sasdasdasdasdasdasdasdasdasdasdasd");
    for (const qreal initial_width : {24.0, 320.0}) {
        image_editor::ImageTextData text;
        text.position = QPointF(40, 100);
        text.box_width = initial_width;
        canvas.beginTextEditing(text, false);
        QCoreApplication::processEvents();
        const int initial_editor_width = editor->width();
        for (qsizetype index = 0; index < heading.size(); ++index) {
            QTest::keyClicks(editor, QString(heading.at(index)));
            QCoreApplication::processEvents();
            const QString typed = heading.left(index + 1);
            const QTextLayout* layout = editor->document()->firstBlock().layout();
            const QRect caret = editor->cursorRect();
            QTextCursor start(editor->document());
            start.setPosition(0);
            const qreal caret_advance = caret.left() - editor->cursorRect(start).left();
            const qreal expected_advance = QFontMetricsF(editor->font()).horizontalAdvance(typed);
            if (editor->toPlainText() != typed ||
                editor->textCursor().position() != typed.size() ||
                layout->lineCount() != 1 || layout->lineAt(0).textLength() != typed.size() ||
                std::abs(caret_advance - expected_advance) > 4.0 ||
                !editor->viewport()->rect().contains(caret.center()) ||
                editor->verticalScrollBar()->value() != 0 ||
                editor->cursorForPosition(caret.center()).position() != typed.size()) {
                std::cerr << "Live text layout became stale after " << typed.size()
                          << " characters, initial width=" << initial_width
                          << ", editor width=" << editor->width()
                          << ", viewport width=" << editor->viewport()->width()
                          << ", document width=" << editor->document()->textWidth()
                          << ", lines=" << layout->lineCount()
                          << ", caret advance=" << caret_advance
                          << ", expected advance=" << expected_advance << ".\n";
                return false;
            }
        }
        if (editor->width() <= initial_editor_width) {
            std::cerr << "The text box did not grow beyond its initial width.\n";
            return false;
        }
        QTextCursor selection_start(editor->document());
        selection_start.setPosition(0);
        QTextCursor selection_end = selection_start;
        selection_end.setPosition(3);
        const QPoint drag_start = editor->cursorRect(selection_start).center();
        const QPoint drag_end = editor->cursorRect(selection_end).center();
        QTest::mousePress(editor->viewport(), Qt::LeftButton, Qt::NoModifier, drag_start);
        QMouseEvent drag_move(QEvent::MouseMove, QPointF(drag_end),
            QPointF(editor->viewport()->mapToGlobal(drag_end)), Qt::NoButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(editor->viewport(), &drag_move);
        QTest::mouseRelease(editor->viewport(), Qt::LeftButton, Qt::NoModifier, drag_end);
        QCoreApplication::processEvents();
        if (editor->textCursor().selectedText() != heading.left(3)) {
            std::cerr << "Mouse selection did not match the visible text after expansion.\n";
            return false;
        }
        if (selectionHighlightPixelCount(canvas.grab().toImage().copy(
                editor->geometry().adjusted(2, 2, -2, -2))) < 10) {
            std::cerr << "The mouse-selected text did not show its selection highlight.\n";
            return false;
        }
        QTest::keyClicks(editor, QStringLiteral("DE"));
        QCoreApplication::processEvents();
        if (editor->toPlainText() != QStringLiteral("DE") + heading.mid(3) ||
            editor->textCursor().position() != 2) {
            std::cerr << "Typing did not replace the mouse-selected text.\n";
            return false;
        }
        QTest::keyClick(editor, Qt::Key_End, Qt::ControlModifier);
        QTest::keyClicks(editor, heading.repeated(3));
        QCoreApplication::processEvents();
        if (editor->document()->firstBlock().layout()->lineCount() <= 1 ||
            editor->verticalScrollBar()->maximum() != 0 ||
            !editor->viewport()->rect().contains(editor->cursorRect().center())) {
            std::cerr << "Canvas-edge wrapping hid text or scrolled the growing box.\n";
            return false;
        }
        const int wrapped_height = editor->height();
        QTest::keyClick(editor, Qt::Key_Return);
        QTest::keyClicks(editor, QStringLiteral("second line"));
        QCoreApplication::processEvents();
        if (editor->height() <= wrapped_height ||
            editor->verticalScrollBar()->maximum() != 0 ||
            !editor->viewport()->rect().contains(editor->cursorRect().center())) {
            std::cerr << "An explicit newline was hidden in the growing box.\n";
            return false;
        }
        QTest::keyClick(editor, Qt::Key_Escape);
    }
    return true;
}

bool testWindowTextGrowth() {
    image_editor::ImageEditorWindow window;
    const bool native = QApplication::platformName() == QStringLiteral("windows");
    if (native) {
        window.showMaximized();
    } else {
        // The offscreen virtual screen is too small for this unwrapped heading.
        // Keep the fixture at the same usable canvas size as a desktop window.
        window.resize(1920, 1080);
        window.show();
    }
    window.raise();
    window.activateWindow();
    if (native && !QTest::qWaitForWindowActive(&window)) return false;
    QCoreApplication::processEvents();
    auto* new_canvas = window.findChild<QAction*>(QStringLiteral("newCanvasAction"));
    if (new_canvas == nullptr) return false;
    bool configured = false;
    QTimer::singleShot(0, &window, [&]() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr) return;
        auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("newCanvasPresetCombo"));
        auto* background = dialog->findChild<QComboBox*>(QStringLiteral("newCanvasBackgroundCombo"));
        if (preset == nullptr || background == nullptr) { dialog->reject(); return; }
        preset->setCurrentIndex(4);
        background->setCurrentIndex(1);
        configured = true;
        dialog->accept();
    });
    new_canvas->trigger();
    if (!configured) return false;
    window.raise();
    window.activateWindow();
    if (native && !QTest::qWaitForWindowActive(&window)) return false;
    QTest::qWait(100);
    auto* canvas = window.findChild<image_editor::ImageCanvas*>();
    auto* editor = window.findChild<QPlainTextEdit*>(QStringLiteral("imageCanvasTextEditor"));
    auto* tool = window.findChild<QToolButton*>(QStringLiteral("textToolButton"));
    if (canvas == nullptr || editor == nullptr || tool == nullptr) return false;
    QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                      tool->mapTo(&window, tool->rect().center()));
    QCoreApplication::processEvents();
    const qreal zoom = canvas->zoomFactor();
    const QPoint start(qRound((canvas->width() - 1920.0 * zoom) / 2.0 + 40 * zoom),
                       qRound((canvas->height() - 1080.0 * zoom) / 2.0 + 100 * zoom));
    QTest::mouseClick(window.windowHandle(), Qt::LeftButton, Qt::NoModifier,
                      canvas->mapTo(&window, start));
    QTest::qWait(30);
    if (!QTest::qWaitFor([editor]() { return editor->hasFocus() && editor->isVisible(); })) {
        window.screen()->grabWindow(window.winId()).save(QStringLiteral("text-editor-window-click-failure.png"));
        std::cerr << "Text click creation failed: tool=" << tool->isChecked()
                  << ", focus=" << editor->hasFocus() << ", visible=" << editor->isVisible()
                  << ", zoom=" << canvas->zoomFactor() << ", click=" << start.x()
                  << "," << start.y() << ".\n";
        return false;
    }
    const auto displayedImage = [&]() {
        if (!native) return canvas->grab().toImage().copy(editor->geometry());
        const QPoint position = editor->mapTo(&window, QPoint());
        return window.screen()->grabWindow(window.winId(), position.x(), position.y(),
            editor->width(), editor->height()).toImage();
    };
    // Grow beyond the initial frame while leaving room for the offscreen
    // backend's wider fallback glyphs. Canvas-edge wrapping has a separate case.
    const QString heading = QStringLiteral("sasdasdasdasdasdasdasdasdasdasdasd");
    int first_ink = 0;
    for (qsizetype index = 0; index < heading.size(); ++index) {
#if defined(Q_OS_WIN)
        if (native && QApplication::arguments().contains(QStringLiteral("--native-keyboard"))) {
            if (GetForegroundWindow() != reinterpret_cast<HWND>(window.winId())) {
                std::cerr << "The native text test lost foreground focus.\n";
                return false;
            }
            INPUT input[2]{};
            input[0].type = INPUT_KEYBOARD;
            input[0].ki.wVk = heading.at(index).toUpper().unicode();
            input[1] = input[0];
            input[1].ki.dwFlags = KEYEVENTF_KEYUP;
            if (SendInput(2, input, sizeof(INPUT)) != 2) {
                std::cerr << "Native keyboard input failed: " << GetLastError() << ".\n";
                return false;
            }
        } else
#endif
        {
            QTest::keyClick(window.windowHandle(), heading.at(index).toLatin1());
        }
        QTest::qWait(20);
        const QImage displayed = displayedImage();
        const int ink = darkPixelCount(displayed);
        if (index == 0) first_ink = ink;
        const QTextLayout* layout = editor->document()->firstBlock().layout();
        if (editor->toPlainText() != heading.left(index + 1) ||
            editor->textCursor().position() != index + 1 ||
            layout->lineCount() != 1 ||
            !editor->viewport()->rect().contains(editor->cursorRect().center()) ||
            (index >= 2 && ink < first_ink * (index + 1) * 0.5)) {
            displayed.save(QStringLiteral("text-editor-window-failure.png"));
            std::cerr << "Window text growth failed after " << index + 1
                      << " characters; ink=" << ink << "/" << first_ink
                      << ", lines=" << layout->lineCount()
                      << ", cursor=" << editor->textCursor().position()
                      << ", editor width=" << editor->width()
                      << ", viewport width=" << editor->viewport()->width()
                      << ", zoom=" << canvas->zoomFactor()
                      << ", window active=" << window.isActiveWindow()
                      << ", screenshot=" << QDir::currentPath().toStdString()
                      << "/text-editor-window-failure.png.\n";
            return false;
        }
    }
    if (native) displayedImage().save(QStringLiteral("text-editor-window-live.png"));
    QTextCursor first(editor->document());
    first.setPosition(0);
    QTextCursor third = first;
    third.setPosition(3);
    const QPoint first_point = editor->viewport()->mapTo(&window, editor->cursorRect(first).center());
    const QPoint third_point = editor->viewport()->mapTo(&window, editor->cursorRect(third).center());
    QTest::mousePress(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, first_point);
    QTest::mouseMove(window.windowHandle(), third_point);
    QTest::mouseRelease(window.windowHandle(), Qt::LeftButton, Qt::NoModifier, third_point);
    QTest::qWait(20);
    if (editor->textCursor().selectedText() != heading.left(3) ||
        selectionHighlightPixelCount(displayedImage()) < 10) {
        std::cerr << "Selection in the full window did not match the displayed text.\n";
        return false;
    }
    QTest::keyClick(window.windowHandle(), 'D');
    QTest::keyClick(window.windowHandle(), 'E');
    if (editor->toPlainText() != QStringLiteral("DE") + heading.mid(3)) {
        std::cerr << "Full-window typing did not replace the selected text.\n";
        return false;
    }
    QTest::keyClick(window.windowHandle(), Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(window.windowHandle(), Qt::Key_Backspace);
    for (const QChar character : heading) {
        QKeyEvent press(QEvent::KeyPress, character.toUpper().unicode(),
                        Qt::NoModifier, QString(character));
        QApplication::sendEvent(editor, &press);
    }
    QTest::qWait(50);
    const QImage burst = displayedImage();
    if (editor->toPlainText() != heading || darkPixelCount(burst) < first_ink * heading.size() * 0.5 ||
        editor->document()->firstBlock().layout()->lineCount() != 1) {
        burst.save(QStringLiteral("text-editor-window-burst-failure.png"));
        std::cerr << "Typing a burst left the editor layout or live pixels stale.\n";
        return false;
    }
    QTest::keyClick(editor, Qt::Key_Escape);
    return true;
}

bool testEditableTextUi(const QString& directory) {
    const QString source_path = directory + QStringLiteral("/editable-text-source.png");
    const QString document_path = directory + QStringLiteral("/editable-text-linked.cimg");
    const QString output_path = directory + QStringLiteral("/editable-text-published.png");
    QImage source(320, 240, QImage::Format_ARGB32_Premultiplied);
    source.fill(Qt::white);
    if (!source.save(source_path, "PNG")) return false;

    image_editor::ImageEditorWindow window;
    window.resize(1100, 800);
    window.show();
    if (!window.openLinkedImage(source_path, document_path, output_path)) return false;
    QCoreApplication::processEvents();
    auto* canvas = window.findChild<image_editor::ImageCanvas*>();
    auto* text_tool = window.findChild<QToolButton*>(QStringLiteral("textToolButton"));
    auto* selection_tool = window.findChild<QToolButton*>(
        QStringLiteral("selectShapesToolButton"));
    auto* text_size = window.findChild<QSpinBox*>(QStringLiteral("textSizeSpinBox"));
    auto* text_font = window.findChild<QFontComboBox*>(QStringLiteral("textFontComboBox"));
    auto* text_color = window.findChild<QPushButton*>(QStringLiteral("textColorButton"));
    auto* text_alignment = window.findChild<QComboBox*>(
        QStringLiteral("textAlignmentComboBox"));
    auto* text_editor = window.findChild<QPlainTextEdit*>(
        QStringLiteral("imageCanvasTextEditor"));
    auto* layer_tree = window.findChild<QTreeWidget*>(QStringLiteral("imageLayerTree"));
    auto* save = window.findChild<QAction*>(QStringLiteral("saveDocumentAction"));
    auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
    if (canvas == nullptr || text_tool == nullptr || selection_tool == nullptr ||
        text_size == nullptr || text_font == nullptr || text_color == nullptr ||
        text_alignment == nullptr || text_editor == nullptr || layer_tree == nullptr ||
        save == nullptr || undo == nullptr) return false;
    text_tool->click();
    text_size->setValue(18);
    const auto widgetPoint = [canvas](qreal x, qreal y) {
        const qreal zoom = canvas->zoomFactor();
        return QPoint(qRound((canvas->width() - 320.0 * zoom) / 2.0 + x * zoom),
                      qRound((canvas->height() - 240.0 * zoom) / 2.0 + y * zoom));
    };
    const QPoint first_start = widgetPoint(70, 80);
    const QPoint first_end = widgetPoint(160, 80);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, first_start);
    QTest::mouseMove(canvas, first_end);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, first_end);
    QCoreApplication::processEvents();
    if (!text_editor->isVisible()) return false;
    const auto liveTextInk = [canvas, text_editor]() {
        const QRect region = text_editor->geometry().adjusted(3, 3, -3, -3);
        return darkPixelCount(canvas->grab().toImage().copy(region));
    };
    const int initial_text_editor_width = text_editor->width();
    const int initial_text_editor_height = text_editor->height();
    QTest::keyClicks(text_editor, QStringLiteral("d"));
    QCoreApplication::processEvents();
    const int single_character_ink = liveTextInk();
    text_editor->clear();
    QCoreApplication::processEvents();
    for (const auto& [key, character] : {
             std::pair{Qt::Key_E, QStringLiteral("e")},
             std::pair{Qt::Key_B, QStringLiteral("b")}}) {
        QKeyEvent shortcut_override(QEvent::ShortcutOverride, key,
                                    Qt::NoModifier, QString{});
        ShortcutOverrideDeliveryProbe delivery_probe;
        text_editor->installEventFilter(&delivery_probe);
        QApplication::sendEvent(text_editor, &shortcut_override);
        text_editor->removeEventFilter(&delivery_probe);
        if (!shortcut_override.isAccepted() || !delivery_probe.received) {
            std::cerr << "A shortcut override was blocked without reaching the text editor.\n";
            return false;
        }
        QTest::keyClick(text_editor, key);
        QCoreApplication::processEvents();
        if (!text_editor->isVisible() || !text_tool->isChecked() ||
            text_editor->toPlainText() != character) {
            std::cerr << "A Paint/Eraser shortcut interrupted text input.\n";
            return false;
        }
        text_editor->clear();
        QCoreApplication::processEvents();
    }
    const QString long_heading = QStringLiteral(
        "This exceptionally long heading continues beyond canvas edge");
    for (qsizetype index = 0; index < long_heading.size(); ++index) {
        QTest::keyClicks(text_editor, QString(long_heading.at(index)));
        QCoreApplication::processEvents();
        if (!text_editor->isVisible() || QApplication::focusWidget() != text_editor ||
            text_editor->toPlainText() != long_heading.left(index + 1) ||
            text_editor->textCursor().position() != index + 1 ||
            !text_editor->geometry().intersects(canvas->rect())) {
            std::cerr << "Typing stopped after character " << (index + 1)
                      << " of the heading or the expanding editor left the canvas.\n";
            return false;
        }
    }
    QTest::keyClick(text_editor, Qt::Key_Home, Qt::ControlModifier);
    for (int index = 0; index < QStringLiteral("This").size(); ++index) {
        QTest::keyClick(text_editor, Qt::Key_Right, Qt::ShiftModifier);
    }
    QCoreApplication::processEvents();
    if (text_editor->textCursor().selectedText() != QStringLiteral("This")) {
        std::cerr << "Selecting text in the expanded editor did not select the word.\n";
        return false;
    }
    if (selectionHighlightPixelCount(canvas->grab().toImage().copy(
            text_editor->geometry().adjusted(2, 2, -2, -2))) < 10) {
        std::cerr << "The native text selection highlight was not visible.\n";
        return false;
    }
    QTest::keyClicks(text_editor, QStringLiteral("This"));
    if (text_editor->toPlainText() != long_heading ||
        text_editor->textCursor().position() != QStringLiteral("This").size()) {
        std::cerr << "Typing over selected text did not replace the selection.\n";
        return false;
    }
    QTest::keyClick(text_editor, Qt::Key_End, Qt::ControlModifier);
    if (text_editor->textCursor().position() != long_heading.size()) {
        std::cerr << "Ctrl+End did not return the cursor to the end of the expanded text.\n";
        return false;
    }
    QCoreApplication::processEvents();
    const int expanded_text_ink = liveTextInk();
    if (expanded_text_ink <= single_character_ink * 3) {
        std::cerr << "The live editor did not render the full typed heading (single-character "
                  << single_character_ink << " dark pixels, heading " << expanded_text_ink
                  << ").\n";
        return false;
    }
    const int maximum_editor_width = qRound((320.0 - 70.0) * canvas->zoomFactor());
    if (text_editor->width() <= initial_text_editor_width ||
        text_editor->width() > maximum_editor_width ||
        text_editor->height() <= initial_text_editor_height) {
        std::cerr << "Typing a long line did not grow to the canvas edge and wrap.\n";
        return false;
    }
    const int wrapped_text_editor_height = text_editor->height();
    QTest::keyClick(text_editor, Qt::Key_Return);
    QTest::keyClicks(text_editor, QStringLiteral("Second line"));
    if (!text_editor->toPlainText().contains(QLatin1Char('\n'))) return false;
    QCoreApplication::processEvents();
    if (text_editor->height() <= wrapped_text_editor_height) {
        std::cerr << "Typing another line did not grow the text box vertically.\n";
        return false;
    }
    QTest::keyClick(text_editor, Qt::Key_Return, Qt::ControlModifier);
    QCoreApplication::processEvents();
    if (text_editor->isVisible() || layerRowCount(layer_tree) != 3) {
        std::cerr << "Ctrl+Enter did not commit a multiline text layer.\n";
        return false;
    }
    save->trigger();
    image_editor::ImageDocumentData document;
    QString error;
    if (!image_editor::ImageDocumentStore::loadDocument(document_path, &document, &error)) {
        std::cerr << "The linked text document could not be loaded: "
                  << error.toStdString() << '\n';
        return false;
    }
    const auto findText = [](const image_editor::ImageDocumentData& data,
                             const QString& id, image_editor::ImageTextData* text) {
        for (const auto& layer : data.layers) {
            for (const auto& operation : layer.operations) {
                if (operation.kind == image_editor::OperationKind::Text &&
                    operation.text.id == id) {
                    if (text != nullptr) *text = operation.text;
                    return true;
                }
            }
        }
        return false;
    };
    image_editor::ImageTextData first_text;
    QString first_text_id;
    for (const auto& layer : document.layers) {
        if (layer.operations.isEmpty() || layer.operations.front().kind !=
            image_editor::OperationKind::Text) continue;
        first_text_id = layer.operations.front().text.id;
        first_text = layer.operations.front().text;
        if (layer.name != QStringLiteral("Text 1")) return false;
        break;
    }
    QImage published(output_path);
    if (first_text_id.isEmpty() ||
        first_text.content != QStringLiteral(
            "This exceptionally long heading continues beyond canvas edge\nSecond line") ||
        first_text.font_pixel_size != 18 || published.isNull() || published == source) {
        std::cerr << "Saving linked text did not persist and publish its multiline content.\n";
        std::cerr << "Saved text='" << first_text.content.toStdString() << "', size="
                  << first_text.font_pixel_size << ", id='" << first_text_id.toStdString()
                  << "', published-null=" << published.isNull()
                  << ", published-equals-source=" << (published == source) << ".\n";
        return false;
    }

    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier,
                       widgetPoint(first_text.position.x() + first_text.box_width - 5,
                                   first_text.position.y() + 4));
    QCoreApplication::processEvents();
    if (!text_editor->isVisible()) {
        std::cerr << "Double-click did not reopen an existing text operation.\n";
        return false;
    }
    if (text_editor->textCursor().position() != 0) {
        const QPoint clicked = widgetPoint(first_text.position.x() + first_text.box_width - 5,
                                           first_text.position.y() + 4);
        std::cerr << "Reopening text placed the caret at "
                  << text_editor->textCursor().position()
                  << " instead of the beginning after reopening at " << clicked.x() << ','
                  << clicked.y() << ".\n";
        return false;
    }
    QTest::keyClicks(text_editor, QStringLiteral("X"));
    if (!text_editor->toPlainText().startsWith(QStringLiteral("XThis exceptionally"))) {
        std::cerr << "Typing after reopening did not insert at the beginning.\n";
        return false;
    }
    text_size->setValue(22);
    QTest::keyClick(text_editor, Qt::Key_Escape);
    QCoreApplication::processEvents();
    if (text_editor->isVisible() || window.windowTitle().startsWith(QLatin1Char('*'))) {
        std::cerr << "Esc did not cancel an existing text edit without dirtying the document.\n";
        return false;
    }
    if (darkPixelCount(canvas->grab().toImage().copy(
            text_editor->geometry().adjusted(3, 3, -3, -3))) == 0) {
        std::cerr << "Cancelling an existing text edit did not restore its rendered text.\n";
        return false;
    }
    if (!image_editor::ImageDocumentStore::loadDocument(document_path, &document, &error) ||
        !findText(document, first_text_id, &first_text) || first_text.font_pixel_size != 18 ||
        first_text.content != QStringLiteral(
            "This exceptionally long heading continues beyond canvas edge\nSecond line")) {
        return false;
    }

    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier,
                       widgetPoint(first_text.position.x() + 4,
                                   first_text.position.y() + 4));
    QCoreApplication::processEvents();
    if (!text_editor->isVisible()) return false;
    text_size->setValue(22);
    QString expected_font_family = first_text.font_family;
    for (const QString& family : QFontDatabase::families()) {
        if (family.compare(first_text.font_family, Qt::CaseInsensitive) == 0) continue;
        text_font->setCurrentFont(QFont(family));
        expected_font_family = text_font->currentFont().family();
        break;
    }
    text_alignment->setCurrentIndex(1);
    QTimer::singleShot(0, []() {
        if (auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget())) {
            dialog->setCurrentColor(QColor(120, 10, 220, 190));
            dialog->accept();
        }
    });
    text_color->click();
    QTest::keyClick(text_editor, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(text_editor, QStringLiteral("Updated caption"));
    QTest::keyClick(text_editor, Qt::Key_Return);
    QTest::keyClicks(text_editor, QStringLiteral("second row"));
    QTest::keyClick(text_editor, Qt::Key_Return, Qt::ControlModifier);
    save->trigger();
    if (!image_editor::ImageDocumentStore::loadDocument(document_path, &document, &error) ||
        !findText(document, first_text_id, &first_text) ||
        first_text.content != QStringLiteral("Updated caption\nsecond row") ||
        first_text.font_pixel_size != 22 ||
        first_text.font_family != expected_font_family ||
        first_text.alignment != image_editor::ImageTextAlignment::Center ||
        first_text.color != QColor(120, 10, 220, 190)) {
        std::cerr << "Confirming an edit did not persist text formatting: family="
                  << first_text.font_family.toStdString() << "/"
                  << expected_font_family.toStdString() << ", size="
                  << first_text.font_pixel_size << ", alignment="
                  << static_cast<int>(first_text.alignment) << ", color="
                  << first_text.color.name(QColor::HexArgb).toStdString() << '\n';
        return false;
    }

    QTest::mouseDClick(canvas, Qt::LeftButton, Qt::NoModifier,
                       widgetPoint(first_text.position.x() + 4,
                                   first_text.position.y() + 4));
    QCoreApplication::processEvents();
    if (!text_editor->isVisible()) return false;
    QTest::keyClick(text_editor, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClick(text_editor, Qt::Key_Backspace);
    QTest::keyClick(text_editor, Qt::Key_Return, Qt::ControlModifier);
    save->trigger();
    if (!image_editor::ImageDocumentStore::loadDocument(document_path, &document, &error) ||
        findText(document, first_text_id, nullptr)) {
        std::cerr << "Confirming empty text did not remove its operation.\n";
        return false;
    }
    undo->trigger();
    save->trigger();
    if (!image_editor::ImageDocumentStore::loadDocument(document_path, &document, &error) ||
        !findText(document, first_text_id, &first_text) ||
        first_text.content != QStringLiteral("Updated caption\nsecond row")) {
        std::cerr << "Undo did not restore text removed by confirming an empty edit.\n";
        return false;
    }

    const int layers_before_empty_cancel = layerRowCount(layer_tree);
    const QPoint empty_start = widgetPoint(20, 160);
    if (!text_tool->isChecked()) text_tool->click();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, empty_start);
    QCoreApplication::processEvents();
    if (!text_editor->isVisible()) {
        std::cerr << "Clicking empty canvas did not start the new text editor.\n";
        return false;
    }
    QTest::keyClicks(text_editor, QStringLiteral("ABC"));
    QTest::keyClicks(text_editor, QStringLiteral("DE"));
    QCoreApplication::processEvents();
    if (text_editor->toPlainText() != QStringLiteral("ABCDE") ||
        text_editor->textCursor().position() != 5) {
        std::cerr << "Typing into a click-created empty text box did not preserve input order.\n";
        return false;
    }

    // Reset the test text, then click before the first character before the
    // deferred preview/geometry update has run. The queued resize must retain
    // the insertion point chosen by the click.
    QTest::keyClick(text_editor, Qt::Key_A, Qt::ControlModifier);
    QTest::keyClicks(text_editor, QStringLiteral("ABC"));
    const QPoint canvas_text_origin = text_editor->mapTo(canvas, QPoint(0, 0));
    const QPoint canvas_click_position = canvas_text_origin +
        QPoint(1, text_editor->fontMetrics().height() / 2);
    QWidget* click_target = canvas->childAt(canvas_click_position);
    if (click_target == nullptr ||
        (click_target != text_editor && !text_editor->isAncestorOf(click_target))) {
        std::cerr << "Clicking at the visible start of new text is not routed to its editor; "
                  << "target=" << (click_target != nullptr
                      ? click_target->metaObject()->className() : "null") << ".\n";
        return false;
    }
    QTest::mouseClick(click_target, Qt::LeftButton, Qt::NoModifier,
                      click_target->mapFrom(canvas, canvas_click_position));
    QCoreApplication::processEvents();
    if (text_editor->textCursor().position() != 0) {
        std::cerr << "Clicking before the first character did not move the insertion cursor.\n";
        return false;
    }
    QTest::keyClicks(text_editor, QStringLiteral("DE"));
    QCoreApplication::processEvents();
    if (text_editor->toPlainText() != QStringLiteral("DEABC") ||
        text_editor->textCursor().position() != 2) {
        std::cerr << "Typing after moving the cursor to the beginning did not insert there.\n";
        return false;
    }
    QTest::keyClick(text_editor, Qt::Key_Escape);
    QCoreApplication::processEvents();
    if (layerRowCount(layer_tree) != layers_before_empty_cancel) {
        std::cerr << "Cancelling a new empty text frame left a layer behind.\n";
        return false;
    }

    const QPoint second_start = widgetPoint(0, 40);
    const QPoint second_end = widgetPoint(70, 40);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, second_start);
    QTest::mouseMove(canvas, second_end);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, second_end);
    QCoreApplication::processEvents();
    QTest::keyClicks(text_editor, QStringLiteral("Outside click"));
    QTimer::singleShot(0, []() {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            message->accept();
        }
    });
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, widgetPoint(300, 220));
    QCoreApplication::processEvents();
    if (text_editor->isVisible() || layerRowCount(layer_tree) != layers_before_empty_cancel + 1) {
        std::cerr << "Clicking outside the editor did not commit a new text layer.\n";
        return false;
    }
    save->trigger();
    if (!image_editor::ImageDocumentStore::loadDocument(document_path, &document, &error)) return false;
    image_editor::ImageTextData second_text;
    QString second_text_id;
    for (const auto& layer : document.layers) {
        if (layer.name != QStringLiteral("Text 2") || layer.operations.isEmpty()) continue;
        second_text_id = layer.operations.front().text.id;
        second_text = layer.operations.front().text;
    }
    if (second_text_id.isEmpty() || second_text.content != QStringLiteral("Outside click")) {
        std::cerr << "The click-outside commit was not saved in the second text layer.\n";
        return false;
    }

    selection_tool->click();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                      widgetPoint(second_text.position.x() + 3,
                                  second_text.position.y() + 3));
    const QRectF second_bounds = image_editor::imageTextBounds(second_text);
    const QPoint resize_start = widgetPoint(second_bounds.right(), second_bounds.center().y());
    const QPoint resize_end = widgetPoint(second_bounds.right() + 25, second_bounds.center().y());
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, resize_start);
    QTest::mouseMove(canvas, resize_end);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, resize_end);
    save->trigger();
    if (!image_editor::ImageDocumentStore::loadDocument(document_path, &document, &error) ||
        !findText(document, second_text_id, &second_text) ||
        second_text.box_width <= second_bounds.width() || second_text.font_pixel_size != 22) {
        std::cerr << "The side resize handle did not change only the text box width: saved="
                  << second_text.box_width << ", before=" << second_bounds.width()
                  << ", font=" << second_text.font_pixel_size << ".\n";
        return false;
    }
    published = QImage(output_path);
    if (published.isNull() || published == source) {
        std::cerr << "Linked publishing did not include the committed text image.\n";
        return false;
    }
    return true;
}

} // namespace

bool testGeneralCanvasSelection() {
    image_editor::ImageCanvas canvas;
    canvas.resize(640, 480);
    canvas.show();
    QCoreApplication::processEvents();

    QImage backing(QSize(100, 80), QImage::Format_ARGB32_Premultiplied);
    backing.fill(Qt::transparent);
    canvas.setImage(backing);
    canvas.setObjectSelectionMode(true);

    image_editor::ImageObjectPlacement top_shape;
    top_shape.layer_id = QStringLiteral("top-shapes");
    top_shape.operation.kind = image_editor::OperationKind::Shape;
    top_shape.operation.shape.id = QStringLiteral("top-shape");
    top_shape.operation.shape.kind = image_editor::ImageShapeKind::Rectangle;
    top_shape.operation.shape.start = QPointF(12, 8);
    top_shape.operation.shape.end = QPointF(24, 18);
    top_shape.operation.shape.stroke_enabled = false;
    top_shape.operation.shape.fill_enabled = true;
    top_shape.operation.shape.fill_color = Qt::green;

    image_editor::ImageObjectPlacement shape;
    shape.layer_id = QStringLiteral("shapes");
    shape.operation.kind = image_editor::OperationKind::Shape;
    shape.operation.shape.id = QStringLiteral("shape");
    shape.operation.shape.kind = image_editor::ImageShapeKind::Rectangle;
    shape.operation.shape.start = QPointF(60, 10);
    shape.operation.shape.end = QPointF(80, 30);
    shape.operation.shape.stroke_color = Qt::black;
    shape.operation.shape.fill_enabled = true;
    shape.operation.shape.fill_color = Qt::blue;

    image_editor::ImageObjectPlacement eraser;
    eraser.layer_id = QStringLiteral("eraser-layer");
    eraser.operation.kind = image_editor::OperationKind::EraseStroke;
    eraser.operation.erase_stroke.id = QStringLiteral("eraser");
    eraser.operation.erase_stroke.points = {QPointF(20, 30), QPointF(50, 30)};
    eraser.operation.erase_stroke.diameter = 6;

    image_editor::ImageObjectPlacement paint;
    paint.layer_id = QStringLiteral("paint-layer");
    paint.operation.kind = image_editor::OperationKind::PaintStroke;
    paint.operation.paint_stroke.id = QStringLiteral("paint");
    paint.operation.paint_stroke.points = {QPointF(10, 10), QPointF(40, 10)};
    paint.operation.paint_stroke.color = Qt::red;
    paint.operation.paint_stroke.diameter = 4;

    const QVector<image_editor::ImageObjectPlacement> placements{
        top_shape, shape, eraser, paint};
    const auto image_point = [&canvas](const QPointF& point) {
        const qreal zoom = canvas.zoomFactor();
        return QPoint(qRound((canvas.width() - 100 * zoom) / 2.0 + point.x() * zoom),
                      qRound((canvas.height() - 80 * zoom) / 2.0 + point.y() * zoom));
    };
    canvas.setObjectPlacements(placements, {});

    QStringList last_selection;
    QString last_active_layer;
    QObject::connect(&canvas, &image_editor::ImageCanvas::objectsSelected,
        [&last_selection, &last_active_layer](const QStringList& ids, const QString& layer) {
            last_selection = ids;
            last_active_layer = layer;
        });

    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, image_point(QPointF(16, 12)));
    if (last_selection != QStringList{QStringLiteral("top-shape")} ||
        last_active_layer != QStringLiteral("top-shapes")) {
        std::cerr << "Selection did not choose the topmost overlapping object and its layer.\n";
        return false;
    }
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, image_point(QPointF(35, 10)));
    if (last_selection != QStringList{QStringLiteral("paint")} ||
        last_active_layer != QStringLiteral("paint-layer")) {
        std::cerr << "Selection could not hit-test a paint stroke.\n";
        return false;
    }
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, image_point(QPointF(35, 30)));
    if (last_selection != QStringList{QStringLiteral("eraser")} ||
        last_active_layer != QStringLiteral("eraser-layer")) {
        std::cerr << "Selection could not hit-test an eraser operation.\n";
        return false;
    }
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, image_point(QPointF(70, 20)));
    if (last_selection != QStringList{QStringLiteral("shape")} ||
        last_active_layer != QStringLiteral("shapes")) {
        std::cerr << "Selection could not hit-test a shape.\n";
        return false;
    }

    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, image_point(QPointF(35, 10)));
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::ShiftModifier, image_point(QPointF(35, 30)));
    if (!last_selection.contains(QStringLiteral("paint")) ||
        !last_selection.contains(QStringLiteral("eraser")) || last_selection.size() != 2) {
        std::cerr << "Shift-click did not add an object to the selection.\n";
        return false;
    }
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::ShiftModifier, image_point(QPointF(35, 30)));
    if (last_selection != QStringList{QStringLiteral("paint")}) {
        std::cerr << "Shift-click did not remove an object from the selection.\n";
        return false;
    }

    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, image_point(QPointF(8, 6)));
    QTest::mouseMove(&canvas, image_point(QPointF(82, 32)));
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, image_point(QPointF(82, 32)));
    const QStringList expected_ids{QStringLiteral("top-shape"), QStringLiteral("shape"),
                                   QStringLiteral("eraser"), QStringLiteral("paint")};
    if (last_selection.size() != expected_ids.size() ||
        !std::all_of(expected_ids.cbegin(), expected_ids.cend(),
            [&last_selection](const QString& id) { return last_selection.contains(id); })) {
        std::cerr << "The marquee did not select every object it intersected.\n";
        return false;
    }
    QTest::mouseClick(&canvas, Qt::LeftButton, Qt::NoModifier, image_point(QPointF(95, 70)));
    if (!last_selection.isEmpty()) {
        std::cerr << "Clicking empty canvas did not clear object selection.\n";
        return false;
    }

    const QStringList all_ids{QStringLiteral("top-shape"), QStringLiteral("shape"),
                              QStringLiteral("eraser"), QStringLiteral("paint")};
    canvas.setObjectPlacements(placements, all_ids);
    QStringList transformed_ids;
    int geometry_changes = 0;
    bool all_geometry_changed = false;
    QObject::connect(&canvas, &image_editor::ImageCanvas::objectTransformStarted,
        [&transformed_ids](const QStringList& ids) { transformed_ids = ids; });
    QObject::connect(&canvas, &image_editor::ImageCanvas::objectsGeometryChanged,
        [&geometry_changes, &all_geometry_changed, &paint](
            const QVector<image_editor::ImageObjectPlacement>& changed) {
            ++geometry_changes;
            all_geometry_changed = changed.size() == 4 &&
                std::any_of(changed.cbegin(), changed.cend(), [&paint](const auto& object) {
                    return object.operation.kind == image_editor::OperationKind::PaintStroke &&
                        object.operation.paint_stroke.points != paint.operation.paint_stroke.points;
                });
        });
    const QPoint move_start = image_point(QPointF(35, 10));
    const QPoint move_end = image_point(QPointF(39, 14));
    QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, move_start);
    QTest::mouseMove(&canvas, move_end);
    QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::NoModifier, move_end);
    if (transformed_ids.size() != all_ids.size() || geometry_changes != 1 ||
        !all_geometry_changed) {
        std::cerr << "Dragging a selected object did not transform the complete mixed selection.\n";
        return false;
    }

    canvas.setObjectPlacements({paint}, {QStringLiteral("paint")});
    image_editor::ImageObjectPlacement scaled_paint;
    QObject::connect(&canvas, &image_editor::ImageCanvas::objectsGeometryChanged,
        [&scaled_paint](const QVector<image_editor::ImageObjectPlacement>& changed) {
            if (!changed.isEmpty()) scaled_paint = changed.front();
        });
    const auto freeform_resize = [&canvas, &image_point, &scaled_paint](
        const image_editor::ImageObjectPlacement& object,
        const QString& id,
        const QPointF& start,
        const QPointF& end) {
        scaled_paint = {};
        canvas.setObjectPlacements({object}, {id});
        const QPoint resize_start = image_point(start);
        const QPoint resize_end = image_point(end);
        QTest::mousePress(&canvas, Qt::LeftButton, Qt::NoModifier, resize_start);
        QMouseEvent freeform_move(QEvent::MouseMove, QPointF(resize_end),
            QPointF(canvas.mapToGlobal(resize_end)), Qt::NoButton, Qt::LeftButton,
            Qt::AltModifier);
        QApplication::sendEvent(&canvas, &freeform_move);
        QTest::mouseRelease(&canvas, Qt::LeftButton, Qt::AltModifier, resize_end);
    };
    freeform_resize(paint, QStringLiteral("paint"), QPointF(42, 12), QPointF(50, 28));
    if (scaled_paint.operation.kind != image_editor::OperationKind::PaintStroke ||
        scaled_paint.operation.paint_stroke.diameter != 10) {
        std::cerr << "Alt resize did not scale a stroke using the geometric mean.\n";
        return false;
    }
    freeform_resize(eraser, QStringLiteral("eraser"), QPointF(53, 33), QPointF(61, 43));
    if (scaled_paint.operation.kind != image_editor::OperationKind::EraseStroke ||
        scaled_paint.operation.erase_stroke.diameter != 11) {
        std::cerr << "Alt resize did not scale eraser width using the geometric mean.\n";
        return false;
    }
    freeform_resize(shape, QStringLiteral("shape"), QPointF(81, 31), QPointF(91, 41));
    if (scaled_paint.operation.kind != image_editor::OperationKind::Shape ||
        scaled_paint.operation.shape.stroke_width != 3) {
        std::cerr << "Alt resize did not scale shape outlines using the geometric mean.\n";
        return false;
    }
    return true;
}

bool testRenamedShortcutPersistence() {
    const QKeySequence select_shortcut(QStringLiteral("Ctrl+Alt+S"));
    const QKeySequence delete_shortcut(QStringLiteral("Ctrl+Alt+D"));
    {
        QSettings settings;
        settings.beginGroup(QStringLiteral("ImageEditor/KeyboardShortcuts"));
        settings.setValue(QStringLiteral("selectShapesToolAction"), select_shortcut.toString());
        settings.setValue(QStringLiteral("deleteSelectedShapeAction"), delete_shortcut.toString());
        settings.endGroup();
        settings.sync();
    }

    image_editor::ImageEditorWindow reopened;
    auto* select_action = reopened.findChild<QAction*>(QStringLiteral("selectShapesToolAction"));
    auto* delete_action = reopened.findChild<QAction*>(QStringLiteral("deleteSelectedShapeAction"));
    if (select_action == nullptr || delete_action == nullptr ||
        select_action->text() != QStringLiteral("Selection") ||
        delete_action->text() != QStringLiteral("Delete Selection") ||
        select_action->shortcut() != select_shortcut ||
        delete_action->shortcut() != delete_shortcut) {
        std::cerr << "The renamed Selection and Delete actions did not retain saved shortcuts.\n";
        return false;
    }
    return true;
}

bool testLayerMasksUi(const QString& directory);
bool testRasterImagesUi(const QString& directory);
bool testDeletionUi(const QString& directory);

bool testDocumentTabs(const QString& directory) {
    const QString first_path = directory + QStringLiteral("/tabs-first.png");
    const QString second_path = directory + QStringLiteral("/tabs-second.png");
    const QString replacement_path = directory + QStringLiteral("/tabs-replacement.png");
    QImage first_image(72, 54, QImage::Format_ARGB32);
    first_image.fill(QColor(210, 40, 35));
    QImage second_image(180, 120, QImage::Format_ARGB32);
    second_image.fill(QColor(30, 150, 220));
    QImage replacement_image(96, 80, QImage::Format_ARGB32);
    replacement_image.fill(QColor(40, 190, 90));
    if (!first_image.save(first_path) || !second_image.save(second_path) ||
        !replacement_image.save(replacement_path)) return false;

    image_editor::ImageEditorWindow window;
    window.resize(1100, 760);
    window.show();
    if (!window.openImagePath(first_path)) return false;
    QCoreApplication::processEvents();

    auto* tabs = window.findChild<QTabBar*>(QStringLiteral("imageDocumentTabBar"));
    auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("imageDocumentStack"));
    auto* new_tab_button = window.findChild<QToolButton*>(QStringLiteral("newDocumentTabButton"));
    auto* add_layer = window.findChild<QToolButton*>(QStringLiteral("addImageLayerButton"));
    auto* layer_tree = window.findChild<QTreeWidget*>(QStringLiteral("imageLayerTree"));
    auto* undo = window.findChild<QAction*>(QStringLiteral("undoAction"));
    auto* redo = window.findChild<QAction*>(QStringLiteral("redoAction"));
    auto* close_tab = window.findChild<QAction*>(QStringLiteral("closeDocumentTabAction"));
    auto* next_tab = window.findChild<QAction*>(QStringLiteral("nextDocumentTabAction"));
    auto* previous_tab = window.findChild<QAction*>(QStringLiteral("previousDocumentTabAction"));
    auto* select_tool = window.findChild<QAction*>(QStringLiteral("selectShapesToolAction"));
    auto* delete_objects = window.findChild<QAction*>(
        QStringLiteral("deleteSelectedObjectsAction"));
    auto* new_tab_open_image = window.findChild<QAction*>(
        QStringLiteral("newTabOpenImageAction"));
    auto* new_tab_open_document = window.findChild<QAction*>(
        QStringLiteral("newTabOpenEditableDocumentAction"));
    if (tabs == nullptr || stack == nullptr || new_tab_button == nullptr ||
        add_layer == nullptr || layer_tree == nullptr || undo == nullptr || redo == nullptr ||
        close_tab == nullptr || next_tab == nullptr || previous_tab == nullptr ||
        select_tool == nullptr || delete_objects == nullptr ||
        new_tab_open_image == nullptr || new_tab_open_document == nullptr ||
        close_tab->property("defaultShortcut").toString() !=
            QKeySequence(QKeySequence::Close).toString(QKeySequence::PortableText) ||
        next_tab->property("defaultShortcut").toString() !=
            QKeySequence(QKeySequence::NextChild).toString(QKeySequence::PortableText) ||
        previous_tab->property("defaultShortcut").toString() !=
            QKeySequence(QKeySequence::PreviousChild).toString(QKeySequence::PortableText) ||
        tabs->count() != 1 || !new_tab_button->isEnabled()) return false;

    const auto chooseFileAndTrigger = [&window](QAction* action, const QString& path) {
        bool selected = false;
        QTimer chooser;
        chooser.setInterval(10);
        QObject::connect(&chooser, &QTimer::timeout, &window, [&selected, &path, &chooser]() {
            for (QWidget* widget : QApplication::topLevelWidgets()) {
                auto* dialog = qobject_cast<QFileDialog*>(widget);
                if (dialog == nullptr || !dialog->isVisible()) continue;
                selected = true;
                chooser.stop();
                dialog->selectFile(path);
                QMetaObject::invokeMethod(dialog, "accept", Qt::QueuedConnection);
                return;
            }
        });
        chooser.start();
        action->trigger();
        chooser.stop();
        return selected;
    };

    auto* first_canvas = stack->currentWidget()->findChild<image_editor::ImageCanvas*>();
    if (first_canvas == nullptr) return false;
    first_canvas->fitToWindow();
    const double first_zoom = first_canvas->zoomFactor();
    add_layer->click();
    if (layerRowCount(layer_tree) != 3 || !undo->isEnabled() ||
        !tabs->tabText(0).startsWith('*')) {
        std::cerr << "Editing the first tab did not update its layers, history, and dirty marker.\n";
        return false;
    }
    const auto first_layer_items = layer_tree->findItems(
        QStringLiteral("Layer 1"), Qt::MatchExactly, 0);
    if (first_layer_items.size() != 1) return false;
    first_layer_items.front()->setSelected(true);
    if (layer_tree->selectedItems().size() != 2) {
        std::cerr << "The first tab could not hold its multi-layer selection.\n";
        return false;
    }

    if (!chooseFileAndTrigger(new_tab_open_image, second_path) ||
        tabs->count() != 2 || tabs->currentIndex() != 1) {
        std::cerr << "Opening an image in a new tab did not preserve the existing tab.\n";
        return false;
    }
    auto* second_canvas = stack->currentWidget()->findChild<image_editor::ImageCanvas*>();
    if (second_canvas == nullptr || second_canvas == first_canvas ||
        layerRowCount(layer_tree) != 2 || undo->isEnabled() ||
        layer_tree->selectedItems().size() != 1 ||
        layer_tree->currentItem() == nullptr ||
        layer_tree->currentItem()->text(0) != QStringLiteral("Layer 1")) {
        std::cerr << "The second tab did not receive an independent canvas and history.\n";
        return false;
    }
    const double second_zoom = second_canvas->zoomFactor();
    next_tab->trigger();
    QCoreApplication::processEvents();
    if (stack->currentWidget()->findChild<image_editor::ImageCanvas*>() != first_canvas ||
        layerRowCount(layer_tree) != 3 || first_canvas->zoomFactor() != first_zoom ||
        layer_tree->selectedItems().size() != 2 ||
        layer_tree->currentItem() == nullptr ||
        layer_tree->currentItem()->text(0) != QStringLiteral("Layer 2")) {
        std::cerr << "Switching back did not restore the first tab's view and layer state.\n";
        return false;
    }
    undo->trigger();
    if (layerRowCount(layer_tree) != 2 || !redo->isEnabled()) return false;
    previous_tab->trigger();
    QCoreApplication::processEvents();
    if (tabs->currentIndex() != 1) {
        std::cerr << "The previous-tab shortcut action did not activate the adjacent tab.\n";
        return false;
    }
    tabs->setCurrentIndex(1);
    QCoreApplication::processEvents();
    if (layerRowCount(layer_tree) != 2 || undo->isEnabled() ||
        layer_tree->selectedItems().size() != 1 ||
        second_canvas->zoomFactor() != second_zoom) {
        std::cerr << "Undo history or zoom leaked between document tabs.\n";
        return false;
    }

    if (!window.openImagePath(replacement_path) || tabs->count() != 2 ||
        tabs->currentIndex() != 1 || layerRowCount(layer_tree) != 2 ||
        !tabs->tabText(1).contains(QStringLiteral("tabs-replacement.png"))) {
        std::cerr << "The normal Open command did not replace only the current tab.\n";
        return false;
    }
    if (!window.importImagePaths({first_path}) || tabs->count() != 2 ||
        layerRowCount(layer_tree) != 3 || layer_tree->currentItem() == nullptr ||
        layer_tree->currentItem()->text(0) != QStringLiteral("tabs-first.png")) {
        std::cerr << "Import as Layer did not stay in the active document tab.\n";
        return false;
    }
    tabs->setCurrentIndex(0);
    QCoreApplication::processEvents();
    if (layer_tree->currentItem() == nullptr ||
        layer_tree->currentItem()->text(0) != QStringLiteral("Layer 1")) {
        std::cerr << "The active layer leaked into another document tab.\n";
        return false;
    }
    tabs->setCurrentIndex(1);
    QCoreApplication::processEvents();
    if (layer_tree->currentItem() == nullptr ||
        layer_tree->currentItem()->text(0) != QStringLiteral("tabs-first.png")) {
        std::cerr << "Switching tabs did not restore the active layer selection.\n";
        return false;
    }
    QTimer::singleShot(0, []() {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt == nullptr) return;
        for (auto* button : prompt->buttons())
            if (button->text().contains(QStringLiteral("Cancel"), Qt::CaseInsensitive)) {
                button->click();
                return;
            }
    });
    if (window.openImagePath(second_path) || tabs->count() != 2 ||
        tabs->currentIndex() != 1 || layerRowCount(layer_tree) != 3 ||
        !tabs->tabText(1).startsWith(QLatin1Char('*'))) {
        std::cerr << "Cancelling replacement of a dirty tab changed its document.\n";
        return false;
    }
    QTimer::singleShot(0, []() {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt == nullptr) return;
        for (auto* button : prompt->buttons())
            if (button->text().contains(QStringLiteral("Discard"), Qt::CaseInsensitive)) {
                button->click();
                return;
            }
    });
    const bool replaced_with_second_image = window.openImagePath(second_path);
    auto* replacement_canvas = stack->currentWidget()->findChild<image_editor::ImageCanvas*>();
    if (!replaced_with_second_image || tabs->count() != 2 ||
        tabs->currentIndex() != 1 || layerRowCount(layer_tree) != 2 ||
        !tabs->tabText(1).contains(QStringLiteral("tabs-second.png")) ||
        replacement_canvas == nullptr) {
        std::cerr << "Discarding a dirty tab did not allow the requested replacement.\n";
        return false;
    }
    select_tool->trigger();
    const bool replacement_kept_a_stale_object_selection = delete_objects->isEnabled();
    select_tool->trigger();
    if (replacement_kept_a_stale_object_selection) {
        std::cerr << "Replacing a document retained its previous object selection.\n";
        return false;
    }

    const QString editable_path = directory + QStringLiteral("/tabs-editable.cimg");
    image_editor::ImageDocumentSession document;
    QString error;
    if (!document.openImage(replacement_path, &error) ||
        !document.saveDocument(editable_path, &error)) return false;
    if (!window.openDocumentPath(editable_path) || tabs->count() != 2 ||
        tabs->currentIndex() != 1 || layerRowCount(layer_tree) != 2 ||
        !tabs->tabText(1).contains(QStringLiteral("tabs-editable.cimg"))) {
        std::cerr << "The normal Open command did not replace the active tab with a .cimg.\n";
        return false;
    }
    const QString second_editable_path = directory + QStringLiteral("/tabs-first-editable.cimg");
    image_editor::ImageDocumentSession second_document;
    if (!second_document.openImage(first_path, &error) ||
        !second_document.saveDocument(second_editable_path, &error)) return false;
    if (!chooseFileAndTrigger(new_tab_open_document, second_editable_path) ||
        tabs->count() != 3 ||
        tabs->currentIndex() != 2 ||
        !chooseFileAndTrigger(new_tab_open_document, second_editable_path) ||
        tabs->count() != 3 || tabs->currentIndex() != 2) {
        std::cerr << "Reopening an already open .cimg created a duplicate tab.\n";
        return false;
    }

    tabs->setCurrentIndex(2);
    QCoreApplication::processEvents();
    add_layer->click();
    const auto request_close = [tabs]() {
        return QMetaObject::invokeMethod(
            tabs, "tabCloseRequested", Qt::DirectConnection, Q_ARG(int, tabs->currentIndex()));
    };
    QTimer::singleShot(0, []() {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt == nullptr) return;
        for (auto* button : prompt->buttons())
            if (button->text().contains(QStringLiteral("Cancel"), Qt::CaseInsensitive)) {
                button->click();
                return;
            }
    });
    if (!request_close() || tabs->count() != 3 || tabs->currentIndex() != 2) {
        std::cerr << "Cancelling a dirty tab close did not keep the tab open.\n";
        return false;
    }
    add_layer->click();
    bool save_prompt_clicked = false;
    QString save_prompt_text;
    QTimer::singleShot(0, [&save_prompt_clicked, &save_prompt_text]() {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt == nullptr) return;
        save_prompt_text = prompt->text();
        for (auto* button : prompt->buttons())
            if (button->text().contains(QStringLiteral("Save"), Qt::CaseInsensitive)) {
                save_prompt_clicked = true;
                button->click();
                return;
            }
    });
    const bool save_close_invoked = request_close();
    if (!save_close_invoked || tabs->count() != 2 || tabs->currentIndex() != 1) {
        std::cerr << "Saving a dirty tab before closing did not close only that tab.\n";
        std::cerr << "tabs=" << tabs->count() << " current=" << tabs->currentIndex()
                  << " signal=" << save_close_invoked
                  << " label=" << (tabs->count() > 2 ? tabs->tabText(2).toStdString() : "none")
                  << " modal=" << (QApplication::activeModalWidget()
                                          ? QApplication::activeModalWidget()->metaObject()->className()
                                          : "none") << '\n';
        std::cerr << "save-prompt-clicked=" << save_prompt_clicked
                  << " prompt='" << save_prompt_text.toStdString() << "'\n";
        return false;
    }
    image_editor::ImageDocumentSession persisted;
    if (!persisted.openDocument(second_editable_path, &error) ||
        persisted.data().layers.size() != 4) {
        std::cerr << "The Save choice did not persist the tab before closing it.\n";
        return false;
    }

    const QString failing_directory = directory + QStringLiteral("/save-failure");
    if (!QDir().mkpath(failing_directory)) return false;
    const QString failing_document_path =
        failing_directory + QStringLiteral("/save-failure.cimg");
    image_editor::ImageDocumentSession failing_document;
    if (!failing_document.openImage(replacement_path, &error) ||
        !failing_document.saveDocument(failing_document_path, &error) ||
        !chooseFileAndTrigger(new_tab_open_document, failing_document_path) ||
        tabs->count() != 3 || tabs->currentIndex() != 2) return false;
    add_layer->click();
    if (!QDir(directory).rename(QStringLiteral("save-failure"),
                                QStringLiteral("save-failure-offline"))) return false;
    QTimer failed_save_dialog_handler;
    failed_save_dialog_handler.setInterval(10);
    QObject::connect(&failed_save_dialog_handler, &QTimer::timeout, []() {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt == nullptr) return;
        if (prompt->text().contains(QStringLiteral("Save the changes"))) {
            for (auto* button : prompt->buttons())
                if (button->text().contains(QStringLiteral("Save"), Qt::CaseInsensitive)) {
                    button->click();
                    return;
                }
        } else {
            prompt->accept();
            return;
        }
    });
    failed_save_dialog_handler.start();
    const bool failed_save_close_invoked = request_close();
    failed_save_dialog_handler.stop();
    const bool failed_save_kept_tab = failed_save_close_invoked && tabs->count() == 3 &&
        tabs->currentIndex() == 2 &&
        tabs->tabText(2).startsWith(QLatin1Char('*'));
    const bool restored_directory = QDir(directory).rename(
        QStringLiteral("save-failure-offline"), QStringLiteral("save-failure"));
    if (!failed_save_kept_tab || !restored_directory) {
        std::cerr << "A failed save did not leave the dirty document tab open.\n";
        return false;
    }
    QTimer::singleShot(0, []() {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt == nullptr) return;
        for (auto* button : prompt->buttons())
            if (button->text().contains(QStringLiteral("Discard"), Qt::CaseInsensitive)) {
                button->click();
                return;
            }
    });
    if (!request_close() || tabs->count() != 2 || tabs->currentIndex() != 1) {
        std::cerr << "The failed-save tab could not be discarded after restoring its path.\n";
        return false;
    }

    add_layer->click();
    QTimer::singleShot(0, []() {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt == nullptr) return;
        for (auto* button : prompt->buttons())
            if (button->text().contains(QStringLiteral("Discard"), Qt::CaseInsensitive)) {
                button->click();
                return;
            }
    });
    if (!request_close() || tabs->count() != 1 || tabs->currentIndex() != 0) {
        std::cerr << "Discarding a dirty tab did not preserve the other tabs.\n";
        return false;
    }

    add_layer->click();
    QTimer::singleShot(0, []() {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt == nullptr) return;
        for (auto* button : prompt->buttons())
            if (button->text().contains(QStringLiteral("Discard"), Qt::CaseInsensitive)) {
                button->click();
                return;
            }
    });
    if (!request_close() || tabs->count() != 0) {
        std::cerr << "Discarding the final dirty tab did not leave the empty workspace.\n";
        return false;
    }

    QCoreApplication::processEvents();
    bool plus_menu_has_all_options = false;
    QTimer::singleShot(0, [&plus_menu_has_all_options]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (menu == nullptr) return;
        bool canvas = false;
        bool image = false;
        bool document = false;
        for (auto* action : menu->actions()) {
            canvas = canvas || action->objectName() == QStringLiteral("newTabCanvasAction");
            image = image || action->objectName() == QStringLiteral("newTabOpenImageAction");
            document = document || action->objectName() ==
                QStringLiteral("newTabOpenEditableDocumentAction");
        }
        plus_menu_has_all_options = canvas && image && document;
        menu->close();
    });
    new_tab_button->click();
    if (!plus_menu_has_all_options || !new_tab_button->isEnabled()) {
        std::cerr << "The explicit new-tab control was not available in the empty workspace.\n";
        return false;
    }
    auto* new_tab_canvas = window.findChild<QAction*>(QStringLiteral("newTabCanvasAction"));
    auto* new_canvas = window.findChild<QAction*>(QStringLiteral("newCanvasAction"));
    if (new_tab_canvas == nullptr || new_canvas == nullptr) return false;
    QTimer new_tab_canvas_acceptor;
    new_tab_canvas_acceptor.setInterval(10);
    QObject::connect(&new_tab_canvas_acceptor, &QTimer::timeout, [&window]() {
        for (QWidget* widget : QApplication::topLevelWidgets()) {
            auto* dialog = qobject_cast<QDialog*>(widget);
            if (dialog == nullptr || widget == &window || !dialog->isVisible()) continue;
            auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("newCanvasPresetCombo"));
            auto* background = dialog->findChild<QComboBox*>(
                QStringLiteral("newCanvasBackgroundCombo"));
            if (preset == nullptr || background == nullptr) continue;
            preset->setCurrentIndex(4);
            background->setCurrentIndex(1);
            dialog->accept();
            return;
        }
    });
    new_tab_canvas_acceptor.start();
    new_tab_canvas->trigger();
    new_tab_canvas_acceptor.stop();
    if (tabs->count() != 1 || tabs->currentIndex() != 0 ||
        stack->currentWidget()->findChild<image_editor::ImageCanvas*>() == nullptr) {
        std::cerr << "New Canvas from the plus menu did not create the first document tab.\n";
        return false;
    }
    QTimer replacement_dialog_clicker;
    replacement_dialog_clicker.setInterval(10);
    QObject::connect(&replacement_dialog_clicker, &QTimer::timeout, []() {
        if (auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            for (auto* button : prompt->buttons())
                if (button->text().contains(QStringLiteral("Discard"), Qt::CaseInsensitive)) {
                    button->click();
                    return;
                }
        }
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            if (auto* preset = dialog->findChild<QComboBox*>(
                    QStringLiteral("newCanvasPresetCombo"))) preset->setCurrentIndex(4);
            if (auto* background = dialog->findChild<QComboBox*>(
                    QStringLiteral("newCanvasBackgroundCombo"))) background->setCurrentIndex(1);
            dialog->accept();
        }
    });
    replacement_dialog_clicker.start();
    new_canvas->trigger();
    replacement_dialog_clicker.stop();
    if (tabs->count() != 1 || tabs->currentIndex() != 0 ||
        stack->currentWidget()->findChild<image_editor::ImageCanvas*>() == nullptr) {
        std::cerr << "The normal New Canvas command did not replace the active tab.\n";
        return false;
    }
    return true;
}

bool testMultiDocumentRecovery(const QString& directory) {
    QCoreApplication::setApplicationName(
        QStringLiteral("Image Editor Multi Tab Recovery Test %1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    const QString first_source = directory + QStringLiteral("/recover-first.png");
    const QString second_source = directory + QStringLiteral("/recover-second.png");
    QImage first_image(80, 55, QImage::Format_ARGB32);
    first_image.fill(Qt::red);
    QImage second_image(130, 90, QImage::Format_ARGB32);
    second_image.fill(Qt::blue);
    if (!first_image.save(first_source) || !second_image.save(second_source)) return false;

    const QString recovery_data_directory =
        directory + QStringLiteral("/multi-tab-recovery-data");
    image_editor::RecoveryStore recovery(recovery_data_directory);
    image_editor::ImageDocumentSession first;
    image_editor::ImageDocumentSession second;
    QString error;
    const bool first_opened = first.openImage(first_source, &error);
    const bool first_layer_added = !first.addLayer().isEmpty();
    const bool second_opened = second.openImage(second_source, &error);
    const bool second_layer_added = !second.addLayer().isEmpty();
    const bool first_saved = recovery.save(first, &error);
    const bool second_saved = recovery.save(second, &error);
    const auto saved_snapshots = recovery.snapshots();
    if (!first_opened || !first_layer_added || !second_opened || !second_layer_added ||
        !first_saved || !second_saved || saved_snapshots.size() != 2) {
        std::cerr << "The multiple-tab recovery fixtures could not be prepared.\n";
        std::cerr << "open=" << first_opened << ',' << second_opened
                  << " add=" << first_layer_added << ',' << second_layer_added
                  << " save=" << first_saved << ',' << second_saved
                  << " snapshots=" << saved_snapshots.size()
                  << " error='" << error.toStdString() << "'\n";
        for (const auto& snapshot : saved_snapshots)
            std::cerr << snapshot.toStdString() << '\n';
        return false;
    }

    QTimer recovery_clicker;
    recovery_clicker.setInterval(10);
    QObject::connect(&recovery_clicker, &QTimer::timeout, []() {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt == nullptr ||
            !prompt->text().contains(QStringLiteral("Restore this recovery snapshot"))) return;
        for (auto* button : prompt->buttons())
            if (button->text().contains(QStringLiteral("Restore in New Tab"))) {
                button->click();
                return;
            }
    });
    recovery_clicker.start();
    image_editor::ImageEditorWindow window(nullptr, recovery_data_directory);
    window.show();
    QTest::qWait(100);
    recovery_clicker.stop();
    auto* tabs = window.findChild<QTabBar*>(QStringLiteral("imageDocumentTabBar"));
    auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("imageDocumentStack"));
    auto* layer_tree = window.findChild<QTreeWidget*>(QStringLiteral("imageLayerTree"));
    auto* autosave = window.findChild<QTimer*>(QStringLiteral("imageEditorRecoveryTimer"));
    if (tabs == nullptr || stack == nullptr || layer_tree == nullptr || autosave == nullptr ||
        tabs->count() != 2 || layerRowCount(layer_tree) != 3) {
        std::cerr << "Recovery did not restore each snapshot as an independent tab.\n";
        return false;
    }

    for (const QString& snapshot : recovery.snapshots())
        static_cast<void>(recovery.remove(snapshot));
    if (!QMetaObject::invokeMethod(autosave, "timeout", Qt::DirectConnection) ||
        recovery.snapshots().size() != 2) {
        std::cerr << "Autosave did not write recovery snapshots for every dirty tab.\n";
        return false;
    }
    for (const QString& snapshot : recovery.snapshots())
        static_cast<void>(recovery.remove(snapshot));
    return true;
}

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Creative Suite"));
    QCoreApplication::setApplicationName(
        QStringLiteral("Image Editor UI Tests %1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QStandardPaths::setTestModeEnabled(true);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temporary.path());

    if (!testWindowTextGrowth()) return 1;
    if (!testTextEditorGrowthLayout()) return 1;
    if (application.arguments().contains(QStringLiteral("--text-layout-only"))) return 0;
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    if (!testDeletionUi(temporary.path())) return 1;
    if (!testRasterImagesUi(temporary.path())) return 1;
    if (!testLayerMasksUi(temporary.path())) {
        std::cerr << "Layer mask thumbnails, brush targeting, or linked publication failed.\n";
        return 1;
    }

    if (!testGeneralCanvasSelection()) return 1;
    if (!testLayerGroupsUi(temporary.path())) {
        std::cerr << "Layer group panel actions, multi-selection, or hierarchy failed.\n";
        return 1;
    }
    if (!testLayerGroupContextMenu(temporary.path())) {
        std::cerr << "Layer group context menu, selection rules, or actions failed.\n";
        return 1;
    }
    if (!testEditableTextUi(temporary.path())) {
        std::cerr << "Editable text creation, editing, cancellation, or linked publication failed.\n";
        return 1;
    }

    image_editor::ImageEditorWindow window;
    window.show();
    QCoreApplication::processEvents();

    auto* startup_options_toolbar = window.findChild<QToolBar*>(
        QStringLiteral("toolOptionsToolBar"));
    auto* startup_paint_options_action = window.findChild<QAction*>(
        QStringLiteral("paintBrushSizeAction"));
    auto* startup_paint_options = window.findChild<QWidget*>(
        QStringLiteral("paintBrushSizeOptions"));
    if (startup_options_toolbar == nullptr || startup_paint_options_action == nullptr ||
        startup_paint_options == nullptr || !startup_options_toolbar->isVisible() ||
        startup_options_toolbar->height() < 40 || startup_paint_options_action->isVisible() ||
        startup_paint_options->isVisible()) {
        std::cerr << "The top options bar did not start empty before opening a document.\n";
        return 1;
    }

    auto* empty_workspace_add = window.findChild<QToolButton*>(
        QStringLiteral("newDocumentTabButton"));
    if (window.findChild<image_editor::ImageCanvas*>() != nullptr ||
        empty_workspace_add == nullptr || !empty_workspace_add->isEnabled() ||
        !window.isVisible()) {
        std::cerr << "The empty Image Editor workspace did not expose the new-tab control.\n";
        return 1;
    }
    image_editor::ImageCanvas* canvas = nullptr;

    auto* settings_menu = window.findChild<QMenu*>(QStringLiteral("settingsMenu"));
    auto* keyboard_shortcuts_action = window.findChild<QAction*>(
        QStringLiteral("keyboardShortcutsAction"));
    auto* paint_tool_action = window.findChild<QAction*>(QStringLiteral("paintToolAction"));
    auto* eraser_tool_action = window.findChild<QAction*>(QStringLiteral("eraserToolAction"));
    auto* shapes_tool_action = window.findChild<QAction*>(QStringLiteral("shapesToolAction"));
    auto* select_shapes_tool_action = window.findChild<QAction*>(
        QStringLiteral("selectShapesToolAction"));
    auto* delete_shape_action = window.findChild<QAction*>(
        QStringLiteral("deleteSelectedShapeAction"));
    auto* cancel_crop_action = window.findChild<QAction*>(QStringLiteral("cancelCropAction"));
    if (settings_menu == nullptr || keyboard_shortcuts_action == nullptr ||
        paint_tool_action == nullptr || eraser_tool_action == nullptr ||
        shapes_tool_action == nullptr || select_shapes_tool_action == nullptr ||
        delete_shape_action == nullptr || cancel_crop_action == nullptr ||
        cancel_crop_action->shortcut() != QKeySequence(Qt::Key_Escape) ||
        settings_menu->title() != QStringLiteral("Settings") ||
        paint_tool_action->shortcut() != QKeySequence(Qt::Key_B) ||
        eraser_tool_action->shortcut() != QKeySequence(Qt::Key_E) ||
        select_shapes_tool_action->text() != QStringLiteral("Selection") ||
        delete_shape_action->text() != QStringLiteral("Delete Selection") ||
        paint_tool_action->isChecked() || paint_tool_action->isEnabled() ||
        eraser_tool_action->isChecked() || eraser_tool_action->isEnabled() ||
        !shapes_tool_action->shortcut().isEmpty() ||
        !select_shapes_tool_action->shortcut().isEmpty() ||
        delete_shape_action->shortcut() != QKeySequence(Qt::Key_Delete) ||
        shapes_tool_action->isEnabled() || select_shapes_tool_action->isEnabled() ||
        delete_shape_action->isEnabled()) {
        std::cerr << "Settings or the default, inactive tool shortcuts were not created.\n";
        return 1;
    }

    auto run_shortcut_dialog = [&](auto interaction) {
        bool dialog_found = false;
        QTimer interaction_timeout;
        interaction_timeout.setSingleShot(true);
        QObject::connect(&interaction_timeout, &QTimer::timeout, &window, [&]() {
            auto* active_modal = QApplication::activeModalWidget();
            if (auto* message = qobject_cast<QMessageBox*>(active_modal)) message->accept();
            if (auto* dialog = window.findChild<QDialog*>(
                    QStringLiteral("shortcutSettingsDialog"))) {
                dialog->reject();
            }
        });
        QTimer::singleShot(0, [&]() {
            auto* dialog = window.findChild<QDialog*>(
                QStringLiteral("shortcutSettingsDialog"));
            if (dialog == nullptr) return;
            dialog_found = true;
            interaction(dialog);
        });
        interaction_timeout.start(5000);
        keyboard_shortcuts_action->trigger();
        interaction_timeout.stop();
        return dialog_found;
    };

    const QKeySequence custom_paint_shortcut(QStringLiteral("Ctrl+Alt+P"));
    const bool custom_shortcut_saved = run_shortcut_dialog(
        [&](QDialog* dialog) {
            auto* table = dialog->findChild<QTableWidget*>(
                QStringLiteral("shortcutSettingsTable"));
            auto* editor = dialog->findChild<QKeySequenceEdit*>(
                QStringLiteral("shortcutEditor_paintToolAction"));
            auto* buttons = dialog->findChild<QDialogButtonBox*>(
                QStringLiteral("shortcutSettingsButtons"));
            const QScreen* dialog_screen = dialog->screen();
            const QSize available = dialog_screen != nullptr
                ? dialog_screen->availableGeometry().size()
                : QSize{};
            const bool should_expand_width = available.width() * 0.9 > 620;
            const bool should_expand_height = available.height() * 0.9 > 520;
            if (table == nullptr || table->rowCount() < 19 || editor == nullptr ||
                buttons == nullptr || !dialog->isSizeGripEnabled() ||
                (should_expand_width && dialog->width() <= 620) ||
                (should_expand_height && dialog->height() <= 520)) {
                dialog->reject();
                return;
            }
            const int initial_height = dialog->height();
            dialog->resize(dialog->width(),
                           std::max(dialog->minimumHeight(),
                                    std::min(initial_height, 640)));
            QCoreApplication::processEvents();
            const bool list_scrolls_when_compact =
                table->verticalScrollBar()->maximum() > 0;
            dialog->resize(dialog->width(), initial_height);
            QCoreApplication::processEvents();
            if (!list_scrolls_when_compact) {
                dialog->reject();
                return;
            }
            editor->setKeySequence(custom_paint_shortcut);
            buttons->button(QDialogButtonBox::Ok)->click();
        });
    if (!custom_shortcut_saved || paint_tool_action->shortcut() != custom_paint_shortcut) {
        std::cerr << "The shortcut dialog did not apply a custom Paint shortcut.\n";
        return 1;
    }
    {
        image_editor::ImageEditorWindow reopened_window;
        auto* reopened_paint_action = reopened_window.findChild<QAction*>(
            QStringLiteral("paintToolAction"));
        if (reopened_paint_action == nullptr ||
            reopened_paint_action->shortcut() != custom_paint_shortcut) {
            std::cerr << "The custom shortcut was not persisted for the next window.\n";
            return 1;
        }
    }

    const QKeySequence custom_eraser_shortcut(QStringLiteral("Ctrl+Alt+E"));
    const bool custom_eraser_shortcut_saved = run_shortcut_dialog(
        [&](QDialog* dialog) {
            auto* editor = dialog->findChild<QKeySequenceEdit*>(
                QStringLiteral("shortcutEditor_eraserToolAction"));
            auto* buttons = dialog->findChild<QDialogButtonBox*>(
                QStringLiteral("shortcutSettingsButtons"));
            if (editor == nullptr || buttons == nullptr) {
                dialog->reject();
                return;
            }
            editor->setKeySequence(custom_eraser_shortcut);
            buttons->button(QDialogButtonBox::Ok)->click();
        });
    if (!custom_eraser_shortcut_saved ||
        eraser_tool_action->shortcut() != custom_eraser_shortcut) {
        std::cerr << "The shortcut dialog did not configure the Eraser shortcut.\n";
        return 1;
    }
    {
        image_editor::ImageEditorWindow reopened_window;
        auto* reopened_eraser_action = reopened_window.findChild<QAction*>(
            QStringLiteral("eraserToolAction"));
        if (reopened_eraser_action == nullptr ||
            reopened_eraser_action->shortcut() != custom_eraser_shortcut) {
            std::cerr << "The custom Eraser shortcut was not persisted.\n";
            return 1;
        }
    }

    const bool cancel_left_shortcut_unchanged = run_shortcut_dialog(
        [&](QDialog* dialog) {
            auto* editor = dialog->findChild<QKeySequenceEdit*>(
                QStringLiteral("shortcutEditor_paintToolAction"));
            auto* buttons = dialog->findChild<QDialogButtonBox*>(
                QStringLiteral("shortcutSettingsButtons"));
            if (editor == nullptr || buttons == nullptr) {
                dialog->reject();
                return;
            }
            editor->setKeySequence(QKeySequence(QStringLiteral("Ctrl+Alt+Q")));
            buttons->button(QDialogButtonBox::Cancel)->click();
        });
    if (!cancel_left_shortcut_unchanged ||
        paint_tool_action->shortcut() != custom_paint_shortcut) {
        std::cerr << "Cancel applied an unconfirmed shortcut change.\n";
        return 1;
    }

    bool duplicate_shortcut_rejected = false;
    const bool duplicate_dialog_completed = run_shortcut_dialog(
        [&](QDialog* dialog) {
            auto* editor = dialog->findChild<QKeySequenceEdit*>(
                QStringLiteral("shortcutEditor_paintToolAction"));
            auto* validation = dialog->findChild<QLabel*>(
                QStringLiteral("shortcutValidationMessage"));
            auto* reset = dialog->findChild<QPushButton*>(
                QStringLiteral("resetAllShortcutsButton"));
            auto* buttons = dialog->findChild<QDialogButtonBox*>(
                QStringLiteral("shortcutSettingsButtons"));
            if (editor == nullptr || validation == nullptr || reset == nullptr ||
                buttons == nullptr) {
                dialog->reject();
                return;
            }
            editor->setKeySequence(QKeySequence::New);
            buttons->button(QDialogButtonBox::Ok)->click();
            duplicate_shortcut_rejected = dialog->isVisible() && validation->isVisible() &&
                validation->text().contains(QStringLiteral("New Canvas"));
            reset->click();
            duplicate_shortcut_rejected = duplicate_shortcut_rejected &&
                editor->keySequence() == QKeySequence(Qt::Key_B) &&
                dialog->findChild<QKeySequenceEdit*>(
                    QStringLiteral("shortcutEditor_eraserToolAction"))->keySequence() ==
                    QKeySequence(Qt::Key_E);
            buttons->button(QDialogButtonBox::Ok)->click();
        });
    if (!duplicate_dialog_completed || !duplicate_shortcut_rejected ||
        paint_tool_action->shortcut() != QKeySequence(Qt::Key_B) ||
        eraser_tool_action->shortcut() != QKeySequence(Qt::Key_E)) {
        std::cerr << "Duplicate detection or Reset All did not restore the shortcut defaults.\n";
        return 1;
    }

    const bool shortcut_cleared = run_shortcut_dialog(
        [&](QDialog* dialog) {
            auto* clear = dialog->findChild<QPushButton*>(
                QStringLiteral("clearShortcut_paintToolAction"));
            auto* editor = dialog->findChild<QKeySequenceEdit*>(
                QStringLiteral("shortcutEditor_paintToolAction"));
            auto* buttons = dialog->findChild<QDialogButtonBox*>(
                QStringLiteral("shortcutSettingsButtons"));
            if (clear == nullptr || editor == nullptr || buttons == nullptr) {
                dialog->reject();
                return;
            }
            clear->click();
            const bool is_empty = editor->keySequence().isEmpty();
            buttons->button(QDialogButtonBox::Ok)->click();
            if (is_empty) QCoreApplication::processEvents();
        });
    if (!shortcut_cleared || !paint_tool_action->shortcut().isEmpty()) {
        std::cerr << "The Clear control did not remove the Paint shortcut.\n";
        return 1;
    }
    const bool defaults_restored = run_shortcut_dialog(
        [&](QDialog* dialog) {
            auto* reset = dialog->findChild<QPushButton*>(
                QStringLiteral("resetAllShortcutsButton"));
            auto* buttons = dialog->findChild<QDialogButtonBox*>(
                QStringLiteral("shortcutSettingsButtons"));
            if (reset == nullptr || buttons == nullptr) {
                dialog->reject();
                return;
            }
            reset->click();
            buttons->button(QDialogButtonBox::Ok)->click();
        });
    if (!defaults_restored || paint_tool_action->shortcut() != QKeySequence(Qt::Key_B) ||
        eraser_tool_action->shortcut() != QKeySequence(Qt::Key_E)) {
        std::cerr << "Reset All did not restore Paint's default B shortcut.\n";
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
    canvas = window.findChild<image_editor::ImageCanvas*>();
    if (canvas == nullptr) {
        std::cerr << "Opening an image did not create its document canvas.\n";
        return 1;
    }
    if (canvas->zoomFactor() <= 0.0) {
        std::cerr << "The image canvas did not calculate a fit scale.\n";
        return 1;
    }

    auto* layers_dock = window.findChild<QDockWidget*>(
        QStringLiteral("imageEditorLayersDock"));
    auto* layer_list = window.findChild<QTreeWidget*>(QStringLiteral("imageLayerTree"));
    if (layers_dock == nullptr || layer_list == nullptr || !layers_dock->isVisible() ||
        window.dockWidgetArea(layers_dock) != Qt::RightDockWidgetArea ||
        layerRowCount(layer_list) != 2 || layerRowItem(layer_list, 0)->text(0) != QStringLiteral("Layer 1") ||
        layerRowItem(layer_list, 1)->text(0) != QStringLiteral("Background") ||
        currentLayerRow(layer_list) != 0) {
        std::cerr << "The right-side layer dock did not show the default selected layer stack.\n";
        return 1;
    }
    const QImage editable_thumbnail =
        layerRowItem(layer_list, 0)->data(0, Qt::UserRole + 5).value<QImage>();
    const QImage background_thumbnail =
        layerRowItem(layer_list, 1)->data(0, Qt::UserRole + 5).value<QImage>();
    if (editable_thumbnail.isNull() || background_thumbnail.isNull() ||
        editable_thumbnail.width() > image_editor::LayerPanel::kThumbnailWidth ||
        editable_thumbnail.height() > image_editor::LayerPanel::kThumbnailHeight ||
        background_thumbnail.pixelColor(background_thumbnail.width() / 2,
                                        background_thumbnail.height() / 2) != Qt::blue) {
        std::cerr << "The layer rows did not receive isolated, aspect-fitted previews.\n";
        return 1;
    }
    QImage rendered_layer_list(layer_list->viewport()->size(), QImage::Format_ARGB32);
    rendered_layer_list.fill(Qt::transparent);
    {
        QPainter painter(&rendered_layer_list);
        layer_list->viewport()->render(&painter);
    }
    const QRect editable_row = layer_list->visualItemRect(layerRowItem(layer_list, 0));
    if (rendered_layer_list.pixelColor(editable_row.left() + 8, editable_row.top() + 6) !=
            QColor(205, 208, 214) ||
        rendered_layer_list.pixelColor(editable_row.left() + 16, editable_row.top() + 6) !=
            QColor(158, 162, 170)) {
        std::cerr << "Layer transparency checkerboard colors do not match the canvas.\n";
        return 1;
    }
    const QRect background_row = layer_list->visualItemRect(layerRowItem(layer_list, 1));
    QTest::mouseClick(layer_list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(background_row.right() - 17, background_row.center().y()));
    QCoreApplication::processEvents();
    if (currentLayerRow(layer_list) != 0 ||
        !layerRowItem(layer_list, 1)->data(0, Qt::AccessibleDescriptionRole).toString()
             .contains(QStringLiteral("Hidden"))) {
        std::cerr << "The right-side eye control did not hide Background independently of selection.\n";
        return 1;
    }
    const QRect hidden_background_row = layer_list->visualItemRect(layerRowItem(layer_list, 1));
    QTest::mouseClick(layer_list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(hidden_background_row.right() - 17,
                             hidden_background_row.center().y()));
    QCoreApplication::processEvents();
    if (currentLayerRow(layer_list) != 0 ||
        !layerRowItem(layer_list, 1)->data(0, Qt::AccessibleDescriptionRole).toString()
             .contains(QStringLiteral("Visible"))) {
        std::cerr << "The right-side eye control did not restore Background visibility.\n";
        return 1;
    }
    auto* visibility_undo_action = window.findChild<QAction*>(QStringLiteral("undoAction"));
    if (visibility_undo_action == nullptr) {
        std::cerr << "The visibility undo action was not exposed by the window.\n";
        return 1;
    }
    visibility_undo_action->trigger();
    visibility_undo_action->trigger();
    if (window.windowTitle().startsWith('*') || visibility_undo_action->isEnabled() ||
        layerRowItem(layer_list, 1)->data(0, Qt::AccessibleDescriptionRole).toString()
            .contains(QStringLiteral("Hidden"))) {
        std::cerr << "Undo did not restore the visibility baseline after the eye-button check.\n";
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
    auto* paint_tool_button = window.findChild<QToolButton*>(QStringLiteral("paintToolButton"));
    auto* layer_edit_hint = window.findChild<QLabel*>(QStringLiteral("layerEditingHint"));
    const QString title_before_layer_selection = window.windowTitle();
    setCurrentLayerRow(layer_list, 1);
    QCoreApplication::processEvents();
    if (paint_tool_button == nullptr || layer_edit_hint == nullptr ||
        paint_tool_button->isEnabled() || rotate_action->isEnabled() ||
        paint_tool_action->isEnabled() ||
        eraser_tool_action->isEnabled() ||
        !layer_edit_hint->isVisible() ||
        window.windowTitle() != title_before_layer_selection) {
        std::cerr << "Selecting locked Background did not disable painting and transforms.\n";
        return 1;
    }
    window.activateWindow();
    QTest::keyClick(&window, Qt::Key_B);
    QCoreApplication::processEvents();
    if (paint_tool_button->isChecked() || paint_tool_action->isChecked()) {
        std::cerr << "The Paint shortcut activated while Background was selected.\n";
        return 1;
    }
    setCurrentLayerRow(layer_list, 0);
    QCoreApplication::processEvents();
    if (!paint_tool_button->isEnabled() || !rotate_action->isEnabled() ||
        !paint_tool_action->isEnabled() || !eraser_tool_action->isEnabled()) {
        std::cerr << "Selecting an editable layer did not enable its tools.\n";
        return 1;
    }
    QTest::keyClick(&window, Qt::Key_B);
    QCoreApplication::processEvents();
    if (!paint_tool_button->isChecked() || !paint_tool_action->isChecked() ||
        !canvas->paintMode()) {
        std::cerr << "The B shortcut did not activate Paint and synchronize its button.\n";
        return 1;
    }
    QTest::keyClick(&window, Qt::Key_B);
    QCoreApplication::processEvents();
    if (paint_tool_button->isChecked() || paint_tool_action->isChecked() ||
        canvas->paintMode()) {
        std::cerr << "The B shortcut did not deactivate Paint and synchronize its button.\n";
        return 1;
    }
    auto* eraser_tool_button = window.findChild<QToolButton*>(
        QStringLiteral("eraserToolButton"));
    QTest::keyClick(&window, Qt::Key_E);
    QCoreApplication::processEvents();
    if (eraser_tool_button == nullptr || !eraser_tool_button->isChecked() ||
        !eraser_tool_action->isChecked() || !canvas->eraserMode() || canvas->paintMode()) {
        std::cerr << "The E shortcut did not activate the exclusive Eraser tool.\n";
        return 1;
    }
    QTest::keyClick(&window, Qt::Key_E);
    QCoreApplication::processEvents();
    if (eraser_tool_button->isChecked() || eraser_tool_action->isChecked() ||
        canvas->eraserMode()) {
        std::cerr << "The E shortcut did not deactivate Eraser.\n";
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

    auto* tool_sidebar = window.findChild<image_editor::ToolSidebar*>(
        QStringLiteral("imageEditorToolSidebar"));
    auto* paint_button = window.findChild<QToolButton*>(QStringLiteral("paintToolButton"));
    auto* eraser_button = window.findChild<QToolButton*>(QStringLiteral("eraserToolButton"));
    auto* shapes_button = window.findChild<QToolButton*>(QStringLiteral("shapesToolButton"));
    auto* color_button = window.findChild<QToolButton*>(QStringLiteral("paintBrushColorButton"));
    auto* tool_options_toolbar = window.findChild<QToolBar*>(
        QStringLiteral("toolOptionsToolBar"));
    auto* paint_options_action = window.findChild<QAction*>(
        QStringLiteral("paintBrushSizeAction"));
    auto* paint_size_options = window.findChild<QWidget*>(
        QStringLiteral("paintBrushSizeOptions"));
    auto* brush_size_slider = window.findChild<QSlider*>(
        QStringLiteral("paintBrushSizeSlider"));
    auto* brush_size = window.findChild<QSpinBox*>(QStringLiteral("paintBrushSizeSpinBox"));
    auto* tool_size_label = window.findChild<QLabel*>(QStringLiteral("paintBrushSizeLabel"));
    auto* eraser_preview = window.findChild<QCheckBox*>(QStringLiteral("eraserPreviewCheckBox"));
    auto* redo_action = window.findChild<QAction*>(QStringLiteral("redoAction"));
    auto* crop_action = window.findChild<QAction*>(QStringLiteral("cropSelectionAction"));
    if (tool_sidebar == nullptr || paint_button == nullptr || eraser_button == nullptr ||
        shapes_button == nullptr ||
        color_button == nullptr || tool_size_label == nullptr || eraser_preview == nullptr ||
        tool_options_toolbar == nullptr || paint_options_action == nullptr ||
        paint_size_options == nullptr ||
        brush_size_slider == nullptr || brush_size == nullptr || redo_action == nullptr ||
        crop_action == nullptr || tool_sidebar->findChildren<QToolButton*>().size() != 6 ||
        paint_button->isChecked() || paint_options_action->isVisible() ||
        paint_size_options->isVisible() ||
        !tool_options_toolbar->isVisible() || tool_options_toolbar->height() < 40 ||
        !color_button->isVisible() ||
        !color_button->isEnabled() || color_button->y() <= paint_button->y() ||
        color_button->geometry().bottom() < tool_sidebar->height() - 20 ||
        color_button->text().size() != 0 || color_button->toolTip() != QStringLiteral("Paint color") ||
        color_button->icon().isNull() ||
        !paint_button->text().isEmpty() ||
        paint_button->toolButtonStyle() != Qt::ToolButtonIconOnly ||
        paint_button->toolTip() != QStringLiteral("Paint") ||
        tool_sidebar->width() != 56 ||
        tool_sidebar->brushColor() != QColor(Qt::black) ||
        eraser_button->isChecked() || eraser_preview->isVisible() ||
        brush_size->value() != 12 || brush_size_slider->value() != 12 ||
        brush_size->minimum() != 1 ||
        brush_size->maximum() != image_editor::ImageDocumentStore::kMaximumPaintBrushDiameter ||
        brush_size_slider->minimum() != 1 ||
        brush_size_slider->maximum() != image_editor::ImageDocumentStore::kMaximumPaintBrushDiameter) {
        std::cerr << "The paint controls did not start in the expected compact layout.\n";
        return 1;
    }

    const QColor selected_brush_color(211, 75, 20, 128);
    bool color_dialog_was_used = false;
    QTimer::singleShot(0, [&]() {
        auto* dialog = qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr) return;
        dialog->setCurrentColor(selected_brush_color);
        dialog->accept();
        color_dialog_was_used = true;
    });
    color_button->click();
    if (!color_dialog_was_used || tool_sidebar->brushColor() != selected_brush_color ||
        window.windowTitle().startsWith('*') || undo_action->isEnabled()) {
        std::cerr << "The color swatch did not open and apply the alpha-capable color picker.\n";
        return 1;
    }

    paint_button->click();
    if (!paint_button->isChecked() || !paint_options_action->isVisible() ||
        !paint_size_options->isVisible() ||
        !brush_size->isVisible() || !brush_size_slider->isVisible() ||
        !canvas->paintMode() || tool_sidebar->width() != 56 ||
        !tool_options_toolbar->isVisible()) {
        std::cerr << "Activating Paint did not expose its top-bar controls and canvas mode. "
                  << "checked=" << paint_button->isChecked()
                  << ", action=" << paint_options_action->isVisible()
                  << ", options=" << paint_size_options->isVisible()
                  << ", spin=" << brush_size->isVisible()
                  << ", slider=" << brush_size_slider->isVisible()
                  << ", canvas=" << canvas->paintMode()
                  << ", toolbar=" << tool_options_toolbar->isVisible() << '\n';
        return 1;
    }
    brush_size_slider->setValue(14);
    if (brush_size->value() != 14) {
        std::cerr << "The brush size slider did not synchronize with the numeric field.\n";
        return 1;
    }
    brush_size->setValue(2);
    if (brush_size_slider->value() != 2 || window.windowTitle().startsWith('*') ||
        undo_action->isEnabled()) {
        std::cerr << "The brush size field did not synchronize with the slider.\n";
        return 1;
    }
    paint_button->click();
    if (paint_button->isChecked() || canvas->paintMode() ||
        paint_options_action->isVisible() || paint_size_options->isVisible()) {
        std::cerr << "Turning Paint off from its tool button did not hide its options.\n";
        return 1;
    }
    const QPoint brush_resize_anchor = canvas->rect().center();
    QTest::mousePress(canvas, Qt::LeftButton,
                      Qt::ControlModifier | Qt::AltModifier,
                      brush_resize_anchor);
    QTest::mouseMove(canvas, brush_resize_anchor + QPoint(20, 0));
    QTest::mouseRelease(canvas, Qt::LeftButton,
                        Qt::ControlModifier | Qt::AltModifier,
                        brush_resize_anchor + QPoint(20, 0));
    if (brush_size->value() != 2 || brush_size_slider->value() != 2) {
        std::cerr << "The brush resize gesture worked while Paint was inactive.\n";
        return 1;
    }
    paint_button->click();
    if (!paint_button->isChecked() || !canvas->paintMode() ||
        !paint_options_action->isVisible() || !paint_size_options->isVisible()) {
        std::cerr << "Reactivating Paint did not restore its options.\n";
        return 1;
    }
    const bool document_was_clean_before_brush_resize =
        !window.windowTitle().startsWith('*') && !undo_action->isEnabled();
    QTest::mousePress(canvas, Qt::LeftButton,
                      Qt::ControlModifier | Qt::AltModifier,
                      brush_resize_anchor);
    QTest::mouseMove(canvas, brush_resize_anchor + QPoint(20, 0));
    const bool moving_right_increased_brush = brush_size->value() == 22 &&
        brush_size_slider->value() == 22;
    QTest::mouseMove(canvas, brush_resize_anchor + QPoint(0, 40));
    const bool vertical_motion_did_not_change_brush = brush_size->value() == 2 &&
        brush_size_slider->value() == 2;
    QTest::mouseMove(canvas, brush_resize_anchor + QPoint(-1, 0));
    const bool moving_left_reduced_brush_to_minimum = brush_size->value() == 1 &&
        brush_size_slider->value() == 1;
    QTest::mouseRelease(canvas, Qt::LeftButton,
                        Qt::ControlModifier | Qt::AltModifier,
                        brush_resize_anchor + QPoint(-1, 0));

    brush_size->setValue(20);
    QTest::mousePress(canvas, Qt::LeftButton,
                      Qt::ControlModifier | Qt::AltModifier,
                      brush_resize_anchor);
    QTest::mouseMove(canvas, brush_resize_anchor + QPoint(-5, 0));
    const bool moving_left_reduced_brush = brush_size->value() == 15 &&
        brush_size_slider->value() == 15;
    QTest::mouseMove(canvas, brush_resize_anchor + QPoint(5, 0));
    const bool crossing_anchor_increased_brush = brush_size->value() == 25 &&
        brush_size_slider->value() == 25;
    QTest::mouseRelease(canvas, Qt::LeftButton,
                        Qt::ControlModifier | Qt::AltModifier,
                        brush_resize_anchor + QPoint(5, 0));

    brush_size->setValue(500);
    QTest::mousePress(canvas, Qt::LeftButton,
                      Qt::ControlModifier | Qt::AltModifier,
                      brush_resize_anchor);
    QTest::mouseMove(canvas, brush_resize_anchor + QPoint(600, 0));
    const bool brush_size_is_capped = brush_size->value() ==
            image_editor::ImageDocumentStore::kMaximumPaintBrushDiameter &&
        brush_size_slider->value() ==
            image_editor::ImageDocumentStore::kMaximumPaintBrushDiameter;
    QTest::mouseMove(canvas, brush_resize_anchor + QPoint(-600, 0));
    const bool brush_size_is_clamped_to_minimum = brush_size->value() == 1 &&
        brush_size_slider->value() == 1;
    QTest::mouseMove(canvas, brush_resize_anchor);
    const bool brush_returns_to_press_value = brush_size->value() == 500 &&
        brush_size_slider->value() == 500;
    QTest::mouseRelease(canvas, Qt::LeftButton,
                        Qt::ControlModifier | Qt::AltModifier,
                        brush_resize_anchor);
    brush_size->setValue(2);
    if (!document_was_clean_before_brush_resize || !moving_right_increased_brush ||
        !vertical_motion_did_not_change_brush || !moving_left_reduced_brush_to_minimum ||
        !moving_left_reduced_brush || !crossing_anchor_increased_brush ||
        !brush_size_is_capped || !brush_size_is_clamped_to_minimum ||
        !brush_returns_to_press_value ||
        window.windowTitle().startsWith('*') || undo_action->isEnabled()) {
        std::cerr << "The brush resize gesture did not update controls without editing the document.\n";
        return 1;
    }

    const auto hasBrightPixelNear = [](const QImage& image, const QPoint& center) {
        for (int y = center.y() - 3; y <= center.y() + 3; ++y) {
            for (int x = center.x() - 3; x <= center.x() + 3; ++x) {
                if (!image.rect().contains(x, y)) continue;
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.red() >= 220 && pixel.green() >= 220 && pixel.blue() >= 220) {
                    return true;
                }
            }
        }
        return false;
    };
    brush_size->setValue(12);
    const QPoint preview_anchor = canvas->rect().center();
    const QPoint expected_restored_cursor = canvas->mapToGlobal(preview_anchor);
    QTest::mousePress(canvas, Qt::LeftButton,
                      Qt::ControlModifier | Qt::AltModifier, preview_anchor);
    const QPoint preview_drag_position = preview_anchor + QPoint(20, 5);
    QTest::mouseMove(canvas, preview_drag_position);
    QCoreApplication::processEvents();
    const QImage resized_brush_preview = canvas->grab().toImage();
    const qreal resized_brush_radius = std::max(
        3.0, brush_size->value() * canvas->zoomFactor()) / 2.0;
    const QPoint anchored_edge(qRound(preview_anchor.x() + resized_brush_radius),
                               preview_anchor.y());
    const QPoint moved_edge(qRound(preview_drag_position.x() + resized_brush_radius),
                            preview_drag_position.y());
    const bool brush_preview_stayed_at_anchor =
        hasBrightPixelNear(resized_brush_preview, anchored_edge) &&
        !hasBrightPixelNear(resized_brush_preview, moved_edge);

    const QPoint outside_image(0, preview_anchor.y());
    QTest::mouseMove(canvas, outside_image);
    QCoreApplication::processEvents();
    const QImage resized_outside_preview = canvas->grab().toImage();
    const qreal minimum_brush_radius = std::max(
        3.0, brush_size->value() * canvas->zoomFactor()) / 2.0;
    const QPoint outside_drag_anchor_edge(
        qRound(preview_anchor.x() + minimum_brush_radius), preview_anchor.y());
    const bool brush_preview_remained_visible_outside_image =
        brush_size->value() == 1 &&
        hasBrightPixelNear(resized_outside_preview, outside_drag_anchor_edge);

    QTest::mouseRelease(canvas, Qt::LeftButton,
                        Qt::ControlModifier | Qt::AltModifier, outside_image);
    QCoreApplication::processEvents();
    const QImage released_preview = canvas->grab().toImage();
    const bool cursor_restored_to_press_position =
        QCursor::pos() == expected_restored_cursor;
    const bool brush_preview_stayed_at_anchor_after_release =
        hasBrightPixelNear(released_preview, outside_drag_anchor_edge);
    const QPoint reentered_image = preview_anchor + QPoint(80, 0);
    QTest::mouseMove(canvas, reentered_image);
    QCoreApplication::processEvents();
    const QImage reentered_preview = canvas->grab().toImage();
    const bool brush_preview_followed_cursor_after_release =
        hasBrightPixelNear(reentered_preview,
                           QPoint(qRound(reentered_image.x() + minimum_brush_radius),
                                  reentered_image.y())) &&
        !hasBrightPixelNear(reentered_preview, outside_drag_anchor_edge);
    brush_size->setValue(2);
    if (!brush_preview_stayed_at_anchor ||
        !brush_preview_remained_visible_outside_image ||
        !cursor_restored_to_press_position ||
        !brush_preview_stayed_at_anchor_after_release ||
        !brush_preview_followed_cursor_after_release ||
        window.windowTitle().startsWith('*') || undo_action->isEnabled()) {
        std::cerr << "The brush preview did not stay anchored during resizing and resume cursor tracking after release.\n";
        return 1;
    }

    const QPoint paint_center = canvas->rect().center();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier,
                      paint_center - QPoint(20, 0));
    QTest::mouseMove(canvas, paint_center + QPoint(20, 0));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier,
                        paint_center + QPoint(20, 0));
    if (!window.windowTitle().startsWith('*') || !undo_action->isEnabled() ||
        !canvas->paintMode()) {
        std::cerr << "A paint drag did not create one undoable document edit.\n";
        return 1;
    }
    undo_action->trigger();
    if (window.windowTitle().startsWith('*') || !redo_action->isEnabled() ||
        undo_action->isEnabled()) {
        std::cerr << "Undo did not restore the saved image after a paint stroke.\n";
        return 1;
    }
    redo_action->trigger();
    if (!window.windowTitle().startsWith('*')) {
        std::cerr << "Redo did not restore the paint stroke in the window.\n";
        return 1;
    }

    QSignalSpy live_erase_preview_spy(canvas, &image_editor::ImageCanvas::erasePreviewRequested);
    QSignalSpy erase_committed_spy(canvas, &image_editor::ImageCanvas::eraseStrokeSelected);
    const QColor painted_center_pixel = canvas->grab().toImage().pixelColor(paint_center);
    eraser_button->click();
    if (!eraser_button->isChecked() || paint_button->isChecked() || !canvas->eraserMode() ||
        !paint_options_action->isVisible() || !eraser_preview->isVisible() ||
        eraser_preview->isChecked() || tool_size_label->text() != QStringLiteral("Eraser Size") ||
        brush_size->value() != 12) {
        std::cerr << "Eraser did not activate with its independent size and Preview off.\n";
        return 1;
    }
    brush_size->setValue(18);
    paint_button->click();
    const bool paint_size_was_independent = brush_size->value() == 2 && canvas->paintMode();
    eraser_button->click();
    const bool eraser_size_was_restored = brush_size->value() == 18 && canvas->eraserMode();
    const QPoint erase_start = paint_center - QPoint(20, 0);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, erase_start);
    QTest::mouseMove(canvas, paint_center + QPoint(20, 0));
    QCoreApplication::processEvents();
    const bool default_eraser_preview_was_live = live_erase_preview_spy.size() > 0;
    const QColor live_erased_center_pixel = canvas->grab().toImage().pixelColor(paint_center);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier,
                        paint_center + QPoint(20, 0));
    const bool live_erase_committed_once = erase_committed_spy.size() == 1 &&
        window.windowTitle().startsWith('*') && undo_action->isEnabled();
    undo_action->trigger();
    const bool live_erase_undo_restored_paint = window.windowTitle().startsWith('*');
    redo_action->trigger();
    undo_action->trigger();

    eraser_preview->setChecked(true);
    const int live_preview_count_before_overlay = live_erase_preview_spy.size();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, erase_start);
    QTest::mouseMove(canvas, paint_center + QPoint(20, 0));
    QCoreApplication::processEvents();
    const bool overlay_preview_kept_layer_pixels_until_release =
        live_erase_preview_spy.size() == live_preview_count_before_overlay &&
        erase_committed_spy.size() == 1;
    const QColor overlay_center_pixel = canvas->grab().toImage().pixelColor(paint_center);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier,
                        paint_center + QPoint(20, 0));
    const bool overlay_erase_committed_once = erase_committed_spy.size() == 2;
    undo_action->trigger();
    redo_action->trigger();
    undo_action->trigger();
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, erase_start);
    QTest::mouseMove(canvas, paint_center + QPoint(15, 0));
    QTest::keyClick(canvas, Qt::Key_Escape);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier,
                        paint_center + QPoint(15, 0));
    const bool erase_cancelled_without_commit = erase_committed_spy.size() == 2;
    eraser_preview->setChecked(false);
    const QPoint eraser_resize_anchor = canvas->rect().center();
    const bool clean_before_eraser_resize = window.windowTitle().startsWith('*') &&
        undo_action->isEnabled();
    QTest::mousePress(canvas, Qt::LeftButton,
                      Qt::ControlModifier | Qt::AltModifier, eraser_resize_anchor);
    QTest::mouseMove(canvas, eraser_resize_anchor + QPoint(5, 0));
    QTest::mouseRelease(canvas, Qt::LeftButton,
                        Qt::ControlModifier | Qt::AltModifier,
                        eraser_resize_anchor + QPoint(5, 0));
    const bool eraser_resize_changed_only_eraser_size = brush_size->value() == 23;
    paint_button->click();
    const bool paint_size_survived_eraser_resize = brush_size->value() == 2;
    eraser_button->click();
    const bool eraser_size_survived_tool_switch = brush_size->value() == 23;
    if (!paint_size_was_independent || !eraser_size_was_restored ||
        !default_eraser_preview_was_live || !live_erase_committed_once ||
        painted_center_pixel.blue() <= painted_center_pixel.red() ||
        live_erased_center_pixel.blue() <= live_erased_center_pixel.red() * 2 ||
        !live_erase_undo_restored_paint || !overlay_preview_kept_layer_pixels_until_release ||
        overlay_center_pixel.red() <= overlay_center_pixel.blue() ||
        !overlay_erase_committed_once || !erase_cancelled_without_commit ||
        !clean_before_eraser_resize || !eraser_resize_changed_only_eraser_size ||
        !paint_size_survived_eraser_resize || !eraser_size_survived_tool_switch ||
        !canvas->eraserMode() || !eraser_button->isChecked()) {
        std::cerr << "Eraser preview, commit/cancel, independent sizing, or resize gesture failed.\n";
        return 1;
    }

    undo_action->trigger();
    crop_action->trigger();
    if (!crop_action->isChecked() || !cancel_crop_action->isEnabled() ||
        paint_button->isChecked() || eraser_button->isChecked() ||
        !canvas->cropMode() || canvas->paintMode() || canvas->eraserMode() ||
        tool_sidebar->width() != 56 ||
        paint_options_action->isVisible() || paint_size_options->isVisible() ||
        !tool_options_toolbar->isVisible()) {
        std::cerr << "Crop mode did not deactivate the paint tool.\n";
        return 1;
    }
    QSignalSpy cancel_crop_spy(cancel_crop_action, &QAction::triggered);
    window.activateWindow();
    QCoreApplication::processEvents();
    QTest::keyClick(&window, Qt::Key_Escape);
    QCoreApplication::processEvents();
    if (crop_action->isChecked() || cancel_crop_action->isEnabled() ||
        canvas->cropMode()) {
        std::cerr << "Escape did not cancel crop through its configurable action. triggered="
                  << cancel_crop_spy.size() << " active=" << window.isActiveWindow()
                  << " shortcut=" << cancel_crop_action->shortcut().toString().toStdString()
                  << " crop=" << crop_action->isChecked()
                  << " actionEnabled=" << cancel_crop_action->isEnabled() << '\n';
        return 1;
    }

    auto* new_canvas_action = window.findChild<QAction*>(QStringLiteral("newCanvasAction"));
    auto* status_label = window.findChild<QLabel*>(QStringLiteral("imageStatusLabel"));
    if (new_canvas_action == nullptr || status_label == nullptr ||
        new_canvas_action->shortcut() != QKeySequence::New) {
        std::cerr << "The New Canvas action or status projection is missing.\n";
        return 1;
    }

    paint_button->click();
    if (!paint_button->isChecked() || !canvas->paintMode()) {
        std::cerr << "The paint tool could not be activated before replacing the document.\n";
        return 1;
    }
    bool preset_values_valid = true;
    QTimer::singleShot(0, [&preset_values_valid]() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr) return;
        auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("newCanvasPresetCombo"));
        auto* background = dialog->findChild<QComboBox*>(QStringLiteral("newCanvasBackgroundCombo"));
        auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("canvasWidthSpin"));
        auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("canvasHeightSpin"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("newCanvasButtons"));
        if (preset == nullptr || background == nullptr || width == nullptr ||
            height == nullptr || buttons == nullptr) return;
        constexpr std::array<QSize, 5> expected_presets = {
            QSize(1080, 1080), QSize(1080, 1350), QSize(1080, 1920),
            QSize(1920, 1080), QSize(2480, 3508)};
        for (int i = 0; i < static_cast<int>(expected_presets.size()); ++i) {
            preset->setCurrentIndex(i + 1);
            if (width->value() != expected_presets.at(i).width() ||
                height->value() != expected_presets.at(i).height()) {
                preset_values_valid = false;
            }
        }
        preset->setCurrentIndex(3); // Story / Reel preset
        background->setCurrentIndex(2); // White
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    new_canvas_action->trigger();
    if (!preset_values_valid) {
        std::cerr << "One or more canvas presets has incorrect dimensions.\n";
        return 1;
    }
    const QString story_size = QStringLiteral("1080 × 1920 px");
    if (!status_label->text().contains(story_size) ||
        !window.windowTitle().startsWith('*') || paint_button->isChecked() ||
        canvas->paintMode()) {
        std::cerr << "Creating a preset canvas did not update dimensions and dirty state.\n";
        return 1;
    }
    rotate_action->trigger();
    if (!status_label->text().contains(story_size)) {
        std::cerr << "Layer rotation changed the fixed canvas dimensions in the window.\n";
        return 1;
    }
    undo_action->trigger();
    if (!status_label->text().contains(story_size)) {
        std::cerr << "Undo did not restore the preset canvas dimensions.\n";
        return 1;
    }

    rotate_action->trigger(); // Leave an edit to exercise the replacement prompt.
    QTimer::singleShot(0, []() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr) return;
        auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("newCanvasPresetCombo"));
        auto* background = dialog->findChild<QComboBox*>(QStringLiteral("newCanvasBackgroundCombo"));
        auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("canvasWidthSpin"));
        auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("canvasHeightSpin"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("newCanvasButtons"));
        if (preset == nullptr || background == nullptr || width == nullptr ||
            height == nullptr || buttons == nullptr) return;
        preset->setCurrentIndex(6); // Custom dimensions
        width->setValue(100);
        height->setValue(80);
        background->setCurrentIndex(1); // Transparent
        QTimer::singleShot(0, []() {
            auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
            if (prompt == nullptr) return;
            for (auto* button : prompt->buttons()) {
                if (button->text() == QStringLiteral("Discard")) {
                    button->click();
                    return;
                }
            }
        });
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    new_canvas_action->trigger();
    if (!status_label->text().contains(QStringLiteral("100 × 80 px")) ||
        !window.windowTitle().startsWith('*')) {
        std::cerr << "Custom canvas creation or dirty replacement handling failed.\n";
        return 1;
    }

    auto* resize_canvas_action = window.findChild<QAction*>(QStringLiteral("resizeCanvasAction"));
    if (resize_canvas_action == nullptr || redo_action == nullptr ||
        !resize_canvas_action->isEnabled()) {
        std::cerr << "Image > Canvas Size is missing or disabled for an open document.\n";
        return 1;
    }
    const QString size_before_cancel = status_label->text();
    QTimer::singleShot(0, []() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr || dialog->objectName() != QStringLiteral("canvasSizeDialog")) return;
        auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("canvasSizeButtons"));
        if (buttons != nullptr) buttons->button(QDialogButtonBox::Cancel)->click();
    });
    resize_canvas_action->trigger();
    if (status_label->text() != size_before_cancel) {
        std::cerr << "Cancelling Canvas Size changed the document.\n";
        return 1;
    }
    bool center_anchor_default = false;
    QTimer::singleShot(0, [&center_anchor_default]() {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (dialog == nullptr || dialog->objectName() != QStringLiteral("canvasSizeDialog")) return;
        auto* preset = dialog->findChild<QComboBox*>(QStringLiteral("canvasSizePresetCombo"));
        auto* width = dialog->findChild<QSpinBox*>(QStringLiteral("canvasSizeWidthSpin"));
        auto* height = dialog->findChild<QSpinBox*>(QStringLiteral("canvasSizeHeightSpin"));
        auto* center = dialog->findChild<QRadioButton*>(QStringLiteral("canvasSizeAnchorCenterButton"));
        auto* bottom_right = dialog->findChild<QRadioButton*>(QStringLiteral("canvasSizeAnchorBottomrightButton"));
        auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("canvasSizeButtons"));
        if (preset == nullptr || width == nullptr || height == nullptr || center == nullptr ||
            bottom_right == nullptr || buttons == nullptr) return;
        center_anchor_default = center->isChecked();
        preset->setCurrentIndex(5);
        width->setValue(130);
        height->setValue(90);
        bottom_right->setChecked(true);
        buttons->button(QDialogButtonBox::Ok)->click();
    });
    resize_canvas_action->trigger();
    if (!center_anchor_default || !status_label->text().contains(QStringLiteral("130 × 90 px"))) {
        std::cerr << "Canvas Size did not use the expected dimensions or centered default anchor.\n";
        return 1;
    }
    undo_action->trigger();
    if (!status_label->text().contains(QStringLiteral("100 × 80 px"))) {
        std::cerr << "Undo did not restore the previous canvas dimensions.\n";
        return 1;
    }
    redo_action->trigger();
    if (!status_label->text().contains(QStringLiteral("130 × 90 px"))) {
        std::cerr << "Redo did not restore the resized canvas dimensions.\n";
        return 1;
    }

    auto* add_layer_button = window.findChild<QToolButton*>(
        QStringLiteral("addImageLayerButton"));
    auto* delete_layer_button = window.findChild<QToolButton*>(
        QStringLiteral("deleteImageLayerButton"));
    auto* move_layer_down_button = window.findChild<QToolButton*>(
        QStringLiteral("moveImageLayerDownButton"));
    auto* opacity_slider = window.findChild<QSlider*>(
        QStringLiteral("imageLayerOpacitySlider"));
    if (add_layer_button == nullptr || delete_layer_button == nullptr ||
        move_layer_down_button == nullptr || opacity_slider == nullptr) {
        std::cerr << "The layer panel controls were not created.\n";
        return 1;
    }
    add_layer_button->click();
    if (layerRowCount(layer_list) != 3 || layer_list->currentItem() == nullptr ||
        layer_list->currentItem()->text(0) != QStringLiteral("Layer 2")) {
        std::cerr << "The layer panel did not add and select a new layer.\n";
        return 1;
    }
    layer_list->currentItem()->setText(0, QStringLiteral("Overlay"));
    QCoreApplication::processEvents();
    if (layer_list->currentItem() == nullptr ||
        layer_list->currentItem()->text(0) != QStringLiteral("Overlay")) {
        std::cerr << "Inline layer renaming did not update the selected layer.\n";
        return 1;
    }
    move_layer_down_button->click();
    if (currentLayerRow(layer_list) != 1 ||
        layer_list->currentItem()->text(0) != QStringLiteral("Overlay")) {
        std::cerr << "Layer reordering did not preserve selection and order.\n";
        return 1;
    }
    opacity_slider->setValue(60);
    if (opacity_slider->value() != 60) {
        std::cerr << "The selected layer opacity control did not update.\n";
        return 1;
    }
    delete_layer_button->click();
    if (layerRowCount(layer_list) != 2 || layer_list->currentItem() == nullptr ||
        layer_list->currentItem()->text(0) != QStringLiteral("Layer 1")) {
        std::cerr << "Deleting a layer did not select the adjacent editable layer.\n";
        return 1;
    }
    setCurrentLayerRow(layer_list, 1);
    QCoreApplication::processEvents();
    if (paint_button->isEnabled() || eraser_button->isEnabled() ||
        !shapes_button->isEnabled() || !shapes_tool_action->isEnabled() ||
        opacity_slider->isEnabled()) {
        std::cerr << "The panel did not lock editing controls for Background.\n";
        return 1;
    }
    shapes_tool_action->trigger();
    QCoreApplication::processEvents();
    if (tool_sidebar->activeTool() != image_editor::ToolSidebar::Tool::Shapes ||
        !shapes_button->isChecked()) {
        std::cerr << "Shapes could not remain active with Background selected.\n";
        return 1;
    }
    const auto canvasPoint = [](image_editor::ImageCanvas* target,
                                const QSize& image_size,
                                const QPointF& point) {
        const qreal zoom = target->zoomFactor();
        return QPoint(qRound((target->width() - image_size.width() * zoom) / 2.0 +
                             point.x() * zoom),
                      qRound((target->height() - image_size.height() * zoom) / 2.0 +
                             point.y() * zoom));
    };
    const QPoint background_shape_start = canvasPoint(canvas, QSize(100, 80), QPointF(70, 55));
    const QPoint background_shape_end = canvasPoint(canvas, QSize(100, 80), QPointF(85, 70));
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, background_shape_start);
    QTest::mouseMove(canvas, background_shape_end);
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, background_shape_end);
    QCoreApplication::processEvents();
    if (layerRowCount(layer_list) != 3 || currentLayerRow(layer_list) != 1 ||
        layer_list->currentItem() == nullptr ||
        layer_list->currentItem()->text(0) != QStringLiteral("Shape 1")) {
        std::cerr << "Drawing with Background selected did not insert Shape 1 above it.\n";
        return 1;
    }
    undo_action->trigger();
    if (layerRowCount(layer_list) != 2 || currentLayerRow(layer_list) != 1 ||
        layer_list->currentItem() == nullptr ||
        layer_list->currentItem()->text(0) != QStringLiteral("Background")) {
        std::cerr << "Undo did not remove the new shape layer and restore Background selection.\n";
        return 1;
    }
    redo_action->trigger();
    if (layerRowCount(layer_list) != 3 || currentLayerRow(layer_list) != 1 ||
        layer_list->currentItem() == nullptr ||
        layer_list->currentItem()->text(0) != QStringLiteral("Shape 1")) {
        std::cerr << "Redo did not restore the shape layer and its selection.\n";
        return 1;
    }

    const QString linked_directory = temporary.filePath(QStringLiteral("linked-image"));
    if (!QDir().mkpath(linked_directory)) {
        std::cerr << "The linked image test directory could not be created.\n";
        return 1;
    }
    const QString linked_source = linked_directory + QStringLiteral("/original.png");
    const QString linked_document = linked_directory + QStringLiteral("/asset.cimg");
    const QString linked_output = linked_directory + QStringLiteral("/published.png");
    QImage linked_source_image(32, 24, QImage::Format_ARGB32);
    linked_source_image.fill(QColor(240, 20, 10, 255));
    linked_source_image.setPixelColor(0, 0, QColor(0, 0, 0, 0));
    if (!linked_source_image.save(linked_source)) {
        std::cerr << "The linked source image could not be created.\n";
        return 1;
    }
    QFile original_source_file(linked_source);
    if (!original_source_file.open(QIODevice::ReadOnly)) return 1;
    const QByteArray original_source_bytes = original_source_file.readAll();
    original_source_file.close();

    image_editor::ImageEditorWindow linked_window;
    linked_window.show();
    if (!linked_window.openLinkedImage(linked_source, linked_document, linked_output) ||
        !QFileInfo::exists(linked_document) || !QFileInfo::exists(linked_output)) {
        std::cerr << "Linked mode did not create and publish its editable document.\n";
        return 1;
    }
    auto* linked_canvas = linked_window.findChild<image_editor::ImageCanvas*>();
    auto* linked_paint_action = linked_window.findChild<QAction*>(
        QStringLiteral("paintToolAction"));
    auto* linked_save_action = linked_window.findChild<QAction*>(
        QStringLiteral("saveDocumentAction"));
    if (linked_canvas == nullptr || linked_paint_action == nullptr ||
        linked_save_action == nullptr) {
        std::cerr << "Linked mode did not expose the normal editing and save actions.\n";
        return 1;
    }
    QImage published_before_edit(linked_output);
    if (published_before_edit.isNull() ||
        published_before_edit.pixelColor(16, 12) != QColor(240, 20, 10, 255) ||
        published_before_edit.pixelColor(0, 0).alpha() != 0) {
        std::cerr << "The first linked PNG did not preserve the source pixels and transparency.\n";
        return 1;
    }
    linked_paint_action->trigger();
    QCoreApplication::processEvents();
    QTest::mouseClick(linked_canvas, Qt::LeftButton, Qt::NoModifier,
                      linked_canvas->rect().center());
    QCoreApplication::processEvents();
    QImage published_before_save(linked_output);
    if (published_before_save.isNull() ||
        published_before_save.pixelColor(16, 12) != QColor(240, 20, 10, 255)) {
        std::cerr << "Unsaved linked edits were published before Save.\n";
        return 1;
    }
    linked_save_action->trigger();
    QCoreApplication::processEvents();
    QImage published_after_save(linked_output);
    if (published_after_save.isNull() ||
        published_after_save.pixelColor(16, 12) == QColor(240, 20, 10, 255)) {
        std::cerr << "Saving the linked document did not publish the edited PNG.\n";
        return 1;
    }
    QFile source_after_edit(linked_source);
    if (!source_after_edit.open(QIODevice::ReadOnly) ||
        source_after_edit.readAll() != original_source_bytes) {
        std::cerr << "Linked editing changed the original image.\n";
        return 1;
    }
    source_after_edit.close();

    auto* linked_shapes_action = linked_window.findChild<QAction*>(
        QStringLiteral("shapesToolAction"));
    auto* linked_select_shapes_action = linked_window.findChild<QAction*>(
        QStringLiteral("selectShapesToolAction"));
    auto* linked_shapes_button = linked_window.findChild<QToolButton*>(
        QStringLiteral("shapesToolButton"));
    auto* linked_select_shapes_button = linked_window.findChild<QToolButton*>(
        QStringLiteral("selectShapesToolButton"));
    auto* delete_objects_button = linked_window.findChild<QPushButton*>(
        QStringLiteral("deleteSelectedShapeButton"));
    auto* shape_options_action = linked_window.findChild<QAction*>(
        QStringLiteral("shapeOptionsAction"));
    auto* shape_palette = linked_window.findChild<QDialog*>(
        QStringLiteral("shapePaletteWindow"));
    auto* shape_line_button = linked_window.findChild<QToolButton*>(
        QStringLiteral("shapePaletteLineButton"));
    auto* shape_rectangle_button = linked_window.findChild<QToolButton*>(
        QStringLiteral("shapePaletteRectangleButton"));
    auto* shape_ellipse_button = linked_window.findChild<QToolButton*>(
        QStringLiteral("shapePaletteEllipseButton"));
    auto* shape_stroke = linked_window.findChild<QCheckBox*>(
        QStringLiteral("shapeStrokeCheckBox"));
    auto* shape_fill = linked_window.findChild<QCheckBox*>(
        QStringLiteral("shapeFillCheckBox"));
    auto* shape_width = linked_window.findChild<QSpinBox*>(
        QStringLiteral("shapeStrokeWidthSpinBox"));
    auto* shape_stroke_color = linked_window.findChild<QPushButton*>(
        QStringLiteral("shapeStrokeColorButton"));
    auto* shape_fill_color = linked_window.findChild<QPushButton*>(
        QStringLiteral("shapeFillColorButton"));
    auto* linked_layer_list = linked_window.findChild<QTreeWidget*>(
        QStringLiteral("imageLayerTree"));
    auto* linked_tool_sidebar = linked_window.findChild<image_editor::ToolSidebar*>(
        QStringLiteral("imageEditorToolSidebar"));
    const QImage selection_icon_24 = linked_select_shapes_button == nullptr
        ? QImage{}
        : linked_select_shapes_button->icon().pixmap(QSize(24, 24)).toImage();
    if (linked_shapes_action == nullptr || linked_select_shapes_action == nullptr ||
        linked_shapes_button == nullptr || linked_select_shapes_button == nullptr ||
        delete_objects_button == nullptr ||
        linked_select_shapes_action->text() != QStringLiteral("Selection") ||
        linked_select_shapes_button->toolTip() != QStringLiteral("Selection") ||
        linked_select_shapes_button->accessibleName() != QStringLiteral("Selection tool") ||
        selection_icon_24.isNull() || selection_icon_24.pixelColor(5, 8).alpha() == 0 ||
        delete_objects_button->text() != QStringLiteral("Delete Selected Objects") ||
        shape_options_action == nullptr || shape_palette == nullptr ||
        shape_line_button == nullptr || shape_rectangle_button == nullptr ||
        shape_ellipse_button == nullptr ||
        linked_window.findChild<QComboBox*>(QStringLiteral("shapeKindComboBox")) != nullptr ||
        !shape_line_button->text().isEmpty() ||
        !shape_rectangle_button->text().isEmpty() ||
        !shape_ellipse_button->text().isEmpty() ||
        shape_line_button->toolButtonStyle() != Qt::ToolButtonIconOnly ||
        shape_rectangle_button->toolButtonStyle() != Qt::ToolButtonIconOnly ||
        shape_ellipse_button->toolButtonStyle() != Qt::ToolButtonIconOnly ||
        shape_line_button->accessibleName() != QStringLiteral("Line shape") ||
        shape_rectangle_button->accessibleName() != QStringLiteral("Rectangle shape") ||
        shape_ellipse_button->accessibleName() != QStringLiteral("Ellipse shape") ||
        shape_line_button->toolTip() != QStringLiteral("Draw a line") ||
        shape_rectangle_button->toolTip() != QStringLiteral("Draw a rectangle") ||
        shape_ellipse_button->toolTip() != QStringLiteral("Draw an ellipse") ||
        shape_line_button->icon().isNull() || shape_rectangle_button->icon().isNull() ||
        shape_ellipse_button->icon().isNull() || shape_stroke == nullptr ||
        shape_fill == nullptr || shape_width == nullptr || shape_stroke_color == nullptr ||
        shape_fill_color == nullptr || linked_layer_list == nullptr ||
        linked_tool_sidebar == nullptr) {
        std::cerr << "The shape tools or their options were not created.\n";
        return 1;
    }
    linked_shapes_button->click();
    QCoreApplication::processEvents();
    if (!shape_palette->isVisible() || linked_shapes_button->isChecked() ||
        linked_tool_sidebar->activeTool() != image_editor::ToolSidebar::Tool::Paint) {
        std::cerr << "Opening the Shapes palette changed the active tool before a shape was chosen.\n";
        return 1;
    }
    shape_line_button->click();
    QCoreApplication::processEvents();
    if (!shape_palette->isVisible() || !shape_line_button->isChecked() ||
        !linked_shapes_button->isChecked() || shape_fill->isEnabled() ||
        linked_tool_sidebar->activeTool() != image_editor::ToolSidebar::Tool::Shapes) {
        std::cerr << "Choosing Line did not activate drawing and keep the palette open.\n";
        return 1;
    }
    linked_paint_action->trigger();
    QCoreApplication::processEvents();
    shape_palette->close();
    QCoreApplication::processEvents();
    linked_shapes_action->trigger();
    QCoreApplication::processEvents();
    if (!linked_shapes_button->isChecked() || !shape_options_action->isVisible() ||
        shape_palette->isVisible() || !shape_line_button->isChecked() ||
        shape_fill->isEnabled() ||
        linked_tool_sidebar->activeTool() != image_editor::ToolSidebar::Tool::Shapes) {
        std::cerr << "The Shapes shortcut did not activate the last type without opening the palette.\n";
        return 1;
    }
    linked_shapes_button->click();
    QCoreApplication::processEvents();
    if (!shape_palette->isVisible() || !shape_palette->isWindow() ||
        !shape_palette->windowFlags().testFlag(Qt::Tool) ||
        !linked_shapes_button->isChecked() ||
        linked_tool_sidebar->activeTool() != image_editor::ToolSidebar::Tool::Shapes) {
        std::cerr << "The Shapes button did not bring its floating palette forward.\n";
        return 1;
    }
    shape_rectangle_button->click();
    QCoreApplication::processEvents();
    shape_fill->setChecked(true);
    if (!shape_palette->isVisible() || !shape_rectangle_button->isChecked() ||
        !shape_stroke->isChecked() || !shape_fill->isChecked() || shape_width->value() != 2 ||
        shape_stroke_color->toolTip() != QStringLiteral("#ff000000") ||
        shape_fill_color->toolTip() != QStringLiteral("#ff000000")) {
        std::cerr << "Shape creation did not show its default Rectangle and style options.\n";
        return 1;
    }
    shape_palette->move(120, 80);
    shape_palette->close();
    QCoreApplication::processEvents();
    if (shape_palette->isVisible() || !linked_shapes_button->isChecked() ||
        linked_tool_sidebar->activeTool() != image_editor::ToolSidebar::Tool::Shapes ||
        !shape_rectangle_button->isChecked()) {
        std::cerr << "Closing the palette changed the active shape tool or selected type.\n";
        return 1;
    }
    linked_shapes_button->click();
    QCoreApplication::processEvents();
    if (!shape_palette->isVisible() || shape_palette->pos() != QPoint(120, 80) ||
        !shape_rectangle_button->isChecked()) {
        std::cerr << "Reopening the Shapes palette did not retain its position and selected type.\n";
        return 1;
    }
    const auto imagePoint = [](image_editor::ImageCanvas* target,
                               const QSize& image_size,
                               const QPointF& point) {
        const qreal zoom = target->zoomFactor();
        return QPoint(qRound((target->width() - image_size.width() * zoom) / 2.0 +
                             point.x() * zoom),
                      qRound((target->height() - image_size.height() * zoom) / 2.0 +
                             point.y() * zoom));
    };
    shape_line_button->click();
    if (shape_fill->isEnabled() || !shape_stroke->isChecked()) {
        std::cerr << "Line options did not disable fill and retain their stroke.\n";
        return 1;
    }
    shape_rectangle_button->click();
    shape_fill->setChecked(true);
    auto* linked_undo_action = linked_window.findChild<QAction*>(QStringLiteral("undoAction"));
    const bool undo_enabled_before_cancel =
        linked_undo_action != nullptr && linked_undo_action->isEnabled();
    const bool dirty_before_cancel = linked_window.windowTitle().startsWith('*');
    const QPoint cancelled_start = imagePoint(linked_canvas, QSize(32, 24), QPointF(1, 1));
    const QPoint cancelled_end = imagePoint(linked_canvas, QSize(32, 24), QPointF(3, 3));
    linked_canvas->setFocus();
    QTest::mousePress(linked_canvas, Qt::LeftButton, Qt::NoModifier, cancelled_start);
    QTest::mouseMove(linked_canvas, cancelled_end);
    QTest::keyClick(linked_canvas, Qt::Key_Escape);
    QTest::mouseRelease(linked_canvas, Qt::LeftButton, Qt::NoModifier, cancelled_end);
    QCoreApplication::processEvents();
    if (linked_undo_action == nullptr ||
        linked_undo_action->isEnabled() != undo_enabled_before_cancel ||
        linked_window.windowTitle().startsWith('*') != dirty_before_cancel) {
        std::cerr << "Escape did not cancel an unfinished shape without adding history.\n";
        return 1;
    }
    const QPoint shape_start = imagePoint(linked_canvas, QSize(32, 24), QPointF(3, 4));
    const QPoint shape_end = imagePoint(linked_canvas, QSize(32, 24), QPointF(11, 10));
    linked_canvas->setFocus();
    QTest::mousePress(linked_canvas, Qt::LeftButton, Qt::NoModifier, shape_start);
    QTest::keyPress(linked_canvas, Qt::Key_Shift);
    QTest::mouseMove(linked_canvas, shape_end);
    QTest::mouseRelease(linked_canvas, Qt::LeftButton, Qt::ShiftModifier, shape_end);
    QTest::keyRelease(linked_canvas, Qt::Key_Shift);
    QCoreApplication::processEvents();
    if (layerRowCount(linked_layer_list) != 3 || currentLayerRow(linked_layer_list) != 0 ||
        linked_layer_list->currentItem() == nullptr ||
        linked_layer_list->currentItem()->text(0) != QStringLiteral("Shape 1")) {
        std::cerr << "The first shape did not create and select its own Shape 1 layer.\n";
        return 1;
    }
    auto* add_shape_layer = linked_window.findChild<QToolButton*>(
        QStringLiteral("addImageLayerButton"));
    if (add_shape_layer == nullptr) return 1;
    add_shape_layer->click();
    QCoreApplication::processEvents();
    shape_ellipse_button->click();
    const QPoint ellipse_start = imagePoint(linked_canvas, QSize(32, 24), QPointF(17, 14));
    const QPoint ellipse_end = imagePoint(linked_canvas, QSize(32, 24), QPointF(25, 22));
    QTest::mousePress(linked_canvas, Qt::LeftButton, Qt::NoModifier, ellipse_start);
    QTest::mouseMove(linked_canvas, ellipse_end);
    QTest::mouseRelease(linked_canvas, Qt::LeftButton, Qt::NoModifier, ellipse_end);
    QCoreApplication::processEvents();
    if (layerRowCount(linked_layer_list) != 5 || currentLayerRow(linked_layer_list) != 0 ||
        linked_layer_list->currentItem() == nullptr ||
        linked_layer_list->currentItem()->text(0) != QStringLiteral("Shape 2")) {
        std::cerr << "The ellipse did not create and select its own Shape 2 layer.\n";
        return 1;
    }
    auto* linked_eraser_action = linked_window.findChild<QAction*>(
        QStringLiteral("eraserToolAction"));
    setCurrentLayerRow(linked_layer_list, layerRowCount(linked_layer_list) - 1);
    QCoreApplication::processEvents();
    if (linked_eraser_action == nullptr || linked_paint_action->isEnabled() ||
        linked_eraser_action->isEnabled() || !linked_shapes_action->isEnabled() ||
        linked_tool_sidebar->activeTool() != image_editor::ToolSidebar::Tool::Shapes) {
        std::cerr << "Selecting Background did not leave Shapes available while locking Paint and Eraser.\n";
        return 1;
    }
    shape_line_button->click();
    const QPoint background_line_start = imagePoint(
        linked_canvas, QSize(32, 24), QPointF(1, 20));
    const QPoint background_line_end = imagePoint(
        linked_canvas, QSize(32, 24), QPointF(5, 20));
    QTest::mousePress(linked_canvas, Qt::LeftButton, Qt::NoModifier, background_line_start);
    QTest::mouseMove(linked_canvas, background_line_end);
    QTest::mouseRelease(linked_canvas, Qt::LeftButton, Qt::NoModifier, background_line_end);
    QCoreApplication::processEvents();
    if (layerRowCount(linked_layer_list) != 6 || currentLayerRow(linked_layer_list) != 4 ||
        linked_layer_list->currentItem() == nullptr ||
        linked_layer_list->currentItem()->text(0) != QStringLiteral("Shape 3")) {
        std::cerr << "Drawing with Background selected did not add a Shape 3 layer above it.\n";
        return 1;
    }
    auto* linked_paint_button = linked_window.findChild<QToolButton*>(
        QStringLiteral("paintToolButton"));
    auto* linked_eraser_button = linked_window.findChild<QToolButton*>(
        QStringLiteral("eraserToolButton"));
    QSignalSpy shape_layer_paint_strokes(
        linked_canvas, &image_editor::ImageCanvas::paintStrokeSelected);
    QSignalSpy shape_layer_erase_strokes(
        linked_canvas, &image_editor::ImageCanvas::eraseStrokeSelected);
    if (linked_paint_button == nullptr || linked_eraser_button == nullptr ||
        !linked_paint_button->isEnabled() || !linked_eraser_button->isEnabled() ||
        !linked_paint_action->isEnabled()) {
        std::cerr << "Creating a shape layer above Background did not enable Paint and Eraser.\n";
        return 1;
    }
    const QPoint brush_start = imagePoint(linked_canvas, QSize(32, 24), QPointF(28, 21));
    const QPoint brush_end = imagePoint(linked_canvas, QSize(32, 24), QPointF(30, 21));
    linked_paint_button->click();
    QCoreApplication::processEvents();
    QTest::mousePress(linked_canvas, Qt::LeftButton, Qt::NoModifier, brush_start);
    QTest::mouseMove(linked_canvas, brush_end);
    QTest::mouseRelease(linked_canvas, Qt::LeftButton, Qt::NoModifier, brush_end);
    QCoreApplication::processEvents();
    if (!linked_canvas->paintMode() || linked_canvas->cursor().shape() != Qt::BlankCursor ||
        shape_layer_paint_strokes.count() != 1) {
        std::cerr << "Paint did not draw on the newly created shape layer.\n";
        return 1;
    }
    linked_eraser_button->click();
    QCoreApplication::processEvents();
    QTest::mousePress(linked_canvas, Qt::LeftButton, Qt::NoModifier, brush_start);
    QTest::mouseMove(linked_canvas, brush_end);
    QTest::mouseRelease(linked_canvas, Qt::LeftButton, Qt::NoModifier, brush_end);
    QCoreApplication::processEvents();
    if (!linked_canvas->eraserMode() || linked_canvas->cursor().shape() != Qt::BlankCursor ||
        shape_layer_erase_strokes.count() != 1) {
        std::cerr << "Eraser did not work on the newly created shape layer.\n";
        return 1;
    }
    shape_ellipse_button->click();
    QCoreApplication::processEvents();
    linked_select_shapes_action->trigger();
    QCoreApplication::processEvents();
    if (!linked_select_shapes_button->isChecked() || linked_shapes_button->isChecked() ||
        shape_options_action->isVisible() || !shape_ellipse_button->isChecked()) {
        std::cerr << "Shapes and Selection were not mutually exclusive.\n";
        return 1;
    }
    setCurrentLayerRow(linked_layer_list, 2);
    QCoreApplication::processEvents();
    const QPoint ellipse_center = imagePoint(linked_canvas, QSize(32, 24), QPointF(21, 18));
    QTest::mouseClick(linked_canvas, Qt::LeftButton, Qt::NoModifier, ellipse_center);
    QCoreApplication::processEvents();
    if (currentLayerRow(linked_layer_list) != 0) {
        std::cerr << "Selecting a shape on another visible layer did not activate its layer.\n";
        return 1;
    }
    const QPoint old_center = imagePoint(linked_canvas, QSize(32, 24), QPointF(7, 8));
    const QPoint moved_center = imagePoint(linked_canvas, QSize(32, 24), QPointF(25, 8));
    QTest::mousePress(linked_canvas, Qt::LeftButton, Qt::NoModifier, old_center);
    QTest::mouseMove(linked_canvas, moved_center);
    QTest::mouseRelease(linked_canvas, Qt::LeftButton, Qt::NoModifier, moved_center);
    QCoreApplication::processEvents();
    const QPoint moved_end = imagePoint(linked_canvas, QSize(32, 24), QPointF(30, 13));
    const QPoint resized_end = imagePoint(linked_canvas, QSize(32, 24), QPointF(31, 14));
    QTest::mousePress(linked_canvas, Qt::LeftButton, Qt::NoModifier, moved_end);
    QTest::mouseMove(linked_canvas, resized_end);
    QTest::mouseRelease(linked_canvas, Qt::LeftButton, Qt::NoModifier, resized_end);
    QCoreApplication::processEvents();
    shape_fill->setChecked(false);
    QCoreApplication::processEvents();
    linked_save_action->trigger();
    QCoreApplication::processEvents();
    QImage published_after_shape(linked_output);
    if (published_after_shape.isNull() ||
        published_after_shape.pixelColor(7, 8) != QColor(240, 20, 10, 255) ||
        published_after_shape.pixelColor(25, 8) != QColor(240, 20, 10, 255) ||
        published_after_shape.pixelColor(25, 4).red() > 80) {
        std::cerr << "Moved, Shift-resized, or unfilled shapes were not published in the linked PNG: "
                  << published_after_shape.size().width() << "x" << published_after_shape.size().height()
                  << " old=" << published_after_shape.pixelColor(7, 8).name(QColor::HexArgb).toStdString()
                  << " inside=" << published_after_shape.pixelColor(25, 8).name(QColor::HexArgb).toStdString()
                  << " edge=" << published_after_shape.pixelColor(25, 4).name(QColor::HexArgb).toStdString()
                  << "\n";
        return 1;
    }
    QFile shape_document_file(linked_document);
    if (!shape_document_file.open(QIODevice::ReadOnly)) return 1;
    const QJsonObject shape_document_json =
        QJsonDocument::fromJson(shape_document_file.readAll()).object();
    QJsonObject persisted_shape_layer;
    QJsonObject persisted_shape;
    for (const QJsonValue& layer_value : shape_document_json.value("layers").toArray()) {
        const QJsonObject layer_object = layer_value.toObject();
        const auto layer_operations = layer_object.value("operations").toArray();
        for (const QJsonValue& operation_value : layer_operations) {
            const QJsonObject operation_object = operation_value.toObject();
            if (operation_object.value("kind").toString() == "shape" &&
                qRound(operation_object.value("start_x").toDouble()) == 21 &&
                qRound(operation_object.value("end_x").toDouble()) == 30 &&
                qRound(operation_object.value("end_y").toDouble()) == 13) {
                persisted_shape_layer = layer_object;
                persisted_shape = operation_object;
            }
        }
    }
    if (shape_document_json.value("version").toInt() != 12 ||
        persisted_shape_layer.isEmpty() ||
        !persisted_shape_layer.value("name").toString().startsWith("Shape ") ||
        persisted_shape_layer.value("operations").toArray().size() != 1 ||
        persisted_shape.value("kind").toString() != "shape" ||
        persisted_shape.value("fill_enabled").toBool()) {
        std::cerr << "The shape's dedicated layer, resize, or style edits were not persisted in v12: version="
                  << shape_document_json.value("version").toInt()
                  << " operations=" << persisted_shape_layer.value("operations").toArray().size()
                  << " layer=" << persisted_shape_layer.value("name").toString().toStdString()
                  << " kind=" << persisted_shape.value("kind").toString().toStdString()
                  << " geometry=" << persisted_shape.value("start_x").toDouble() << ","
                  << persisted_shape.value("start_y").toDouble() << " to "
                  << persisted_shape.value("end_x").toDouble() << ","
                  << persisted_shape.value("end_y").toDouble()
                  << " fill=" << persisted_shape.value("fill_enabled").toBool() << "\n";
        return 1;
    }
    auto* linked_delete_shape_action = linked_window.findChild<QAction*>(
        QStringLiteral("deleteSelectedObjectsAction"));
    if (linked_delete_shape_action == nullptr || linked_undo_action == nullptr ||
        !linked_delete_shape_action->isEnabled()) {
        std::cerr << "The selected object was not available to Delete Selected Objects.\n";
        return 1;
    }
    linked_delete_shape_action->trigger();
    if (linked_delete_shape_action->isEnabled() || !linked_undo_action->isEnabled()) {
        std::cerr << "Deleting the selected shape did not update the selection and history.\n";
        return 1;
    }
    linked_undo_action->trigger();
    if (linked_window.windowTitle().startsWith('*')) {
        std::cerr << "Undo did not restore the saved baseline after deleting the linked shape.\n";
        return 1;
    }

    QFile external_revision(linked_document);
    if (!external_revision.open(QIODevice::Append) ||
        external_revision.write(QByteArrayLiteral("external revision")) < 0) {
        std::cerr << "The external linked document revision could not be simulated.\n";
        return 1;
    }
    external_revision.close();
    QFile newer_document(linked_document);
    if (!newer_document.open(QIODevice::ReadOnly)) return 1;
    const QByteArray newer_document_bytes = newer_document.readAll();
    newer_document.close();
    const QColor latest_published_pixel = published_after_shape.pixelColor(16, 12);
    QTimer::singleShot(0, []() {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            message->accept();
        }
    });
    linked_save_action->trigger();
    QFile document_after_conflict(linked_document);
    if (!document_after_conflict.open(QIODevice::ReadOnly) ||
        document_after_conflict.readAll() != newer_document_bytes) {
        std::cerr << "A stale linked editor overwrote a newer document revision.\n";
        return 1;
    }
    QImage output_after_conflict(linked_output);
    if (output_after_conflict.isNull() ||
        output_after_conflict.pixelColor(16, 12) != latest_published_pixel) {
        std::cerr << "A rejected stale save changed the published image.\n";
        return 1;
    }

    const QString failed_publish_source =
        linked_directory + QStringLiteral("/publish-failure-source.png");
    const QString failed_publish_document =
        linked_directory + QStringLiteral("/publish-failure.cimg");
    const QString failed_publish_output =
        linked_directory + QStringLiteral("/publish-failure.png");
    QImage failed_publish_image(24, 24, QImage::Format_ARGB32);
    failed_publish_image.fill(QColor(30, 180, 210, 255));
    if (!failed_publish_image.save(failed_publish_source)) {
        std::cerr << "The linked publication-failure source could not be created.\n";
        return 1;
    }
    image_editor::ImageEditorWindow failed_publish_window;
    failed_publish_window.show();
    if (!failed_publish_window.openLinkedImage(
            failed_publish_source, failed_publish_document,
            failed_publish_output)) {
        std::cerr << "The linked publication-failure document could not be opened.\n";
        return 1;
    }
    auto* failed_publish_canvas = failed_publish_window.findChild<image_editor::ImageCanvas*>();
    auto* failed_publish_paint = failed_publish_window.findChild<QAction*>(
        QStringLiteral("paintToolAction"));
    auto* failed_publish_save = failed_publish_window.findChild<QAction*>(
        QStringLiteral("saveDocumentAction"));
    if (failed_publish_canvas == nullptr || failed_publish_paint == nullptr ||
        failed_publish_save == nullptr) {
        std::cerr << "The linked publication-failure actions were unavailable.\n";
        return 1;
    }
    QFile failed_publish_document_before_file(failed_publish_document);
    if (!failed_publish_document_before_file.open(QIODevice::ReadOnly)) return 1;
    const QByteArray failed_publish_document_before =
        failed_publish_document_before_file.readAll();
    failed_publish_document_before_file.close();
    failed_publish_paint->trigger();
    QCoreApplication::processEvents();
    QTest::mouseClick(failed_publish_canvas, Qt::LeftButton, Qt::NoModifier,
                      failed_publish_canvas->rect().center());
    if (!QFile::remove(failed_publish_output) ||
        !QDir().mkpath(failed_publish_output)) {
        std::cerr << "The linked publication failure could not be simulated.\n";
        return 1;
    }
    QTimer::singleShot(0, []() {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            message->accept();
        }
    });
    failed_publish_save->trigger();
    QFile failed_publish_document_after_file(failed_publish_document);
    if (!failed_publish_document_after_file.open(QIODevice::ReadOnly)) return 1;
    const QByteArray failed_publish_document_after =
        failed_publish_document_after_file.readAll();
    if (failed_publish_document_after == failed_publish_document_before ||
        !QFileInfo(failed_publish_output).isDir()) {
        std::cerr << "A failed PNG publication damaged the document or destination.\n";
        return 1;
    }

    const QString incompatible_document =
        linked_directory + QStringLiteral("/incompatible.cimg");
    const QString incompatible_output =
        linked_directory + QStringLiteral("/incompatible.png");
    QFile incompatible_file(incompatible_document);
    if (!incompatible_file.open(QIODevice::WriteOnly) ||
        incompatible_file.write(QByteArrayLiteral("not a supported .cimg document")) < 0) {
        std::cerr << "The incompatible linked document could not be created.\n";
        return 1;
    }
    incompatible_file.close();
    image_editor::ImageEditorWindow incompatible_window;
    QTimer::singleShot(0, []() {
        if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
            message->accept();
        }
    });
    if (incompatible_window.openLinkedImage(
            failed_publish_source, incompatible_document, incompatible_output) ||
        QFileInfo::exists(incompatible_output)) {
        std::cerr << "An incompatible linked document was opened or published.\n";
        return 1;
    }
    if (!testRenamedShortcutPersistence()) return 1;
    if (!testDocumentTabs(temporary.path())) {
        std::cerr << "Document tab workflows failed.\n";
        return 1;
    }
    if (!testMultiDocumentRecovery(temporary.path())) {
        std::cerr << "Multi-document recovery workflows failed.\n";
        return 1;
    }
    return 0;
}
