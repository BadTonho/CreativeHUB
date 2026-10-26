#include "main_window/main_window.h"
#include "main_window/main_window_support.h"

#include "ui/preview/preview_widget.h"
#include "ui/functions/function_palette.h"
#include "ui/media_browser/media_drag_mime.h"
#include "ui/effects/effects_list_widget.h"
#include "ui/effects/effects_toolbox_widget.h"
#include "ui/effects/effects_favorites_widget.h"
#include "ui/workspace/workspace_host.h"
#include "workspaces/fusion/ui/fusion_workspace.h"
#include "workspaces/audio/ui/audio_workspace.h"
#include "workspaces/audio/ui/audio_effect_chain_dialog.h"
#include "workspaces/audio/ui/audio_ducking_dialog.h"
#include "media/audio_ducking_analysis.h"
#include "workspaces/color/ui/color_workspace.h"
#include "workspaces/color/ui/color_node_canvas.h"
#include "workspaces/fusion/nodes/ui/node_canvas.h"
#include "workspaces/render/queue/render_queue_model.h"
#include "workspaces/render/queue/render_queue_controller.h"
#include "workspaces/render/ui/render_workspace.h"
#include "project/project_file.h"
#include "settings/user_preferences.h"
#include "settings/settings_dialog.h"
#include "timeline/timeline_geometry.h"
#include "timeline/timeline_widget.h"
#include "ui/media_browser/media_browser_bin_tree_widget.h"
#include "ui/media_browser/media_browser_list_widget.h"
#include "ui/system/system_memory_indicator.h"
#include "ui/inspector/inspector_double_spin_box.h"
#include <creative_suite/effects/effects.h>
#if defined(CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS)
#include "image_document_session.h"
#endif

#include <QApplication>
#include <QAction>
#include <QAbstractSpinBox>
#include <QDialog>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QEventLoop>
#include <QDockWidget>
#include <QClipboard>
#include <QLineEdit>
#include <QLabel>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QBuffer>
#include <QIcon>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QSaveFile>
#include <QProgressDialog>
#include <QElapsedTimer>
#include <QThread>
#include <QProgressBar>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTimer>
#include <QToolBar>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QEvent>
#include <QGraphicsScene>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSplitter>
#include <QTabWidget>
#include <QUrl>
#include <QWheelEvent>
#include <QRunnable>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace {

QAction* editAction(MainWindow& window, const char* object_name) {
    return window.findChild<QAction*>(QString::fromLatin1(object_name));
}

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void sendLeftDoubleClick(QWidget* widget) {
    require(widget != nullptr, "Cannot double-click a missing Inspector widget.");
    const auto local_position = QPointF(widget->rect().center());
    QMouseEvent event(QEvent::MouseButtonDblClick,
        local_position, QPointF(widget->mapToGlobal(local_position.toPoint())),
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
}

void verifyInspectorScrollArea(QScrollArea* scroll_area, const char* name) {
    require(scroll_area != nullptr && scroll_area->widgetResizable() &&
                scroll_area->horizontalScrollBarPolicy() == Qt::ScrollBarAlwaysOff &&
                scroll_area->verticalScrollBarPolicy() == Qt::ScrollBarAsNeeded,
            std::string(name) + " does not use a vertical on-demand scroll area.");
    auto* content = scroll_area->widget();
    require(content != nullptr,
            std::string(name) + " has no scrollable content.");

    const auto original_minimum_height = content->minimumHeight();
    content->setMinimumHeight(std::max(
        original_minimum_height, scroll_area->viewport()->height() + 300));
    QApplication::processEvents();

    auto* vertical_bar = scroll_area->verticalScrollBar();
    require(vertical_bar->maximum() > 0,
            std::string(name) + " did not expose overflow through its scrollbar.");
    vertical_bar->setValue(0);
    const auto local_position = QPoint(8, 8);
    QWheelEvent wheel_event(
        local_position, scroll_area->viewport()->mapToGlobal(local_position),
        QPoint(), QPoint(0, -120), Qt::NoButton, Qt::NoModifier,
        Qt::NoScrollPhase, false);
    QApplication::sendEvent(scroll_area->viewport(), &wheel_event);
    QApplication::processEvents();
    require(vertical_bar->value() > 0,
            std::string(name) + " did not scroll in response to the mouse wheel.");
    vertical_bar->setValue(vertical_bar->maximum());
    require(vertical_bar->value() == vertical_bar->maximum(),
            std::string(name) + " could not scroll to the end of its content.");

    content->setMinimumHeight(original_minimum_height);
    QApplication::processEvents();
}

void sendShortcutKey(
    QWidget* target,
    int key,
    Qt::KeyboardModifiers modifiers) {
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers);
    QApplication::sendEvent(target, &release);
}

std::filesystem::path uniqueTestDirectory() {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("creative-suite-main-window-test-" + std::to_string(stamp));
}

void writeLittleEndian(std::ostream& output, std::uint16_t value) {
    const char bytes[] = {
        static_cast<char>(value & 0xffU),
        static_cast<char>((value >> 8U) & 0xffU)};
    output.write(bytes, sizeof(bytes));
}

void writeLittleEndian(std::ostream& output, std::uint32_t value) {
    writeLittleEndian(output, static_cast<std::uint16_t>(value & 0xffffU));
    writeLittleEndian(output, static_cast<std::uint16_t>(value >> 16U));
}

void createWaveformWav(const std::filesystem::path& path) {
    constexpr std::uint32_t sample_rate = 8000;
    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t block_align = channels * 2;
    constexpr std::uint32_t sample_frames = sample_rate;
    constexpr std::uint32_t data_size = sample_frames * block_align;
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Could not create the waveform WAV fixture.");
    output.write("RIFF", 4);
    writeLittleEndian(output, 36U + data_size);
    output.write("WAVEfmt ", 8);
    writeLittleEndian(output, 16U);
    writeLittleEndian(output, static_cast<std::uint16_t>(1));
    writeLittleEndian(output, channels);
    writeLittleEndian(output, sample_rate);
    writeLittleEndian(output, sample_rate * block_align);
    writeLittleEndian(output, block_align);
    writeLittleEndian(output, static_cast<std::uint16_t>(16));
    output.write("data", 4);
    writeLittleEndian(output, data_size);
    for (std::uint32_t index = 0; index < sample_frames; ++index) {
        const auto sample = static_cast<std::uint16_t>(
            index >= sample_rate / 4 && index < sample_rate / 2 ? 16384 : 0);
        writeLittleEndian(output, sample);
        writeLittleEndian(output, sample);
    }
}

project::ProjectDocument makeMultiTrackProject(
    const std::filesystem::path& first_source,
    const std::filesystem::path& second_source) {
    project::ProjectDocument document;
    document.bins = {"Unsorted"};
    document.media = {
        {first_source, "Reference", "Unsorted", false},
        {second_source, "Reference Second", "Unsorted", false},
    };
    document.timeline_tracks = {
        {"Video 1", 1.0, false, {
            {first_source, 0, 0, 1},
        }},
        {"Video 2", 1.0, false, {
            {second_source, 0, 0, 1},
        }},
    };
    document.timeline_tracks[0].track_id = 1;
    document.timeline_tracks[0].clips[0].clip_id = 1;
    document.timeline_tracks[0].clips[0].source_duration_frames = 1;
    document.timeline_tracks[1].track_id = 2;
    document.timeline_tracks[1].clips[0].clip_id = 2;
    document.timeline_tracks[1].clips[0].source_duration_frames = 1;
    return document;
}

QColor framePixel(const media::VideoFrame& frame) {
    if (frame.width <= 0 || frame.height <= 0 || frame.stride < 4 ||
        frame.rgba_pixels.size() < 4) return {};
    return QColor(frame.rgba_pixels[0], frame.rgba_pixels[1],
                  frame.rgba_pixels[2], frame.rgba_pixels[3]);
}

bool writePngAtomically(const std::filesystem::path& path, const QImage& image) {
    QSaveFile file(QString::fromStdString(path.string()));
    if (!file.open(QIODevice::WriteOnly)) return false;
    QImageWriter writer(&file, QByteArrayLiteral("png"));
    if (!writer.write(image)) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

} // namespace

class MainWindowIntegrationTest final {
public:
    static void runColorWorkspaceOnly() {
        QStandardPaths::setTestModeEnabled(true);
        const auto directory = uniqueTestDirectory();
        std::filesystem::create_directories(directory);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QString::fromStdString((directory / "settings").string()));
        QCoreApplication::setOrganizationName("CreativeSuiteTests");
        QCoreApplication::setApplicationName("ColorWorkspaceIntegration");
        QSettings().clear();
        settings::setProjectAutosaveEnabled(false);
        settings::setWorkspacePageTransitionsEnabled(false);
        require(!settings::workspacePageTransitionsEnabled(),
                "Color integration needs immediate workspace transitions.");
        MainWindow window;
        window.show();
        QApplication::processEvents();
        require(window.color_workspace_ == nullptr &&
                window.shortcut_manager_->shortcut(QStringLiteral("workspace.switch_color")) ==
                    QKeySequence(QStringLiteral("Alt+5")),
                "Color must start lazy with the Alt+5 binding.");
        auto* effects_button = window.findChild<QAction*>("effectsGroupAction");
        auto* effects_toolbar = window.findChild<QToolBar*>("mediaPoolToolbar");
        require(effects_button != nullptr,
                "The shared Effects action must be available for Color.");
        require(effects_toolbar != nullptr,
                "The shared Effects toolbar must be available for Color.");
        setWorkspacePageImmediately(window, ui::WorkspacePageId::Color);
        QApplication::processEvents();
        auto* effects_button_widget = effects_toolbar->widgetForAction(effects_button);
        auto* color_effects_panel = window.color_workspace_->findChild<QWidget*>(
            "colorEffectsPanel");
        auto* color_node_panel = window.color_workspace_->findChild<QWidget*>(
            "colorNodesPanel");
        auto* color_upper_splitter = window.color_workspace_->findChild<QSplitter*>(
            "colorWorkspaceUpperSplitter");
        require(effects_button->isVisible() && effects_toolbar->isVisible() &&
                    effects_button_widget != nullptr &&
                    effects_button_widget->geometry().center().x() >
                        effects_toolbar->width() / 2 && color_effects_panel != nullptr &&
                    color_node_panel != nullptr &&
                    color_upper_splitter != nullptr &&
                    !window.effects_dock_view_action_->isVisible(),
                "Color must show the Effects button on the right side of the top toolbar.");
        auto* no_target_brightness = window.effects_list_->findItems(
            QStringLiteral("Brightness"), Qt::MatchExactly).value(0, nullptr);
        effects_button->trigger();
        QApplication::processEvents();
        require(no_target_brightness != nullptr &&
                    window.effects_dock_->isHidden() &&
                    color_effects_panel->isVisible() && effects_button->isChecked() &&
                    !window.effects_list_->isEnabled() &&
                    window.effects_list_->mimeData({
                        no_target_brightness}) == nullptr,
                "Color must show its embedded Effects panel and reject drags without a compatible clip.");
        effects_button->trigger();
        require(color_effects_panel->isHidden() && window.effects_dock_->isHidden(),
                "Closing Color Effects must hide only its embedded panel.");
        setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
        QApplication::processEvents();
        require(window.effects_dock_view_action_->isVisible() &&
                    window.effects_dock_->widget() == window.effects_catalog_host_ &&
                    window.effects_list_->parentWidget() == window.effects_catalog_host_,
                "Leaving Color must return the shared catalog to its Effects dock.");
        window.activateMediaPoolGroup();
        effects_button->trigger();
        QApplication::processEvents();
        require(window.effects_dock_->isVisible() &&
                    window.toolbox_dock_->isVisible() &&
                    window.favorites_dock_->isVisible(),
                "The Effects button must retain its dock-group behavior in Edit.");
        window.activateMediaPoolGroup();
        setWorkspacePageImmediately(window, ui::WorkspacePageId::Fusion);
        effects_button->trigger();
        QApplication::processEvents();
        require(effects_button->isVisible() && window.effects_dock_->isVisible() &&
                    window.toolbox_dock_->isVisible() &&
                    window.favorites_dock_->isVisible(),
                "The Effects button must retain its dock-group behavior in Fusion.");
        setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
        window.activateMediaPoolGroup();
        QApplication::processEvents();

        auto* controller = window.edit_workspace_->controller();
        controller->setPlaybackPresentation(false, false, true);
        const auto track_id = window.timeline_model_.tracks().front().track_id;
        controller->handleTimelineEffectDrop("video.background", track_id, 0);
        require(!window.timeline_model_.tracks().front().clips.empty(),
                "Color fixture could not create a visual clip.");
        const auto clip_id = window.timeline_model_.tracks().front().clips.front().clip_id;
        controller->handleTimelineClipSelectionChanged(track_id, clip_id);
        window.refreshColorSelection();
        const std::array<QDockWidget*, 8> docks{
            window.bins_dock_, window.media_dock_, window.toolbox_dock_,
            window.favorites_dock_, window.effects_dock_, window.inspector_dock_,
            window.preview_dock_, window.timeline_dock_};
        std::array<bool, 8> visible{};
        for (std::size_t i = 0; i < docks.size(); ++i) visible[i] = !docks[i]->isHidden();
        setWorkspacePageImmediately(window, ui::WorkspacePageId::Color);
        QApplication::processEvents();
        require(window.color_workspace_ && window.color_workspace_button_->isChecked() &&
                window.preview_widget_->parentWidget() != nullptr &&
                window.preview_widget_->parentWidget()->objectName() == "colorViewerContainer",
                "Color must mount the shared Viewer on first entry.");
        auto* clips = window.color_workspace_->findChild<QListWidget*>("colorClipStrip");
        auto* nodes = static_cast<ui::ColorNodeCanvas*>(
            window.color_workspace_->findChild<QWidget*>("colorNodeCanvas"));
        auto* add = window.color_workspace_->findChild<QPushButton*>("colorAddNodeButton");
        require(clips && clips->count() == 1 && nodes && nodes->selectedNode() && add &&
                    color_effects_panel && color_node_panel,
                "Color must show the selected visual clip and its graph canvas.");
        require(effects_button != nullptr && !effects_button->isChecked(),
                "Color must enter with the Effects panel closed.");
        effects_button->trigger();
        QApplication::processEvents();
        const auto node_panel_position = color_node_panel->mapTo(
            window.color_workspace_, QPoint(0, 0));
        const auto effects_panel_position = color_effects_panel->mapTo(
            window.color_workspace_, QPoint(0, 0));
        require(color_effects_panel->isVisible() &&
                    window.effects_dock_->isHidden() && effects_button->isChecked() &&
                    window.toolbox_dock_->isHidden() &&
                    window.favorites_dock_->isHidden() &&
                    window.bins_dock_->isHidden() && window.media_dock_->isHidden() &&
                    window.inspector_dock_->isHidden() && window.preview_dock_->isHidden() &&
                    window.timeline_dock_->isHidden() &&
                    window.effects_list_->isEnabled() &&
                    window.effects_list_->visibleEffectCount() == 5 &&
                    color_upper_splitter->handle(2)->isVisible() &&
                    effects_panel_position.x() >= node_panel_position.x() +
                        color_node_panel->width() &&
                    color_effects_panel->height() ==
                        color_upper_splitter->height() &&
                    effects_panel_position.y() + color_effects_panel->height() <=
                        clips->mapTo(window.color_workspace_, QPoint(0, 0)).y(),
                "Color Effects must occupy a resizable pane to the right of the node canvas.");
        effects_button->trigger();
        QApplication::processEvents();
        require(color_effects_panel->isHidden() && window.effects_dock_->isHidden() &&
                    !effects_button->isChecked(),
                "The Color Effects button must close its panel when toggled off.");
        add->click();
        auto location = window.timeline_model_.locateClip(clip_id);
        require(location && window.timeline_model_.tracks()[location->track_index]
                    .clips[location->clip_index].color_graph.has_value() &&
                window.timeline_model_.tracks()[location->track_index]
                    .clips[location->clip_index].effects.size() == 1 &&
                window.timeline_model_.tracks()[location->track_index]
                    .clips[location->clip_index].effects.front().id == "video.primary_grade",
                "Color Add must commit a correction node to the clip graph.");
        auto* red = window.color_workspace_->findChild<QDoubleSpinBox*>("colorWheel_gain_r_value");
        require(red, "Color must expose Primary Grade red gain.");
        red->setValue(145);
        require(QMetaObject::invokeMethod(red, "editingFinished"),
                "Color parameter edit must be committed.");
        location = window.timeline_model_.locateClip(clip_id);
        require(location && creative_suite::effects::parameterValue(
                window.timeline_model_.tracks()[location->track_index]
                    .clips[location->clip_index].effects.front(), "red_gain") == 145 &&
                controller->undo().changed() && controller->redo().changed(),
                "Color gains must use the Timeline history.");
        auto* add_branch = window.color_workspace_->findChild<QPushButton*>(
            "colorAddParallelBranchButton");
        require(add_branch, "Color must offer parallel correction branches.");
        add_branch->click();
        location = window.timeline_model_.locateClip(clip_id);
        require(location && window.timeline_model_.tracks()[location->track_index]
                    .clips[location->clip_index].color_graph->nodes.size() == 5 &&
                controller->undo().changed() && controller->redo().changed(),
                "Color graph topology must use the shared Timeline Undo/Redo history.");

        effects_button->trigger();
        QApplication::processEvents();
        auto* brightness_item = window.effects_list_->findItems(
            QStringLiteral("Brightness"), Qt::MatchExactly).value(0, nullptr);
        require(brightness_item != nullptr,
                "The Color Effects panel must list Brightness for drag and drop.");
        std::unique_ptr<QMimeData> brightness_mime(
            window.effects_list_->mimeData({brightness_item}));
        require(brightness_mime &&
                    brightness_mime->data(ui::kEffectIdMimeType) ==
                        QByteArrayLiteral("video.brightness"),
                "Color Effects must produce the shared Brightness drag payload.");
        location = window.timeline_model_.locateClip(clip_id);
        require(location.has_value(),
                "The Color drag target disappeared before effect insertion.");
        const auto graph_before_drop = timeline::colorGraphForClip(
            window.timeline_model_.tracks()[location->track_index]
                .clips[location->clip_index]);
        const QPoint drop_point(std::max(20, nodes->viewport()->width() - 30),
                                std::max(20, nodes->viewport()->height() - 30));
        const auto expected_drop_position = nodes->mapToScene(drop_point);
        QDragEnterEvent brightness_enter(drop_point, Qt::CopyAction,
            brightness_mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(nodes->viewport(), &brightness_enter);
        QDragMoveEvent brightness_move(drop_point, Qt::CopyAction,
            brightness_mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(nodes->viewport(), &brightness_move);
        QDropEvent brightness_drop(QPointF(drop_point), Qt::CopyAction,
            brightness_mime.get(), Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(nodes->viewport(), &brightness_drop);
        location = window.timeline_model_.locateClip(clip_id);
        require(brightness_enter.isAccepted() && brightness_move.isAccepted() &&
                    brightness_drop.isAccepted() && location.has_value(),
                "Dragging a Color effect onto the graph must commit it.");
        const auto graph_after_drop = timeline::colorGraphForClip(
            window.timeline_model_.tracks()[location->track_index]
                .clips[location->clip_index]);
        const auto dropped_node_id = nodes->selectedNode();
        const auto* dropped_node = dropped_node_id
            ? color::findNode(graph_after_drop, *dropped_node_id) : nullptr;
        require(graph_after_drop.nodes.size() == graph_before_drop.nodes.size() + 1 &&
                    graph_after_drop.connections == graph_before_drop.connections &&
                    dropped_node && dropped_node->type == color::NodeType::Effect &&
                    dropped_node->effect &&
                    dropped_node->effect->id == "video.brightness" &&
                    std::abs(dropped_node->x - expected_drop_position.x()) < 0.01 &&
                    std::abs(dropped_node->y - expected_drop_position.y()) < 0.01 &&
                    controller->undo().changed(),
                "A Color catalog drop must add an unconnected node through Timeline history.");
        location = window.timeline_model_.locateClip(clip_id);
        require(location && timeline::colorGraphForClip(
                    window.timeline_model_.tracks()[location->track_index]
                        .clips[location->clip_index]) == graph_before_drop &&
                    controller->redo().changed(),
                "Undo/Redo must remove and restore the dropped Color effect node.");
        location = window.timeline_model_.locateClip(clip_id);
        require(location && timeline::colorGraphForClip(
                    window.timeline_model_.tracks()[location->track_index]
                        .clips[location->clip_index]) == graph_after_drop,
                "Redo must restore the disconnected Color effect node.");
        effects_button->trigger();
        QApplication::processEvents();
        require(color_effects_panel->isHidden() && window.effects_dock_->isHidden() &&
                    !effects_button->isChecked(),
                "Closing Color Effects must leave the prior dock layout ready for restoration.");

        QImage reference_image(32, 18, QImage::Format_RGBA8888);
        reference_image.fill(QColor(20, 40, 220));
        QByteArray reference_png;
        QBuffer reference_buffer(&reference_png);
        require(reference_buffer.open(QIODevice::WriteOnly) &&
                    reference_image.save(&reference_buffer, "PNG"),
                "Color comparison fixture could not encode its Gallery PNG.");
        project::ColorGalleryStill reference_still;
        reference_still.id = "color-reference-integration";
        reference_still.name = "Integration Still";
        reference_still.source_clip_id = clip_id;
        reference_still.timeline_frame = window.timelinePlayheadFrame();
        reference_still.image_file = reference_still.id + ".png";
        reference_still.image_png = std::make_shared<const std::vector<std::uint8_t>>(
            reinterpret_cast<const std::uint8_t*>(reference_png.constData()),
            reinterpret_cast<const std::uint8_t*>(reference_png.constData()) +
                reference_png.size());
        window.editor_session_.colorGalleryStillsForUi().push_back(reference_still);
        window.updateProjectDirtyState();
        window.refreshColorSelection();
        auto* gallery = window.color_workspace_->findChild<QListWidget*>("colorGalleryStills");
        auto* comparison = window.color_workspace_->findChild<QComboBox*>(
            "colorGalleryComparisonMode");
        auto* wipe = window.color_workspace_->findChild<QSlider*>("colorGalleryWipePosition");
        auto* reference_status = window.color_workspace_->findChild<QLabel*>(
            "colorGalleryReferenceName");
        require(gallery && comparison && wipe && reference_status && gallery->count() == 1,
                "MainWindow did not expose the project Gallery reference controls.");
        const auto clip_before_comparison = window.active_timeline_clip_id_;
        const auto playhead_before_comparison = window.timelinePlayheadFrame();
        const auto dirty_before_comparison = window.project_dirty_;
        const auto can_undo_before_comparison = window.timeline_command_service_.canUndo();
        const auto can_redo_before_comparison = window.timeline_command_service_.canRedo();
        const auto graph_before_comparison = timeline::colorGraphForClip(
            window.timeline_model_.tracks()[location->track_index]
                .clips[location->clip_index]);
        gallery->setCurrentRow(0);
        for (int attempt = 0; attempt < 200 &&
                !reference_status->text().startsWith(QStringLiteral("Reference:"));
             ++attempt) {
            QApplication::processEvents();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        require(reference_status->text().contains(QStringLiteral("Integration Still")) &&
                    comparison->currentData().toInt() == 0 && comparison->isEnabled(),
                "MainWindow did not decode the selected still asynchronously with comparison Off.");
        comparison->setCurrentIndex(1);
        comparison->setCurrentIndex(2);
        wipe->setValue(25);
        require(window.active_timeline_clip_id_ == clip_before_comparison &&
                    window.timelinePlayheadFrame() == playhead_before_comparison &&
                    window.project_dirty_ == dirty_before_comparison &&
                    window.timeline_command_service_.canUndo() == can_undo_before_comparison &&
                    window.timeline_command_service_.canRedo() == can_redo_before_comparison &&
                    timeline::colorGraphForClip(window.timeline_model_.tracks()
                        [location->track_index].clips[location->clip_index]) ==
                        graph_before_comparison,
                "MainWindow Gallery comparison changed the clip, playhead, history or project state.");
        setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
        setWorkspacePageImmediately(window, ui::WorkspacePageId::Color);
        QApplication::processEvents();
        require(comparison->currentData().toInt() == 2,
                "Returning to Color did not restore its temporary wipe mode.");
        window.removeColorStill(QStringLiteral("color-reference-integration"));
        require(comparison->currentData().toInt() == 0 && !comparison->isEnabled() &&
                    !wipe->isEnabled() &&
                    reference_status->text().contains(QStringLiteral("No comparison")),
                "Removing the active Gallery still did not clear comparison and return Off.");

        location = window.timeline_model_.locateClip(clip_id);
        require(location.has_value(), "The comparison clip disappeared before saved-grade setup.");
        const auto saved_comparison_graph = timeline::colorGraphForClip(
            window.timeline_model_.tracks()[location->track_index].clips[location->clip_index]);
        const auto saved_version_result = window.timeline_command_service_.execute(
            application::SetClipColorGradeVersionsCommand{clip_id,
                {{"integration-compare-version", "Saved Compare Grade",
                    saved_comparison_graph}}});
        require(saved_version_result.changed(),
                "The selected clip could not receive a saved version for comparison.");
        window.updateTimelineState();
        window.refreshColorSelection();
        auto* grade_versions = window.color_workspace_->findChild<QListWidget*>(
            "colorGradeVersions");
        auto* compare_version = window.color_workspace_->findChild<QPushButton*>(
            "colorCompareGradeVersionButton");
        require(grade_versions && grade_versions->count() == 1 && compare_version,
                "MainWindow did not expose Compare Version for the selected clip.");
        grade_versions->setCurrentRow(0);
        const auto clip_before_grade_comparison = window.active_timeline_clip_id_;
        const auto playhead_before_grade_comparison = window.timelinePlayheadFrame();
        const auto dirty_before_grade_comparison = window.project_dirty_;
        const auto can_undo_before_grade_comparison = window.timeline_command_service_.canUndo();
        const auto can_redo_before_grade_comparison = window.timeline_command_service_.canRedo();
        compare_version->click();
        require(comparison->currentData().toInt() == 1 &&
                    window.active_timeline_clip_id_ == clip_before_grade_comparison &&
                    window.timelinePlayheadFrame() == playhead_before_grade_comparison &&
                    window.project_dirty_ == dirty_before_grade_comparison &&
                    window.timeline_command_service_.canUndo() == can_undo_before_grade_comparison &&
                    window.timeline_command_service_.canRedo() == can_redo_before_grade_comparison &&
                    timeline::colorGraphForClip(window.timeline_model_.tracks()
                        [location->track_index].clips[location->clip_index]) ==
                        saved_comparison_graph,
                "Compare Version changed clip, playhead, dirty state, history, or active grade.");
        comparison->setCurrentIndex(2);
        wipe->setValue(25);
        setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
        setWorkspacePageImmediately(window, ui::WorkspacePageId::Color);
        QApplication::processEvents();
        require(comparison->currentData().toInt() == 2 && wipe->value() == 25,
                "Returning to Color did not restore the saved-grade Wipe reference.");
        const auto remove_comparison_version = window.timeline_command_service_.execute(
            application::SetClipColorGradeVersionsCommand{clip_id, {}});
        require(remove_comparison_version.changed(),
                "The active saved grade version could not be removed.");
        window.updateTimelineState();
        window.refreshColorSelection();
        require(comparison->currentData().toInt() == 0 && !comparison->isEnabled() &&
                    !wipe->isEnabled(),
                "Removing the active grade version did not clear and disable its comparison.");

        const auto screenshot_path = qEnvironmentVariable("CS_COLOR_SCREENSHOT");
        if (!screenshot_path.isEmpty()) {
            QApplication::processEvents();
            require(window.grab().save(screenshot_path),
                    "Color visual review screenshot could not be saved.");
        }
        setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
        QApplication::processEvents();
        for (std::size_t i = 0; i < docks.size(); ++i)
            require(!docks[i]->isHidden() == visible[i],
                    "Leaving Color must restore the Edit dock visibility.");
        window.edit_workspace_->ui().clip_effects_list->setCurrentRow(0);
        auto* selector = window.edit_workspace_->ui().clip_effect_parameter_selector;
        require(selector && selector->count() == 26,
                "Edit Inspector must expose the same Primary Grade parameters.");
        selector->setCurrentIndex(selector->findData(QStringLiteral("red_gain")));
        controller->applySelectedClipEffectParameter(130);
        location = window.timeline_model_.locateClip(clip_id);
        require(location && creative_suite::effects::parameterValue(
                window.timeline_model_.tracks()[location->track_index]
                    .clips[location->clip_index].effects.front(), "red_gain") == 130,
                "Edit Inspector must write the same grade used by Color.");

        controller->handleTimelineEffectDrop("video.background", track_id, 180);
        window.updateTimelineState();
        window.refreshColorSelection();
        const auto target_clip_id = window.timeline_model_.tracks().front().clips.back().clip_id;
        require(target_clip_id != clip_id,
                "Color grade copy coverage needs a second visual clip.");
        location = window.timeline_model_.locateClip(clip_id);
        require(location.has_value(), "The source clip disappeared before grade copy.");
        const auto source_graph = timeline::colorGraphForClip(
            window.timeline_model_.tracks()[location->track_index].clips[location->clip_index]);
        const auto versions_result = window.timeline_command_service_.execute(
            application::SetClipColorGradeVersionsCommand{clip_id,
                {{"integration-version", "Source Snapshot", source_graph}}});
        require(versions_result.changed(),
                "The source clip could not receive a saved Color grade version.");
        window.updateTimelineState();
        clips = window.color_workspace_->findChild<QListWidget*>("colorClipStrip");
        require(clips && clips->count() == 2,
                "Color must list both clips for grade copy.");
        const auto invoke_clip_menu_action = [clips](int row, int action_index) {
            auto* item = clips->item(row);
            require(item != nullptr, "Color clip context-menu target is missing.");
            const auto position = clips->visualItemRect(item).center();
            QTimer::singleShot(0, [action_index] {
                for (auto* widget : QApplication::topLevelWidgets()) {
                    if (auto* menu = qobject_cast<QMenu*>(widget);
                        menu != nullptr && menu->isVisible()) {
                        if (menu->actions().size() > action_index)
                            menu->actions()[action_index]->trigger();
                        menu->close();
                        return;
                    }
                }
            });
            require(QMetaObject::invokeMethod(clips, "customContextMenuRequested",
                        Qt::DirectConnection, Q_ARG(QPoint, position)),
                    "Color clip context menu did not open.");
        };
        invoke_clip_menu_action(0, 0);
        require(window.copied_color_grade_.has_value() &&
                    *window.copied_color_grade_ == source_graph,
                "Copy Grade must retain the complete source graph.");
        invoke_clip_menu_action(1, 1);
        auto target_location = window.timeline_model_.locateClip(target_clip_id);
        require(target_location &&
                    timeline::colorGraphForClip(window.timeline_model_.tracks()
                        [target_location->track_index].clips[target_location->clip_index]) ==
                        source_graph &&
                    window.timeline_model_.tracks()[target_location->track_index]
                        .clips[target_location->clip_index].color_grade_versions.empty() &&
                    controller->undo().changed(),
                "Apply Grade must copy only the graph and create one shared history edit.");
        target_location = window.timeline_model_.locateClip(target_clip_id);
        require(target_location &&
                    !window.timeline_model_.tracks()[target_location->track_index]
                        .clips[target_location->clip_index].color_graph.has_value() &&
                    controller->redo().changed(),
                "Undo/Redo must restore and reapply a copied Color grade.");
        target_location = window.timeline_model_.locateClip(target_clip_id);
        require(target_location &&
                    timeline::colorGraphForClip(window.timeline_model_.tracks()
                        [target_location->track_index].clips[target_location->clip_index]) ==
                        source_graph,
                "Redo did not restore the copied Color graph.");
        window.clearProjectState(1920, 1080, timeline::FrameRate{30, 1});
        require(!window.copied_color_grade_.has_value(),
                "Creating a project must clear the temporary Color grade clipboard.");
    }

    static void runAudioWorkspaceOnly() {
        QStandardPaths::setTestModeEnabled(true);
        const auto directory = uniqueTestDirectory();
        std::filesystem::create_directories(directory);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QString::fromStdString((directory / "settings").string()));
        QCoreApplication::setOrganizationName("CreativeSuiteTests");
        QCoreApplication::setApplicationName("AudioWorkspaceIntegration");
        QSettings().clear();
        settings::setProjectAutosaveEnabled(false);
        settings::setWorkspacePageTransitionsEnabled(false);
        {
            const std::vector<timeline::AudioEffectInstance> original;
            ui::AudioEffectChainDialog full(QStringLiteral("Processing"), original, nullptr);
            full.findChild<QPushButton*>("audioEffectAddCleanup")->click();
            require(full.effects().size() == 1 && std::holds_alternative<timeline::AudioDialogueCleanupParameters>(full.effects().front().parameters) &&
                !full.effects().front().enabled, "Audio cleanup was not inserted disabled.");
            full.findChild<QDoubleSpinBox*>("audioCleanupHighpass")->setValue(120);
            require(std::get<timeline::AudioDialogueCleanupParameters>(full.effects().front().parameters).highpass_frequency_hz == 120,
                "Cleanup parameter editing failed.");
            full.reject(); require(original.empty(), "Cancelling the effect editor changed its input chain.");
            ui::AudioEffectChainDialog basic(QStringLiteral("Basic"), full.effects(), nullptr, false);
            require(basic.findChild<QPushButton*>("audioEffectAddCleanup")->isHidden() && basic.effects() == full.effects() &&
                !basic.findChild<QCheckBox*>("audioEffectEnabled"), "Edit exposed or lost Audio-only processors.");
            timeline::AudioDuckingSettings settings; settings.source_track_id = 1;
            settings.mode = timeline::AudioDuckingMode::Automatic; settings.curve = {{0, 0}, {100000, 12}};
            ui::AudioDuckingDialog duck(settings, {{1, QStringLiteral("Voice")}, {2, QStringLiteral("Music")}}, 2, nullptr);
            require(duck.findChild<QComboBox*>("audioDuckingSource")->count() == 2 && duck.settings() == settings,
                "Ducking editor allowed self-reference or changed the saved settings.");
            duck.findChild<QPushButton*>("audioDuckingGenerateCurve")->click();
            require(duck.generateRequested() && duck.result() == QDialog::Accepted, "The curve-generation action was not exposed.");
        }
        qputenv("CREATIVE_SUITE_VOICE_ISOLATION_CACHE_ROOT", QString::fromStdU16String((directory / "isolation-cache").u16string()).toUtf8());
        const auto audio_source = directory / "audio-workspace.wav";
        createWaveformWav(audio_source);
        {
            project::ProjectDocument analysis;
            project::ProjectTrack source; source.track_id = 1; source.name = "Voice"; source.kind = timeline::TrackKind::Audio;
            project::ProjectClip clip; clip.clip_id = 1; clip.kind = timeline::ClipKind::Audio; clip.source_path = audio_source;
            clip.duration_frames = 15; clip.source_duration_time_us = 500000; source.clips.push_back(clip);
            analysis.timeline_tracks.push_back(source);
            timeline::AudioDuckingSettings duck; duck.source_track_id = 1; duck.mode = timeline::AudioDuckingMode::Automatic;
            int progress = -1;
            const auto curve = media::generateAudioDuckingCurve(analysis, duck, [] { return false; }, [&](int value) { progress = value; });
            require(!curve.empty() && progress == 100, "Ducking analysis did not produce a complete curve.");
            int checks = 0;
            const auto cancelled = media::generateAudioDuckingCurve(analysis, duck, [&] { return ++checks > 2; }, {});
            require(cancelled.empty(), "Cancelled analysis returned a partial curve.");
        }
        {
            MainWindow window;
            window.show();
            QApplication::processEvents();
            auto* timeline = window.editUi().timeline;
            const auto track = std::find_if(
                window.timeline_model_.tracks().begin(),
                window.timeline_model_.tracks().end(),
                [](const timeline::TimelineTrack& value) {
                    return value.kind == timeline::TrackKind::Audio;
                });
            require(track != window.timeline_model_.tracks().end(),
                    "The Audio workspace needs an Audio track.");
            const auto track_id = track->track_id;
            media::VideoMetadata metadata;
            metadata.kind = media::MediaKind::Audio;
            metadata.source_path = audio_source;
            metadata.display_name = "Audio workspace";
            metadata.duration_seconds = 1.0;
            metadata.audio = media::AudioMetadata{"pcm_s16le", 8000, 2, 1.0};
            require(window.media_controller_.commitImported({
                        metadata, {}, metadata.display_name, "Unsorted", false}).changed() &&
                        window.timeline_command_service_.execute(
                            application::AddMediaClipCommand{
                                audio_source, track_id, 0}).changed(),
                    "The Audio workspace fixture could not add its source clip.");
            require(window.timeline_command_service_.execute(
                        application::AddAudioBusCommand{"Dialogue"}).changed() &&
                    window.timeline_command_service_.execute(
                        application::AddAudioBusCommand{"Reverb"}).changed(),
                    "The Audio mixer fixture could not add its submix buses.");
            const auto dialogue_bus = window.timeline_model_.audioBuses().at(0).bus_id;
            const auto reverb_bus = window.timeline_model_.audioBuses().at(1).bus_id;
            window.updateTimelineState();
            const auto clip_id = window.timeline_model_.tracks()
                [*window.timeline_model_.locateTrack(track_id)].clips.front().clip_id;
            window.edit_workspace_->controller()->handleTimelineClipSelectionChanged(
                track_id, clip_id);
            const auto clip_count = window.timeline_model_.tracks()
                [*window.timeline_model_.locateTrack(track_id)].clips.size();
            const auto playhead = timeline->playheadFrame();
            const std::array<QDockWidget*, 8> docks{
                window.bins_dock_, window.media_dock_, window.toolbox_dock_,
                window.favorites_dock_, window.effects_dock_,
                window.inspector_dock_, window.preview_dock_, window.timeline_dock_};
            std::array<bool, 8> dock_visible{};
            std::array<Qt::DockWidgetArea, 8> dock_area{};
            for (std::size_t index = 0; index < docks.size(); ++index) {
                dock_visible[index] = !docks[index]->isHidden();
                dock_area[index] = window.dockWidgetArea(docks[index]);
            }
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Audio);
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Audio &&
                        timeline->audioOnlyDisplayEnabled() &&
                        !timeline->isReadOnly() &&
                        timeline->trackGroupViewportRect(
                            timeline::TrackKind::Video).isEmpty() &&
                        timeline->trackGroupViewportRect(
                            timeline::TrackKind::Audio).height() > 0 &&
                        timeline->playheadFrame() == playhead &&
                        window.editor_session_.selection().active_clip_id == clip_id &&
                        window.timeline_model_.tracks()
                            [*window.timeline_model_.locateTrack(track_id)]
                            .clips.size() == clip_count,
                    "Audio must share the editable Timeline and preserve its state.");
            auto* gain = window.audio_workspace_->mixerPanel()
                ->findChild<QSlider*>("audioTrackGain");
            auto* mute = window.audio_workspace_->mixerPanel()
                ->findChild<QCheckBox*>("audioTrackMute");
            auto* pan = window.audio_workspace_->mixerPanel()
                ->findChild<QSlider*>("audioTrackPan");
            auto* output_route = window.audio_workspace_->mixerPanel()
                ->findChild<QComboBox*>("audioTrackOutput");
            auto* send_target = window.audio_workspace_->mixerPanel()
                ->findChild<QComboBox*>("audioTrackSendDestination");
            auto* add_send = window.audio_workspace_->mixerPanel()
                ->findChild<QPushButton*>("audioTrackAddSend");
            auto* master_gain = window.audio_workspace_->mixerPanel()
                ->findChild<QSlider*>("audioMasterGain");
            auto* bus_solo = window.audio_workspace_->mixerPanel()
                ->findChild<QCheckBox*>("audioBusSolo");
            auto* bus_rename = window.audio_workspace_->mixerPanel()
                ->findChild<QPushButton*>("audioBusRename");
            auto* bus_remove = window.audio_workspace_->mixerPanel()
                ->findChild<QPushButton*>("audioBusRemove");
            auto* track_effects = static_cast<QPushButton*>(nullptr);
            for (auto* button : window.audio_workspace_->mixerPanel()
                     ->findChildren<QPushButton*>("audioTrackEffects")) {
                if (button->property("trackId").toULongLong() == track_id) {
                    track_effects = button;
                    break;
                }
            }
            require(gain != nullptr && mute != nullptr && gain->value() == 100 &&
                        pan != nullptr && pan->value() == 0 &&
                        output_route != nullptr && send_target != nullptr &&
                        add_send != nullptr && master_gain != nullptr &&
                        master_gain->value() == 100 && !mute->isChecked() &&
                        bus_solo != nullptr && bus_rename != nullptr &&
                        bus_remove != nullptr && track_effects != nullptr,
                    "The Audio mixer must expose neutral track mix, routing, and effects controls.");
            const auto effects_baseline_path = directory / "audio-effects-baseline.csp";
            require(window.saveProjectTo(effects_baseline_path, "save") &&
                        !window.project_dirty_,
                    "The Audio effects UI test could not establish a clean baseline.");
            bool audio_effect_dialog_edited = false;
            QTimer::singleShot(0, &window, [&audio_effect_dialog_edited] {
                auto* dialog = QApplication::activeModalWidget();
                if (dialog == nullptr ||
                    dialog->objectName() != QStringLiteral("audioTrackEffectsDialog"))
                    return;
                auto* add_eq = dialog->findChild<QPushButton*>("audioEffectAddEqualizer");
                if (add_eq == nullptr) {
                    dialog->close();
                    return;
                }
                add_eq->click();
                auto* enabled = dialog->findChild<QCheckBox*>("audioEffectEnabled");
                auto* high_gain = dialog->findChild<QDoubleSpinBox*>("audioEqHighGain");
                auto* buttons = dialog->findChild<QDialogButtonBox*>();
                if (enabled == nullptr || high_gain == nullptr || buttons == nullptr) {
                    dialog->close();
                    return;
                }
                enabled->setChecked(true);
                high_gain->setValue(6.0);
                audio_effect_dialog_edited = true;
                buttons->button(QDialogButtonBox::Ok)->click();
            });
            track_effects->click();
            const auto effect_track_index = window.timeline_model_.locateTrack(track_id);
            require(audio_effect_dialog_edited && effect_track_index.has_value() &&
                        window.project_dirty_ &&
                        window.timeline_model_.tracks()[*effect_track_index]
                            .audio_effects.size() == 1 &&
                        window.timeline_model_.tracks()[*effect_track_index]
                            .audio_effects.front().enabled &&
                        std::get<timeline::AudioEqualizerParameters>(
                            window.timeline_model_.tracks()[*effect_track_index]
                                .audio_effects.front().parameters)
                                .high_shelf_gain_db == 6.0,
                    "The track effects editor must apply an enabled EQ to the shared timeline.");
            require(window.edit_workspace_->controller()->undo().changed() &&
                        !window.project_dirty_ &&
                        window.timeline_model_.tracks()
                            [*effect_track_index]
                            .audio_effects.empty() &&
                        window.edit_workspace_->controller()->redo().changed() &&
                        window.project_dirty_ &&
                        window.timeline_model_.tracks()
                            [*effect_track_index]
                            .audio_effects.size() == 1,
                    "Audio effects must participate in dirty state and shared Undo/Redo.");
            auto* track_meter = window.audio_workspace_->meterPage()
                ->findChild<QProgressBar*>("audioTrackMeter");
            auto* output_meter = window.audio_workspace_->meterPage()
                ->findChild<QProgressBar*>("audioOutputMeter");
            auto* mixer_output_meter = window.audio_workspace_->mixerPanel()
                ->findChild<QProgressBar*>("audioOutputMixerMeter");
            window.audio_workspace_->setPeaks(
                {{track_id, 0.4F}}, {{dialogue_bus, 0.3F}}, 0.7F);
            auto* bus_meter = window.audio_workspace_->mixerPanel()
                ->findChild<QProgressBar*>("audioBusMixerMeter");
            require(track_meter != nullptr && output_meter != nullptr &&
                        mixer_output_meter != nullptr &&
                        bus_meter != nullptr && bus_meter->value() == 30 &&
                        track_meter->value() == 40 &&
                        output_meter->value() == 70 &&
                        mixer_output_meter->value() == 70,
                    "Audio meters must show the matching track and summed output.");
            window.audio_workspace_->clearPeaks();
            require(track_meter->value() == 0 && output_meter->value() == 0 &&
                        bus_meter->value() == 0 &&
                        mixer_output_meter->value() == 0,
                    "Audio meters must clear after playback stops.");
            gain->setValue(65);
            require(std::abs(window.timeline_model_.tracks()
                        [*window.timeline_model_.locateTrack(track_id)]
                        .audio_gain - 0.65) < 1.0e-6,
                    "The mixer must edit track gain by stable ID.");
            require(window.edit_workspace_->controller()->undo().changed() &&
                        std::abs(window.timeline_model_.tracks()
                            [*window.timeline_model_.locateTrack(track_id)]
                            .audio_gain - 1.0) < 1.0e-6,
                    "Audio gain Undo must use the shared history.");
            require(window.edit_workspace_->controller()->redo().changed(),
                    "Audio gain Redo must use the shared history.");
            pan->setValue(-25);
            require(std::abs(window.timeline_model_.tracks()
                        [*window.timeline_model_.locateTrack(track_id)]
                        .audio_pan + 0.25) < 1.0e-6,
                    "The Audio mixer must apply pan by stable track ID.");
            output_route->setCurrentIndex(output_route->findData(
                QVariant::fromValue<qulonglong>(dialogue_bus)));
            send_target->setCurrentIndex(send_target->findData(
                QVariant::fromValue<qulonglong>(reverb_bus)));
            add_send->click();
            const auto routed_track_index = window.timeline_model_.locateTrack(track_id);
            require(routed_track_index.has_value() &&
                        window.timeline_model_.tracks()[*routed_track_index].output_bus_id ==
                            dialogue_bus &&
                        window.timeline_model_.tracks()[*routed_track_index].audio_sends.size() == 1 &&
                        window.timeline_model_.tracks()[*routed_track_index].audio_sends.front()
                            .destination_bus_id == reverb_bus,
                    "Track output and sends must update the saved timeline model.");
            auto* send_pre_fader = window.audio_workspace_->mixerPanel()
                ->findChild<QCheckBox*>("audioTrackSendPreFader");
            require(send_pre_fader != nullptr,
                    "The Audio mixer must expose the send tap mode.");
            send_pre_fader->setChecked(true);
            require(window.timeline_model_.tracks()[*routed_track_index]
                        .audio_sends.front().tap == timeline::AudioSendTap::PreFader,
                    "The send tap control must update the timeline model.");
            master_gain->setValue(80);
            require(std::abs(window.timeline_model_.masterAudioMix().audio_gain - 0.8) <
                        1.0e-6,
                    "The Audio mixer must edit the Master gain.");
            auto* solo = window.audio_workspace_->mixerPanel()
                ->findChild<QCheckBox*>("audioTrackSolo");
            require(solo != nullptr, "The Audio mixer must expose temporary solo.");
            solo->setChecked(true);
            bus_solo->setChecked(true);
            mute->setChecked(true);
            const auto project_path = directory / "audio-workspace.csp";
            require(window.saveProjectTo(project_path, "save"),
                    "Audio track mix settings could not be saved.");
            const auto saved = project::load(project_path);
            const auto saved_track = std::find_if(
                saved.timeline_tracks.begin(), saved.timeline_tracks.end(),
                [track_id](const auto& value) {
                    return value.track_id == track_id;
                });
            require(saved_track != saved.timeline_tracks.end() &&
                        std::abs(saved_track->audio_gain - 0.65) < 1.0e-6 &&
                        saved_track->audio_muted &&
                        std::abs(saved_track->audio_pan + 0.25) < 1.0e-6 &&
                        saved_track->output_bus_id == dialogue_bus &&
                        saved_track->audio_sends.size() == 1 &&
                        saved_track->audio_sends.front().tap ==
                            timeline::AudioSendTap::PreFader &&
                        saved_track->audio_effects.size() == 1 &&
                        saved_track->audio_effects.front().enabled &&
                        std::get<timeline::AudioEqualizerParameters>(
                            saved_track->audio_effects.front().parameters)
                                .high_shelf_gain_db == 6.0 &&
                        saved.audio_buses.size() == 2 &&
                        std::abs(saved.master_audio_mix.audio_gain - 0.8) < 1.0e-6,
                    "Audio mixer settings must survive a project round trip.");
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
            bool restored_docks = true;
            for (std::size_t index = 0; index < docks.size(); ++index) {
                restored_docks &= !docks[index]->isHidden() == dock_visible[index] &&
                    window.dockWidgetArea(docks[index]) == dock_area[index];
            }
            require(!timeline->audioOnlyDisplayEnabled() && restored_docks,
                    "Leaving Audio must restore the Edit dock layout.");
            require(window.editor_session_.selection().active_clip_id == clip_id,
                    "Changing workspaces must preserve the selected clip.");
            QEventLoop reopen_loop;
            QTimer reopen_timeout;
            reopen_timeout.setSingleShot(true);
            QObject::connect(&reopen_timeout, &QTimer::timeout,
                             &reopen_loop, &QEventLoop::quit);
            bool reopened = false;
            require(window.openProjectPath(
                        project_path, std::nullopt, std::nullopt,
                        [&reopen_loop, &reopened](bool succeeded) {
                            reopened = succeeded;
                            reopen_loop.quit();
                        }),
                    "The saved Audio project could not begin reopening.");
            reopen_timeout.start(5000);
            reopen_loop.exec();
            require(reopened, "The saved Audio project did not reopen.");
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Audio);
            const auto reopened_track = window.timeline_model_.locateTrack(track_id);
            auto* reopened_gain = window.audio_workspace_->mixerPanel()
                ->findChild<QSlider*>("audioTrackGain");
            auto* reopened_mute = window.audio_workspace_->mixerPanel()
                ->findChild<QCheckBox*>("audioTrackMute");
            auto* reopened_pan = window.audio_workspace_->mixerPanel()
                ->findChild<QSlider*>("audioTrackPan");
            auto* reopened_output = window.audio_workspace_->mixerPanel()
                ->findChild<QComboBox*>("audioTrackOutput");
            auto* reopened_send_pre = window.audio_workspace_->mixerPanel()
                ->findChild<QCheckBox*>("audioTrackSendPreFader");
            auto* reopened_master_gain = window.audio_workspace_->mixerPanel()
                ->findChild<QSlider*>("audioMasterGain");
            require(reopened_track.has_value() &&
                        std::abs(window.timeline_model_.tracks()[*reopened_track]
                            .audio_gain - 0.65) < 1.0e-6 &&
                        window.timeline_model_.tracks()[*reopened_track].audio_muted &&
                        window.timeline_model_.tracks()[*reopened_track]
                            .audio_effects.size() == 1 &&
                        std::get<timeline::AudioEqualizerParameters>(
                            window.timeline_model_.tracks()[*reopened_track]
                                .audio_effects.front().parameters)
                                .high_shelf_gain_db == 6.0 &&
                        reopened_gain != nullptr && reopened_mute != nullptr &&
                        reopened_gain->value() == 65 && reopened_mute->isChecked() &&
                        reopened_pan != nullptr && reopened_pan->value() == -25 &&
                        reopened_output != nullptr &&
                        reopened_output->currentData().toULongLong() == dialogue_bus &&
                        reopened_send_pre != nullptr && reopened_send_pre->isChecked() &&
                        reopened_master_gain != nullptr &&
                        reopened_master_gain->value() == 80,
                    "Reopening must synchronize the Audio mixer with the saved project.");
            auto* reopened_solo = window.audio_workspace_->mixerPanel()
                ->findChild<QCheckBox*>("audioTrackSolo");
            auto* reopened_bus_solo = window.audio_workspace_->mixerPanel()
                ->findChild<QCheckBox*>("audioBusSolo");
            require(reopened_solo != nullptr && !reopened_solo->isChecked() &&
                        reopened_bus_solo != nullptr &&
                        !reopened_bus_solo->isChecked(),
                    "Solo must remain transient and clear when a project is reopened.");
            const auto selected_clip = window.timeline_model_.tracks()[*reopened_track].clips.front().clip_id;
            const auto selected_location = window.timeline_model_.locateClip(selected_clip);
            window.audio_workspace_->setSelectedClip(&window.timeline_model_.tracks()[selected_location->track_index].clips[selected_location->clip_index]);
            auto* clip_effects = window.audio_workspace_->mixerPanel()->findChild<QPushButton*>("audioSelectedClipEffects");
            require(clip_effects && clip_effects->isEnabled(), "Audio did not expose processing for the selected clip.");
            bool clip_edited = false;
            QTimer::singleShot(0, &window, [&] {
                auto* dialog = QApplication::activeModalWidget();
                auto* add = dialog ? dialog->findChild<QPushButton*>("audioEffectAddNoiseReduction") : nullptr;
                if (!add) { if (dialog) dialog->close(); return; }
                add->click();
                clip_edited = true;
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            });
            clip_effects->click();
            const auto after_clip_edit = window.timeline_model_.locateClip(selected_clip);
            const auto& clip_chain = window.timeline_model_.tracks()[after_clip_edit->track_index].clips[after_clip_edit->clip_index].audio_effects;
            require(clip_edited && std::any_of(clip_chain.begin(), clip_chain.end(), [](const auto& effect) {
                return std::holds_alternative<timeline::AudioNoiseReductionParameters>(effect.parameters);
            }), "Audio's selected-clip editor did not commit through shared history.");
            auto* isolation_panel = window.audio_workspace_->mixerPanel()->findChild<QGroupBox*>("audioSelectedClipIsolation");
            require(isolation_panel && isolation_panel->isEnabled(), "Selected clip isolation controls were not available in Audio.");
            QTimer::singleShot(0, &window, [&] {
                auto* dialog = QApplication::activeModalWidget();
                auto* enabled = dialog ? dialog->findChild<QCheckBox*>("audioVoiceIsolationEnabled") : nullptr;
                if (!enabled) { if (dialog) dialog->close(); return; }
                enabled->setChecked(true);
                dialog->findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Ok)->click();
            });
            isolation_panel->findChild<QPushButton*>("audioVoiceIsolationConfigure")->click();
            const auto isolated_location = window.timeline_model_.locateClip(selected_clip);
            require(window.timeline_model_.tracks()[isolated_location->track_index].clips[isolated_location->clip_index].voice_isolation.enabled,
                "Audio's voice isolation controls did not commit through shared history.");
            const auto voice_id = window.timeline_model_.tracks()[*reopened_track].track_id;
            require(window.editor_session_.legacyTimelineForUi().addTrack("Music", timeline::TrackKind::Audio) == timeline::AddTrackResult::Added,
                "Could not create a music track for async ducking checks.");
            window.updateTimelineState();
            const auto music_id = window.timeline_model_.tracks().back().track_id;
            timeline::AudioDuckingSettings duck; duck.source_track_id = voice_id;
            const auto wait_for_analysis = [&] {
                QElapsedTimer timer; timer.start();
                while (window.findChild<QProgressDialog*>("audioDuckingProgress") && timer.elapsed() < 5000) {
                    QApplication::processEvents(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); QThread::msleep(1);
                }
                require(!window.findChild<QProgressDialog*>("audioDuckingProgress"), "Ducking analysis did not finish or release its progress dialog.");
            };
            window.generateAudioDuckingCurve(music_id, duck);
            wait_for_analysis();
            require(window.timeline_model_.tracks().back().audio_ducking.mode == timeline::AudioDuckingMode::Curve &&
                !window.timeline_model_.tracks().back().audio_ducking.curve.empty(), "Async curve generation did not apply its complete result.");
            const auto before_cancel = window.currentProjectDocument();
            duck.reduction_db = 24;
            window.generateAudioDuckingCurve(music_id, duck);
            window.findChild<QProgressDialog*>("audioDuckingProgress")->findChild<QPushButton*>()->click();
            wait_for_analysis();
            require(window.currentProjectDocument() == before_cancel, "Cancelling async analysis changed the saved curve.");
            window.generateAudioDuckingCurve(music_id, duck);
            window.clearProjectState(1920, 1080, timeline::FrameRate{30, 1});
            const auto replacement = window.currentProjectDocument();
            wait_for_analysis();
            require(window.currentProjectDocument() == replacement, "A stale ducking result modified the replacement project.");
        }
        std::filesystem::remove_all(directory);
    }

    static void runLinkedImageOnly() {
        QStandardPaths::setTestModeEnabled(true);
        const auto directory = uniqueTestDirectory();
        std::filesystem::create_directories(directory);
        verifyLinkedImageRefresh(directory);
        std::filesystem::remove_all(directory);
    }

    static void verifyLinkedImageRefresh(const std::filesystem::path& directory) {
        const auto linked_source = directory / "linked-original.png";
        const auto linked_document = directory / "linked-edit.cimg";
        const auto linked_output = directory / "linked-output.png";
        const auto variant_document = directory / "linked-clip-variant.cimg";
        const auto variant_output = directory / "linked-clip-variant.png";
        const auto linked_project = directory / "linked-image.csp";
        QImage source_image(16, 16, QImage::Format_ARGB32);
        source_image.fill(QColor(230, 20, 15, 255));
        QImage first_output(16, 16, QImage::Format_ARGB32);
        first_output.fill(QColor(20, 220, 35, 255));
        QImage isolated_variant(16, 16, QImage::Format_ARGB32);
        isolated_variant.fill(QColor(230, 210, 15, 255));
        require(source_image.save(QString::fromStdString(linked_source.string())) &&
                    writePngAtomically(linked_output, first_output) &&
                    writePngAtomically(variant_output, isolated_variant),
                "The MainWindow linked-image fixtures could not be written.");
        const media::LinkedImageReference shared_link{
            "shared-main-window-link", linked_document, linked_output};
        project::ProjectDocument linked_document_data;
        linked_document_data.media.push_back({
            linked_source, "Linked still", "Unsorted", false,
            media::MediaKind::Image, shared_link});
        project::ProjectTrack linked_track;
        linked_track.track_id = 71;
        linked_track.name = "V1";
        project::ProjectClip linked_clip;
        linked_clip.clip_id = 81;
        linked_clip.source_path = linked_source;
        linked_clip.duration_frames = 30;
        linked_clip.source_duration_frames = 30;
        linked_clip.kind = timeline::ClipKind::Image;
        linked_track.clips.push_back(linked_clip);
        project::ProjectClip variant_clip;
        variant_clip.clip_id = 82;
        variant_clip.source_path = linked_source;
        variant_clip.timeline_start_frame = 30;
        variant_clip.duration_frames = 30;
        variant_clip.source_duration_frames = 30;
        variant_clip.kind = timeline::ClipKind::Image;
        variant_clip.image_editor_variant = media::LinkedImageReference{
            "clip-specific-window-link", variant_document, variant_output};
        linked_track.clips.push_back(variant_clip);
        linked_document_data.timeline_tracks.push_back(linked_track);
        project::save(linked_project, linked_document_data);

        {
            MainWindow linked_window;
            linked_window.show();
            QEventLoop linked_open_loop;
            QTimer linked_open_timeout;
            linked_open_timeout.setSingleShot(true);
            QObject::connect(&linked_open_timeout, &QTimer::timeout,
                             &linked_open_loop, &QEventLoop::quit);
            bool linked_open_succeeded = false;
            require(linked_window.openProjectPath(
                        linked_project, std::nullopt, std::nullopt,
                        [&linked_open_loop, &linked_open_succeeded](bool succeeded) {
                            linked_open_succeeded = succeeded;
                            linked_open_loop.quit();
                        }),
                    "The MainWindow could not start opening a linked-image project.");
            linked_open_timeout.start(30000);
            linked_open_loop.exec();
            require(linked_open_succeeded && linked_window.media_items_.size() == 1 &&
                        framePixel(linked_window.media_items_.front().first_frame) ==
                            QColor(20, 220, 35, 255) &&
                        linked_window.timeline_model_.clipCount(0) == 2 &&
                        linked_window.timeline_model_.tracks().front().clips[1]
                                .still_image_override != nullptr &&
                        framePixel(*linked_window.timeline_model_.tracks().front()
                                        .clips[1].still_image_override) ==
                            QColor(230, 210, 15, 255),
                    "The MainWindow did not open the saved shared image output.");

            media::VideoMetadata stale_metadata;
            stale_metadata.kind = media::MediaKind::Image;
            stale_metadata.source_path = linked_output;
            stale_metadata.width = 1;
            stale_metadata.height = 1;
            const media::VideoFrame stale_frame{1, 1, 4, {250, 0, 250, 255}};
            linked_window.applyLinkedImageRefresh(
                shared_link, linked_source, {}, true,
                linked_window.project_generation_ - 1, 4,
                std::filesystem::file_time_type{}, stale_metadata,
                stale_frame, {});
            require(framePixel(linked_window.media_items_.front().first_frame) ==
                        QColor(20, 220, 35, 255),
                    "A linked-output result from an old project generation changed the current media.");

            std::error_code revision_error;
            const auto current_output_size = std::filesystem::file_size(
                linked_output, revision_error);
            require(!revision_error && current_output_size > 0,
                    "The linked output revision could not be inspected in the integration test.");
            const auto current_output_modified = std::filesystem::last_write_time(
                linked_output, revision_error);
            require(!revision_error,
                    "The linked output timestamp could not be inspected in the integration test.");
            linked_window.applyLinkedImageRefresh(
                shared_link, linked_source, {}, true,
                linked_window.project_generation_, current_output_size - 1,
                current_output_modified, stale_metadata, stale_frame, {});
            require(framePixel(linked_window.media_items_.front().first_frame) ==
                        QColor(20, 220, 35, 255),
                    "A linked-output result from an obsolete file revision changed the current media.");

#if defined(CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS)
            image_editor::ImageDocumentSession masked_image;
            auto per_channel_curves = creative_suite::effects::CurvesParameters{};
            per_channel_curves.channels[0] = {{0, 255}, {255, 0}};
            QString mask_error;
            QColor refreshed_image_color;
            auto per_channel_levels = creative_suite::effects::LevelsParameters{0, 255, 1, 0, 200};
            per_channel_levels.channels[0] = {0, 255, 1, 8, 232};
            per_channel_levels.channels[1] = {4, 250, 1.2, 12, 245};
            QImage imported_source(16, 16, QImage::Format_ARGB32);
            imported_source.fill(QColor(25, 40, 235));
            const QString imported_path = QString::fromStdString((directory / "imported-layer.png").string());
            require(imported_source.save(imported_path), "Could not create imported image fixture.");
            require(masked_image.createCanvas(QSize(16, 16), Qt::transparent, &mask_error) &&
                        masked_image.importRasterImages(image_editor::prepareRasterImport({imported_path}).images, {}, &mask_error) &&
                        masked_image.addLayerMask(masked_image.selectedLayerId()) &&
                        masked_image.applyLayerMaskEraseStroke({QPointF(12, 10)}, 3, &mask_error) &&
                        masked_image.resizeCanvas(QSize(24, 12), image_editor::CanvasAnchor::TopLeft, &mask_error) &&
                        !masked_image.addExposureOffsetGammaAdjustment().isEmpty() &&
                        masked_image.setExposureOffsetGamma(masked_image.selectedLayerId(), {1, 0, 1}) &&
                        !masked_image.addHueSaturationAdjustment().isEmpty() &&
                        masked_image.setHueSaturation(masked_image.selectedLayerId(), {120, 0, 0}) &&
                        !masked_image.addLevelsAdjustment().isEmpty() &&
                        masked_image.setLevels(masked_image.selectedLayerId(), per_channel_levels) &&
                        !masked_image.addCurvesAdjustment().isEmpty() &&
                        masked_image.setCurves(masked_image.selectedLayerId(), per_channel_curves) &&
                        masked_image.saveDocument(QString::fromStdString(linked_document.string()), &mask_error) &&
                        masked_image.exportImage(QString::fromStdString(linked_output.string()), &mask_error),
                    "The Image Editor could not publish its resized, masked and per-channel Levels/Curves-adjusted v27 image: " + mask_error.toStdString());
            const QImage published_image(QString::fromStdString(linked_output.string()));
            require(!published_image.isNull(), "The v27 Image Editor PNG publication can be read.");
            refreshed_image_color = published_image.pixelColor(0, 0);
#else
            const QColor refreshed_image_color(25, 40, 235, 255);
            QImage next_output(24, 12, QImage::Format_ARGB32);
            next_output.fill(refreshed_image_color);
            for (int x = 0; x < next_output.width(); ++x) {
                next_output.setPixelColor(x, 11, QColor(x * 10, 245 - x * 8, 100, 255));
            }
            require(writePngAtomically(linked_output, next_output),
                    "The updated linked PNG could not be atomically published.");
#endif

            QEventLoop linked_refresh_loop;
            QTimer linked_refresh_timeout;
            linked_refresh_timeout.setSingleShot(true);
            QObject::connect(&linked_refresh_timeout, &QTimer::timeout,
                             &linked_refresh_loop, &QEventLoop::quit);
            QTimer linked_refresh_poll;
            QObject::connect(&linked_refresh_poll, &QTimer::timeout,
                             &linked_refresh_loop, [&]() {
                const bool media_refreshed = !linked_window.media_items_.empty() &&
                    linked_window.media_items_.front().metadata.width == 24 &&
                    linked_window.media_items_.front().metadata.height == 12 &&
                    framePixel(linked_window.media_items_.front().first_frame) ==
                        refreshed_image_color;
                const auto preview = linked_window.preview_widget_->grab().toImage();
                const bool preview_refreshed = !preview.isNull() &&
                    preview.pixelColor(preview.width() / 2, preview.height() / 2) ==
                        refreshed_image_color;
                if (media_refreshed && preview_refreshed) linked_refresh_loop.quit();
            });
            linked_refresh_timeout.start(10000);
            linked_refresh_poll.start(20);
            linked_refresh_loop.exec();
            const auto preview = linked_window.preview_widget_->grab().toImage();
            const auto media_color = linked_window.media_items_.empty()
                ? QColor() : framePixel(linked_window.media_items_.front().first_frame);
            const auto preview_color = preview.isNull()
                ? QColor() : preview.pixelColor(preview.width() / 2, preview.height() / 2);
            const auto variant_color = linked_window.timeline_model_.tracks().front()
                    .clips[1].still_image_override == nullptr
                ? QColor()
                : framePixel(*linked_window.timeline_model_.tracks().front()
                                  .clips[1].still_image_override);
            require(!linked_window.media_items_.empty() &&
                        linked_window.media_items_.front().metadata.width == 24 &&
                        linked_window.media_items_.front().metadata.height == 12 &&
                        media_color == refreshed_image_color && !preview.isNull() &&
                        preview_color == refreshed_image_color &&
                        linked_window.timeline_model_.tracks().front().clips[1]
                                .still_image_override != nullptr &&
                        variant_color == QColor(230, 210, 15, 255),
                    "A shared-image save did not refresh its preview while keeping the clip variant isolated. media=" +
                        media_color.name(QColor::HexArgb).toStdString() + " preview=" +
                        preview_color.name(QColor::HexArgb).toStdString() + " variant=" +
                        variant_color.name(QColor::HexArgb).toStdString());
#if defined(CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS)
            const auto& refreshed_frame = linked_window.media_items_.front().first_frame;
            const auto alpha_offset = static_cast<std::size_t>(10 * refreshed_frame.stride + 12 * 4 + 3);
            require(refreshed_frame.rgba_pixels.size() > alpha_offset &&
                        refreshed_frame.rgba_pixels[alpha_offset] == 0,
                    "The Video Editor lost transparency from the Image Editor's mask publication.");
#endif
        }

    }

    static void verifyRecoveryDurability(const std::filesystem::path& directory) {
        project::ProjectDocument original;
        original.bins = {"Unsorted"};
        auto recovered = original;
        recovered.timeline_zoom = 2.0;
        const auto project_path = directory / "recovery-durability.csp";
        project::save(project_path, original);
        project::AutosaveManager manager(directory / "recovery", "previous-session");
        manager.saveSnapshot(recovered, project_path, 5);
        const auto snapshot = manager.validSnapshotsForProject(project_path).front().path;
        const auto restore = [&](MainWindow& window, const std::filesystem::path& path,
                                 const std::filesystem::path& active_project) {
            require(window.restoreAutosaveSnapshot(
                        QString::fromStdWString(path.wstring()),
                        QString::fromStdWString(active_project.wstring())),
                    "A valid recovery snapshot could not be opened.");
            QEventLoop loop;
            QTimer poll;
            QTimer timeout;
            timeout.setSingleShot(true);
            QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
                if (!window.project_load_pending_) loop.quit();
            });
            QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
            poll.start(5);
            timeout.start(10000);
            loop.exec();
            require(!window.project_load_pending_ && window.project_dirty_ &&
                        window.currentProjectDocument().timeline_zoom == 2.0,
                    "Recovery did not finish with the recovered dirty document.");
        };
        {
            MainWindow window;
            require(window.findChild<QAction*>("importCompositionInterchangeAction") &&
                        window.findChild<QAction*>("exportCompositionInterchangeAction"),
                    "Composition interchange File commands are missing.");
            restore(window, snapshot, project_path);
            require(manager.validSnapshotsForProject(project_path).size() == 1 &&
                        project::load(project_path).timeline_zoom == 1.0,
                    "Recovery removed its durable copy before saving the main project.");
            QTimer::singleShot(0, [] {
                if (auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    for (auto* button : prompt->buttons()) {
                        if (button->text() == QStringLiteral("Discard")) {
                            button->click();
                            break;
                        }
                    }
                }
            });
            restore(window, snapshot, project_path);
            require(manager.validSnapshotsForProject(project_path).size() == 1,
                    "Discard during a new Restore deleted the selected recovery snapshot.");
            window.editUi().timeline->setZoomFactor(3.0);
            window.updateProjectDirtyState();
            QTimer::singleShot(0, [] {
                if (auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    for (auto* button : prompt->buttons()) {
                        if (button->text() == QStringLiteral("Save")) {
                            button->click();
                            break;
                        }
                    }
                }
            });
            restore(window, snapshot, project_path);
            require(manager.validSnapshotsForProject(project_path).size() == 1 &&
                        project::load(project_path).timeline_zoom == 3.0,
                    "Save during a new Restore deleted the selected recovery snapshot.");
        }
        require(manager.validSnapshotsForProject(project_path).size() == 1,
                "Destroying a session without Save/Discard lost the recovered copy.");
        {
            MainWindow window;
            restore(window, snapshot, project_path);
            QTimer::singleShot(0, [] {
                if (auto* warning = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    warning->accept();
            });
            require(!window.saveProjectTo(directory, "save") && window.project_dirty_ &&
                        manager.validSnapshotsForProject(project_path).size() == 1,
                    "A failed recovery save removed the snapshot or cleared dirty state.");
            require(window.saveProjectTo(project_path, "save") && !window.project_dirty_ &&
                        project::load(project_path).timeline_zoom == 2.0 &&
                        manager.validSnapshotsForProject(project_path).empty(),
                    "A successful recovery save did not durably save before cleanup.");
        }
        manager.saveSnapshot(recovered, 5);
        const auto unsaved = manager.unsavedSnapshots().front().path;
        {
            MainWindow window;
            restore(window, unsaved, {});
            require(manager.unsavedSnapshots().size() == 1,
                    "Unsaved-project restoration removed its recovery copy.");
            require(window.saveProjectTo(directory / "recovered-save-as.csp", "save_as") &&
                        manager.unsavedSnapshots().empty(),
                    "Save As did not clean up the recovered previous session after saving.");
        }
        project::save(project_path, original);
        manager.saveSnapshot(recovered, project_path, 5);
        {
            MainWindow window;
            restore(window, manager.validSnapshotsForProject(project_path).front().path,
                    project_path);
            QTimer::singleShot(0, [] {
                if (auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                    for (auto* button : prompt->buttons()) {
                        if (button->text() == QStringLiteral("Discard")) {
                            button->click();
                            break;
                        }
                    }
                }
            });
            require(window.confirmProjectChange() &&
                        manager.validSnapshotsForProject(project_path).empty() &&
                        project::load(project_path).timeline_zoom == 1.0,
                    "Explicit Discard did not release the restored recovery set.");
        }
    }

    static void setWorkspacePageImmediately(
        MainWindow& window,
        ui::WorkspacePageId page) {
        window.setWorkspacePage(page);
        QApplication::processEvents();
    }

    static void runProjectSettingsOnly(
        const std::filesystem::path& first_source,
        const std::filesystem::path& second_source) {
        run(first_source, second_source, true);
    }

    static void verifyRenderQueueCancellationOnClose(
        const std::filesystem::path& directory) {
        const auto image_path = directory / "render-close-source.png";
        QImage image(4, 4, QImage::Format_RGBA8888);
        image.fill(QColor(48, 112, 176, 255));
        require(writePngAtomically(image_path, image),
                "The shutdown Render image could not be created.");

        MainWindow window;
        window.show();
        QApplication::processEvents();
        setWorkspacePageImmediately(window, ui::WorkspacePageId::Render);

        auto* render_settings = window.render_workspace_->settingsPanel();
        auto* render_page = window.render_workspace_->centralPage();
        auto* container = render_settings->findChild<QComboBox*>("renderContainerCombo");
        auto* video_encoder = render_settings->findChild<QComboBox*>("renderVideoEncoderCombo");
        auto* start_button = render_page->findChild<QPushButton*>("renderStartQueueButton");
        auto* controller = window.render_workspace_->findChild<ui::RenderQueueController*>();
        require(container != nullptr && video_encoder != nullptr &&
                    start_button != nullptr && controller != nullptr &&
                    container->currentIndex() >= 0 && video_encoder->currentIndex() >= 0,
                "The shutdown Render queue controls were unavailable.");

        rendering::RenderJob job;
        job.display_name = QStringLiteral("Canceled on application close");
        job.settings.output_path = QString::fromStdString(
            (directory / "render-canceled-on-close.mp4").string());
        job.settings.container_name = container->currentData().toString();
        job.settings.video_encoder_name = video_encoder->currentData().toString();
        job.settings.width = 320;
        job.settings.height = 180;
        job.settings.frame_rate = 30.0;
        job.settings.video_bitrate_mbps = 1.0;
        job.settings.export_audio = false;
        job.project_snapshot.timeline_frame_rate = timeline::FrameRate{30, 1};
        project::ProjectTrack track;
        track.track_id = 701;
        track.name = "Still image";
        project::ProjectClip clip;
        clip.source_path = image_path;
        clip.duration_frames = 3000;
        clip.kind = timeline::ClipKind::Image;
        track.clips.push_back(std::move(clip));
        job.project_snapshot.timeline_tracks.push_back(std::move(track));
        require(window.render_workspace_->queueModel()->addJob(std::move(job)) != 0,
                "The shutdown Render job could not be queued.");

        bool queue_finished = false;
        QEventLoop queue_loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        QTimer dismiss_dialogs;
        dismiss_dialogs.setInterval(10);
        QObject::connect(controller, &ui::RenderQueueController::queueFinished,
            &queue_loop, [&] {
                queue_finished = true;
                queue_loop.quit();
            });
        QObject::connect(&timeout, &QTimer::timeout,
            &queue_loop, &QEventLoop::quit);
        QObject::connect(&dismiss_dialogs, &QTimer::timeout,
            &queue_loop, [] {
                if (auto* dialog = qobject_cast<QMessageBox*>(
                        QApplication::activeModalWidget())) {
                    dialog->accept();
                }
            });

        start_button->click();
        require(window.render_workspace_->isQueueRunning(),
                "The shutdown fixture did not start its Render queue.");
        require(window.close(),
                "Closing the application with a clean project should be accepted.");
        require(!window.isVisible(),
                "The application window remained visible after an accepted close.");
        if (!queue_finished) {
            timeout.start(30000);
            dismiss_dialogs.start();
            queue_loop.exec();
            dismiss_dialogs.stop();
        }
        const auto* canceled_job = window.render_workspace_->queueModel()->jobAt(0);
        require(queue_finished && !window.render_workspace_->isQueueRunning(),
                "The shutdown Render queue did not finish after the application close request.");
        require(canceled_job != nullptr &&
                    canceled_job->status == rendering::RenderJobStatus::Canceled,
                "Closing the application must mark the active Render job Canceled.");
        require(!std::filesystem::exists(directory / "render-canceled-on-close.mp4"),
                "Closing during Render published an incomplete destination file.");
    }

    static void run(
        const std::filesystem::path& first_source,
        const std::filesystem::path& second_source,
        bool project_settings_only = false) {
        const auto directory = uniqueTestDirectory();
        std::filesystem::create_directories(directory);

        const auto image_patterns = main_window_detail::stillImageFilePatterns();
        for (const auto& format : QImageReader::supportedImageFormats()) {
            const auto pattern = QStringLiteral("*.") +
                QString::fromLatin1(format).toLower();
            require(image_patterns.contains(pattern),
                    "The Open Media image filter omitted a runtime QImageReader format.");
        }
        if (avcodec_find_decoder(AV_CODEC_ID_WEBP) != nullptr) {
            require(image_patterns.contains(QStringLiteral("*.webp")),
                    "The Open Media image filter omitted the available FFmpeg WebP fallback.");
        }
        if (avcodec_find_decoder(AV_CODEC_ID_TIFF) != nullptr) {
            require(image_patterns.contains(QStringLiteral("*.tif")) &&
                        image_patterns.contains(QStringLiteral("*.tiff")),
                    "The Open Media image filter omitted the available FFmpeg TIFF fallback.");
        }

        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(
            QSettings::IniFormat,
            QSettings::UserScope,
            QString::fromStdString((directory / "settings").string()));
        QCoreApplication::setOrganizationName("CreativeSuiteTests");
        QCoreApplication::setApplicationName("MainWindowIntegration");
        QSettings().clear();
        settings::setProjectAutosaveEnabled(false);
        settings::setPreviewPerformanceMetricsEnabled(false);
        // This integration test makes immediate workspace assertions for
        // shortcuts and dock state. Transition timing is covered by the
        // dedicated workspace-page-transition test.
        settings::setWorkspacePageTransitionsEnabled(false);

        verifyRecoveryDurability(directory);

        std::array<bool, 9> dock_visibility_before_close{};
        {
            MainWindow render_window;
            render_window.show();
            QApplication::processEvents();
            const auto startup_project = render_window.currentProjectDocument();
            require(startup_project.canvas_width == 1920 &&
                        startup_project.canvas_height == 1080 &&
                        startup_project.timeline_frame_rate ==
                            timeline::FrameRate{30, 1} &&
                        !render_window.project_dirty_,
                    "The editor must open on a clean default 16:9, 30 fps project.");
            require(render_window.preview_dock_ != nullptr &&
                        render_window.preview_dock_->isVisible() &&
                        render_window.dockWidgetArea(
                            render_window.preview_dock_) == Qt::RightDockWidgetArea &&
                        render_window.preview_dock_->widget()->isAncestorOf(
                            render_window.preview_widget_) &&
                        render_window.preview_dock_->geometry().left() <
                            render_window.inspector_dock_->geometry().left() &&
                        render_window.preview_dock_->height() > 100 &&
                        render_window.render_workspace_->centralPage()->isHidden(),
                    "The default layout must show Preview as a dock in the central work area.");
            const auto& inspector_ui = render_window.edit_workspace_->ui();
            auto* inspector_tabs = inspector_ui.inspector_tabs;
            require(inspector_tabs != nullptr && inspector_tabs->count() == 3 &&
                        inspector_tabs->tabText(0) == QStringLiteral("Inspector") &&
                        inspector_tabs->tabText(1) == QStringLiteral("Audio") &&
                        inspector_tabs->tabText(2) == QStringLiteral("Effects") &&
                        inspector_tabs->widget(0)->isAncestorOf(
                            inspector_ui.transform_spins[0]) &&
                        inspector_tabs->widget(1)->isAncestorOf(inspector_ui.clip_volume) &&
                        inspector_tabs->widget(1)->isAncestorOf(
                            inspector_ui.clip_audio_effects) &&
                        inspector_tabs->widget(2)->isAncestorOf(
                            inspector_ui.clip_effects_controls) &&
                        inspector_tabs->widget(2)->isAncestorOf(
                            inspector_ui.effect_selection_hint),
                    "Inspector, Audio, and Effects controls must live on their dedicated tabs.");
            const auto inspector_number_fields =
                inspector_tabs->findChildren<QAbstractSpinBox*>();
            require(!inspector_number_fields.isEmpty(),
                    "The Edit Inspector should expose its numeric fields to the UI regression.");
            for (const auto* field : inspector_number_fields) {
                const auto* decimal_field = qobject_cast<const QDoubleSpinBox*>(field);
                require(field->buttonSymbols() == QAbstractSpinBox::NoButtons &&
                            !field->isReadOnly() &&
                            (decimal_field == nullptr || decimal_field->decimals() == 3),
                        "Edit Inspector numeric fields must hide step buttons and remain editable.");
            }
            require(inspector_ui.transform_sliders[0] != nullptr &&
                        inspector_ui.transform_keys[0] != nullptr &&
                        inspector_ui.transform_keys[0]->isCheckable(),
                    "Hiding numeric step buttons must preserve transform sliders and keyframe toggles.");
            const auto* transform_key = inspector_ui.transform_keys[0];
            const auto keyframe_off = transform_key->icon().pixmap(
                QSize(24, 24), QIcon::Normal, QIcon::Off).toImage();
            const auto keyframe_on = transform_key->icon().pixmap(
                QSize(24, 24), QIcon::Normal, QIcon::On).toImage();
            require(transform_key->text().isEmpty() && !transform_key->icon().isNull() &&
                        keyframe_off.pixelColor(12, 12).alpha() == 0 &&
                        keyframe_on.pixelColor(12, 12).alpha() > 0,
                    "Edit keyframe toggles must render outlined and filled diamond icons without glyph text.");
            for (int index = 0; index < inspector_tabs->count(); ++index) {
                inspector_tabs->setCurrentIndex(index);
                QApplication::processEvents();
                verifyInspectorScrollArea(
                    qobject_cast<QScrollArea*>(inspector_tabs->widget(index)),
                    inspector_tabs->tabText(index).toUtf8().constData());
            }
            inspector_tabs->setCurrentIndex(0);
            QApplication::processEvents();
            require(!inspector_ui.clip_effects_controls->isEnabled() &&
                        !inspector_ui.effect_selection_hint->isHidden(),
                    "The Effects tab must guide users and disable controls when no clip is selected.");
            require(editAction(render_window, "edit.copy_attributes") != nullptr &&
                        editAction(render_window, "edit.paste_attributes") != nullptr &&
                        render_window.edit_workspace_->findChild<QAction*>(
                            "edit.copy_attributes") ==
                            editAction(render_window, "edit.copy_attributes") &&
                        render_window.edit_workspace_->findChild<QAction*>(
                            "edit.delete_clip") ==
                            editAction(render_window, "edit.delete_clip") &&
                        editAction(render_window, "edit.copy_attributes")->shortcut() ==
                            QKeySequence(QStringLiteral("Ctrl+C")) &&
                        editAction(render_window, "edit.paste_attributes")->shortcut() ==
                            QKeySequence(QStringLiteral("Ctrl+Shift+V")) &&
                        !editAction(render_window, "edit.copy_attributes")->isEnabled() &&
                        !editAction(render_window, "edit.paste_attributes")->isEnabled() &&
                        !render_window.shortcut_manager_->shortcut(
                            QStringLiteral("edit.copy_attributes")).isEmpty() &&
                        !render_window.shortcut_manager_->shortcut(
                            QStringLiteral("edit.paste_attributes")).isEmpty(),
                    "EditWorkspace must own Copy/Delete actions and retain their shortcut registration and default availability.");
            auto* require_alt_action = editAction(
                render_window, "edit.require_alt_to_move");
            auto* move_playhead_action = editAction(
                render_window, "edit.move_playhead_on_clip_selection");
            require(require_alt_action != nullptr && move_playhead_action != nullptr &&
                        !render_window.edit_workspace_->ui().timeline->moveRequiresAlt() &&
                        !QSettings().value(
                            QStringLiteral("timeline/move_playhead_on_clip_selection"),
                            false).toBool(),
                    "Timeline preference commands must initialize from their persisted defaults.");
            require_alt_action->setChecked(true);
            move_playhead_action->setChecked(true);
            require(render_window.edit_workspace_->ui().timeline->moveRequiresAlt() &&
                        QSettings().value(
                            QStringLiteral("timeline/require_alt_to_move")).toBool() &&
                        QSettings().value(
                            QStringLiteral("timeline/move_playhead_on_clip_selection")).toBool(),
                    "EditWorkspace preference commands must apply and persist their Timeline settings.");
            require_alt_action->setChecked(false);
            move_playhead_action->setChecked(false);
            auto* blade_tool_action = editAction(render_window, "edit.blade_tool");
            require(blade_tool_action != nullptr,
                    "The EditWorkspace must provide the Blade Tool command.");
            blade_tool_action->trigger();
            require(blade_tool_action->isChecked() &&
                        render_window.edit_workspace_->ui().timeline->razorMode(),
                    "Blade Tool must update the Timeline mode through the Edit workspace.");
            blade_tool_action->trigger();
            require(!blade_tool_action->isChecked() &&
                        !render_window.edit_workspace_->ui().timeline->razorMode(),
                    "Blade Tool must return the Timeline to its normal mode when toggled off.");
            require(editAction(render_window, "edit.delete_clip") != nullptr &&
                        editAction(render_window, "edit.ripple_delete_clip") != nullptr &&
                        editAction(render_window, "edit.delete_clip")->shortcut() ==
                            QKeySequence(Qt::Key_Delete) &&
                        editAction(render_window, "edit.ripple_delete_clip")->shortcut() ==
                            QKeySequence(QStringLiteral("Shift+Delete")) &&
                        !editAction(render_window, "edit.ripple_delete_clip")->isEnabled() &&
                        !render_window.shortcut_manager_->shortcut(
                            QStringLiteral("edit.ripple_delete_clip")).isEmpty(),
                    "Ripple Delete must be a separate registered command with Shift+Delete and be disabled without a selected clip.");
            require(
                render_window.shortcut_manager_->shortcut(
                    QStringLiteral("timeline.nudge_left")) ==
                    QKeySequence(QStringLiteral("Ctrl+Left")) &&
                    render_window.shortcut_manager_->shortcut(
                        QStringLiteral("timeline.nudge_right")) ==
                    QKeySequence(QStringLiteral("Ctrl+Right")) &&
                    editAction(render_window, "timeline.nudge_left") != nullptr &&
                    editAction(render_window, "timeline.nudge_right") != nullptr,
                "EditWorkspace must own the existing Timeline nudge shortcut assignments.");
            require(render_window.shortcut_manager_->setShortcut(
                        QStringLiteral("edit.ripple_delete_clip"),
                        QKeySequence(QStringLiteral("Ctrl+Alt+Delete"))) &&
                        editAction(render_window, "edit.ripple_delete_clip")->shortcut() ==
                            QKeySequence(QStringLiteral("Ctrl+Alt+Delete")) &&
                        render_window.shortcut_manager_->resetShortcut(
                            QStringLiteral("edit.ripple_delete_clip")) &&
                        editAction(render_window, "edit.ripple_delete_clip")->shortcut() ==
                            QKeySequence(QStringLiteral("Shift+Delete")),
                    "Ripple Delete must support shortcut customization and reset to Shift+Delete.");
            require(render_window.shortcut_manager_->setShortcut(
                        QStringLiteral("edit.copy_attributes"),
                        QKeySequence(QStringLiteral("Ctrl+Alt+C"))) &&
                        editAction(render_window, "edit.copy_attributes")->shortcut() ==
                            QKeySequence(QStringLiteral("Ctrl+Alt+C")) &&
                        render_window.shortcut_manager_->resetShortcut(
                            QStringLiteral("edit.copy_attributes")) &&
                        editAction(render_window, "edit.copy_attributes")->shortcut() ==
                            QKeySequence(QStringLiteral("Ctrl+C")),
                    "Copy Attributes must support shortcut customization and reset to its default.");
            setWorkspacePageImmediately(render_window, ui::WorkspacePageId::Fusion);
            require(render_window.save_project_action_->shortcut() ==
                        QKeySequence(QStringLiteral("Ctrl+S")) &&
                        render_window.undo_action_->shortcut() == QKeySequence::Undo &&
                        editAction(render_window, "edit.delete_clip")->shortcut().isEmpty() &&
                        editAction(render_window, "timeline.nudge_left")->shortcut().isEmpty() &&
                        editAction(render_window, "timeline.nudge_right")->shortcut().isEmpty() &&
                        render_window.shortcut_manager_->shortcut(
                            QStringLiteral("edit.delete_clip")) ==
                            QKeySequence(Qt::Key_Delete),
                    "Fusion must retain Application and Shared shortcuts while preserving inactive Edit assignments.");
            setWorkspacePageImmediately(render_window, ui::WorkspacePageId::Render);
            require(render_window.save_project_action_->shortcut() ==
                        QKeySequence(QStringLiteral("Ctrl+S")) &&
                        render_window.undo_action_->shortcut().isEmpty() &&
                        editAction(render_window, "edit.delete_clip")->shortcut().isEmpty(),
                    "Render must retain Application shortcuts and disable Shared and Edit shortcuts.");
            setWorkspacePageImmediately(render_window, ui::WorkspacePageId::Edit);
            require(render_window.undo_action_->shortcut() == QKeySequence::Undo &&
                        editAction(render_window, "edit.delete_clip")->shortcut() ==
                            QKeySequence(Qt::Key_Delete) &&
                        editAction(render_window, "timeline.nudge_left")->shortcut() ==
                            QKeySequence(QStringLiteral("Ctrl+Left")) &&
                        editAction(render_window, "timeline.nudge_right")->shortcut() ==
                            QKeySequence(QStringLiteral("Ctrl+Right")),
                    "Returning to Edit must restore its configured shortcuts.");
            const bool dirty_before_inspector_tab_change = render_window.project_dirty_;
            inspector_tabs->setCurrentIndex(2);
            QApplication::processEvents();
            require(QSettings().value("inspector/active_tab").toInt() == 2 &&
                        render_window.project_dirty_ == dirty_before_inspector_tab_change,
                    "Selecting the Effects tab must persist globally without dirtying the project.");
            auto* full_quality = render_window.findChild<QAction*>(
                "playbackPreviewQualityFull");
            auto* half_quality = render_window.findChild<QAction*>(
                "playbackPreviewQualityHalf");
            auto* quarter_quality = render_window.findChild<QAction*>(
                "playbackPreviewQualityQuarter");
            require(full_quality != nullptr && half_quality != nullptr &&
                        quarter_quality != nullptr && full_quality->isChecked() &&
                        !half_quality->isChecked() && !quarter_quality->isChecked(),
                    "Playback Preview Quality must offer three exclusive options and default to Full.");
            const bool dirty_before_quality_change = render_window.project_dirty_;
            quarter_quality->trigger();
            QApplication::processEvents();
            require(render_window.playback_preview_quality_ ==
                        playback::PreviewQuality::Quarter &&
                        quarter_quality->isChecked() && !full_quality->isChecked() &&
                        QSettings().value("preview/playback_quality").toInt() == 2,
                    "Selecting Quarter must persist and check the selected Playback Preview Quality.");
            require(render_window.project_dirty_ == dirty_before_quality_change,
                    "Changing Playback Preview Quality must not dirty the project.");

            auto* render_settings = render_window.render_workspace_->settingsPanel();
            auto* render_resolution = render_settings != nullptr
                ? render_settings->findChild<QComboBox*>("renderResolutionCombo")
                : nullptr;
            auto* render_frame_rate = render_settings != nullptr
                ? render_settings->findChild<QDoubleSpinBox*>("renderFrameRate")
                : nullptr;
            require(render_resolution != nullptr && render_frame_rate != nullptr,
                    "The Render settings controls were not available to Project Settings.");
            const auto edit_project_settings_from_dialog =
                [&render_window](int canvas_index, int frame_rate_index, bool accept) {
                    bool handled = false;
                    bool choices_valid = false;
                    QTimer::singleShot(0, [&] {
                        auto* dialog = qobject_cast<QDialog*>(
                            QApplication::activeModalWidget());
                        auto* canvas = dialog != nullptr
                            ? dialog->findChild<QComboBox*>(
                                  "projectSettingsCanvasCombo")
                            : nullptr;
                        auto* frame_rate = dialog != nullptr
                            ? dialog->findChild<QComboBox*>(
                                  "projectSettingsFrameRateCombo")
                            : nullptr;
                        auto* buttons = dialog != nullptr
                            ? dialog->findChild<QDialogButtonBox*>()
                            : nullptr;
                        if (dialog == nullptr || canvas == nullptr ||
                            frame_rate == nullptr || buttons == nullptr ||
                            dialog->objectName() !=
                                QStringLiteral("projectSettingsDialog")) {
                            return;
                        }
                        choices_valid = canvas->count() == 2 &&
                            canvas->currentIndex() == 0 &&
                            canvas->itemData(0).toSize() == QSize(1920, 1080) &&
                            canvas->itemData(1).toSize() == QSize(1080, 1920) &&
                            frame_rate->count() == 6 &&
                            frame_rate->currentData().toMap()
                                .value(QStringLiteral("numerator")).toLongLong() == 30 &&
                            frame_rate->currentData().toMap()
                                .value(QStringLiteral("denominator")).toLongLong() == 1;
                        canvas->setCurrentIndex(canvas_index);
                        frame_rate->setCurrentIndex(frame_rate_index);
                        if (accept) {
                            buttons->button(QDialogButtonBox::Ok)->click();
                        } else {
                            dialog->reject();
                        }
                        handled = true;
                    });
                    render_window.project_settings_action_->trigger();
                    return handled && choices_valid;
                };
            require(edit_project_settings_from_dialog(1, 0, false) &&
                        render_window.currentProjectDocument().canvas_width == 1920 &&
                        render_window.currentProjectDocument().timeline_frame_rate ==
                            timeline::FrameRate{30, 1} && !render_window.project_dirty_,
                    "Canceling Project Settings changed the active project.");
            require(edit_project_settings_from_dialog(1, 0, true),
                    "Project Settings did not show its defaults or supported choices.");
            require(render_window.currentProjectDocument().canvas_width == 1080 &&
                        render_window.currentProjectDocument().canvas_height == 1920 &&
                        render_window.currentProjectDocument().timeline_frame_rate ==
                            timeline::FrameRate{24, 1} && render_window.project_dirty_ &&
                        render_resolution->currentText() ==
                            QStringLiteral("Project (1080 × 1920)") &&
                        render_frame_rate->value() == 24.0,
                    "Project Settings did not update the project and Render defaults.");
            const auto project_settings_undo =
                render_window.edit_workspace_->controller()->undo();
            auto* resolution_after_undo = render_settings->findChild<QComboBox*>(
                "renderResolutionCombo");
            auto* frame_rate_after_undo = render_settings->findChild<QDoubleSpinBox*>(
                "renderFrameRate");
            const auto document_after_undo = render_window.currentProjectDocument();
            require(project_settings_undo.changed() &&
                        document_after_undo.canvas_width == 1920 &&
                        document_after_undo.canvas_height == 1080 &&
                        document_after_undo.timeline_frame_rate ==
                            timeline::FrameRate{30, 1} && !render_window.project_dirty_ &&
                        resolution_after_undo != nullptr &&
                        resolution_after_undo->currentText() ==
                            QStringLiteral("Project (1920 × 1080)") &&
                        frame_rate_after_undo != nullptr &&
                        frame_rate_after_undo->value() == 30.0,
                    "Undo did not restore Project Settings and Render defaults.");
            require(render_window.edit_workspace_->controller()->redo().changed() &&
                        render_window.currentProjectDocument().canvas_width == 1080 &&
                        render_window.currentProjectDocument().timeline_frame_rate ==
                            timeline::FrameRate{24, 1},
                    "Redo did not reapply Project Settings.");
            require(render_window.edit_workspace_->controller()->undo().changed() &&
                        !render_window.project_dirty_,
                    "Undo could not return Project Settings to the saved baseline.");

            const auto unusual_rate_change = render_window.timeline_command_service_.execute(
                application::SetProjectSettingsCommand{1920, 1080, {30000, 1001}});
            require(unusual_rate_change.changed(),
                    "The Project Settings UI test could not prepare a nonstandard current rate.");
            render_window.applyTimelineEditResult(unusual_rate_change, true);
            render_window.updateTimelineState();
            render_window.refreshPlaybackComposition();
            bool unusual_rate_option_valid = false;
            QTimer::singleShot(0, [&] {
                auto* dialog = qobject_cast<QDialog*>(
                    QApplication::activeModalWidget());
                auto* frame_rate = dialog != nullptr
                    ? dialog->findChild<QComboBox*>(
                          "projectSettingsFrameRateCombo")
                    : nullptr;
                if (dialog == nullptr || frame_rate == nullptr) return;
                const auto value = frame_rate->currentData().toMap();
                unusual_rate_option_valid = frame_rate->count() == 7 &&
                    frame_rate->currentIndex() == 6 &&
                    value.value(QStringLiteral("numerator")).toLongLong() == 30000 &&
                    value.value(QStringLiteral("denominator")).toLongLong() == 1001;
                dialog->reject();
            });
            render_window.project_settings_action_->trigger();
            require(unusual_rate_option_valid &&
                        render_window.currentProjectDocument().timeline_frame_rate ==
                            timeline::FrameRate{30000, 1001},
                    "Project Settings did not preserve a valid nonstandard current rate in its choices.");
            require(render_window.edit_workspace_->controller()->undo().changed() &&
                        render_window.currentProjectDocument().timeline_frame_rate ==
                            timeline::FrameRate{30, 1} && !render_window.project_dirty_,
                    "Undo did not restore the nonstandard-rate test fixture.");
            if (project_settings_only) return;

            bool gpu_ui_forwarded = false;
            QTimer::singleShot(0, [&] {
                auto* dialog = qobject_cast<settings::SettingsDialog*>(QApplication::activeModalWidget());
                auto* checkbox = dialog ? dialog->findChild<QCheckBox*>("gpuCompositionCheckBox") : nullptr;
                if (checkbox) {
                    checkbox->setChecked(true);
                    gpu_ui_forwarded = settings::gpuCompositionEnabled() &&
                        render_window.playback_controller_->gpuCompositionEnabled();
                }
                if (dialog) dialog->accept();
            });
            render_window.showSettingsDialog();
            require(gpu_ui_forwarded && render_window.project_dirty_ == dirty_before_quality_change,
                    "Settings GPU checkbox did not reach controller or modified project.");

            const auto create_project_from_dialog =
                [&render_window](int canvas_index, int frame_rate_index, bool accept) {
                    bool handled = false;
                    bool choices_valid = false;
                    QTimer::singleShot(0, [&] {
                        auto* dialog = qobject_cast<QDialog*>(
                            QApplication::activeModalWidget());
                        auto* canvas = dialog != nullptr
                            ? dialog->findChild<QComboBox*>("newProjectCanvasCombo")
                            : nullptr;
                        auto* frame_rate = dialog != nullptr
                            ? dialog->findChild<QComboBox*>("newProjectFrameRateCombo")
                            : nullptr;
                        auto* buttons = dialog != nullptr
                            ? dialog->findChild<QDialogButtonBox*>()
                            : nullptr;
                        if (canvas == nullptr || frame_rate == nullptr ||
                            buttons == nullptr) {
                            return;
                        }
                        constexpr std::array<int, 6> expected_rates{
                            24, 25, 30, 48, 50, 60};
                        choices_valid = canvas->count() == 2 &&
                            canvas->currentIndex() == 0 &&
                            canvas->itemData(0).toSize() == QSize(1920, 1080) &&
                            canvas->itemData(1).toSize() == QSize(1080, 1920) &&
                            frame_rate->count() == static_cast<int>(expected_rates.size()) &&
                            frame_rate->currentData().toInt() == 30;
                        for (std::size_t index = 0; choices_valid &&
                             index < expected_rates.size(); ++index) {
                            choices_valid = frame_rate->itemData(
                                static_cast<int>(index)).toInt() == expected_rates[index];
                        }
                        if (canvas_index >= 0) canvas->setCurrentIndex(canvas_index);
                        if (frame_rate_index >= 0) {
                            frame_rate->setCurrentIndex(frame_rate_index);
                        }
                        if (accept) {
                            buttons->button(QDialogButtonBox::Ok)->click();
                        } else {
                            dialog->reject();
                        }
                        handled = true;
                    });
                    render_window.new_project_action_->trigger();
                    return handled && choices_valid;
                };
            require(create_project_from_dialog(1, 5, true),
                    "The project-creation dialog did not expose its canvas and frame-rate choices.");
            auto created_portrait = render_window.currentProjectDocument();
            require(created_portrait.canvas_width == 1080 &&
                        created_portrait.canvas_height == 1920 &&
                        created_portrait.timeline_frame_rate == timeline::FrameRate{60, 1} &&
                        !render_window.project_dirty_,
                    "Creating a portrait 60 fps project did not apply clean project settings.");
            require(render_resolution != nullptr &&
                        render_resolution->currentText() ==
                            QStringLiteral("Project (1080 × 1920)") &&
                        render_frame_rate != nullptr &&
                        render_frame_rate->value() == 60.0,
                    "Render defaults did not follow the active portrait canvas and project FPS.");
            require(create_project_from_dialog(-1, -1, false),
                    "Canceling project creation did not close its dialog.");
            require(render_window.currentProjectDocument().canvas_width == 1080 &&
                        render_window.currentProjectDocument().canvas_height == 1920 &&
                        render_window.editor_session_.timeline().frameRate() ==
                            timeline::FrameRate{60, 1},
                    "Canceling project creation changed the active project.");
            require(create_project_from_dialog(-1, -1, true),
                    "Creating a project with the default choices failed.");
            const auto created_default = render_window.currentProjectDocument();
            require(created_default.canvas_width == 1920 &&
                        created_default.canvas_height == 1080 &&
                        created_default.timeline_frame_rate == timeline::FrameRate{30, 1} &&
                        !render_window.project_dirty_ &&
                        render_resolution->currentText() ==
                            QStringLiteral("Project (1920 × 1080)") &&
                        render_frame_rate->value() == 30.0,
                    "The New Project defaults or clean-state behavior changed.");
            render_window.tabifyDockWidget(
                render_window.preview_dock_, render_window.inspector_dock_);
            render_window.preview_dock_->raise();
            QApplication::processEvents();
            const std::array<QDockWidget*, 9> docks{
                render_window.bins_dock_, render_window.media_dock_,
                render_window.toolbox_dock_, render_window.favorites_dock_,
                render_window.effects_dock_, render_window.inspector_dock_,
                render_window.preview_dock_, render_window.timeline_dock_,
                render_window.render_settings_dock_};
            for (std::size_t index = 0; index < docks.size(); ++index) {
                if (index == docks.size() - 1) {
                    docks[index]->hide();
                }
                dock_visibility_before_close[index] = !docks[index]->isHidden();
            }
            setWorkspacePageImmediately(render_window, ui::WorkspacePageId::Render);
            for (std::size_t index = 0; index < docks.size(); ++index) {
                require(docks[index]->isVisible() == (index == 7 || index == 8),
                        "Render must show Timeline and Settings before the close-persistence check.");
            }
            require(render_window.close(),
                    "Closing the editor from Render must be accepted.");
            settings::setHardwareDecodingEnabled(true);
            require(render_window.tabifiedDockWidgets(
                        render_window.preview_dock_).contains(
                            render_window.inspector_dock_),
                    "Closing from Render must restore the Preview dock's tab arrangement.");
        }
        {
            MainWindow reopened_window;
            reopened_window.show();
            QApplication::processEvents();
            const std::array<QDockWidget*, 9> docks{
                reopened_window.bins_dock_, reopened_window.media_dock_,
                reopened_window.toolbox_dock_, reopened_window.favorites_dock_,
                reopened_window.effects_dock_, reopened_window.inspector_dock_,
                reopened_window.preview_dock_, reopened_window.timeline_dock_,
                reopened_window.render_settings_dock_};
            require(reopened_window.preview_dock_->isVisible() &&
                        reopened_window.preview_dock_->height() > 100,
                    "The Preview dock must remain available after reopening the editor.");
            require(reopened_window.edit_workspace_button_->isChecked(),
                    "The application must reopen in Edit after closing from Render.");
            require(reopened_window.edit_workspace_->ui().inspector_tabs->currentIndex() == 2,
                    "The Effects Inspector tab must be restored after reopening the application.");
            auto* reopened_quarter_quality = reopened_window.findChild<QAction*>(
                "playbackPreviewQualityQuarter");
            auto* reopened_full_quality = reopened_window.findChild<QAction*>(
                "playbackPreviewQualityFull");
            require(reopened_window.playback_preview_quality_ ==
                        playback::PreviewQuality::Quarter &&
                        reopened_quarter_quality != nullptr &&
                        reopened_quarter_quality->isChecked() &&
                        reopened_full_quality != nullptr,
                    "Playback Preview Quality must persist across application restarts.");
            reopened_full_quality->trigger();
            require(reopened_window.playback_controller_->gpuCompositionEnabled(),
                    "Persisted GPU preference was not applied at startup.");
            require(reopened_window.playback_controller_->hardwareDecodingEnabled(),
                    "Persisted hardware decode preference was not applied at startup.");
            settings::setHardwareDecodingEnabled(false);
            reopened_window.playback_controller_->setHardwareDecodingEnabled(false);
            settings::setGpuCompositionEnabled(false);
            reopened_window.playback_controller_->setGpuCompositionEnabled(false);
            QApplication::processEvents();
            require(QSettings().value("preview/playback_quality").toInt() == 0,
                    "The Full Playback Preview Quality selection was not persisted.");
            for (std::size_t index = 0; index < docks.size(); ++index) {
                require(!docks[index]->isHidden() == dock_visibility_before_close[index],
                        "Closing from Render must preserve the previous dock layout.");
            }
        }

        QByteArray version_eight_layout;
        {
            // Serialize a legacy layout with fresh docks. Moving the real
            // Preview out of a live tab/split arrangement can fail inside Qt's
            // pending dock animation, independently of state migration.
            QSettings().remove("workspace/dock_layout_state");
            QSettings().sync();
            QMainWindow legacy_layout_window;
            legacy_layout_window.resize(1200, 800);
            legacy_layout_window.setDockOptions(QMainWindow::AllowTabbedDocks);
            const auto add_legacy_dock = [&](const char* name, Qt::DockWidgetArea area) {
                auto* dock = new QDockWidget(QString::fromLatin1(name), &legacy_layout_window);
                dock->setObjectName(QString::fromLatin1(name));
                dock->setWidget(new QWidget(dock));
                legacy_layout_window.addDockWidget(area, dock, Qt::Vertical);
            };
            for (const auto* name : {"binsDock", "mediaDock", "toolboxDock", "favoritesDock", "effectsDock"})
                add_legacy_dock(name, Qt::LeftDockWidgetArea);
            add_legacy_dock("inspectorDock", Qt::RightDockWidgetArea);
            add_legacy_dock("previewDock", Qt::RightDockWidgetArea);
            add_legacy_dock("timelineDock", Qt::BottomDockWidgetArea);
            version_eight_layout = legacy_layout_window.saveState(8);
            require(!version_eight_layout.isEmpty(),
                    "The Preview layout migration fixture could not be saved.");
            require(legacy_layout_window.close(),
                    "The legacy Preview layout fixture did not close cleanly.");
        }
        QSettings().setValue(
            QStringLiteral("workspace/dock_layout_state"), version_eight_layout);
        QSettings().sync();
        {
            MainWindow migrated_layout_window;
            migrated_layout_window.show();
            QApplication::processEvents();
            require(migrated_layout_window.preview_dock_->isVisible() &&
                        migrated_layout_window.preview_dock_->geometry().left() <
                            migrated_layout_window.inspector_dock_->geometry().left() &&
                        migrated_layout_window.preview_dock_->height() > 100 &&
                        migrated_layout_window.render_workspace_->centralPage()->isHidden(),
                    "A version 8 layout must repair the Preview dock beside Inspector and keep Render hidden.");
            require(migrated_layout_window.close(),
                    "The repaired Preview layout did not close cleanly.");
        }
        QSettings().remove("workspace/dock_layout_state");
        QSettings().sync();

        const auto project_path = directory / "multi-track.csp";
        const auto round_trip_path = directory / "multi-track-round-trip.csp";
        const auto original = makeMultiTrackProject(first_source, second_source);
        project::save(project_path, original);
        const auto loaded = project::load(project_path);

        {
            MainWindow window;
            window.show();
            QApplication::processEvents();
            const auto& workspace_ui = window.edit_workspace_->ui();
            auto* media_pool_toolbar =
                window.findChild<QToolBar*>("mediaPoolToolbar");
            require(media_pool_toolbar != nullptr &&
                        window.statusBar()->isVisible() &&
                        workspace_ui.workspace_footer != nullptr &&
                        workspace_ui.workspace_footer->isVisible() &&
                        workspace_ui.workspace_footer->height() == 22 &&
                        window.statusBar()->height() == 26 &&
                        workspace_ui.workspace_footer->parentWidget() ==
                            window.statusBar() &&
                        workspace_ui.workspace_navigation_slot != nullptr &&
                        window.workspace_buttons_container_->parentWidget() ==
                            workspace_ui.workspace_navigation_slot &&
                        !media_pool_toolbar->isAncestorOf(
                            window.workspace_buttons_container_) &&
                        workspace_ui.playback_status->text() ==
                            QStringLiteral("No media selected.") &&
                        workspace_ui.system_memory_indicator->isVisible(),
                    "The workspace selectors, playback status, and memory indicator must share the persistent application footer, outside the top toolbar.");
            const auto require_footer_centered = [&window, &workspace_ui]() {
                const auto nav_center_x =
                    window.workspace_buttons_container_->mapTo(
                        workspace_ui.workspace_footer,
                        window.workspace_buttons_container_->rect().center()).x();
                require(std::abs(
                            nav_center_x -
                            workspace_ui.workspace_footer->rect().center().x()) <= 1,
                        "The workspace selectors must be centered in the global footer.");
            };
            const bool footer_test_was_maximized = window.isMaximized();
            const auto footer_test_geometry = window.geometry();
            window.showNormal();
            window.resize(1100, 720);
            QApplication::processEvents();
            require_footer_centered();
            window.resize(1400, 900);
            QApplication::processEvents();
            require_footer_centered();
            if (footer_test_was_maximized) {
                window.showMaximized();
            } else {
                window.setGeometry(footer_test_geometry);
            }
            QApplication::processEvents();
            auto* edit_workspace_shortcut = window.findChild<QAction*>(
                "workspaceSwitchEditAction");
            auto* fusion_workspace_shortcut = window.findChild<QAction*>(
                "workspaceSwitchFusionAction");
            auto* audio_workspace_shortcut = window.findChild<QAction*>(
                "workspaceSwitchAudioAction");
            auto* render_workspace_shortcut = window.findChild<QAction*>(
                "workspaceSwitchRenderAction");
            require(edit_workspace_shortcut != nullptr &&
                        fusion_workspace_shortcut != nullptr &&
                        audio_workspace_shortcut != nullptr &&
                        render_workspace_shortcut != nullptr &&
                        edit_workspace_shortcut->shortcut() ==
                            QKeySequence("Alt+1") &&
                        fusion_workspace_shortcut->shortcut() ==
                            QKeySequence("Alt+2") &&
                        audio_workspace_shortcut->shortcut() ==
                            QKeySequence("Alt+4") &&
                        render_workspace_shortcut->shortcut() ==
                            QKeySequence("Alt+3") &&
                        edit_workspace_shortcut->shortcutContext() ==
                            Qt::WindowShortcut &&
                        window.shortcut_manager_->shortcut(
                            QStringLiteral("workspace.switch_edit")) ==
                            QKeySequence("Alt+1") &&
                        window.edit_workspace_button_->toolTip().contains(
                            QStringLiteral("Alt+1")) &&
                        window.fusion_workspace_button_->toolTip().contains(
                            QStringLiteral("Alt+2")) &&
                        window.audio_workspace_button_->toolTip().contains(
                            QStringLiteral("Alt+4")) &&
                        window.render_workspace_button_->toolTip().contains(
                            QStringLiteral("Alt+3")),
                    "Workspace navigation shortcuts must be global, customizable, and shown in each selector tooltip.");
            sendShortcutKey(
                window.workspace_host_, Qt::Key_2, Qt::AltModifier);
            QApplication::processEvents();
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Fusion &&
                        window.fusion_workspace_button_->isChecked(),
                    "Alt+2 must switch to Fusion and select its footer button.");
            sendShortcutKey(
                window.workspace_host_, Qt::Key_4, Qt::AltModifier);
            QApplication::processEvents();
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Audio &&
                        window.audio_workspace_button_->isChecked() &&
                        window.edit_workspace_->ui().timeline->audioOnlyDisplayEnabled() &&
                        window.workspace_host_->inspectorPanel()->currentWidget() ==
                            window.audio_workspace_->mixerPanel() &&
                        window.audio_workspace_->meterPage()->isVisible(),
                    "Alt+4 must show the Audio workspace and audio-only Timeline.");
            sendShortcutKey(
                window.workspace_host_, Qt::Key_3, Qt::AltModifier);
            QApplication::processEvents();
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Render &&
                        window.render_workspace_button_->isChecked() &&
                        window.edit_workspace_->ui().timeline->isReadOnly() &&
                        window.edit_workspace_->ui().workspace_footer->isVisible(),
                    "Alt+3 must switch to Render while preserving its read-only Timeline and global footer.");
            sendShortcutKey(
                window.workspace_host_, Qt::Key_1, Qt::AltModifier);
            QApplication::processEvents();
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Edit &&
                        window.edit_workspace_button_->isChecked() &&
                        !window.edit_workspace_->ui().timeline->isReadOnly() &&
                        !window.edit_workspace_->ui().timeline->audioOnlyDisplayEnabled(),
                    "Alt+1 must return from Render to editable Edit.");

            QString shortcut_conflict;
            require(!window.shortcut_manager_->setShortcut(
                        QStringLiteral("workspace.switch_edit"),
                        QKeySequence("Alt+2"), &shortcut_conflict) &&
                        shortcut_conflict.contains(
                            QStringLiteral("Switch to Fusion workspace")) &&
                        window.shortcut_manager_->setShortcut(
                            QStringLiteral("workspace.switch_fusion"),
                            QKeySequence("Alt+6")) &&
                        window.fusion_workspace_button_->toolTip().contains(
                            QStringLiteral("Alt+6")) &&
                        window.shortcut_manager_->load() &&
                        window.shortcut_manager_->shortcut(
                            QStringLiteral("workspace.switch_fusion")) ==
                            QKeySequence("Alt+6") &&
                        window.fusion_workspace_button_->toolTip().contains(
                            QStringLiteral("Alt+6")),
                    "Workspace shortcuts must reject duplicate assignments and update tooltips after customization.");
            sendShortcutKey(
                window.workspace_host_, Qt::Key_2, Qt::AltModifier);
            QApplication::processEvents();
            require(window.workspace_host_->currentPage() ==
                        ui::WorkspacePageId::Edit,
                    "Changing the Fusion shortcut must release its previous key sequence.");
            sendShortcutKey(
                window.workspace_host_, Qt::Key_6, Qt::AltModifier);
            QApplication::processEvents();
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Fusion &&
                        window.shortcut_manager_->setShortcut(
                            QStringLiteral("workspace.switch_fusion"), {}) &&
                        window.shortcut_manager_->shortcut(
                            QStringLiteral("workspace.switch_fusion")).isEmpty() &&
                        window.fusion_workspace_button_->toolTip() ==
                            QStringLiteral("Switch to the Fusion workspace"),
                    "Clearing the Fusion shortcut must remove its key binding and its tooltip suffix.");
            sendShortcutKey(
                window.workspace_host_, Qt::Key_1, Qt::AltModifier);
            QApplication::processEvents();
            sendShortcutKey(
                window.workspace_host_, Qt::Key_6, Qt::AltModifier);
            QApplication::processEvents();
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Edit &&
                        window.shortcut_manager_->resetShortcut(
                            QStringLiteral("workspace.switch_fusion")) &&
                        window.fusion_workspace_button_->toolTip().contains(
                            QStringLiteral("Alt+2")),
                    "A cleared Fusion shortcut must stay inactive and reset to Alt+2 with its tooltip restored.");
            sendShortcutKey(
                window.workspace_host_, Qt::Key_1, Qt::AltModifier);
            QApplication::processEvents();
            require(window.workspace_host_->currentPage() ==
                        ui::WorkspacePageId::Edit,
                    "The Edit shortcut must remain available after another page shortcut is reset.");

            QEventLoop open_loop;
            QTimer timeout;
            timeout.setSingleShot(true);
            bool open_succeeded = false;
            QObject::connect(&timeout, &QTimer::timeout, &open_loop, &QEventLoop::quit);
            require(window.openProjectPath(
                        project_path,
                        std::nullopt,
                        std::nullopt,
                        [&open_loop, &open_succeeded](bool succeeded) {
                            open_succeeded = succeeded;
                            open_loop.quit();
                        }),
                    "The MainWindow could not open the multi-track project.");
            require(window.project_load_pending_ && window.timeline_dock_ != nullptr &&
                        !window.timeline_dock_->isEnabled() &&
                        window.menuBar()->isEnabled() &&
                        window.project_load_progress_ != nullptr &&
                        window.project_load_progress_->windowModality() == Qt::NonModal &&
                        window.new_project_action_ != nullptr &&
                        !window.new_project_action_->isEnabled() &&
                        !editAction(window, "edit.split_clip")->isEnabled() &&
                        !editAction(window, "timeline.nudge_left")->isEnabled() &&
                        window.timeline_model_.trackCount() == 2 &&
                        !window.timeline_model_.hasClip(),
                    "Opening a project did not preserve the visible session while disabling editing.");
            const auto menu_is_available = [&window](const QString& title) {
                for (auto* action : window.menuBar()->actions()) {
                    auto visible_title = action->text();
                    visible_title.remove('&');
                    if (visible_title == title && action->menu() != nullptr) {
                        return action->isEnabled() && action->menu()->isEnabled();
                    }
                }
                return false;
            };
            require(menu_is_available(QStringLiteral("File")) &&
                        menu_is_available(QStringLiteral("Edit")) &&
                        menu_is_available(QStringLiteral("View")) &&
                        menu_is_available(QStringLiteral("Help")),
                    "The top-level menus were unavailable during project preparation.");
            timeout.start(30000);
            open_loop.exec();
            require(open_succeeded && !window.project_load_pending_,
                    "The background project open did not finish successfully.");
            require(editAction(window, "edit.split_clip")->isEnabled() &&
                        editAction(window, "timeline.nudge_left")->isEnabled(),
                    "Edit-only commands disabled during loading must return to their enabled state afterward.");
            const auto& inspector_ui = window.edit_workspace_->ui();
            const auto& first_track = window.editor_session_.timeline().tracks().front();
            require(!first_track.clips.empty(),
                    "The Inspector tab test requires the opened project to contain a video clip.");
            const auto selected_track_id = first_track.track_id;
            const auto selected_clip_id = first_track.clips.front().clip_id;
            window.edit_workspace_->controller()->handleTimelineClipSelectionChanged(
                selected_track_id, selected_clip_id);
            QApplication::processEvents();
            require(inspector_ui.inspector_tabs->currentIndex() == 2 &&
                        inspector_ui.clip_effects_controls->isEnabled() &&
                        inspector_ui.effect_selection_hint->isHidden() &&
                        inspector_ui.clip_audio_effects->isEnabled(),
                    "Selecting a compatible clip must enable Effects without changing the active tab.");
            const auto clip_effects_path = directory / "clip-audio-effects-baseline.csp";
            require(window.saveProjectTo(clip_effects_path, "save") &&
                        !window.project_dirty_,
                    "The clip audio effects Inspector test could not establish a clean baseline.");
            bool clip_audio_dialog_edited = false;
            QTimer::singleShot(0, &window, [&clip_audio_dialog_edited] {
                auto* dialog = QApplication::activeModalWidget();
                if (dialog == nullptr ||
                    dialog->objectName() != QStringLiteral("audioTrackEffectsDialog"))
                    return;
                auto* add_eq = dialog->findChild<QPushButton*>(
                    "audioEffectAddEqualizer");
                if (add_eq == nullptr) {
                    dialog->close();
                    return;
                }
                add_eq->click();
                auto* enabled = dialog->findChild<QCheckBox*>("audioEffectEnabled");
                auto* low_gain = dialog->findChild<QDoubleSpinBox*>("audioEqLowGain");
                auto* buttons = dialog->findChild<QDialogButtonBox*>();
                if (enabled == nullptr || low_gain == nullptr || buttons == nullptr) {
                    dialog->close();
                    return;
                }
                enabled->setChecked(true);
                low_gain->setValue(3.0);
                clip_audio_dialog_edited = true;
                buttons->button(QDialogButtonBox::Ok)->click();
            });
            inspector_ui.clip_audio_effects->click();
            auto clip_effect_location = window.timeline_model_.locateClip(
                selected_clip_id);
            require(clip_audio_dialog_edited && clip_effect_location.has_value() &&
                        window.project_dirty_ &&
                        window.timeline_model_.tracks()[clip_effect_location->track_index]
                            .clips[clip_effect_location->clip_index].audio_effects.size() == 1 &&
                        std::get<timeline::AudioEqualizerParameters>(
                            window.timeline_model_.tracks()[clip_effect_location->track_index]
                                .clips[clip_effect_location->clip_index]
                                .audio_effects.front().parameters).low_shelf_gain_db == 3.0,
                    "The Edit Audio Inspector did not apply its clip EQ through the project model.");
            require(window.edit_workspace_->controller()->undo().changed() &&
                        !window.project_dirty_ &&
                        window.timeline_model_.tracks()[clip_effect_location->track_index]
                            .clips[clip_effect_location->clip_index].audio_effects.empty() &&
                        window.edit_workspace_->controller()->redo().changed() &&
                        window.project_dirty_,
                    "Clip EQ changes in the Edit Inspector must participate in Undo/Redo and dirty state.");
            require(window.edit_workspace_->controller()->undo().changed() &&
                        !window.project_dirty_,
                    "Undoing the clip EQ test edit must restore the clean project state.");
            auto* position_x = inspector_ui.transform_spins[0];
            auto* position_x_text = position_x->findChild<QLineEdit*>();
            require(position_x_text != nullptr,
                    "The Position X Inspector field has no editable text input.");
            const auto initial_position_x = position_x->value();
            const auto position_x_step = position_x->singleStep();
            const auto send_position_x_key =
                [position_x_text](int key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
                QKeyEvent press(QEvent::KeyPress, key, modifiers);
                QApplication::sendEvent(position_x_text, &press);
                QKeyEvent release(QEvent::KeyRelease, key, modifiers);
                QApplication::sendEvent(position_x_text, &release);
            };
            inspector_ui.inspector_tabs->setCurrentIndex(0);
            QApplication::processEvents();
            position_x->setFocus();
            {
                const QSignalBlocker block_position_x(position_x);
                position_x_text->setText(
                    position_x->locale().toString(0.123, 'f', position_x->decimals()));
                send_position_x_key(Qt::Key_Return);
                require(std::abs(position_x->value() - 0.123) < 1e-9,
                        "Typing and committing a value must remain available in Edit Inspector fields.");
                position_x->setValue(initial_position_x);
                send_position_x_key(Qt::Key_Up);
                require(std::abs(position_x->value() -
                                 (initial_position_x + position_x_step)) < 1e-9,
                        "Up must increase the focused Edit Inspector field by its configured step.");
                send_position_x_key(Qt::Key_Down);
                require(std::abs(position_x->value() - initial_position_x) < 1e-9,
                        "Down must reverse one Edit Inspector step.");
                send_position_x_key(Qt::Key_Up, Qt::ShiftModifier);
                require(std::abs(position_x->value() - (initial_position_x + 0.001)) < 1e-9,
                        "Shift+Up must increase an Edit Inspector field by 0.001.");
                send_position_x_key(Qt::Key_Down, Qt::ShiftModifier);
                require(std::abs(position_x->value() - initial_position_x) < 1e-9,
                        "Shift+Down must reverse the fine Edit Inspector step.");
                send_position_x_key(Qt::Key_Up, Qt::AltModifier);
                require(std::abs(position_x->value() - (initial_position_x + 0.100)) < 1e-9,
                        "Alt+Up must increase an Edit Inspector field by 0.100.");
                send_position_x_key(Qt::Key_Down, Qt::AltModifier);
                require(std::abs(position_x->value() - initial_position_x) < 1e-9,
                        "Alt+Down must reverse the coarse Edit Inspector step.");
                send_position_x_key(
                    Qt::Key_Up, Qt::ShiftModifier | Qt::AltModifier);
                require(std::abs(position_x->value() -
                                 (initial_position_x + position_x_step)) < 1e-9,
                        "Shift+Alt+Up must retain the normal Edit Inspector step.");
                send_position_x_key(
                    Qt::Key_Down, Qt::ShiftModifier | Qt::AltModifier);
                require(std::abs(position_x->value() - initial_position_x) < 1e-9,
                        "Shift+Alt+Down must reverse the normal Edit Inspector step.");
                send_position_x_key(Qt::Key_Up, Qt::ControlModifier);
                require(std::abs(position_x->value() -
                                 (initial_position_x + 1.000)) < 1e-9,
                        "Ctrl+Up must increase an Edit Inspector field by 1.000.");
                send_position_x_key(Qt::Key_Down, Qt::ControlModifier);
                require(std::abs(position_x->value() - initial_position_x) < 1e-9,
                        "Ctrl+Down must reverse the coarse Edit Inspector step.");
                for (const auto modifiers : {
                         Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier),
                         Qt::KeyboardModifiers(Qt::ControlModifier | Qt::AltModifier)}) {
                    send_position_x_key(Qt::Key_Up, modifiers);
                    require(std::abs(position_x->value() -
                                     (initial_position_x + position_x_step)) < 1e-9,
                            "Ctrl combined with Shift or Alt must retain the normal Edit Inspector step.");
                    send_position_x_key(Qt::Key_Down, modifiers);
                    require(std::abs(position_x->value() - initial_position_x) < 1e-9,
                            "Combined Edit Inspector modifiers must reverse the normal step.");
                }
                position_x->setValue(position_x->maximum());
                send_position_x_key(Qt::Key_Up);
                require(position_x->value() == position_x->maximum(),
                        "Up must respect the Edit Inspector field's maximum.");
                position_x->setValue(position_x->minimum());
                send_position_x_key(Qt::Key_Down);
                require(position_x->value() == position_x->minimum(),
                        "Down must respect the Edit Inspector field's minimum.");
                position_x->setValue(initial_position_x);
            }
            auto* position_x_slider = inspector_ui.transform_sliders[0];
            auto* edit_controller = window.edit_workspace_->controller();
            const auto slider_value_for_position_x = [](double value) {
                return static_cast<int>(std::lround(
                    std::clamp((value + 10.0) / 20.0, 0.0, 1.0) * 1000.0));
            };
            const auto selected_position_x = [&window, selected_clip_id]() {
                const auto location = window.editor_session_.timeline().locateClip(
                    selected_clip_id);
                require(location.has_value(),
                        "The selected clip disappeared during a Transform edit.");
                return window.editor_session_.timeline().tracks()[location->track_index]
                    .clips[location->clip_index].transform.position_x;
            };
            int live_composition_refreshes = 0;
            const auto live_edit_connection = QObject::connect(
                edit_controller,
                &ui::EditWorkspaceController::timelineEditCommitted,
                &window,
                [&live_composition_refreshes](
                    const application::TimelineEditResult&,
                    bool, bool refresh_composition, const QString&) {
                    if (refresh_composition) ++live_composition_refreshes;
                });
            const auto send_position_x_event =
                [position_x_text](QEvent::Type type, int key, bool auto_repeat = false) {
                    QKeyEvent event(
                        type, key, Qt::NoModifier, QString(), auto_repeat, 1);
                    QApplication::sendEvent(position_x_text, &event);
                };
            inspector_ui.inspector_tabs->setCurrentIndex(0);
            QApplication::processEvents();
            position_x->setFocus();
            const auto live_initial_position_x = selected_position_x();
            send_position_x_event(QEvent::KeyPress, Qt::Key_Up);
            require(std::abs(position_x->value() -
                             (live_initial_position_x + position_x_step)) < 1e-9 &&
                        position_x_slider->value() ==
                            slider_value_for_position_x(position_x->value()) &&
                        std::abs(selected_position_x() - position_x->value()) < 1e-9 &&
                        live_composition_refreshes == 1,
                    "An arrow press must immediately update the Transform value, slider, and preview composition.");
            send_position_x_event(QEvent::KeyRelease, Qt::Key_Up);
            require(edit_controller->undo().changed() &&
                        std::abs(position_x->value() - live_initial_position_x) < 1e-9 &&
                        position_x_slider->value() ==
                            slider_value_for_position_x(live_initial_position_x),
                    "One arrow press must create one Undo entry and restore its value and slider.");
            require(edit_controller->redo().changed() &&
                        std::abs(position_x->value() -
                                 (live_initial_position_x + position_x_step)) < 1e-9,
                    "Redo must restore the live Transform value and synchronized slider.");
            require(edit_controller->undo().changed(),
                    "The live Transform redo could not be undone for the following checks.");

            const auto refreshes_before_repeat = live_composition_refreshes;
            send_position_x_event(QEvent::KeyPress, Qt::Key_Up);
            send_position_x_event(QEvent::KeyPress, Qt::Key_Up, true);
            require(std::abs(position_x->value() -
                             (live_initial_position_x + 2.0 * position_x_step)) < 1e-9 &&
                        position_x_slider->value() ==
                            slider_value_for_position_x(position_x->value()) &&
                        live_composition_refreshes == refreshes_before_repeat + 2,
                    "Held-arrow repeats must update the slider and preview on each repeat.");
            send_position_x_event(QEvent::KeyRelease, Qt::Key_Up);
            require(edit_controller->undo().changed() &&
                        std::abs(position_x->value() - live_initial_position_x) < 1e-9,
                    "A held-arrow sequence must group all repeats into one Undo entry.");

            send_position_x_event(QEvent::KeyPress, Qt::Key_Up);
            send_position_x_event(QEvent::KeyRelease, Qt::Key_Up);
            send_position_x_event(QEvent::KeyPress, Qt::Key_Up);
            send_position_x_event(QEvent::KeyRelease, Qt::Key_Up);
            require(edit_controller->undo().changed() &&
                        std::abs(position_x->value() -
                                 (live_initial_position_x + position_x_step)) < 1e-9 &&
                        position_x_slider->value() ==
                            slider_value_for_position_x(position_x->value()) &&
                        edit_controller->undo().changed() &&
                        std::abs(position_x->value() - live_initial_position_x) < 1e-9,
                    "Separate arrow taps must create separate Undo entries with synchronized sliders.");
            edit_controller->beginTransformEdit();
            edit_controller->applyTransformProperty(0, 0.303);
            edit_controller->finishTransformEdit();
            auto* position_x_label = inspector_ui.inspector_panel
                ->findChild<QLabel*>("transformPropertyLabel0");
            require(std::abs(selected_position_x() - 0.303) < 1e-9 &&
                        position_x_label != nullptr,
                    "The Transform reset test could not set up a non-default value and find its label.");
            sendLeftDoubleClick(position_x);
            require(std::abs(selected_position_x() - 0.303) < 1e-9 &&
                        std::abs(position_x->value() - 0.303) < 1e-9,
                    "Double-clicking the numeric field must not reset its value.");
            const auto refreshes_before_reset = live_composition_refreshes;
            sendLeftDoubleClick(position_x_label);
            require(std::abs(selected_position_x() - 0.5) < 1e-9 &&
                        std::abs(position_x->value() - 0.5) < 1e-9 &&
                        position_x_slider->value() ==
                            slider_value_for_position_x(0.5) &&
                        live_composition_refreshes == refreshes_before_reset + 1,
                    "Double-clicking the Transform property name must reset the model, slider, and preview immediately.");
            require(edit_controller->undo().changed() &&
                        std::abs(selected_position_x() - 0.303) < 1e-9 &&
                        position_x_slider->value() ==
                            slider_value_for_position_x(0.303) &&
                        edit_controller->redo().changed() &&
                        std::abs(selected_position_x() - 0.5) < 1e-9 &&
                        edit_controller->undo().changed() &&
                        edit_controller->undo().changed() &&
                        std::abs(selected_position_x() - live_initial_position_x) < 1e-9,
                    "A property-name reset must be one undoable edit with working Undo/Redo.");
            QObject::disconnect(live_edit_connection);
            inspector_ui.inspector_tabs->setCurrentIndex(2);
            QApplication::processEvents();

            auto* timeline_widget = window.edit_workspace_->ui().timeline;
            auto* timeline_viewport = window.edit_workspace_->ui().timeline_scroll->viewport();
            const auto clicked_track_index =
                window.editor_session_.timeline().locateTrack(selected_track_id);
            require(timeline_widget != nullptr && timeline_viewport != nullptr &&
                        clicked_track_index.has_value(),
                    "The Timeline track-menu integration could not locate its fixed header.");
            const auto volume_slider_left = workspace_ui.monitor_volume->mapTo(
                workspace_ui.timeline_controls, QPoint(0, 0)).x();
            const auto zoom_slider_right = workspace_ui.zoom_slider->mapTo(
                workspace_ui.timeline_controls,
                QPoint(workspace_ui.zoom_slider->width(), 0)).x();
            const auto volume_indicator_right =
                workspace_ui.monitor_volume_indicator->mapTo(
                    workspace_ui.timeline_controls,
                    QPoint(workspace_ui.monitor_volume_indicator->width(), 0)).x();
            require(volume_slider_left > zoom_slider_right &&
                        workspace_ui.timeline_controls->contentsRect().right() -
                                volume_indicator_right <= 16,
                    "The monitoring-volume control should sit after zoom at the right edge of the Timeline control row.");
            for (const auto* button : window.edit_workspace_->ui().timeline_controls
                     ->findChildren<QPushButton*>()) {
                require(button->text() != QStringLiteral("Add Video Track") &&
                            button->text() != QStringLiteral("Rename Track") &&
                            button->text() != QStringLiteral("Track Up") &&
                            button->text() != QStringLiteral("Track Down") &&
                            button->text() != QStringLiteral("Remove Track"),
                        "Track-management buttons should no longer appear in the Timeline control row.");
            }
            // A restored compact Timeline can intentionally clip entire rows.
            // Expand it before testing a visible track-header interaction.
            window.resizeDocks({window.timeline_dock_}, {360}, Qt::Vertical);
            window.edit_workspace_->ui().timeline_scroll->ensureVisible(0,
                static_cast<int>(std::lround(timeline_widget->trackBounds(*clicked_track_index).center().y())), 0, 0);
            QApplication::processEvents();
            const QPoint track_menu_viewport_position(
                32, timeline_widget->mapTo(timeline_viewport, QPoint(0,
                    static_cast<int>(std::lround(timeline_widget->trackBounds(
                        *clicked_track_index).center().y())))).y());
            const auto track_menu_widget_position = timeline_widget->mapFrom(
                timeline_viewport, track_menu_viewport_position);
            const auto track_menu_global_position = timeline_viewport->mapToGlobal(
                track_menu_viewport_position);
            const auto selection_before_track_menu = window.editor_session_.selection();
            const auto timeline_before_track_menu =
                window.editor_session_.timeline().snapshot();
            const auto undo_before_track_menu =
                window.timeline_command_service_.undoCount();
            const auto dirty_before_track_menu = window.project_dirty_;
            bool track_menu_labels_verified = false;
            QTimer::singleShot(0, [&]() {
                auto* menu = timeline_widget->findChild<QMenu*>(
                    QStringLiteral("timelineTrackContextMenu"));
                if (menu == nullptr) return;
                QStringList labels;
                for (const auto* action : menu->actions()) labels.push_back(action->text());
                track_menu_labels_verified = labels == QStringList{
                    QStringLiteral("Add Video Track"),
                    QStringLiteral("Rename Track"),
                    QStringLiteral("Track Up"),
                    QStringLiteral("Track Down"),
                    QStringLiteral("Remove Track")};
                menu->close();
            });
            QMouseEvent track_header_right_click(
                QEvent::MouseButtonPress,
                QPointF(track_menu_widget_position),
                QPointF(track_menu_widget_position),
                QPointF(track_menu_global_position),
                Qt::RightButton, Qt::RightButton, Qt::NoModifier,
                Qt::MouseEventNotSynthesized,
                QPointingDevice::primaryPointingDevice());
            QApplication::sendEvent(timeline_widget, &track_header_right_click);
            QContextMenuEvent duplicate_track_context(
                QContextMenuEvent::Mouse,
                track_menu_widget_position,
                track_menu_global_position);
            QApplication::sendEvent(timeline_widget, &duplicate_track_context);
            const auto& selection_after_track_menu = window.editor_session_.selection();
            require(track_menu_labels_verified, "The visible track header did not open the expected track menu.");
            require(track_menu_labels_verified &&
                        selection_after_track_menu.active_track_id ==
                            selection_before_track_menu.active_track_id &&
                        selection_after_track_menu.active_clip_id ==
                            selection_before_track_menu.active_clip_id &&
                        selection_after_track_menu.selected_source_path ==
                            selection_before_track_menu.selected_source_path &&
                        window.editor_session_.timeline().snapshot() ==
                            timeline_before_track_menu &&
                        window.timeline_command_service_.undoCount() ==
                            undo_before_track_menu &&
                        window.project_dirty_ == dirty_before_track_menu,
                    "Canceling the track menu changed its labels, selection, project, or history.");
            const auto fusion_target_location =
                window.editor_session_.timeline().locateClip(selected_clip_id);
            require(timeline_widget != nullptr && fusion_target_location.has_value(),
                    "The Fusion context-menu test could not locate the selected Timeline clip.");
            const auto fusion_context_position = timeline_widget->clipBounds(
                *fusion_target_location).center().toPoint();
            const auto playback_start_result = window.playback_controller_->activateClip(
                selected_clip_id, 0, true);
            require((playback_start_result == playback::PlaybackCommandResult::Applied ||
                     playback_start_result == playback::PlaybackCommandResult::Pending) &&
                        window.playback_controller_->isPlaying(),
                    "The Fusion routing test could not start playback before opening the workspace.");
            bool fusion_context_action_found = false;
            QTimer::singleShot(0, [timeline_widget, &fusion_context_action_found]() {
                auto* menu = timeline_widget->findChild<QMenu*>();
                if (menu == nullptr) return;
                for (auto* action : menu->actions()) {
                    if (action->text() == QStringLiteral("Open in Fusion")) {
                        fusion_context_action_found = true;
                        action->trigger();
                        break;
                    }
                }
                menu->close();
            });
            QMouseEvent open_fusion_mouse_event(
                QEvent::MouseButtonPress,
                QPointF(fusion_context_position),
                QPointF(fusion_context_position),
                QPointF(timeline_widget->mapToGlobal(fusion_context_position)),
                Qt::RightButton, Qt::RightButton, Qt::NoModifier,
                Qt::MouseEventNotSynthesized,
                QPointingDevice::primaryPointingDevice());
            QApplication::sendEvent(timeline_widget, &open_fusion_mouse_event);
            QApplication::processEvents();
            auto* fusion_canvas = window.fusion_workspace_->nodeEditorPanel()
                ->findChild<fusion::nodes::NodeCanvas*>("fusionNodeCanvas");
            require(fusion_context_action_found &&
                        window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Fusion &&
                        window.fusion_workspace_->isActive() &&
                        window.editor_session_.selection().active_clip_id ==
                            selected_clip_id && fusion_canvas != nullptr &&
                        window.editor_session_.playheadFrame() == 0 &&
                        window.playback_controller_ != nullptr &&
                        !window.playback_controller_->isPlaying() &&
                        fusion_canvas->isEnabled() && fusion_canvas->scene() != nullptr &&
                        fusion_canvas->scene()->items().size() >= 2,
                    "Timeline Open in Fusion did not open the selected clip's node graph.");
            const auto fusion_graph_snapshot = [&window, selected_clip_id]() {
                const auto location = window.editor_session_.timeline().locateClip(
                    selected_clip_id);
                require(location.has_value(),
                        "The Fusion reset test lost its selected clip.");
                const auto& clip = window.editor_session_.timeline()
                    .tracks()[location->track_index].clips[location->clip_index];
                return clip.node_graph.value_or(
                    fusion::nodes::makePassthroughGraph());
            };
            const auto graph_before_reset = fusion_graph_snapshot();
            require(window.fusion_workspace_->addCatalogNode(
                        QStringLiteral("fusion.transform")),
                    "The Main Window Fusion reset test could not add a Transform node.");
            const auto reset_node_id = window.fusion_workspace_->selectedNodeId();
            auto* fusion_scale = window.fusion_workspace_->inspectorPanel()
                ->findChild<QDoubleSpinBox*>("fusionTransformScale");
            auto* fusion_apply = window.fusion_workspace_->inspectorPanel()
                ->findChild<QPushButton*>("fusionApplyNodeSettingsButton");
            require(fusion_scale && fusion_apply,
                    "The new Fusion Transform node has no scale control or Apply button.");
            fusion_scale->setValue(2.0);
            fusion_apply->click();
            fusion_scale = window.fusion_workspace_->inspectorPanel()
                ->findChild<QDoubleSpinBox*>("fusionTransformScale");
            auto* fusion_scale_label = window.fusion_workspace_->inspectorPanel()
                ->findChild<QLabel*>("fusionTransformScaleLabel");
            auto* fusion_pending_y = window.fusion_workspace_->inspectorPanel()
                ->findChild<QDoubleSpinBox*>("fusionTransformY");
            require(fusion_scale && fusion_scale_label && fusion_pending_y,
                    "The Fusion Inspector did not rebuild its reset controls after Apply.");
            fusion_pending_y->setValue(2.25);
            sendLeftDoubleClick(fusion_scale_label);
            const auto reset_graph = fusion_graph_snapshot();
            const auto* reset_node = fusion::nodes::findNode(
                reset_graph, reset_node_id);
            require(reset_node && reset_node->transform.scale == 1.0 &&
                        reset_node->transform.position_y == 0.5 &&
                        fusion_pending_y->value() == 2.25,
                    "The Main Window must apply only the named Fusion reset while preserving other pending controls.");
            require(edit_controller->undo().changed() &&
                        fusion_graph_snapshot().nodes.back().transform.scale == 2.0 &&
                        edit_controller->redo().changed() &&
                        fusion_graph_snapshot().nodes.back().transform.scale == 1.0 &&
                        edit_controller->undo().changed() &&
                        edit_controller->undo().changed() &&
                        edit_controller->undo().changed() &&
                        fusion_graph_snapshot() == graph_before_reset,
                    "A Fusion property-name reset must be one Undo/Redo graph edit and leave the original graph after cleanup.");
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
            QApplication::processEvents();
            require(window.workspace_host_->currentPage() == ui::WorkspacePageId::Edit &&
                        !window.fusion_workspace_->isActive(),
                    "The integration test could not restore Edit or deactivate Fusion after routing.");
            require(!edit_controller->addEffectToSelectedClip(QStringLiteral("video.grayscale")),
                    "Edit must leave correction-node creation in Color.");
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Color);
            auto* color_type = window.color_workspace_->findChild<QComboBox*>("colorNewNodeType");
            auto* color_add = window.color_workspace_->findChild<QPushButton*>("colorAddNodeButton");
            require(color_type && color_add,
                    "Color did not expose correction-node creation controls.");
            color_type->setCurrentIndex(color_type->findData(QStringLiteral("video.grayscale")));
            color_add->click();
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
            QApplication::processEvents();
            auto* effect_item = inspector_ui.clip_effects_list->item(0);
            require(effect_item != nullptr &&
                        effect_item->flags().testFlag(Qt::ItemIsUserCheckable) &&
                        effect_item->checkState() == Qt::Checked,
                    "The Effects list must show a checked state control for each new filter.");
            effect_item->setCheckState(Qt::Unchecked);
            QApplication::processEvents();
            auto selected_clip_location = window.timeline_model_.locateClip(selected_clip_id);
            require(selected_clip_location.has_value() &&
                        !window.timeline_model_.tracks()[selected_clip_location->track_index]
                             .clips[selected_clip_location->clip_index].effects.front().enabled &&
                        inspector_ui.clip_effect_parameter_value->isEnabled(),
                    "Unchecking a filter must disable processing while keeping its parameters editable.");
            edit_controller->applySelectedClipEffectParameter(37.0);
            selected_clip_location = window.timeline_model_.locateClip(selected_clip_id);
            require(selected_clip_location.has_value() &&
                        !window.timeline_model_.tracks()[selected_clip_location->track_index]
                             .clips[selected_clip_location->clip_index].effects.front().enabled &&
                        creative_suite::effects::parameterValue(
                            window.timeline_model_.tracks()[selected_clip_location->track_index]
                                .clips[selected_clip_location->clip_index].effects.front(),
                            "amount") == 37.0,
                    "A disabled filter's parameters must remain editable without re-enabling it.");
            const auto* grayscale_definition =
                creative_suite::effects::findDefinition("video.grayscale");
            require(grayscale_definition != nullptr &&
                        !grayscale_definition->parameters.empty(),
                    "The grayscale effect has no default parameter for reset coverage.");
            const auto grayscale_default =
                grayscale_definition->parameters.front().default_value;
            sendLeftDoubleClick(inspector_ui.clip_effect_parameter_label);
            selected_clip_location = window.timeline_model_.locateClip(selected_clip_id);
            require(selected_clip_location.has_value() &&
                        std::abs(creative_suite::effects::parameterValue(
                            window.timeline_model_.tracks()[selected_clip_location->track_index]
                                .clips[selected_clip_location->clip_index].effects.front(),
                            "amount") - grayscale_default) < 1e-9 &&
                        inspector_ui.clip_effect_parameter_slider->value() ==
                            static_cast<int>(std::lround(grayscale_default)),
                    "Double-clicking an Edit effect parameter name must restore its declared default and slider immediately.");
            require(edit_controller->undo().changed() &&
                        std::abs(inspector_ui.clip_effect_parameter_value->value() - 37.0) < 1e-9 &&
                        edit_controller->redo().changed() &&
                        std::abs(inspector_ui.clip_effect_parameter_value->value() -
                                 grayscale_default) < 1e-9 &&
                        edit_controller->undo().changed(),
                    "An Edit effect parameter reset must be one undoable edit.");
            require(edit_controller->undo().changed() && edit_controller->undo().changed() &&
                        edit_controller->undo().changed(),
                    "Effect parameter, checkbox, and Color graph edits must each be undoable.");
            selected_clip_location = window.timeline_model_.locateClip(selected_clip_id);
            require(selected_clip_location.has_value() &&
                        window.timeline_model_.tracks()[selected_clip_location->track_index]
                            .clips[selected_clip_location->clip_index].effects.empty() &&
                        !window.project_dirty_,
                    "Undoing the Effects UI test must restore the clean project state.");
            require(!edit_controller->addEffectToSelectedClip(QStringLiteral("video.primary_grade")),
                    "Edit must leave correction-node creation in Color.");
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Color);
            auto* color_primary_type = window.color_workspace_->findChild<QComboBox*>("colorNewNodeType");
            require(color_primary_type, "Color correction type selector is missing.");
            color_primary_type->setCurrentIndex(
                color_primary_type->findData(QStringLiteral("video.primary_grade")));
            window.color_workspace_->findChild<QPushButton*>("colorAddNodeButton")->click();
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
            QApplication::processEvents();
            selected_clip_location = window.timeline_model_.locateClip(selected_clip_id);
            require(selected_clip_location &&
                        window.timeline_model_.tracks()[selected_clip_location->track_index]
                            .clips[selected_clip_location->clip_index].effects.size() == 1,
                    "The Color workspace did not create the node shared with Edit.");
            auto* parameter_selector = inspector_ui.clip_effect_parameter_selector;
            require(parameter_selector && parameter_selector->isVisible() &&
                    parameter_selector->count() == 26,
                    "Edit Inspector must expose every shared Primary Grade control.");
            parameter_selector->setCurrentIndex(parameter_selector->findData(QStringLiteral("red_gain")));
            edit_controller->applySelectedClipEffectParameter(142.0);
            selected_clip_location = window.timeline_model_.locateClip(selected_clip_id);
            require(selected_clip_location.has_value() &&
                    creative_suite::effects::parameterValue(
                        window.timeline_model_.tracks()[selected_clip_location->track_index]
                            .clips[selected_clip_location->clip_index].effects.front(), "red_gain") == 142.0 &&
                    edit_controller->undo().changed() && edit_controller->redo().changed(),
                    "Edit and Color must edit the same Primary Grade parameter with Undo/Redo.");
            require(edit_controller->undo().changed() && edit_controller->undo().changed() &&
                    !window.project_dirty_,
                    "Primary Grade cleanup must restore the saved project.");

            const std::array<QDockWidget*, 8> workspace_docks{
                window.bins_dock_, window.media_dock_, window.toolbox_dock_,
                window.favorites_dock_, window.effects_dock_,
                window.inspector_dock_, window.preview_dock_,
                window.timeline_dock_};
            const auto dock_visibility = [&workspace_docks]() {
                std::array<bool, 8> visibility{};
                for (std::size_t index = 0; index < workspace_docks.size(); ++index) {
                    visibility[index] = workspace_docks[index]->isVisible();
                }
                return visibility;
            };
            const auto require_dock_visibility =
                [&workspace_docks](const std::array<bool, 8>& expected,
                                   const char* message) {
                    for (std::size_t index = 0; index < workspace_docks.size(); ++index) {
                        require(workspace_docks[index]->isVisible() == expected[index],
                                message);
                    }
                };
            require(window.edit_workspace_button_ != nullptr &&
                        window.fusion_workspace_button_ != nullptr &&
                        window.render_workspace_button_ != nullptr &&
                        window.edit_workspace_button_->isChecked() &&
                        window.edit_workspace_ != nullptr &&
                        window.edit_workspace_->controller() != nullptr &&
                        &window.edit_workspace_->controller()->session() ==
                            &window.editor_session_ &&
                        window.edit_workspace_->previewWidget() ==
                            window.preview_widget_ &&
                        window.edit_workspace_->inspectorPanel() ==
                            window.workspace_host_->editInspectorPage() &&
                        window.edit_workspace_->timelinePanel() ==
                            window.workspace_host_->timelinePanel() &&
                        window.fusion_workspace_ != nullptr &&
                        window.workspace_host_->nodeEditorPanel() ==
                            window.fusion_workspace_->nodeEditorPanel() &&
                        window.workspace_host_->fusionInspectorPage() ==
                            window.fusion_workspace_->inspectorPanel() &&
                        window.preview_dock_ != nullptr &&
                        window.preview_dock_->objectName() ==
                            QStringLiteral("previewDock") &&
                        window.preview_dock_->widget() != nullptr &&
                        window.preview_dock_->widget()->objectName() ==
                            QStringLiteral("previewDockContents") &&
                        window.preview_dock_->widget()->findChild<QWidget*>(
                            "workspaceViewerTitle") ==
                            window.fusion_workspace_->viewerTitle() &&
                        window.render_workspace_ != nullptr &&
                        window.workspace_host_->renderPage() ==
                            window.render_workspace_->centralPage(),
                    "The MainWindow must start with Edit selected and expose all workspace selectors.");
            auto* file_menu = window.menuBar()->findChild<QMenu*>("fileMenu");
            auto* edit_menu = window.menuBar()->findChild<QMenu*>("editMenu");
            auto* view_menu = window.menuBar()->findChild<QMenu*>("viewMenu");
            auto* help_menu = window.menuBar()->findChild<QMenu*>("helpMenu");
            auto* settings_menu_action = window.menuBar()->findChild<QAction*>(
                "settingsMenuAction");
            auto* media_pool_view_menu =
                window.menuBar()->findChild<QMenu*>("mediaPoolViewMenu");
            auto* effects_view_menu =
                window.menuBar()->findChild<QMenu*>("effectsViewMenu");
            auto* preview_view_action =
                window.findChild<QAction*>("previewViewAction");
            require(file_menu != nullptr && edit_menu != nullptr &&
                        view_menu != nullptr && help_menu != nullptr &&
                        settings_menu_action != nullptr &&
                        media_pool_view_menu != nullptr &&
                        effects_view_menu != nullptr &&
                        preview_view_action != nullptr &&
                        preview_view_action->isCheckable() &&
                        preview_view_action->isChecked(),
                    "The MainWindow must expose its scoped menus for integration coverage.");
            preview_view_action->trigger();
            require(!window.preview_dock_->isVisible() &&
                        !preview_view_action->isChecked(),
                    "View > Preview must close the Preview dock.");
            preview_view_action->trigger();
            require(window.preview_dock_->isVisible() &&
                        preview_view_action->isChecked(),
                    "View > Preview must restore a closed Preview dock.");

            auto* fusion_fixture_menu =
                new QMenu(QStringLiteral("Fusion Fixture"), window.menuBar());
            fusion_fixture_menu->setObjectName(QStringLiteral("fusionFixtureMenu"));
            auto* fusion_fixture_action = fusion_fixture_menu->addAction(
                QStringLiteral("Fusion Fixture Command"));
            window.menuBar()->addMenu(fusion_fixture_menu);
            window.registerWorkspaceMenuAction(
                fusion_fixture_action, settings::ShortcutScope::Fusion);
            auto* render_fixture_menu =
                new QMenu(QStringLiteral("Render Fixture"), window.menuBar());
            render_fixture_menu->setObjectName(QStringLiteral("renderFixtureMenu"));
            auto* render_fixture_action = render_fixture_menu->addAction(
                QStringLiteral("Render Fixture Command"));
            window.menuBar()->addMenu(render_fixture_menu);
            window.registerWorkspaceMenuAction(
                render_fixture_action, settings::ShortcutScope::Render);
            const auto require_menu_scopes = [&window, file_menu, edit_menu,
                view_menu, help_menu, settings_menu_action,
                media_pool_view_menu, effects_view_menu, preview_view_action,
                fusion_fixture_menu, fusion_fixture_action,
                render_fixture_menu, render_fixture_action](
                    ui::WorkspacePageId page) {
                const bool undo_enabled = window.undo_action_->isEnabled();
                const bool redo_enabled = window.redo_action_->isEnabled();
                const bool delete_enabled = editAction(window, "edit.delete_clip")->isEnabled();
                const bool edit_or_fusion = page != ui::WorkspacePageId::Render;
                const bool edit_active = page == ui::WorkspacePageId::Edit;
                const bool fusion_active = page == ui::WorkspacePageId::Fusion;
                const bool render_active = page == ui::WorkspacePageId::Render;
                require(file_menu->menuAction()->isVisible() &&
                            view_menu->menuAction()->isVisible() &&
                            help_menu->menuAction()->isVisible() &&
                            settings_menu_action->isVisible() &&
                            window.new_project_action_->isVisible() &&
                            window.undo_action_->isVisible() == edit_or_fusion &&
                            window.redo_action_->isVisible() == edit_or_fusion &&
                            editAction(window, "edit.delete_clip")->isVisible() == edit_active &&
                            edit_menu->menuAction()->isVisible() == edit_or_fusion &&
                            media_pool_view_menu->menuAction()->isVisible() ==
                                edit_or_fusion &&
                            effects_view_menu->menuAction()->isVisible() ==
                                edit_or_fusion &&
                            preview_view_action->isVisible() == edit_or_fusion &&
                            window.media_pool_action_->isVisible() == edit_or_fusion &&
                            window.effects_action_->isVisible() ==
                                (edit_or_fusion || page == ui::WorkspacePageId::Color) &&
                            fusion_fixture_action->isVisible() == fusion_active &&
                            fusion_fixture_menu->menuAction()->isVisible() ==
                                fusion_active &&
                            render_fixture_action->isVisible() == render_active &&
                            render_fixture_menu->menuAction()->isVisible() ==
                                render_active,
                        "Menu and toolbar actions must follow their workspace scopes.");
                for (const auto* command_id : {
                         "edit.delete_clip", "edit.ripple_delete_clip",
                         "edit.split_clip", "edit.copy_attributes",
                         "edit.paste_attributes", "edit.add_video_track",
                         "edit.rename_track", "edit.move_track_up",
                         "edit.move_track_down", "edit.remove_track",
                         "edit.blade_tool", "edit.require_alt_to_move",
                         "edit.move_playhead_on_clip_selection"}) {
                    auto* action = editAction(window, command_id);
                    require(action != nullptr && action->isVisible() == edit_active,
                            "Every EditWorkspace command must be mounted with the Edit menu scope.");
                }
                bool found_separator = false;
                const bool separators_active = page == ui::WorkspacePageId::Edit;
                for (auto* action : edit_menu->actions()) {
                    if (!action->isSeparator()) continue;
                    found_separator = true;
                    require(action->isVisible() == separators_active,
                            "Edit menu separators must disappear when no following scoped command is visible.");
                }
                require(found_separator,
                        "The Edit menu integration fixture expected separators.");
                window.refreshWorkspaceMenuVisibility();
                require(window.undo_action_->isEnabled() == undo_enabled &&
                            window.redo_action_->isEnabled() == redo_enabled &&
                            editAction(window, "edit.delete_clip")->isEnabled() == delete_enabled,
                        "Refreshing command visibility must not change QAction enablement.");
            };
            window.refreshWorkspaceMenuVisibility();
            QApplication::processEvents();
            require_menu_scopes(ui::WorkspacePageId::Edit);
            const auto edit_dock_visibility = dock_visibility();
            window.editor_session_.setPlayheadFrame(11);
            const auto history_probe =
                window.edit_workspace_->controller()->addTrack(
                    "Workspace history probe");
            require(history_probe.changed() &&
                        window.edit_workspace_->controller()->undo().changed() &&
                        window.edit_workspace_->controller()->canRedo() &&
                        !window.project_dirty_,
                    "The workspace preservation check could not prepare a clean redo history state.");
            const auto selection_before_workspace_switch =
                window.editor_session_.selection();
            const auto can_undo_before_workspace_switch =
                window.edit_workspace_->controller()->canUndo();
            const auto can_redo_before_workspace_switch =
                window.edit_workspace_->controller()->canRedo();
            const auto playback_controller_before_workspace_switch =
                window.playback_controller_.get();
            const auto project_dirty_before_workspace_switch =
                window.project_dirty_;
            const auto require_workspace_state_unchanged = [&window,
                &selection_before_workspace_switch,
                can_undo_before_workspace_switch,
                can_redo_before_workspace_switch,
                playback_controller_before_workspace_switch,
                project_dirty_before_workspace_switch]() {
                const auto& selection_after = window.editor_session_.selection();
                require(
                    selection_after.active_track_id ==
                            selection_before_workspace_switch.active_track_id &&
                        selection_after.active_clip_id ==
                            selection_before_workspace_switch.active_clip_id &&
                        selection_after.selected_source_path ==
                            selection_before_workspace_switch.selected_source_path &&
                        selection_after.active_transition.has_value() ==
                            selection_before_workspace_switch.active_transition.has_value() &&
                        window.edit_workspace_->controller()->canUndo() ==
                            can_undo_before_workspace_switch &&
                        window.edit_workspace_->controller()->canRedo() ==
                            can_redo_before_workspace_switch &&
                        window.playback_controller_.get() ==
                            playback_controller_before_workspace_switch &&
                        window.playback_controller_ != nullptr &&
                        window.project_dirty_ == project_dirty_before_workspace_switch,
                    "Switching workspaces must preserve selection, history, and dirty state.");
            };
            require(window.color_workspace_button_ != nullptr,
                    "Color workspace navigation must remain available after prior Color entry.");
            require(window.shortcut_manager_->shortcut(QStringLiteral("workspace.switch_color")) ==
                    QKeySequence(QStringLiteral("Alt+5")),
                    "Color must preserve Alt+5 as its default workspace shortcut.");
            const auto visibility_before_color = dock_visibility();
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Color);
            QApplication::processEvents();
            require(window.color_workspace_ != nullptr && window.color_workspace_button_->isChecked() &&
                    window.workspace_host_->currentPage() == ui::WorkspacePageId::Color &&
                    window.color_workspace_->findChild<QListWidget*>("colorClipStrip") != nullptr &&
                    window.color_workspace_->findChild<QWidget*>("colorNodeCanvas") != nullptr &&
                    window.color_workspace_->findChild<QLabel*>("colorWaveform") != nullptr &&
                    window.preview_widget_->parentWidget()->objectName() == "colorViewerContainer",
                    "Color must mount the shared viewer, clip strip, nodes, and RGB waveform.");
            for (auto* dock : workspace_docks)
                require(!dock->isVisible(), "Color must use the central layout without Edit docks.");
            require_workspace_state_unchanged();
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
            QApplication::processEvents();
            require_dock_visibility(visibility_before_color,
                "Leaving Color must restore the previous dock arrangement.");
            require_workspace_state_unchanged();
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Fusion);
            require_workspace_state_unchanged();
            QApplication::processEvents();
            require_menu_scopes(ui::WorkspacePageId::Fusion);
            require(window.editor_session_.playheadFrame() == 0 &&
                        !window.playback_controller_->isPlaying(),
                    "Entering Fusion must pause playback and seek to the selected clip's start.");
            const auto fusion_playhead = window.editor_session_.playheadFrame();
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Render);
            require_workspace_state_unchanged();
            QApplication::processEvents();
            require_menu_scopes(ui::WorkspacePageId::Render);
            require(window.editor_session_.playheadFrame() == fusion_playhead &&
                        !window.playback_controller_->isPlaying(),
                    "Leaving Fusion for Render must preserve the paused playhead position.");
            QApplication::processEvents();
            require(window.render_workspace_button_->isChecked() &&
                        !window.edit_workspace_button_->isChecked() &&
                        !window.fusion_workspace_button_->isChecked(),
                    "The MainWindow must select only Render.");
            require(window.workspace_host_->renderPage()->isVisible() &&
                        window.preview_widget_->isVisible() &&
                        window.render_workspace_->previewWidget() ==
                            window.preview_widget_,
                    "Render must show its settings, shared Preview, and queue page.");
            auto* render_settings_panel = window.render_workspace_->settingsPanel();
            auto* render_output_path = render_settings_panel
                ->findChild<QLineEdit*>("renderOutputPath");
            auto* add_render_job = render_settings_panel
                ->findChild<QPushButton*>("renderAddToQueueButton");
            auto* start_render_queue = window.render_workspace_->centralPage()
                ->findChild<QPushButton*>("renderStartQueueButton");
            auto* cancel_render_queue = window.render_workspace_->centralPage()
                ->findChild<QPushButton*>("renderCancelQueueButton");
            auto* gpu_export = render_settings_panel->findChild<QCheckBox*>("renderGpuCompositionCheck");
            require(gpu_export && !gpu_export->isChecked() && !gpu_export->accessibleDescription().isEmpty(),
                "Render GPU option must default off and describe fallback accessibly.");
            gpu_export->setChecked(true);
            require(render_settings_panel != nullptr &&
                        window.render_settings_dock_->widget() == render_settings_panel &&
                        window.dockWidgetArea(window.render_settings_dock_) ==
                            Qt::LeftDockWidgetArea &&
                        window.render_settings_dock_->isVisible() &&
                        window.workspace_host_->renderPage()->findChild<QWidget*>(
                            "renderPreviewPanel") != nullptr &&
                        window.workspace_host_->renderPage()->findChild<QWidget*>(
                            "renderQueuePanel") != nullptr &&
                        render_output_path != nullptr && add_render_job != nullptr &&
                        start_render_queue != nullptr && cancel_render_queue != nullptr,
                    "Render must expose Settings in the left dock and Preview and Queue in the center.");
            render_output_path->setText(QString::fromStdString(
                (directory / "queued-render.mp4").string()));
            const auto project_before_queue_add = window.currentProjectDocument();
            require(add_render_job->isEnabled(),
                    "A valid Render configuration must be addable to the queue.");
            add_render_job->click();
            const auto* queued_render_job = window.render_workspace_->queueModel()->jobAt(0);
            require(window.render_workspace_->queueModel()->jobCount() == 1 &&
                        queued_render_job != nullptr &&
                        queued_render_job->project_snapshot == project_before_queue_add &&
                        queued_render_job->settings.gpu_composition_enabled &&
                        !window.project_dirty_,
                    "Preparing a queued Render job must snapshot the project without marking it dirty.");
            gpu_export->setChecked(false);
            require(queued_render_job->settings.gpu_composition_enabled && !window.project_dirty_ &&
                window.currentProjectDocument() == project_before_queue_add,
                "Changing the GPU export option mutated the prepared job or project.");
            auto* export_controller = window.render_workspace_->findChild<ui::RenderQueueController*>();
            auto* gpu_warning = render_settings_panel->findChild<QLabel*>("renderGpuWarning");
            require(export_controller && gpu_warning && gpu_warning->isHidden(), "GPU warning must start hidden.");
            export_controller->jobWarning(queued_render_job->id, "Export is continuing with CPU fallback.", "-37");
            require(!gpu_warning->isHidden() && gpu_warning->text().contains("CPU fallback") &&
                QApplication::activeModalWidget() == nullptr, "Export fallback must show a nonmodal warning.");

            const auto background_image_path = directory / "render-queue-background.png";
            QImage background_image(4, 4, QImage::Format_RGBA8888);
            background_image.fill(QColor(32, 96, 160, 255));
            require(writePngAtomically(background_image_path, background_image),
                    "The workspace lifecycle render image could not be created.");
            auto background_job = *queued_render_job;
            background_job.id = 0;
            background_job.display_name = QStringLiteral("Background queue lifecycle");
            background_job.settings.output_path = QString::fromStdString(
                (directory / "queued-background-render.mp4").string());
            background_job.settings.width = 320;
            background_job.settings.height = 180;
            background_job.settings.frame_rate = 30.0;
            background_job.settings.export_audio = false;
            background_job.settings.gpu_composition_enabled = false;
            background_job.project_snapshot = project::ProjectDocument{};
            background_job.project_snapshot.timeline_frame_rate =
                timeline::FrameRate{30, 1};
            project::ProjectTrack background_track;
            background_track.track_id = 301;
            background_track.name = "Background fixture";
            project::ProjectClip background_clip;
            background_clip.source_path = background_image_path;
            background_clip.duration_frames = 90;
            background_clip.kind = timeline::ClipKind::Image;
            background_track.clips.push_back(std::move(background_clip));
            background_job.project_snapshot.timeline_tracks.push_back(
                std::move(background_track));
            require(window.render_workspace_->queueModel()->addJob(
                        std::move(background_job)) != 0 &&
                        window.render_workspace_->queueModel()->jobCount() == 2,
                    "The Render workspace did not accept a second immutable queue snapshot.");

            bool render_queue_finished = false;
            QEventLoop render_queue_loop;
            QTimer render_queue_timeout;
            render_queue_timeout.setSingleShot(true);
            QTimer dismiss_render_dialogs;
            dismiss_render_dialogs.setInterval(10);
            QObject::connect(export_controller, &ui::RenderQueueController::queueFinished,
                &render_queue_loop, [&] {
                    render_queue_finished = true;
                    render_queue_loop.quit();
                });
            QObject::connect(&render_queue_timeout, &QTimer::timeout,
                &render_queue_loop, &QEventLoop::quit);
            QObject::connect(&dismiss_render_dialogs, &QTimer::timeout,
                &render_queue_loop, [] {
                    if (auto* dialog = qobject_cast<QMessageBox*>(
                            QApplication::activeModalWidget())) {
                        dialog->accept();
                    }
                });
            start_render_queue->click();
            require(window.render_workspace_->isQueueRunning() &&
                        cancel_render_queue->isEnabled() &&
                        window.render_workspace_->queueModel()->isLocked(),
                    "Starting a Render must transfer queue ownership to RenderWorkspace and enable its Cancel command.");
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Render &&
                        window.workspace_host_->lowerWorkspacePanel()->currentWidget() ==
                            window.workspace_host_->timelinePanel() &&
                        window.timeline_dock_->windowTitle() == "Timeline" &&
                        window.edit_workspace_->ui().timeline->isReadOnly() &&
                        window.edit_workspace_->ui().timeline_controls->isHidden() &&
                        window.edit_workspace_->ui().workspace_footer->isVisible(),
                    "Render must show the read-only Timeline without its controls while keeping the global footer visible.");
            require(window.render_workspace_button_->isVisible(),
                    "The Render selector must remain visible while Render is active.");
            for (std::size_t index = 0; index < workspace_docks.size(); ++index) {
                require(workspace_docks[index]->isVisible() ==
                            (workspace_docks[index] == window.timeline_dock_),
                        "Entering Render must keep only the Timeline dock visible.");
            }
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Fusion);
            require((window.render_workspace_->isQueueRunning() || render_queue_finished) &&
                        !cancel_render_queue->isVisible() &&
                        !window.edit_workspace_->ui().timeline->isReadOnly(),
                    "Leaving Render must keep its queued snapshot running or completed while restoring Timeline interaction; Cancel remains on the Render page.");
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
            require((window.render_workspace_->isQueueRunning() || render_queue_finished) &&
                        !cancel_render_queue->isVisible() &&
                        window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Edit &&
                        !window.edit_workspace_->ui().timeline->isReadOnly(),
                    "Switching from Render to Edit must keep the background queue running or completed and keep Cancel available only in Render.");
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Fusion);
            require((window.render_workspace_->isQueueRunning() || render_queue_finished) &&
                        !cancel_render_queue->isVisible() &&
                        window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Fusion,
                    "Switching from Render to Fusion must leave the background queue running or completed.");
            if (!render_queue_finished) {
                render_queue_timeout.start(30000);
                dismiss_render_dialogs.start();
                render_queue_loop.exec();
                dismiss_render_dialogs.stop();
            }
            const auto* finished_render_job =
                window.render_workspace_->queueModel()->jobAt(0);
            const auto* finished_background_job =
                window.render_workspace_->queueModel()->jobAt(1);
            require(render_queue_finished &&
                        !window.render_workspace_->isQueueRunning() &&
                        finished_render_job != nullptr &&
                        finished_render_job->status == rendering::RenderJobStatus::Completed &&
                        finished_background_job != nullptr &&
                        finished_background_job->status == rendering::RenderJobStatus::Completed &&
                        std::filesystem::is_regular_file(
                            directory / "queued-render.mp4") &&
                        std::filesystem::is_regular_file(
                            directory / "queued-background-render.mp4"),
                    "A Render queue started before switching workspaces must finish every captured snapshot without being canceled.");
            QApplication::processEvents();
            require_workspace_state_unchanged();
            require_menu_scopes(ui::WorkspacePageId::Fusion);
            require_dock_visibility(
                edit_dock_visibility,
                "Returning to Fusion must restore the dock visibility from before Render.");
            require(window.workspace_host_->currentPage() ==
                            ui::WorkspacePageId::Fusion &&
                        window.workspace_host_->previewWidget()->isVisible() &&
                        window.render_workspace_->previewWidget() == nullptr,
                    "Returning to Fusion must restore its Preview.");
            require(window.timeline_dock_->windowTitle() == "Node Editor",
                    "Returning to Fusion must restore the Node Editor title.");
            require(!window.edit_workspace_->ui().timeline->isReadOnly() &&
                        !window.edit_workspace_->ui().timeline_controls->isHidden() &&
                        window.edit_workspace_->ui().workspace_footer->isVisible(),
                    "Returning to Fusion must restore Timeline interaction and keep the global footer visible.");

            window.media_dock_->hide();
            window.effects_dock_->show();
            QApplication::processEvents();
            const auto mixed_dock_visibility = dock_visibility();
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Render);
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
            QApplication::processEvents();
            require_workspace_state_unchanged();
            require_menu_scopes(ui::WorkspacePageId::Edit);
            require_dock_visibility(
                mixed_dock_visibility,
                "Returning to Edit must restore mixed dock visibility from before Render.");

            window.timeline_dock_->hide();
            QApplication::processEvents();
            const auto hidden_timeline_visibility = dock_visibility();
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Render);
            QApplication::processEvents();
            require_menu_scopes(ui::WorkspacePageId::Render);
            require(window.timeline_dock_->isVisible(),
                    "Render must show the Timeline even when its prior workspace visibility was hidden.");
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
            QApplication::processEvents();
            require_menu_scopes(ui::WorkspacePageId::Edit);
            require_dock_visibility(
                hidden_timeline_visibility,
                "Leaving Render must restore the prior hidden state of the Timeline dock.");
            require(!window.edit_workspace_->ui().timeline->isReadOnly() &&
                        !window.edit_workspace_->ui().timeline_controls->isHidden() &&
                        window.edit_workspace_->ui().workspace_footer->isVisible() &&
                        !window.project_dirty_,
                    "Workspace changes must restore Timeline interaction and preserve the global footer without dirtying the project.");
            window.restoreDefaultLayout();
            setWorkspacePageImmediately(window, ui::WorkspacePageId::Edit);
            QApplication::processEvents();

            require(window.timeline_model_.trackCount() == 2,
                    "Opening the project did not preserve both timeline tracks.");
            require(window.timeline_model_.clipCount(0) == 1 &&
                        window.timeline_model_.clipCount(1) == 1,
                    "Opening the project did not preserve clips on both tracks.");
            require(window.timeline_model_.tracks()[0].track_id == 1 &&
                        window.timeline_model_.tracks()[1].track_id == 2 &&
                        window.timeline_model_.tracks()[0].clips[0].clip_id == 1 &&
                        window.timeline_model_.tracks()[1].clips[0].clip_id == 2,
                    "Opening the project did not preserve stable track and clip identifiers.");
            require(window.active_timeline_track_id_.has_value() &&
                        window.active_timeline_clip_id_.has_value() &&
                        *window.active_timeline_track_id_ == 1 &&
                        *window.active_timeline_clip_id_ == 1,
                    "Opening the project did not select the first clip by stable identity.");
            require(!window.project_dirty_,
                    "Opening a saved multi-track project incorrectly marked it dirty.");
            require(window.windowTitle() == QStringLiteral("Video Editor"),
                    "Opening a saved multi-track project incorrectly added the dirty marker.");

            const auto failed_open_path = directory / "corrupt.csp";
            {
                std::ofstream corrupt_file(failed_open_path, std::ios::binary);
                corrupt_file << "not a project document";
            }
            const auto preserved_timeline = window.editor_session_.timeline().snapshot();
            const auto preserved_project_path = window.project_controller_.projectPath();
            const auto preserved_media_count = window.editor_session_.mediaItems().size();
            const auto preserved_selection = window.editor_session_.selection();
            const auto preserved_playhead = window.editor_session_.playheadFrame();
            QEventLoop failed_open_loop;
            QTimer failed_open_timeout;
            failed_open_timeout.setSingleShot(true);
            QTimer dismiss_project_error;
            dismiss_project_error.setInterval(10);
            QObject::connect(&dismiss_project_error, &QTimer::timeout, []() {
                auto* modal = QApplication::activeModalWidget();
                auto* message = qobject_cast<QMessageBox*>(modal);
                if (message != nullptr &&
                    message->windowTitle() == QStringLiteral("Could not open project")) {
                    message->accept();
                }
            });
            bool failed_open_succeeded = true;
            QObject::connect(&failed_open_timeout, &QTimer::timeout,
                             &failed_open_loop, &QEventLoop::quit);
            require(window.openProjectPath(
                        failed_open_path,
                        std::nullopt,
                        std::nullopt,
                        [&failed_open_loop, &failed_open_succeeded](bool succeeded) {
                            failed_open_succeeded = succeeded;
                            failed_open_loop.quit();
                        }),
                    "The MainWindow did not start a corrupt-project open attempt.");
            dismiss_project_error.start();
            failed_open_timeout.start(30000);
            failed_open_loop.exec();
            dismiss_project_error.stop();
            require(!failed_open_succeeded && !window.project_load_pending_ &&
                        window.editor_session_.timeline().snapshot() == preserved_timeline &&
                        window.project_controller_.projectPath() == preserved_project_path &&
                        window.editor_session_.mediaItems().size() == preserved_media_count &&
                        window.editor_session_.selection().active_track_id ==
                            preserved_selection.active_track_id &&
                        window.editor_session_.selection().active_clip_id ==
                            preserved_selection.active_clip_id &&
                        window.editor_session_.playheadFrame() == preserved_playhead,
                    "A failed project open replaced or changed the current editor session.");

            const auto current = window.currentProjectDocument();
            require(current.canvas_width == loaded.canvas_width &&
                        current.canvas_height == loaded.canvas_height &&
                        current.timeline_zoom == loaded.timeline_zoom &&
                        current.timeline_video_row_height ==
                            loaded.timeline_video_row_height &&
                        current.timeline_audio_row_height ==
                            loaded.timeline_audio_row_height,
                    "The current MainWindow document has different canvas or view settings.");
            require(current.media == loaded.media,
                    "The current MainWindow document has different media entries.");
            require(current.timeline_tracks == loaded.timeline_tracks,
                    "The current MainWindow document has different timeline tracks.");
            require(current.bins == loaded.bins,
                    "The current MainWindow document has different media bins.");

            require(window.saveProjectTo(round_trip_path, "save_as"),
                    "The MainWindow could not save the multi-track project.");
            const auto round_tripped = project::load(round_trip_path);
            require(round_tripped == current,
                    "The MainWindow multi-track project did not round-trip through save.");
            require(round_tripped.timeline_tracks.size() == 2 &&
                        round_tripped.timeline_tracks[0].clips.size() == 1 &&
                        round_tripped.timeline_tracks[1].clips.size() == 1,
                    "The round-trip project lost a track or clip.");

            window.clearActiveTimelineSelection();
            require(window.playback_controller_ != nullptr,
                    "The MainWindow playback controller was unavailable.");
            bool activation_terminal = false;
            bool activation_committed = false;
            std::string activation_observed_events;
            std::string activation_outcome = "no terminal activation event";
            const auto activation_wait_started = std::chrono::steady_clock::now();
            window.playback_controller_->setEventHandler(
                [&](const playback::PlaybackControllerEvent& event) {
                    if (!activation_observed_events.empty()) activation_observed_events += ",";
                    activation_observed_events += std::to_string(event.index());
                    if (const auto* activation =
                            std::get_if<playback::PlaybackActivationEvent>(&event);
                        activation != nullptr && activation->clip_id == 1 &&
                        activation->phase != playback::PlaybackActivationPhase::Pending) {
                        activation_terminal = true;
                        activation_committed = activation->phase ==
                            playback::PlaybackActivationPhase::Committed;
                        activation_outcome = activation_committed
                            ? "committed"
                            : "discarded";
                    } else if (const auto* error =
                                   std::get_if<playback::PlaybackErrorEvent>(&event);
                               error != nullptr &&
                               error->activation_clip_id == std::optional<timeline::ClipId>{1}) {
                        activation_terminal = true;
                        activation_outcome =
                            "worker error: " + error->message.toStdString();
                    }
                    window.handlePlaybackEvent(event);
                });
            require(window.playback_controller_->activateClip(1, 0, false) ==
                            playback::PlaybackCommandResult::Pending,
                    "The playback controller did not accept a stable-identity activation.");
            const auto activation_deadline = activation_wait_started +
                std::chrono::seconds(30);
            while (!activation_terminal &&
                   std::chrono::steady_clock::now() < activation_deadline) {
                QApplication::processEvents(QEventLoop::AllEvents, 10);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            window.playback_controller_->setEventHandler(
                [&window](const playback::PlaybackControllerEvent& event) {
                    window.handlePlaybackEvent(event);
                });
            const auto activation_wait_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - activation_wait_started).count();
            if (!activation_terminal) {
                activation_outcome += " after " + std::to_string(activation_wait_ms) +
                    "; events=" + activation_observed_events;
            }
            require(window.playback_controller_ != nullptr &&
                        activation_terminal && activation_committed &&
                        !window.playback_activation_loading_,
                    "The controller activation did not commit; terminal result: " +
                        activation_outcome);
            require(window.active_timeline_track_id_ == 1 &&
                        window.active_timeline_clip_id_ == 1 &&
                        window.active_timeline_track_index_cache_ == 0 &&
                        window.active_timeline_clip_index_cache_ == 0 &&
                        window.editor_session_.selection().active_track_id == 1 &&
                        window.editor_session_.selection().active_clip_id == 1,
                    "A controller activation did not update the timeline selection projection.");
            require(window.media_list_ != nullptr &&
                        window.media_list_->currentRow() == 0,
                    "A controller activation did not update the media-browser selection projection.");
            require(window.timeline_command_service_.execute(
                        application::DeleteClipCommand{1}).changed(),
                    "The integration test could not prepare its cross-track move fixture.");
            window.timeline_command_service_.clearHistory();
            window.clearActiveTimelineSelection();

            window.setActiveTimelineSelection(timeline::ClipLocation{1, 0});
            window.edit_workspace_->controller()->moveClip(2, 1, 0);
            require(window.active_timeline_track_id_ == 1 &&
                        window.active_timeline_clip_id_ == 2 &&
                        window.active_timeline_track_index_cache_ == 0 &&
                        window.active_timeline_clip_index_cache_ == 0 &&
                        window.editor_session_.selection().active_track_id == 1 &&
                        window.editor_session_.selection().active_clip_id == 2 &&
                        window.playback_frame_index_ == window.editor_session_.playheadFrame(),
                    "A service-backed move did not update the MainWindow selection projection.");
            require(window.project_dirty_,
                    "A service-backed move did not update the project dirty state.");

            static_cast<void>(window.edit_workspace_->controller()->undo());
            require(window.active_timeline_track_id_ == 2 &&
                        window.active_timeline_clip_id_ == 2 &&
                        window.active_timeline_track_index_cache_ == 1 &&
                        window.timeline_model_.locateClip(2) == timeline::ClipLocation{1, 0},
                    "Undo did not restore the service-backed move and selection.");

            window.media_controller_.clear();
            window.populateMediaBrowser();
            require(window.startMediaImport({first_source}),
                    "The MainWindow did not start the background media import.");
            media::VideoMetadata selected_metadata;
            selected_metadata.source_path = second_source;
            selected_metadata.display_name = "Keep this selection";
            require(window.media_controller_.commitImported({
                        selected_metadata, {}, selected_metadata.display_name,
                        "Unsorted", true}).changed(),
                    "The integration test could not add its selection fixture.");
            const auto selection_after_import_start = window.selection_generation_;
            window.populateMediaBrowser(second_source);
            QEventLoop import_loop;
            QTimer import_timeout;
            import_timeout.setSingleShot(true);
            QObject::connect(&import_timeout, &QTimer::timeout,
                             &import_loop, &QEventLoop::quit);
            QTimer import_poll;
            QObject::connect(&import_poll, &QTimer::timeout, &import_loop, [&]() {
                if (!window.active_media_import_cancel_) import_loop.quit();
            });
            require(window.media_import_progress_ != nullptr,
                    "The MainWindow did not present media import progress.");
            import_timeout.start(30000);
            import_poll.start(10);
            import_loop.exec();
            require(!window.active_media_import_cancel_ &&
                        window.media_controller_.library().contains(first_source) &&
                        window.selection_generation_ > selection_after_import_start,
                    "The MainWindow did not apply the completed import to the session library.");
            require(window.selectedMediaIndex().has_value(),
                    "The media-browser selection was lost after the import completed.");
            require(window.media_items_[*window.selectedMediaIndex()].metadata.source_path ==
                        media::MediaLibrary::canonicalPath(second_source),
                    "A late import changed the selection made while it was running.");

            window.media_controller_.clear();
            window.populateMediaBrowser();
            QEventLoop stale_import_loop;
            QTimer stale_import_timeout;
            stale_import_timeout.setSingleShot(true);
            QObject::connect(&stale_import_timeout, &QTimer::timeout,
                             &stale_import_loop, &QEventLoop::quit);
            QTimer stale_import_poll;
            QObject::connect(&stale_import_poll, &QTimer::timeout,
                             &stale_import_loop, [&]() {
                if (!window.active_media_import_cancel_) stale_import_loop.quit();
            });
            require(window.startMediaImport({second_source}),
                    "The MainWindow did not start the stale-generation import.");
            ++window.project_generation_;
            stale_import_timeout.start(30000);
            stale_import_poll.start(10);
            stale_import_loop.exec();
            require(!window.active_media_import_cancel_ &&
                        !window.media_controller_.library().contains(second_source) &&
                        !window.selectedMediaIndex().has_value(),
                    "An import result from an earlier project generation changed the session.");

            const auto drop_bin = window.media_controller_.createBin("DropTarget");
            require(drop_bin.changed(),
                    "The external-drop integration test could not create its destination bin.");
            window.populateMediaBrowser();
            window.edit_workspace_->controller()->clearTimeline();
            window.timeline_command_service_.clearHistory();
            const auto video_track = std::find_if(
                window.timeline_model_.tracks().begin(),
                window.timeline_model_.tracks().end(),
                [](const timeline::TimelineTrack& track) {
                    return track.kind == timeline::TrackKind::Video;
                });
            require(video_track != window.timeline_model_.tracks().end(),
                    "The external-drop integration test has no video track.");
            const auto video_track_id = video_track->track_id;

            const auto waitForDropImport = [&window](const char* failure_message) {
                require(window.media_task_pool_.waitForDone(30000),
                        "Background media import work did not finish within 30 seconds.");
                QApplication::processEvents();
                require(!window.active_media_import_cancel_, failure_message);
            };
            const auto sendFileDrop = [](QWidget* viewport,
                                         const QPoint& position,
                                         QMimeData* mime_data) {
                QDragEnterEvent enter(
                    position, Qt::CopyAction, mime_data,
                    Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(viewport, &enter);
                QDragMoveEvent move(
                    position, Qt::CopyAction, mime_data,
                    Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(viewport, &move);
                QDropEvent drop(
                    QPointF(position), Qt::CopyAction, mime_data,
                    Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(viewport, &drop);
                return enter.isAccepted() && move.isAccepted() && drop.isAccepted();
            };
            const auto first_source_qt = QString::fromStdWString(first_source.wstring());
            const auto second_source_qt = QString::fromStdWString(second_source.wstring());
            QListWidgetItem* drop_target_item = nullptr;
            for (int row = 0; row < window.media_list_->count(); ++row) {
                auto* item = window.media_list_->item(row);
                if (item->data(media_browser_ui::kMediaItemTypeRole).toInt() ==
                        media_browser_ui::kMediaItemTypeBin &&
                    item->data(media_browser_ui::kMediaBinPathRole).toString() ==
                        QStringLiteral("DropTarget")) {
                    drop_target_item = item;
                    break;
                }
            }
            require(drop_target_item != nullptr,
                    "The external-drop integration test could not find its target bin in the list.");
            window.media_list_->scrollToItem(drop_target_item);
            QApplication::processEvents();
            const auto list_drop_position =
                window.media_list_->visualItemRect(drop_target_item).center();
            QMimeData browser_drop_mime;
            browser_drop_mime.setUrls({QUrl::fromLocalFile(first_source_qt)});
            require(sendFileDrop(
                        window.media_list_->viewport(),
                        list_drop_position,
                        &browser_drop_mime),
                    "The Media Browser viewport did not accept the operating-system drop.");
            waitForDropImport(
                "The Media Browser did not complete an operating-system file drop.");
            const auto first_media_index =
                window.media_controller_.library().indexForPath(first_source);
            require(first_media_index != window.media_controller_.library().size() &&
                        window.media_controller_.library().items()[first_media_index].bin_path ==
                            "DropTarget",
                    "A file dropped on the Media Browser did not use its target bin.");

            const auto fallback_source = directory /
                ("unassigned-" + first_source.filename().string());
            std::filesystem::copy_file(first_source, fallback_source);
            require(window.bin_tree_ != nullptr &&
                        window.bin_tree_->topLevelItemCount() > 0,
                    "The external-drop integration test could not find All Media.");
            window.bin_tree_->setCurrentItem(window.bin_tree_->topLevelItem(0));
            QApplication::processEvents();
            QListWidgetItem* media_drop_target = nullptr;
            for (int row = 0; row < window.media_list_->count(); ++row) {
                auto* item = window.media_list_->item(row);
                if (item->data(media_browser_ui::kMediaItemTypeRole).toInt() ==
                    media_browser_ui::kMediaItemTypeMedia) {
                    media_drop_target = item;
                    break;
                }
            }
            require(media_drop_target != nullptr,
                    "All Media did not show an item for the fallback drop test.");
            window.media_list_->scrollToItem(media_drop_target);
            QApplication::processEvents();
            const auto fallback_drop_position =
                window.media_list_->visualItemRect(media_drop_target).center();
            const auto fallback_source_qt =
                QString::fromStdWString(fallback_source.wstring());
            QMimeData fallback_drop_mime;
            fallback_drop_mime.setUrls({QUrl::fromLocalFile(fallback_source_qt)});
            int stale_bin_warning_count = 0;
            QTimer dismiss_stale_bin_warning;
            QObject::connect(&dismiss_stale_bin_warning, &QTimer::timeout, [&]() {
                auto* message = qobject_cast<QMessageBox*>(
                    QApplication::activeModalWidget());
                if (message != nullptr &&
                    message->windowTitle() ==
                        QStringLiteral("Some media could not be imported")) {
                    if (message->text().contains(
                            QStringLiteral("The destination bin no longer exists"))) {
                        ++stale_bin_warning_count;
                    }
                    message->accept();
                }
            });
            dismiss_stale_bin_warning.start(10);
            require(sendFileDrop(
                        window.media_list_->viewport(),
                        fallback_drop_position,
                        &fallback_drop_mime),
                    "The Media Browser did not accept a drop without a bin target.");
            waitForDropImport(
                "The Media Browser did not complete a drop without a bin target.");
            dismiss_stale_bin_warning.stop();
            const auto fallback_media_index =
                window.media_controller_.library().indexForPath(fallback_source);
            require(fallback_media_index != window.media_controller_.library().size() &&
                        window.media_controller_.library().items()[fallback_media_index]
                                .bin_path == "Unsorted" &&
                        stale_bin_warning_count == 0,
                    "A drop without a concrete bin must fall back to Unsorted without a stale-bin warning.");

            auto& edit_ui = window.edit_workspace_->ui();
            const auto track_location =
                window.timeline_model_.locateTrack(video_track_id);
            require(track_location.has_value() && edit_ui.timeline != nullptr &&
                        edit_ui.timeline_scroll != nullptr,
                    "The external-drop integration test could not find the production Timeline viewport.");
            const auto visible_video_track_bounds =
                edit_ui.timeline->trackBounds(*track_location).intersected(
                    edit_ui.timeline->trackGroupViewportRect(
                        timeline::TrackKind::Video));
            require(!visible_video_track_bounds.isEmpty(),
                    "The external Timeline drop fixture has no visible Video track area.");
            const auto timeline_local_position = QPoint(
                360,
                static_cast<int>(std::lround(
                    visible_video_track_bounds.center().y())));
            const auto expected_timeline_frame =
                edit_ui.timeline->frameAtContentX(timeline_local_position.x());
            require(expected_timeline_frame.has_value(),
                    "The Timeline integration drop point did not map to a frame.");
            edit_ui.timeline_scroll->ensureVisible(
                timeline_local_position.x(), timeline_local_position.y(), 8, 4);
            QApplication::processEvents();
            const auto timeline_drop_position = edit_ui.timeline->mapTo(
                edit_ui.timeline_scroll->viewport(),
                timeline_local_position);
            const auto timeline_drop_first_source = directory / "timeline-drop-first.png";
            const auto timeline_drop_second_source = directory / "timeline-drop-second.png";
            QImage timeline_drop_first_image(4, 4, QImage::Format_RGBA8888);
            timeline_drop_first_image.fill(QColor(72, 144, 216, 255));
            QImage timeline_drop_second_image(4, 4, QImage::Format_RGBA8888);
            timeline_drop_second_image.fill(QColor(144, 216, 72, 255));
            require(writePngAtomically(
                        timeline_drop_first_source, timeline_drop_first_image) &&
                        writePngAtomically(
                            timeline_drop_second_source, timeline_drop_second_image),
                    "The Timeline drop image fixtures could not be created.");
            const auto timeline_drop_first_source_qt =
                QString::fromStdWString(timeline_drop_first_source.wstring());
            const auto timeline_drop_second_source_qt =
                QString::fromStdWString(timeline_drop_second_source.wstring());
            QMimeData timeline_drop_mime;
            timeline_drop_mime.setUrls({
                QUrl::fromLocalFile(timeline_drop_first_source_qt),
                QUrl::fromLocalFile(timeline_drop_second_source_qt)});
            require(sendFileDrop(
                        edit_ui.timeline_scroll->viewport(),
                        timeline_drop_position,
                        &timeline_drop_mime),
                    "The Timeline scroll viewport did not route the operating-system drop.");
            waitForDropImport(
                "The Timeline did not complete an operating-system file drop.");
            const auto placed_track_index = window.timeline_model_.locateTrack(
                video_track_id);
            require(placed_track_index.has_value(),
                    "The Timeline drop target disappeared during import.");
            const auto& placed_clips = window.timeline_model_.tracks()[
                *placed_track_index].clips;
            require(placed_clips.size() == 2 &&
                        placed_clips[0].source_path ==
                            media::MediaLibrary::canonicalPath(timeline_drop_first_source) &&
                        placed_clips[1].source_path ==
                            media::MediaLibrary::canonicalPath(timeline_drop_second_source),
                    "External Timeline drop did not preserve image batch order on the Video track.");
            require(placed_clips[0].timeline_start_frame ==
                            *expected_timeline_frame,
                    "External Timeline drop did not preserve its insertion frame: expected " +
                        std::to_string(*expected_timeline_frame) + ", got " +
                        std::to_string(placed_clips[0].timeline_start_frame) + ".");
            require(placed_clips[1].timeline_start_frame ==
                        placed_clips[0].timeline_start_frame +
                            placed_clips[0].timeline_duration_frames,
                    "External Timeline batch clips were not placed sequentially.");
            require(window.timeline_command_service_.undoCount() == 1,
                    "External Timeline batch drop did not create one undo step.");
            static_cast<void>(window.edit_workspace_->controller()->undo());
            const auto emptied_track_index = window.timeline_model_.locateTrack(
                video_track_id);
            require(emptied_track_index.has_value() &&
                        window.timeline_model_.tracks()[*emptied_track_index].clips.empty() &&
                        window.media_controller_.library().contains(timeline_drop_first_source) &&
                        window.media_controller_.library().contains(timeline_drop_second_source),
                    "Undo of a dropped Timeline batch removed imported media or left partial clips.");

            std::vector<timeline::TrackId> video_track_ids;
            require(window.editor_session_.legacyTimelineForUi().addTrack(
                        "Empty-group audio fixture", timeline::TrackKind::Audio) ==
                        timeline::AddTrackResult::Added,
                    "The empty-group integration test could not prepare an Audio track.");
            window.updateTimelineState();
            for (const auto& track : window.timeline_model_.tracks()) {
                if (track.kind == timeline::TrackKind::Video) {
                    video_track_ids.push_back(track.track_id);
                }
            }
            for (const auto track_id : video_track_ids) {
                require(window.edit_workspace_->controller()->removeTrack(track_id).changed(),
                        "The empty-group integration test could not remove a Video track.");
            }
            window.timeline_command_service_.clearHistory();
            const auto empty_group_source = directory / "empty-group.png";
            QImage empty_group_image(4, 4, QImage::Format_RGBA8888);
            empty_group_image.fill(QColor(96, 168, 240, 255));
            require(writePngAtomically(empty_group_source, empty_group_image),
                    "The empty-group drop image fixture could not be created.");
            const auto empty_group_source_qt =
                QString::fromStdWString(empty_group_source.wstring());
            window.handleExternalTimelineFilesGroupDrop(
                {empty_group_source_qt}, timeline::TrackKind::Video, 0);
            waitForDropImport(
                "An external file dropped on an empty Video pane did not finish importing.");
            const auto created_video = std::find_if(
                window.timeline_model_.tracks().begin(),
                window.timeline_model_.tracks().end(),
                [&empty_group_source](const timeline::TimelineTrack& track) {
                    return track.kind == timeline::TrackKind::Video &&
                        std::any_of(
                            track.clips.begin(), track.clips.end(),
                            [&empty_group_source](const timeline::TimelineClip& clip) {
                                return media::MediaLibrary::canonicalPath(clip.source_path) ==
                                    media::MediaLibrary::canonicalPath(empty_group_source);
                            });
                });
            require(created_video != window.timeline_model_.tracks().end() &&
                        window.timeline_command_service_.undoCount() == 1,
                    "An external drop did not create the first Video track and clip in one operation.");
            const auto created_video_track_id = created_video->track_id;
            static_cast<void>(window.edit_workspace_->controller()->undo());
            require(!window.timeline_model_.locateTrack(created_video_track_id).has_value(),
                    "Undo did not remove the empty-group Video track with its dropped clip.");
            static_cast<void>(window.edit_workspace_->controller()->redo());
            const auto restored_video = window.timeline_model_.locateTrack(
                created_video_track_id);
            require(restored_video.has_value() &&
                        window.timeline_model_.tracks()[*restored_video].clips.size() == 1,
                    "Redo did not restore the empty-group Video track and clip together.");
        }

        const auto waveform_source = directory / "waveform-background.wav";
        createWaveformWav(waveform_source);
        {
            MainWindow waveform_window;
            waveform_window.show();
            QApplication::processEvents();
            media::VideoMetadata metadata;
            metadata.kind = media::MediaKind::Audio;
            metadata.source_path = waveform_source;
            metadata.display_name = "Waveform background";
            metadata.duration_seconds = 1.0;
            metadata.audio = media::AudioMetadata{
                "pcm_s16le", 8000, 2, 1.0};
            require(waveform_window.media_controller_.commitImported({
                        metadata, {}, metadata.display_name, "Unsorted", false}).changed(),
                    "The waveform integration test could not register its audio source.");
            const auto audio_track = std::find_if(
                waveform_window.timeline_model_.tracks().begin(),
                waveform_window.timeline_model_.tracks().end(),
                [](const timeline::TimelineTrack& track) {
                    return track.kind == timeline::TrackKind::Audio;
                });
            require(audio_track != waveform_window.timeline_model_.tracks().end(),
                    "A new project did not provide an Audio track for the waveform test.");
            const auto audio_track_id = audio_track->track_id;
            require(waveform_window.timeline_command_service_.execute(
                        application::AddMediaClipCommand{
                            waveform_source, audio_track_id, 0}).changed(),
                    "The waveform integration test could not add its clip.");

            const auto canonical_waveform_source = media::MediaLibrary::canonicalPath(
                waveform_source);
            const auto signature = media::audioWaveformSourceSignature(
                canonical_waveform_source);
            require(signature.has_value(),
                    "The waveform integration fixture has no file signature.");
            std::atomic_bool request_blocker_entered = false;
            std::atomic_bool release_request_blocker = false;
            waveform_window.media_task_pool_.start(QRunnable::create([&]() {
                request_blocker_entered.store(true, std::memory_order_release);
                while (!release_request_blocker.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            }));
            for (int attempt = 0; attempt < 2000 &&
                 !request_blocker_entered.load(std::memory_order_acquire); ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const bool request_worker_blocked =
                request_blocker_entered.load(std::memory_order_acquire);
            waveform_window.updateTimelineState();
            const bool waveform_request_queued =
                waveform_window.pending_audio_waveforms_.contains(
                    canonical_waveform_source);
            const auto dirty_before_waveform = waveform_window.project_dirty_;

            bool event_loop_responsive = false;
            QTimer::singleShot(0, [&event_loop_responsive, &release_request_blocker]() {
                event_loop_responsive = true;
                release_request_blocker.store(true, std::memory_order_release);
            });
            QEventLoop waveform_loop;
            QTimer waveform_poll;
            waveform_poll.setInterval(5);
            QObject::connect(&waveform_poll, &QTimer::timeout, &waveform_loop, [&]() {
                if (waveform_window.audio_waveform_cache_.find(
                        canonical_waveform_source, *signature)) {
                    waveform_loop.quit();
                }
            });
            QTimer waveform_timeout;
            waveform_timeout.setSingleShot(true);
            QObject::connect(&waveform_timeout, &QTimer::timeout, &waveform_loop, [&]() {
                release_request_blocker.store(true, std::memory_order_release);
                waveform_loop.quit();
            });
            waveform_poll.start();
            waveform_timeout.start(10000);
            waveform_loop.exec();
            release_request_blocker.store(true, std::memory_order_release);
            waveform_window.media_task_pool_.waitForDone();
            const auto cached_waveform = waveform_window.audio_waveform_cache_.find(
                canonical_waveform_source, *signature);
            require(request_worker_blocked && waveform_request_queued &&
                        event_loop_responsive && cached_waveform != nullptr,
                    "The waveform did not arrive asynchronously while the UI remained responsive.");
            require(waveform_window.project_dirty_ == dirty_before_waveform,
                    "Generating a waveform changed the project dirty state.");

            const auto dirty_before_waveform_mode = waveform_window.project_dirty_;
            bool stereo_mode_applied = false;
            bool mono_mode_applied = false;
            auto* waveform_timeline = waveform_window.editUi().timeline;
            QTimer::singleShot(0, [&]() {
                auto* settings_dialog = qobject_cast<settings::SettingsDialog*>(
                    QApplication::activeModalWidget());
                if (settings_dialog == nullptr) return;
                auto* mode = settings_dialog->findChild<QComboBox*>(
                    "audioWaveformDisplayModeComboBox");
                if (mode == nullptr || waveform_timeline == nullptr) {
                    settings_dialog->accept();
                    return;
                }
                mode->setCurrentIndex(1);
                stereo_mode_applied =
                    waveform_timeline->stereoWaveformDisplayEnabled();
                mode->setCurrentIndex(0);
                mono_mode_applied =
                    !waveform_timeline->stereoWaveformDisplayEnabled();
                settings_dialog->accept();
            });
            waveform_window.showSettingsDialog();
            require(stereo_mode_applied && mono_mode_applied &&
                        settings::audioWaveformDisplayMode() ==
                            settings::AudioWaveformDisplayMode::Mono &&
                        waveform_window.project_dirty_ == dirty_before_waveform_mode,
                    "Changing the waveform view mode did not apply live without dirtying the project.");

            std::atomic_bool blocker_entered = false;
            std::atomic_bool release_blocker = false;
            waveform_window.media_task_pool_.start(QRunnable::create([&]() {
                blocker_entered.store(true, std::memory_order_release);
                while (!release_blocker.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
            }));
            for (int attempt = 0; attempt < 2000 &&
                 !blocker_entered.load(std::memory_order_acquire); ++attempt) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const bool blocker_started =
                blocker_entered.load(std::memory_order_acquire);
            std::error_code original_time_error;
            const auto old_modified = std::filesystem::last_write_time(
                canonical_waveform_source, original_time_error);
            std::error_code modified_error;
            if (blocker_started && !original_time_error) {
                std::filesystem::last_write_time(
                    canonical_waveform_source,
                    old_modified + std::chrono::seconds(2),
                    modified_error);
            }
            const auto changed_signature = modified_error || original_time_error
                ? std::optional<media::AudioWaveformSourceSignature>{}
                : media::audioWaveformSourceSignature(canonical_waveform_source);
            if (changed_signature.has_value() && *changed_signature != *signature) {
                waveform_window.updateTimelineState();
            }
            const bool regeneration_queued =
                !waveform_window.pending_audio_waveforms_.empty();
            waveform_window.clearProjectState();
            release_blocker.store(true, std::memory_order_release);
            waveform_window.media_task_pool_.waitForDone();
            QApplication::processEvents();
            require(blocker_started,
                    "The waveform cancellation test could not occupy the media worker.");
            require(!modified_error && !original_time_error &&
                        changed_signature.has_value() &&
                        *changed_signature != *signature && regeneration_queued,
                    "The changed waveform source was not queued for regeneration.");
            require(waveform_window.pending_audio_waveforms_.empty() &&
                        !waveform_window.audio_waveform_cache_.find(
                            canonical_waveform_source, *changed_signature),
                    "Changing projects did not cancel an obsolete waveform request.");
        }

        verifyLinkedImageRefresh(directory);

        {
            MainWindow effects_window;
            media::VideoMetadata image_metadata;
            image_metadata.kind = media::MediaKind::Image;
            image_metadata.source_path = first_source;
            image_metadata.display_name = "Effect clipboard fixture";
            image_metadata.width = 2;
            image_metadata.height = 2;
            image_metadata.frame_rate = 30.0;
            image_metadata.duration_seconds = 5.0;
            image_metadata.frame_count = 150;
            media::VideoFrame image_frame;
            image_frame.width = 2;
            image_frame.height = 2;
            image_frame.stride = 8;
            image_frame.rgba_pixels = {
                255, 0, 0, 255, 0, 255, 0, 255,
                0, 0, 255, 255, 255, 255, 255, 255};
            require(effects_window.media_controller_.commitImported({
                        image_metadata, image_frame,
                        image_metadata.display_name, "Unsorted", false}).changed(),
                    "The effects clipboard integration test could not register an image.");

            const auto source_track_id =
                effects_window.timeline_model_.tracks().front().track_id;
            const auto source_add = effects_window.timeline_command_service_.execute(
                application::AddMediaClipCommand{
                    first_source, source_track_id, 0});
            require(source_add.changed(),
                    "The effects clipboard integration test could not add its source clip.");
            const auto source_clip_id = source_add.affected_clip_ids.front();
            const auto target_track_add =
                effects_window.timeline_command_service_.execute(
                    application::AddTrackCommand{"Effects Paste Target"});
            require(target_track_add.changed(),
                    "The effects clipboard integration test could not add a destination track.");
            const auto target_track_id = target_track_add.affected_track_ids.front();
            const auto target_add = effects_window.timeline_command_service_.execute(
                application::AddMediaClipCommand{
                    first_source, target_track_id, 0});
            require(target_add.changed(),
                    "The effects clipboard integration test could not add its destination clip.");
            const auto target_clip_id = target_add.affected_clip_ids.front();
            const auto mixed_text_target = effects_window.timeline_command_service_.execute(
                application::AddTextClipCommand{target_track_id, 150, 90, 30.0});
            require(mixed_text_target.changed(),
                    "The effects clipboard integration test could not add a mixed-type destination.");
            const auto mixed_text_clip_id = mixed_text_target.affected_clip_ids.front();

            auto source_effects = std::vector<creative_suite::effects::EffectInstance>{
                creative_suite::effects::makeDefaultInstance("video.grayscale"),
                creative_suite::effects::makeDefaultInstance("video.saturation")};
            require(creative_suite::effects::setParameterValue(
                        source_effects[0], "amount", 64.0) &&
                        creative_suite::effects::setParameterValue(
                            source_effects[1], "amount", 135.0),
                    "The effects clipboard integration test could not prepare its source stack.");
            source_effects[0].enabled = false;
            auto previous_effects = std::vector<creative_suite::effects::EffectInstance>{
                creative_suite::effects::makeDefaultInstance("video.brightness")};
            require(creative_suite::effects::setParameterValue(
                        previous_effects[0], "amount", 30.0),
                    "The effects clipboard integration test could not prepare its destination stack.");
            require(effects_window.timeline_command_service_.execute(
                        application::SetClipEffectsCommand{
                            source_clip_id, source_effects}).changed() &&
                        effects_window.timeline_command_service_.execute(
                            application::SetClipEffectsCommand{
                                target_clip_id, previous_effects}).changed(),
                    "The effects clipboard integration test could not set both effect stacks.");

            auto& selection = effects_window.editor_session_.selectionForUi();
            selection.active_track_id = source_track_id;
            selection.active_clip_id = source_clip_id;
            selection.active_transition.reset();
            effects_window.edit_workspace_->refreshCommandAvailability();
            effects_window.updatePlaybackControls();
            require(editAction(effects_window, "edit.copy_attributes")->isEnabled() &&
                        !editAction(effects_window, "edit.paste_attributes")->isEnabled() &&
                        editAction(effects_window, "edit.delete_clip")->isEnabled() &&
                        editAction(effects_window, "edit.ripple_delete_clip")->isEnabled(),
                    "Copy Attributes, Delete, and Ripple Delete must be enabled for a selected Timeline clip, while Paste remains disabled before copying.");
            const bool dirty_before_timeline_shortcut = effects_window.project_dirty_;
            effects_window.show();
            QApplication::processEvents();
            auto* timeline_widget = effects_window.edit_workspace_->ui().timeline;
            timeline_widget->setFocus();
            QApplication::processEvents();
            require(QApplication::focusWidget() == timeline_widget,
                    "The Timeline shortcut fixture could not focus the Timeline.");
            sendShortcutKey(
                timeline_widget, Qt::Key_C, Qt::ControlModifier);
            require(editAction(effects_window, "edit.paste_attributes")->isEnabled() &&
                        effects_window.project_dirty_ == dirty_before_timeline_shortcut,
                    "Ctrl+C with Timeline focus must copy clip attributes without dirtying the project.");
            sendShortcutKey(timeline_widget, Qt::Key_Delete, Qt::NoModifier);
            require(!effects_window.timeline_model_.locateClip(source_clip_id).has_value() &&
                        effects_window.edit_workspace_->controller()->undo().changed() &&
                        effects_window.timeline_model_.locateClip(source_clip_id).has_value(),
                    "Delete with Timeline focus must remove the clip and remain undoable.");
            selection.active_track_id = source_track_id;
            selection.active_clip_id = source_clip_id;
            selection.active_transition.reset();
            effects_window.edit_workspace_->refreshCommandAvailability();
            sendShortcutKey(timeline_widget, Qt::Key_Delete, Qt::ShiftModifier);
            require(!effects_window.timeline_model_.locateClip(source_clip_id).has_value() &&
                        effects_window.edit_workspace_->controller()->undo().changed() &&
                        effects_window.timeline_model_.locateClip(source_clip_id).has_value(),
                    "Shift+Delete with Timeline focus must ripple-delete and remain undoable.");

            QLineEdit text_field(&effects_window);
            text_field.setGeometry(16, 16, 240, 28);
            text_field.setText(QStringLiteral("normal text copy"));
            text_field.show();
            text_field.selectAll();
            text_field.setFocus();
            QApplication::processEvents();
            require(QApplication::focusWidget() == &text_field,
                    "The text-copy fixture could not focus its editable field.");
            QApplication::clipboard()->clear();
            sendShortcutKey(
                &text_field, Qt::Key_C, Qt::ControlModifier);
            require(QApplication::clipboard()->text() == QStringLiteral("normal text copy"),
                    "Ctrl+C must preserve normal text-field copy when a clip is selected.");

            text_field.setText(QStringLiteral("normal text delete"));
            text_field.selectAll();
            sendShortcutKey(&text_field, Qt::Key_Delete, Qt::NoModifier);
            require(text_field.text().isEmpty() &&
                        effects_window.timeline_model_.locateClip(source_clip_id).has_value(),
                    "Delete must edit focused text instead of deleting a Timeline clip.");

            text_field.setText(QStringLiteral("normal text cut"));
            text_field.selectAll();
            QApplication::clipboard()->clear();
            sendShortcutKey(
                &text_field, Qt::Key_Delete, Qt::ShiftModifier);
            require(text_field.text().isEmpty() &&
                        QApplication::clipboard()->text() == QStringLiteral("normal text cut") &&
                        effects_window.timeline_model_.locateClip(source_clip_id).has_value(),
                    "Shift+Delete must preserve normal text-field cut instead of Ripple Delete.");
            text_field.hide();
            effects_window.hide();
            QApplication::processEvents();
            require(QApplication::focusWidget() == nullptr,
                    "The attribute-copy fixture could not clear text focus before testing clip copy.");
            selection.active_track_id = source_track_id;
            selection.active_clip_id = source_clip_id;
            selection.active_transition.reset();
            effects_window.edit_workspace_->refreshCommandAvailability();
            const bool dirty_before_copy = effects_window.project_dirty_;
            editAction(effects_window, "edit.copy_attributes")->trigger();
            effects_window.show();
            QApplication::processEvents();
            require(effects_window.project_dirty_ == dirty_before_copy &&
                        editAction(effects_window, "edit.paste_attributes")->isEnabled(),
                    "Copy Attributes must fill the in-memory clipboard without dirtying the project.");

            selection.active_track_id = target_track_id;
            selection.active_clip_id = target_clip_id;
            effects_window.edit_workspace_->controller()->updateTimelineState();
            effects_window.edit_workspace_->ui().timeline->setSelectedClipIds(
                {target_clip_id, mixed_text_clip_id});
            effects_window.edit_workspace_->refreshCommandAvailability();
            const auto history_before_paste =
                effects_window.timeline_command_service_.undoCount();
            bool paste_dialog_verified = false;
            QTimer::singleShot(0, [&paste_dialog_verified]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (dialog == nullptr) return;
                auto* effects = dialog->findChild<QCheckBox*>(
                    QStringLiteral("pasteAttributesEffects"));
                auto* transform = dialog->findChild<QCheckBox*>(
                    QStringLiteral("pasteAttributesTransform"));
                auto* audio = dialog->findChild<QCheckBox*>(
                    QStringLiteral("pasteAttributesAudioGain"));
                auto* envelope = dialog->findChild<QCheckBox*>(
                    QStringLiteral("pasteAttributesAudioEnvelope"));
                auto* text = dialog->findChild<QCheckBox*>(
                    QStringLiteral("pasteAttributesText"));
                auto* buttons = dialog->findChild<QDialogButtonBox*>();
                paste_dialog_verified = effects != nullptr && effects->isEnabled() &&
                    effects->isChecked() &&
                    effects->text().contains(QStringLiteral("1/2 compatible")) &&
                    transform != nullptr && transform->isEnabled() &&
                    transform->isChecked() &&
                    transform->text().contains(QStringLiteral("2/2 compatible")) &&
                    text != nullptr && !text->isEnabled() && buttons != nullptr;
                if (transform != nullptr) transform->setChecked(false);
                if (audio != nullptr) audio->setChecked(false);
                if (envelope != nullptr) envelope->setChecked(false);
                if (auto* apply = buttons != nullptr
                        ? buttons->button(QDialogButtonBox::Apply) : nullptr) {
                    apply->click();
                }
            });
            editAction(effects_window, "edit.paste_attributes")->trigger();
            require(paste_dialog_verified,
                    "Paste Attributes must show compatible groups checked and incompatible groups disabled.");
            const auto target_location =
                effects_window.timeline_model_.locateClip(target_clip_id);
            require(target_location.has_value() &&
                        effects_window.timeline_model_.tracks()[target_location->track_index]
                                .clips[target_location->clip_index].effects == source_effects &&
                        effects_window.timeline_command_service_.undoCount() ==
                            history_before_paste + 1,
                    "Paste Attributes did not replace the selected group in one edit.");
            require(effects_window.edit_workspace_->controller()->undo().changed() &&
                        effects_window.timeline_model_.tracks()[target_location->track_index]
                                .clips[target_location->clip_index].effects == previous_effects &&
                        effects_window.edit_workspace_->controller()->redo().changed() &&
                        effects_window.timeline_model_.tracks()[target_location->track_index]
                                .clips[target_location->clip_index].effects == source_effects,
                    "Paste Attributes did not integrate with Undo/Redo.");
            require(effects_window.edit_workspace_->ui().timeline->selectedClipIds().size() == 2,
                    "Paste Attributes did not preserve the destination multi-selection.");
            const auto single_clip_edit =
                effects_window.edit_workspace_->controller()->execute(
                    application::SetClipEffectsCommand{target_clip_id, source_effects});
            require(single_clip_edit.status == application::EditStatus::NoChange &&
                        effects_window.edit_workspace_->ui().timeline->selectedClipIds() ==
                            std::vector<timeline::ClipId>{target_clip_id},
                    "Starting an individual Timeline edit did not reduce the selection to its primary clip.");

            selection.active_track_id.reset();
            selection.active_clip_id.reset();
            selection.selected_source_path.reset();
            selection.active_transition.reset();
            effects_window.edit_workspace_->controller()->updateTimelineState();
            effects_window.edit_workspace_->refreshCommandAvailability();
            bool no_target_dialog_verified = false;
            QTimer::singleShot(0, [&no_target_dialog_verified]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                if (dialog == nullptr) return;
                auto* effects = dialog->findChild<QCheckBox*>(
                    QStringLiteral("pasteAttributesEffects"));
                auto* buttons = dialog->findChild<QDialogButtonBox*>();
                auto* apply = buttons != nullptr
                    ? buttons->button(QDialogButtonBox::Apply) : nullptr;
                no_target_dialog_verified = effects != nullptr &&
                    !effects->isEnabled() && apply != nullptr && !apply->isEnabled();
                if (auto* cancel = buttons != nullptr
                        ? buttons->button(QDialogButtonBox::Cancel) : nullptr) {
                    cancel->click();
                }
            });
            editAction(effects_window, "edit.paste_attributes")->trigger();
            require(no_target_dialog_verified,
                    "Paste Attributes without a selected clip must explain the missing target and disable Apply.");
        }

        verifyRenderQueueCancellationOnClose(directory);

        {
            MainWindow adjustment_window;
            auto* controller = adjustment_window.edit_workspace_->controller();
            controller->setPlaybackPresentation(false, false, true);
            const auto track_id = adjustment_window.timeline_model_.tracks()[0].track_id;
            controller->handleTimelineEffectDrop("video.adjustment_clip", track_id, 0);
            require(adjustment_window.project_dirty_ &&
                    adjustment_window.timeline_model_.tracks()[0].clips[0].kind == timeline::ClipKind::Adjustment &&
                    adjustment_window.edit_workspace_->ui().clip_effects_controls->isEnabled(),
                    "Adjustment insertion must dirty the real document and enable its Inspector.");
            require(adjustment_window.saveProjectTo(directory / "adjustment-ui.csp", "save") &&
                    !adjustment_window.project_dirty_, "Adjustment save did not establish a clean document.");
            const auto adjustment_clip_id = adjustment_window.timeline_model_.tracks()[0].clips[0].clip_id;
            const auto adjustment_graph = color::fromEffectStack(std::vector{
                creative_suite::effects::makeDefaultInstance("video.grayscale")});
            require(controller->execute(application::SetClipColorGraphCommand{
                        adjustment_clip_id, adjustment_graph}).changed(),
                    "Color could not create a correction graph for the Adjustment Clip.");
            controller->selectClipEffect(0);
            controller->setSelectedClipEffectEnabled(0, false);
            require(adjustment_window.project_dirty_, "Adjustment effects must dirty the real document.");
            auto* duplicate = editAction(adjustment_window, "edit.duplicate_adjustment_clip");
            require(duplicate && duplicate->isEnabled(), "Adjustment duplication is unavailable in Edit.");
            duplicate->trigger();
            require(adjustment_window.timeline_model_.clipCount(0) == 2 &&
                    controller->undo().changed() && controller->undo().changed() &&
                    controller->undo().changed() &&
                    adjustment_window.timeline_model_.clipCount(0) == 1 && !adjustment_window.project_dirty_,
                    "Adjustment Undo must restore the saved document and dirty state.");
        }
        {
            MainWindow composition_window;
            auto* controller = composition_window.edit_workspace_->controller();
            controller->setPlaybackPresentation(false, false, true);
            const auto track_id = composition_window.timeline_model_.tracks()[0].track_id;
            controller->handleTimelineEffectDrop("video.fusion_composition", track_id, 0);
            require(composition_window.project_dirty_ &&
                    composition_window.timeline_model_.tracks()[0].clips[0].kind == timeline::ClipKind::FusionComposition &&
                    composition_window.edit_workspace_->ui().clip_effects_controls->isEnabled(),
                    "Fusion Composition insertion must dirty the real document and enable its Inspector.");
            require(composition_window.saveProjectTo(directory / "fusion-ui.csp", "save") &&
                    !composition_window.project_dirty_, "Fusion Composition save did not establish a clean document.");
            const auto composition_clip_id = composition_window.timeline_model_.tracks()[0].clips[0].clip_id;
            const auto composition_graph = color::fromEffectStack(std::vector{
                creative_suite::effects::makeDefaultInstance("video.grayscale")});
            require(controller->execute(application::SetClipColorGraphCommand{
                        composition_clip_id, composition_graph}).changed(),
                    "Color could not create a correction graph for the Fusion Composition clip.");
            controller->selectClipEffect(0);
            controller->setSelectedClipEffectEnabled(0, false);
            require(composition_window.project_dirty_, "Fusion Composition effects must dirty the real document.");
            auto* duplicate = editAction(composition_window, "edit.duplicate_fusion_composition");
            require(duplicate && duplicate->isEnabled(), "Fusion Composition duplication is unavailable in Edit.");
            duplicate->trigger();
            require(composition_window.timeline_model_.clipCount(0) == 2 &&
                    controller->undo().changed() && controller->undo().changed() &&
                    controller->undo().changed() &&
                    composition_window.timeline_model_.clipCount(0) == 1 && !composition_window.project_dirty_,
                    "Fusion Composition Undo must restore the saved document and dirty state.");
        }

        {
            MainWindow functions_window;
            auto* controller = functions_window.edit_workspace_->controller();
            controller->setPlaybackPresentation(false, false, true);
            const auto track = functions_window.timeline_model_.tracks()[0].track_id;
            controller->handleTimelineEffectDrop("video.fusion_composition", track, 0);
            require(functions_window.saveProjectTo(directory / "functions-ui.csp", "save") && !functions_window.project_dirty_,
                    "Functions test could not establish a clean composition baseline.");
            auto* palette = functions_window.function_palette_;
            const auto openQuery = [&](const QString& query) -> QDialog* {
                palette->toggle();
                auto* dialog = palette->dialog();
                require(dialog && dialog->isVisible(), "Functions did not open.");
                dialog->findChild<QLineEdit*>("functionsEffectSearch")->setText(query);
                return dialog;
            };
            const auto graphSize = [&] { return functions_window.timeline_model_.tracks()[0].clips[0].node_graph->nodes.size(); };
            setWorkspacePageImmediately(functions_window, ui::WorkspacePageId::Fusion);
            auto* dialog = openQuery("Grayscale");
            auto* add = dialog->findChild<QPushButton*>("functionsAddButton");
            require(add && add->isEnabled(), "Fusion filter creation should be available in Functions.");
            add->click();
            require(!palette->dialog() && graphSize() == 3 && functions_window.project_dirty_ &&
                    functions_window.timeline_model_.tracks()[0].clips[0].effects.empty() &&
                    functions_window.timeline_model_.tracks()[0].clips[0].node_graph->nodes.back().type == fusion::nodes::NodeType::Effect,
                    "Fusion Functions Add must create a graph node without applying an Edit clip effect.");
            require(controller->undo().changed() && graphSize() == 2 && !functions_window.project_dirty_,
                    "Functions node Undo must restore the clean saved graph.");
            require(controller->redo().changed() && graphSize() == 3 && functions_window.project_dirty_,
                    "Functions node Redo must restore the edited graph.");
            require(controller->undo().changed(), "Functions node reset failed.");
            dialog = openQuery("Text");
            sendShortcutKey(dialog->findChild<QLineEdit*>("functionsEffectSearch"), Qt::Key_Return, Qt::NoModifier);
            require(!palette->dialog() && graphSize() == 3 &&
                    functions_window.timeline_model_.tracks()[0].clips[0].node_graph->nodes.back().type == fusion::nodes::NodeType::Text,
                    "Fusion Functions Enter must create a Text node.");
            require(controller->undo().changed(), "Functions Text reset failed.");
            dialog = openQuery("Rectangle Mask");
            auto* list = dialog->findChild<QListWidget*>("functionsEffectList");
            list->itemDoubleClicked(list->currentItem());
            require(!palette->dialog() && graphSize() == 3 &&
                    functions_window.timeline_model_.tracks()[0].clips[0].node_graph->nodes.back().type == fusion::nodes::NodeType::RectangleMask &&
                    functions_window.fusion_workspace_->selectedNodeId() == functions_window.timeline_model_.tracks()[0].clips[0].node_graph->nodes.back().id,
                    "Fusion Functions double click must create and select a mask node.");
            require(controller->undo().changed(), "Functions mask reset failed.");
            dialog = openQuery("Background");
            list = dialog->findChild<QListWidget*>("functionsEffectList");
            std::unique_ptr<QMimeData> fusion_mime(list->model()->mimeData({list->indexFromItem(list->currentItem())}));
            require(fusion_mime && fusion_mime->data(ui::kEffectIdMimeType) == "video.background", "Functions generator MIME is incorrect.");
            auto* canvas = functions_window.fusion_workspace_->nodeEditorPanel()->findChild<fusion::nodes::NodeCanvas*>();
            const QPoint point(150, 200);
            require(QMetaObject::invokeMethod(palette, "onEffectDragStateChanged", Qt::DirectConnection,
                    Q_ARG(bool, true), Q_ARG(bool, false)), "Functions drag could not start.");
            QDragEnterEvent fusion_enter(point, Qt::CopyAction, fusion_mime.get(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas->viewport(), &fusion_enter);
            QDropEvent fusion_drop(QPointF(point), Qt::CopyAction, fusion_mime.get(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(canvas->viewport(), &fusion_drop);
            require(fusion_drop.isAccepted() && graphSize() == 3 &&
                    functions_window.timeline_model_.tracks()[0].clips[0].node_graph->nodes.back().background_color[3] == 0,
                    "Functions Background drop must create the transparent Fusion node.");
            require(QMetaObject::invokeMethod(palette, "onEffectDragStateChanged", Qt::DirectConnection,
                    Q_ARG(bool, false), Q_ARG(bool, true)) && !palette->dialog(), "Accepted Fusion drop must close Functions.");
            require(controller->undo().changed() && !functions_window.project_dirty_, "Functions Background reset failed.");
            setWorkspacePageImmediately(functions_window, ui::WorkspacePageId::Edit);
            dialog = openQuery(QString{});
            auto* edit_add_button = dialog->findChild<QPushButton*>("functionsAddButton");
            auto* edit_effect_list = dialog->findChild<QListWidget*>("functionsEffectList");
            auto* edit_search = dialog->findChild<QLineEdit*>("functionsEffectSearch");
            QString brightness_label;
            for (int row = 0; row < edit_effect_list->count(); ++row) {
                auto* item = edit_effect_list->item(row);
                if (item->data(Qt::UserRole).toString() == QStringLiteral("video.brightness")) {
                    brightness_label = item->text();
                    break;
                }
            }
            require(!brightness_label.isEmpty(),
                    "The Functions catalog has no localized Brightness entry.");
            edit_search->setText(brightness_label);
            require(!edit_add_button->isEnabled() && dialog->isVisible() &&
                    graphSize() == 2 &&
                    functions_window.timeline_model_.tracks()[0].clips[0].effects.empty(),
                    "Edit Functions must leave color graph structure to the Color workspace.");
            dialog->findChild<QPushButton*>("functionsCancelButton")->click();
            dialog = openQuery("Background");
            list = dialog->findChild<QListWidget*>("functionsEffectList");
            add = dialog->findChild<QPushButton*>("functionsAddButton");
            require(!add->isEnabled() && dialog->findChild<QLabel*>("functionsEntryHint")->text() == "Drag this item to the timeline.",
                    "Edit Functions generator must explain its drag-only behavior.");
            list->itemDoubleClicked(list->currentItem());
            sendShortcutKey(dialog->findChild<QLineEdit*>("functionsEffectSearch"), Qt::Key_Return, Qt::NoModifier);
            require(palette->dialog() && functions_window.timeline_model_.clipCount(0) == 1 && !functions_window.project_dirty_,
                    "Edit generator Add/Enter/double click must not create a clip or close Functions.");
            std::unique_ptr<QMimeData> edit_mime(list->model()->mimeData({list->indexFromItem(list->currentItem())}));
            auto* timeline = functions_window.edit_workspace_->ui().timeline;
            timeline->resize(1000, 350); timeline->setZoomFactor(3);
            require(functions_window.saveProjectTo(directory / "functions-ui.csp", "save") && !functions_window.project_dirty_,
                    "Functions drag test could not save its updated Timeline zoom baseline.");
            const QPointF position(timeline->contentXForFrame(200), timeline->trackBounds(0).center().y());
            require(QMetaObject::invokeMethod(palette, "onEffectDragStateChanged", Qt::DirectConnection,
                    Q_ARG(bool, true), Q_ARG(bool, false)), "Functions timeline drag could not start.");
            QDragEnterEvent enter(position.toPoint(), Qt::CopyAction, edit_mime.get(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(timeline, &enter);
            QDragMoveEvent move(position.toPoint(), Qt::CopyAction, edit_mime.get(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(timeline, &move);
            QDropEvent drop(position, Qt::CopyAction, edit_mime.get(), Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(timeline, &drop);
            require(drop.isAccepted() && functions_window.timeline_model_.clipCount(0) == 2 && functions_window.project_dirty_ &&
                    functions_window.timeline_model_.tracks()[0].clips[1].display_name == "Background" &&
                    functions_window.timeline_model_.tracks()[0].clips[1].node_graph->nodes[0].background_color[3] == 255,
                    "Functions Edit Background drag must create the opaque-white clip preset.");
            require(QMetaObject::invokeMethod(palette, "onEffectDragStateChanged", Qt::DirectConnection,
                    Q_ARG(bool, false), Q_ARG(bool, true)) && !palette->dialog(), "Accepted timeline drop must close Functions.");
            require(controller->undo().changed() && !functions_window.project_dirty_, "Functions generator Undo failed.");
        }

        {
            MainWindow catalog_window;
            auto* controller = catalog_window.edit_workspace_->controller();
            controller->setPlaybackPresentation(false, false, true);
            setWorkspacePageImmediately(catalog_window, ui::WorkspacePageId::Fusion);
            require(!catalog_window.effects_list_->isEnabled() && !catalog_window.effects_favorites_->isEnabled() &&
                    catalog_window.effects_list_->visibleEffectCount() == 13,
                    "Fusion must show its catalog with creation disabled without a clip.");
            setWorkspacePageImmediately(catalog_window, ui::WorkspacePageId::Edit);
            const auto track = catalog_window.timeline_model_.tracks()[0].track_id;
            controller->handleTimelineEffectDrop("video.fusion_composition", track, 0);
            require(catalog_window.saveProjectTo(directory / "catalog-ui.csp", "save") && !catalog_window.project_dirty_,
                    "Catalog test could not establish a clean composition baseline.");
            require(effects::setFavorite("fusion.rectangle_mask", true) &&
                    effects::setFavorite("video.adjustment_clip", true), "Catalog favorites setup failed.");
            catalog_window.effects_toolbox_->setCurrentRow(3); // Audio is Edit-only.
            setWorkspacePageImmediately(catalog_window, ui::WorkspacePageId::Fusion);
            require(catalog_window.effects_list_->isEnabled() && catalog_window.effects_favorites_->isEnabled() &&
                    catalog_window.effects_toolbox_->currentCategoryId() == "all" &&
                    catalog_window.effects_list_->categoryId() == "all",
                    "Workspace switching must enable creation and reset unavailable categories.");
            auto* entry = catalog_window.effects_list_->findItems("Rectangle Mask", Qt::MatchExactly).front();
            catalog_window.effects_list_->itemDoubleClicked(entry);
            const auto& clip = catalog_window.timeline_model_.tracks()[0].clips[0];
            require(catalog_window.project_dirty_ && clip.node_graph->nodes.size() == 3 &&
                    clip.node_graph->nodes.back().type == fusion::nodes::NodeType::RectangleMask &&
                    catalog_window.fusion_workspace_->selectedNodeId() == clip.node_graph->nodes.back().id,
                    "Effects double click must create a selected node and dirty the real document.");
            require(controller->undo().changed() && !catalog_window.project_dirty_ &&
                    catalog_window.timeline_model_.tracks()[0].clips[0].node_graph->nodes.size() == 2,
                    "Catalog node Undo must restore the saved graph and clean state.");
            require(controller->redo().changed() && catalog_window.project_dirty_ &&
                    catalog_window.timeline_model_.tracks()[0].clips[0].node_graph->nodes.size() == 3,
                    "Catalog node Redo must restore the graph.");
            const auto favorite = catalog_window.effects_favorites_->findItems("Rectangle Mask", Qt::MatchExactly);
            require(favorite.size() == 1 &&
                    catalog_window.effects_favorites_->findItems("Adjustment Clip", Qt::MatchExactly).isEmpty(),
                    "Fusion Favorites must hide incompatible saved items.");
            catalog_window.effects_favorites_->itemDoubleClicked(favorite.front());
            require(catalog_window.timeline_model_.tracks()[0].clips[0].node_graph->nodes.size() == 4,
                    "Favorite double click must create an independent node.");
            setWorkspacePageImmediately(catalog_window, ui::WorkspacePageId::Edit);
            catalog_window.effects_list_->itemDoubleClicked(entry);
            const auto adjustment_favorite =
                catalog_window.effects_favorites_->findItems("Adjustment Clip", Qt::MatchExactly);
            const bool rectangle_is_favorite = effects::favoriteIds().contains(
                "fusion.rectangle_mask");
            require(entry->isHidden() &&
                    catalog_window.timeline_model_.tracks()[0].clips[0].node_graph->nodes.size() == 4 &&
                    adjustment_favorite.size() == 1 && rectangle_is_favorite,
                    "Returning to Edit must hide Fusion actions and retain their favorites.");
            setWorkspacePageImmediately(catalog_window, ui::WorkspacePageId::Render);
            require(catalog_window.effects_list_->visibleEffectCount() == 0 &&
                    !catalog_window.effects_list_->isEnabled(), "Render must not offer catalog creation.");
            setWorkspacePageImmediately(catalog_window, ui::WorkspacePageId::Fusion);
            controller->handleTimelineClipSelectionCleared();
            require(!catalog_window.effects_list_->isEnabled() && !catalog_window.effects_favorites_->isEnabled() &&
                    !catalog_window.active_timeline_clip_id_.has_value() &&
                    !catalog_window.active_timeline_clip_index_cache_.has_value() &&
                    catalog_window.fusion_workspace_->selectedClipId() == 0,
                    "Clearing selection must disable Fusion catalog creation immediately.");
            require(effects::setFavorite("fusion.rectangle_mask", false) &&
                    effects::setFavorite("video.adjustment_clip", false), "Catalog favorites cleanup failed.");
        }

        {
            MainWindow background_window;
            auto* controller=background_window.edit_workspace_->controller();
            controller->setPlaybackPresentation(false,false,true);
            const auto track=background_window.timeline_model_.tracks()[0].track_id;
            controller->handleTimelineEffectDrop("video.background",track,0);
            require(background_window.project_dirty_ && background_window.timeline_model_.tracks()[0].clips[0].display_name=="Background",
                "Background insertion did not dirty the real document.");
            require(background_window.saveProjectTo(directory/"background-ui.csp","save") && !background_window.project_dirty_,
                "Background save did not establish a clean document.");
            QTimer::singleShot(0,[] {
                auto* dialog=qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
                require(dialog!=nullptr,"Background color dialog is unavailable.");
                dialog->setCurrentColor(QColor(90,120,160,128));dialog->accept();
            });
            background_window.edit_workspace_->ui().background_color->click();
            require(background_window.project_dirty_,"Background color did not dirty the document.");
            require(controller->undo().changed() && !background_window.project_dirty_,"Background color Undo did not restore the saved baseline.");
            QTimer::singleShot(0,[] {
                auto* dialog=qobject_cast<QColorDialog*>(QApplication::activeModalWidget());
                require(dialog!=nullptr,"Background color dialog is unavailable.");dialog->reject();
            });
            background_window.edit_workspace_->ui().background_color->click();
            require(!background_window.project_dirty_,"Cancelled Background color changed dirty state.");
        }

        std::error_code cleanup_error;
        std::filesystem::remove_all(directory, cleanup_error);
        if (cleanup_error) {
            throw std::runtime_error(
                "The MainWindow integration test could not clean up its temporary directory.");
        }
    }
};

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setQuitOnLastWindowClosed(false);
    if (argc == 2 && std::string(argv[1]) == "--color-workspace-only") {
        try {
            MainWindowIntegrationTest::runColorWorkspaceOnly();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string(argv[1]) == "--audio-workspace-only") {
        try {
            MainWindowIntegrationTest::runAudioWorkspaceOnly();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 2 && std::string(argv[1]) == "--linked-image-only") {
        try {
            MainWindowIntegrationTest::runLinkedImageOnly();
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc == 4 && std::string(argv[3]) == "--project-settings") {
        try {
            MainWindowIntegrationTest::runProjectSettingsOnly(argv[1], argv[2]);
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    if (argc != 3) {
        std::cerr << "Expected two media fixture paths.\n";
        return 1;
    }

    try {
        MainWindowIntegrationTest::run(argv[1], argv[2]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
