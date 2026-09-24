#include "image_canvas.h"
#include "image_editor_window.h"
#include "layer_panel.h"
#include "tool_sidebar.h"

#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QImage>
#include <QImageWriter>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QTableWidget>
#include <QSlider>
#include <QSpinBox>
#include <QToolBar>
#include <QToolButton>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

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
    auto* cancel_crop_action = window.findChild<QAction*>(QStringLiteral("cancelCropAction"));
    if (settings_menu == nullptr || keyboard_shortcuts_action == nullptr ||
        paint_tool_action == nullptr || cancel_crop_action == nullptr ||
        cancel_crop_action->shortcut() != QKeySequence(Qt::Key_Escape) ||
        settings_menu->title() != QStringLiteral("Settings") ||
        paint_tool_action->shortcut() != QKeySequence(Qt::Key_B) ||
        paint_tool_action->isChecked() || paint_tool_action->isEnabled()) {
        std::cerr << "Settings or the default, inactive Paint shortcut was not created.\n";
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
            if (table == nullptr || table->rowCount() < 19 || editor == nullptr ||
                buttons == nullptr) {
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
                editor->keySequence() == QKeySequence(Qt::Key_B);
            buttons->button(QDialogButtonBox::Ok)->click();
        });
    if (!duplicate_dialog_completed || !duplicate_shortcut_rejected ||
        paint_tool_action->shortcut() != QKeySequence(Qt::Key_B)) {
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
    if (!defaults_restored || paint_tool_action->shortcut() != QKeySequence(Qt::Key_B)) {
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
        !paint_tool_action->isEnabled()) {
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
    auto* redo_action = window.findChild<QAction*>(QStringLiteral("redoAction"));
    auto* crop_action = window.findChild<QAction*>(QStringLiteral("cropSelectionAction"));
    if (tool_sidebar == nullptr || paint_button == nullptr || color_button == nullptr ||
        tool_options_toolbar == nullptr || paint_options_action == nullptr ||
        paint_size_options == nullptr ||
        brush_size_slider == nullptr || brush_size == nullptr || redo_action == nullptr ||
        crop_action == nullptr || tool_sidebar->findChildren<QToolButton*>().size() != 2 ||
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
        brush_size->value() != 12 || brush_size_slider->value() != 12 ||
        brush_size->minimum() != 1 || brush_size->maximum() != 512 ||
        brush_size_slider->minimum() != 1 || brush_size_slider->maximum() != 512) {
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
    paint_button->click();
    if (!paint_button->isChecked() || !canvas->paintMode() ||
        !paint_options_action->isVisible() || !paint_size_options->isVisible()) {
        std::cerr << "Reactivating Paint did not restore its options.\n";
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
    undo_action->trigger();
    crop_action->trigger();
    if (!crop_action->isChecked() || !cancel_crop_action->isEnabled() ||
        paint_button->isChecked() ||
        !canvas->cropMode() || canvas->paintMode() || tool_sidebar->width() != 56 ||
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
    return 0;
}
