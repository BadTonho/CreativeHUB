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
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageWriter>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QScreen>
#include <QSettings>
#include <QTableWidget>
#include <QSlider>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <array>
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
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temporary.path());

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

    auto* canvas = window.findChild<image_editor::ImageCanvas*>();
    if (canvas == nullptr || !window.isVisible()) {
        std::cerr << "The Image Editor window or canvas was not created.\n";
        return 1;
    }

    auto* settings_menu = window.findChild<QMenu*>(QStringLiteral("settingsMenu"));
    auto* keyboard_shortcuts_action = window.findChild<QAction*>(
        QStringLiteral("keyboardShortcutsAction"));
    auto* paint_tool_action = window.findChild<QAction*>(QStringLiteral("paintToolAction"));
    auto* eraser_tool_action = window.findChild<QAction*>(QStringLiteral("eraserToolAction"));
    auto* cancel_crop_action = window.findChild<QAction*>(QStringLiteral("cancelCropAction"));
    if (settings_menu == nullptr || keyboard_shortcuts_action == nullptr ||
        paint_tool_action == nullptr || eraser_tool_action == nullptr || cancel_crop_action == nullptr ||
        cancel_crop_action->shortcut() != QKeySequence(Qt::Key_Escape) ||
        settings_menu->title() != QStringLiteral("Settings") ||
        paint_tool_action->shortcut() != QKeySequence(Qt::Key_B) ||
        eraser_tool_action->shortcut() != QKeySequence(Qt::Key_E) ||
        paint_tool_action->isChecked() || paint_tool_action->isEnabled() ||
        eraser_tool_action->isChecked() || eraser_tool_action->isEnabled()) {
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
    if (canvas->zoomFactor() <= 0.0) {
        std::cerr << "The image canvas did not calculate a fit scale.\n";
        return 1;
    }

    auto* layers_dock = window.findChild<QDockWidget*>(
        QStringLiteral("imageEditorLayersDock"));
    auto* layer_list = window.findChild<QListWidget*>(QStringLiteral("imageLayerList"));
    if (layers_dock == nullptr || layer_list == nullptr || !layers_dock->isVisible() ||
        window.dockWidgetArea(layers_dock) != Qt::RightDockWidgetArea ||
        layer_list->count() != 2 || layer_list->item(0)->text() != QStringLiteral("Layer 1") ||
        layer_list->item(1)->text() != QStringLiteral("Background") ||
        layer_list->currentRow() != 0) {
        std::cerr << "The right-side layer dock did not show the default selected layer stack.\n";
        return 1;
    }
    const QImage editable_thumbnail =
        layer_list->item(0)->data(Qt::UserRole + 4).value<QImage>();
    const QImage background_thumbnail =
        layer_list->item(1)->data(Qt::UserRole + 4).value<QImage>();
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
    const QRect editable_row = layer_list->visualItemRect(layer_list->item(0));
    if (rendered_layer_list.pixelColor(editable_row.left() + 8, editable_row.top() + 6) !=
            QColor(205, 208, 214) ||
        rendered_layer_list.pixelColor(editable_row.left() + 16, editable_row.top() + 6) !=
            QColor(158, 162, 170)) {
        std::cerr << "Layer transparency checkerboard colors do not match the canvas.\n";
        return 1;
    }
    const QRect background_row = layer_list->visualItemRect(layer_list->item(1));
    QTest::mouseClick(layer_list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(background_row.right() - 17, background_row.center().y()));
    QCoreApplication::processEvents();
    if (layer_list->currentRow() != 0 ||
        !layer_list->item(1)->data(Qt::AccessibleDescriptionRole).toString()
             .contains(QStringLiteral("Hidden"))) {
        std::cerr << "The right-side eye control did not hide Background independently of selection.\n";
        return 1;
    }
    const QRect hidden_background_row = layer_list->visualItemRect(layer_list->item(1));
    QTest::mouseClick(layer_list->viewport(), Qt::LeftButton, Qt::NoModifier,
                      QPoint(hidden_background_row.right() - 17,
                             hidden_background_row.center().y()));
    QCoreApplication::processEvents();
    if (layer_list->currentRow() != 0 ||
        !layer_list->item(1)->data(Qt::AccessibleDescriptionRole).toString()
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
        layer_list->item(1)->data(Qt::AccessibleDescriptionRole).toString()
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
    layer_list->setCurrentRow(1);
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
    layer_list->setCurrentRow(0);
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
        color_button == nullptr || tool_size_label == nullptr || eraser_preview == nullptr ||
        tool_options_toolbar == nullptr || paint_options_action == nullptr ||
        paint_size_options == nullptr ||
        brush_size_slider == nullptr || brush_size == nullptr || redo_action == nullptr ||
        crop_action == nullptr || tool_sidebar->findChildren<QToolButton*>().size() != 3 ||
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
    if (layer_list->count() != 3 || layer_list->currentItem() == nullptr ||
        layer_list->currentItem()->text() != QStringLiteral("Layer 2")) {
        std::cerr << "The layer panel did not add and select a new layer.\n";
        return 1;
    }
    layer_list->currentItem()->setText(QStringLiteral("Overlay"));
    QCoreApplication::processEvents();
    if (layer_list->currentItem() == nullptr ||
        layer_list->currentItem()->text() != QStringLiteral("Overlay")) {
        std::cerr << "Inline layer renaming did not update the selected layer.\n";
        return 1;
    }
    move_layer_down_button->click();
    if (layer_list->currentRow() != 1 ||
        layer_list->currentItem()->text() != QStringLiteral("Overlay")) {
        std::cerr << "Layer reordering did not preserve selection and order.\n";
        return 1;
    }
    opacity_slider->setValue(60);
    if (opacity_slider->value() != 60) {
        std::cerr << "The selected layer opacity control did not update.\n";
        return 1;
    }
    delete_layer_button->click();
    if (layer_list->count() != 2 || layer_list->currentItem() == nullptr ||
        layer_list->currentItem()->text() != QStringLiteral("Layer 1")) {
        std::cerr << "Deleting a layer did not select the adjacent editable layer.\n";
        return 1;
    }
    layer_list->setCurrentRow(1);
    QCoreApplication::processEvents();
    if (paint_button->isEnabled() || opacity_slider->isEnabled()) {
        std::cerr << "The panel did not lock editing controls for Background.\n";
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
    const QColor latest_published_pixel = published_after_save.pixelColor(16, 12);
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
    return 0;
}
