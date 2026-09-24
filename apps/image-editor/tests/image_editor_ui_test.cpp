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
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
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
        !layer_edit_hint->isVisible() ||
        window.windowTitle() != title_before_layer_selection) {
        std::cerr << "Selecting locked Background did not disable painting and transforms.\n";
        return 1;
    }
    layer_list->setCurrentRow(0);
    QCoreApplication::processEvents();
    if (!paint_tool_button->isEnabled() || !rotate_action->isEnabled()) {
        std::cerr << "Selecting an editable layer did not enable its tools.\n";
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
    if (!crop_action->isChecked() || paint_button->isChecked() ||
        !canvas->cropMode() || canvas->paintMode() || tool_sidebar->width() != 56 ||
        paint_options_action->isVisible() || paint_size_options->isVisible() ||
        !tool_options_toolbar->isVisible()) {
        std::cerr << "Crop mode did not deactivate the paint tool.\n";
        return 1;
    }
    crop_action->trigger();

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
