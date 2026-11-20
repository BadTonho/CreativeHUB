#include "workspaces/color/ui/color_workspace.h"
#include "workspaces/color/ui/color_node_canvas.h"
#include "workspaces/color/evaluation/color_graph_evaluator.h"
#include "ui/media_browser/media_drag_mime.h"
#include "ui/preview/preview_widget.h"

#include <QApplication>
#include <QColor>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QDockWidget>
#include <QListWidget>
#include <QMainWindow>
#include <QSpinBox>
#include <QStackedWidget>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMetaObject>
#include <QPushButton>
#include <QProgressDialog>
#include <QSlider>
#include <QThread>
#include <QTimer>
#include <QTest>

#include <creative_suite/effects/effects.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    try {
        {
            auto trigger_overlay_action = [](QWidget* overlay, const QPoint& position,
                                             const QString& action_text) {
                bool triggered = false;
                QTimer::singleShot(0, [&] {
                    for (auto* widget : QApplication::topLevelWidgets()) {
                        auto* menu = qobject_cast<QMenu*>(widget);
                        if (!menu || !menu->isVisible()) continue;
                        bool found_action = false;
                        for (auto* action : menu->actions()) {
                            if (action->text() != action_text) continue;
                            action->trigger();
                            triggered = true;
                            found_action = true;
                            return;
                        }
                        if (!found_action) QTest::keyClick(menu, Qt::Key_Escape);
                    }
                });
                QContextMenuEvent event(QContextMenuEvent::Mouse, position,
                    overlay->mapToGlobal(position));
                QApplication::sendEvent(overlay, &event);
                QTest::qWait(10);
                return triggered;
            };
            auto drag_overlay = [](QWidget* overlay, const QPoint& start,
                                   const QPoint& end) {
                const auto global_start = overlay->mapToGlobal(start);
                const auto global_end = overlay->mapToGlobal(end);
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(start),
                    QPointF(global_start), Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);
                QApplication::sendEvent(overlay, &press);
                QMouseEvent move(QEvent::MouseMove, QPointF(end),
                    QPointF(global_end), Qt::NoButton, Qt::LeftButton,
                    Qt::NoModifier);
                QApplication::sendEvent(overlay, &move);
                QMouseEvent release(QEvent::MouseButtonRelease, QPointF(end),
                    QPointF(global_end), Qt::LeftButton, Qt::NoButton,
                    Qt::NoModifier);
                QApplication::sendEvent(overlay, &release);
            };

            ui::ColorWorkspace window_workspace;
            window_workspace.resize(1000, 720);
            auto window_graph = color::fromEffectStack(std::vector{
                creative_suite::effects::makeDefaultInstance("video.brightness")});
            const auto window_node_id = color::effectNodesInOrder(window_graph).front();
            int window_edits = 0;
            window_workspace.setEffectsChangedHandler(
                [&](timeline::ClipId clip_id, color::Graph updated) {
                    require(clip_id == 7,
                        "Viewer Power Window edits must target the selected clip.");
                    window_graph = std::move(updated);
                    ++window_edits;
                });
            window_workspace.setClips({{7, QStringLiteral("Window target")}}, 7,
                window_graph);
            auto* window_viewer = new PreviewWidget(&window_workspace);
            auto window_frame = std::make_shared<media::VideoFrame>();
            window_frame->width = 64;
            window_frame->height = 64;
            window_frame->stride = 64 * 4;
            window_frame->rgba_pixels.resize(64 * 64 * 4, 255);
            window_viewer->setFrame(window_frame);
            window_workspace.setPreviewWidget(window_viewer);
            window_workspace.setActive(true);
            window_workspace.show();
            application.processEvents();
            {
                auto* host = window_workspace.findChild<QMainWindow*>("colorBottomDockHost");
                require(host && !host->isWindow() && host->isVisible(),
                        "Color bottom dock host must be embedded and visible, not a separate window.");
                for (const char* dock_name : {"colorMotionEffectsDock", "colorSizingDock", "colorKeyframesDock"}) {
                    auto* dock = window_workspace.findChild<QDockWidget*>(dock_name);
                    require(dock && dock->isVisible() && dock->width() > 40 && dock->height() > 20,
                            "Color child docks must be visible with a usable size.");
                }
            }

            auto* window_canvas = static_cast<ui::ColorNodeCanvas*>(
                window_workspace.findChild<QWidget*>("colorNodeCanvas"));
            auto* overlay = window_workspace.findChild<QWidget*>(
                "colorPowerWindowOverlay");
            require(window_canvas && overlay,
                "The Color Viewer must provide its Power Window overlay.");
            const auto* effect_node = color::findNode(window_graph, window_node_id);
            require(effect_node != nullptr,
                "The Power Window UI fixture must contain a correction node.");
            const auto node_point = window_canvas->mapFromScene(QPointF(
                effect_node->x + 80.0, effect_node->y + 22.0));
            QTest::mouseClick(window_canvas->viewport(), Qt::LeftButton,
                Qt::NoModifier, node_point);
            require(window_canvas->selectedNode() == window_node_id,
                "The Power Window overlay must follow the selected correction node.");

            const auto center = overlay->rect().center();
            const auto extent = std::max(12, std::min(overlay->width(),
                overlay->height()) / 8);
            require(trigger_overlay_action(overlay, center,
                        QStringLiteral("Add Rectangle Power Window")),
                "The Viewer context menu must offer rectangle Power Window creation.");
            require(overlay->cursor().shape() == Qt::CrossCursor,
                "Choosing a Power Window shape must arm Viewer drawing.");
            drag_overlay(overlay, center - QPoint(extent, extent),
                center + QPoint(extent, extent));
            require(overlay->cursor().shape() == Qt::ArrowCursor,
                "A Power Window drag must complete its drawing gesture.");
            application.processEvents();
            auto* window_list = window_workspace.findChild<QListWidget*>(
                "colorPowerWindowList");
            require(color::findNode(window_graph, window_node_id)->power_windows.size() == 1,
                "The Viewer gesture must update the selected node graph.");
            require(window_list && window_list->count() == 1,
                "Dragging in the Viewer must create a Power Window in the selected node.");

            const auto second_start = center + QPoint(extent * 3 / 2, extent * 3 / 2);
            require(trigger_overlay_action(overlay, second_start,
                        QStringLiteral("Add Ellipse Power Window")),
                "The Viewer context menu must offer ellipse Power Window creation.");
            drag_overlay(overlay, second_start, second_start + QPoint(extent, extent));
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            application.processEvents();
            window_list = window_workspace.findChild<QListWidget*>(
                "colorPowerWindowList");
            auto* combine_mode = window_workspace.findChild<QComboBox*>(
                "colorPowerWindowCombineMode");
            auto* softness = window_workspace.findChild<QDoubleSpinBox*>(
                "colorPowerWindowSoftness");
            auto* invert = window_workspace.findChild<QCheckBox*>(
                "colorPowerWindowInvert");
            require(window_list && window_list->count() == 2 && combine_mode &&
                        softness && invert,
                "The Inspector must list multiple windows and expose their settings.");
            require(window_list->currentRow() == 1,
                "Adding a Power Window must select the new window in the Inspector.");
            combine_mode->setCurrentIndex(combine_mode->findData(
                static_cast<int>(color::WindowCombineMode::Subtract)));
            softness->setValue(.05);
            invert->setChecked(true);
            require(QMetaObject::invokeMethod(softness, "editingFinished"),
                "Power Window softness must commit through the Inspector.");
            const auto* edited_node = color::findNode(window_graph, window_node_id);
            require(edited_node && edited_node->power_windows.size() == 2,
                "The Inspector edit must retain both Power Windows.");
            require(edited_node->power_windows[1].kind ==
                        creative_suite::composition::ShapeMaskKind::Ellipse,
                "The selected second Power Window must remain an ellipse.");
            require(edited_node->power_windows[1].combine_mode ==
                        color::WindowCombineMode::Subtract,
                "The Inspector must apply the selected window's ordered combine mode.");
            require(std::abs(edited_node->power_windows[1].parameters.soft_edge - .05) < 1e-9,
                "The Inspector must apply the selected window's softness.");
            require(edited_node->power_windows[1].parameters.invert && window_edits >= 3,
                "The Inspector must apply inversion and report its edit.");

            int track_direction = 0;
            color::PowerWindowId tracked_window = 0;
            quint64 track_request_id = 0;
            quint64 track_project_generation = 0;
            quint64 track_revision = 0;
            quint64 track_playback_generation = 0;
            qint64 track_timeline_frame = 0;
            color::Graph track_original_graph;
            std::shared_ptr<std::atomic_bool> track_cancel;
            window_workspace.setPowerWindowTrackRequestHandler(
                [&](quint64 request_id, timeline::ClipId clip_id, color::NodeId node_id,
                    color::PowerWindowId window_id, qint64 timeline_frame, int direction,
                    quint64 project_generation, quint64 revision,
                    quint64 playback_generation, color::Graph original_graph,
                    std::shared_ptr<std::atomic_bool> cancel) {
                    require(clip_id == 7 && node_id == window_node_id,
                        "Viewer tracking must target the selected clip and node.");
                    track_direction = direction;
                    tracked_window = window_id;
                    track_request_id = request_id;
                    track_project_generation = project_generation;
                    track_revision = revision;
                    track_playback_generation = playback_generation;
                    track_timeline_frame = timeline_frame;
                    track_original_graph = std::move(original_graph);
                    track_cancel = std::move(cancel);
                });
            const auto selected_window_center = second_start + QPoint(
                extent / 2, extent / 2);
            require(trigger_overlay_action(overlay, selected_window_center,
                        QStringLiteral("Track Forward")) && track_direction == 1 &&
                        tracked_window != 0 && track_cancel && !track_cancel->load(),
                "The selected Power Window context menu must request forward tracking.");
            auto* tracking_progress = window_workspace.findChild<QProgressDialog*>(
                "colorPowerWindowTrackingProgress");
            require(tracking_progress != nullptr,
                "Power Window tracking must present nonmodal progress.");
            QTest::keyClick(tracking_progress, Qt::Key_Escape);
            application.processEvents();
            require(track_cancel->load(),
                "Canceling Power Window progress must signal worker cancellation.");
            track_direction = 0;
            require(trigger_overlay_action(overlay, selected_window_center,
                        QStringLiteral("Track Backward")) && track_direction == -1,
                "The selected Power Window context menu must request backward tracking.");
            const auto graph_before_stale_result = window_graph;
            auto stale_graph = window_graph;
            require(color::setPowerWindowSettings(stale_graph, window_node_id,
                        tracked_window, .12, false, color::WindowCombineMode::Subtract),
                "The stale tracking fixture must differ from the current graph.");
            window_workspace.acceptPowerWindowTrack(track_request_id,
                track_project_generation, 7, window_node_id, tracked_window,
                track_timeline_frame + 1, track_revision, track_playback_generation, window_graph,
                stale_graph, 4, false, .8, false, {});
            require(window_graph == graph_before_stale_result,
                "An outdated Power Window tracking result must not replace the current grade.");

            require(trigger_overlay_action(overlay, selected_window_center,
                        QStringLiteral("Track Forward")) && track_direction == 1,
                "A new Power Window run must start after a stale result is discarded.");
            auto concurrent_graph = window_graph;
            require(color::setPowerWindowSettings(concurrent_graph, window_node_id,
                        tracked_window, .16, false, color::WindowCombineMode::Subtract),
                "The concurrent tracking fixture must edit the active grade.");
            window_graph = concurrent_graph;
            window_workspace.setClips({{7, QStringLiteral("Window target")}}, 7,
                concurrent_graph);
            require(track_cancel->load(),
                "A concurrent grade edit must cancel its active tracking request.");
            window_workspace.acceptPowerWindowTrack(track_request_id,
                track_project_generation, 7, window_node_id, tracked_window,
                track_timeline_frame, track_revision, track_playback_generation, track_original_graph,
                stale_graph, 5, false, .75, false, {});
            require(window_graph == concurrent_graph,
                "A tracking result must not overwrite a concurrent Color edit.");
            window_workspace.setActive(false);
        }

        {
            ui::ColorWorkspace drop_workspace;
            drop_workspace.resize(1100, 760);
            int drop_commits = 0;
            auto dropped_graph = color::makePassthroughGraph();
            drop_workspace.setEffectsChangedHandler(
                [&](timeline::ClipId id, color::Graph graph) {
                    require(id == 7,
                            "A dropped Color effect must target the selected clip.");
                    dropped_graph = std::move(graph);
                    ++drop_commits;
                });
            drop_workspace.setClips({{7, QStringLiteral("Drop target")}}, 7,
                dropped_graph);
            drop_workspace.setActive(true);
            drop_workspace.show();
            application.processEvents();
            auto* drop_canvas = static_cast<ui::ColorNodeCanvas*>(
                drop_workspace.findChild<QWidget*>("colorNodeCanvas"));
            require(drop_canvas != nullptr,
                    "Color must expose its correction node canvas for effect drops.");
            const QPoint drop_point(60, 45);
            const auto expected_position = drop_canvas->mapToScene(drop_point);
            QMimeData brightness_mime;
            brightness_mime.setData(ui::kEffectIdMimeType,
                QByteArrayLiteral("video.brightness"));
            QDragEnterEvent brightness_enter(drop_point, Qt::CopyAction,
                &brightness_mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(drop_canvas->viewport(), &brightness_enter);
            QDragMoveEvent brightness_move(drop_point, Qt::CopyAction,
                &brightness_mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(drop_canvas->viewport(), &brightness_move);
            QDropEvent brightness_drop(QPointF(drop_point), Qt::CopyAction,
                &brightness_mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(drop_canvas->viewport(), &brightness_drop);
            const auto dropped_id = drop_canvas->selectedNode();
            const auto* dropped_node = dropped_id
                ? color::findNode(dropped_graph, *dropped_id) : nullptr;
            require(brightness_enter.isAccepted() && brightness_move.isAccepted() &&
                        brightness_drop.isAccepted() && drop_commits == 1 &&
                        dropped_id == 3 && dropped_node && dropped_node->effect &&
                        dropped_node->effect->id == "video.brightness" &&
                        std::abs(dropped_node->x - expected_position.x()) < 0.01 &&
                        std::abs(dropped_node->y - expected_position.y()) < 0.01 &&
                        dropped_graph.connections ==
                            color::makePassthroughGraph().connections &&
                        color::validate(dropped_graph),
                    "Dropping an effect must create and select an unconnected node at the drop point.");

            const auto before_unsupported_drop = dropped_graph;
            QMimeData unsupported_mime;
            unsupported_mime.setData(ui::kEffectIdMimeType,
                QByteArrayLiteral("video.gaussian_blur"));
            QDropEvent unsupported_drop(QPointF(drop_point), Qt::CopyAction,
                &unsupported_mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(drop_canvas->viewport(), &unsupported_drop);
            require(!unsupported_drop.isAccepted() && drop_commits == 1 &&
                        dropped_graph == before_unsupported_drop,
                    "Color must reject catalog entries unavailable to its graph.");

            ui::ColorWorkspace no_target_workspace;
            int no_target_commits = 0;
            no_target_workspace.setEffectsChangedHandler(
                [&](timeline::ClipId, color::Graph) { ++no_target_commits; });
            no_target_workspace.setClips({}, 0, color::makePassthroughGraph());
            no_target_workspace.setActive(true);
            no_target_workspace.resize(1100, 760);
            no_target_workspace.show();
            application.processEvents();
            auto* no_target_canvas = static_cast<ui::ColorNodeCanvas*>(
                no_target_workspace.findChild<QWidget*>("colorNodeCanvas"));
            require(no_target_canvas != nullptr,
                    "Color without a selected clip must retain its canvas.");
            QDropEvent no_target_drop(QPointF(drop_point), Qt::CopyAction,
                &brightness_mime, Qt::LeftButton, Qt::NoModifier);
            QDragEnterEvent no_target_enter(drop_point, Qt::CopyAction,
                &brightness_mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(no_target_canvas->viewport(), &no_target_enter);
            QDragMoveEvent no_target_move(drop_point, Qt::CopyAction,
                &brightness_mime, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(no_target_canvas->viewport(), &no_target_move);
            QApplication::sendEvent(no_target_canvas->viewport(), &no_target_drop);
            require(no_target_enter.isAccepted() && no_target_move.isAccepted() &&
                        !no_target_drop.isAccepted() && no_target_commits == 0,
                    "Color must reject effect drops when no compatible clip is selected.");
        }

        ui::ColorWorkspace workspace;
        auto graph = color::makePassthroughGraph();
        int edits = 0;
        int grade_batches_started = 0;
        int grade_batches_finished = 0;
        workspace.setEffectsChangedHandler([&](timeline::ClipId id, color::Graph updated) {
            require(id == 7, "Color must edit the selected clip identity.");
            graph = std::move(updated);
            ++edits;
        });
        workspace.setGradeEditBatchHandlers([&] { ++grade_batches_started; },
            [&] { ++grade_batches_finished; });
        workspace.setClips({{7, QStringLiteral("Shot A")}}, 7, graph);
        auto* add = workspace.findChild<QPushButton*>("colorAddNodeButton");
        auto* add_branch = workspace.findChild<QPushButton*>("colorAddParallelBranchButton");
        auto* toggle = workspace.findChild<QPushButton*>("colorToggleNodeButton");
        auto* remove = workspace.findChild<QPushButton*>("colorRemoveNodeButton");
        auto* new_type = workspace.findChild<QComboBox*>("colorNewNodeType");
        auto* action_controls = workspace.findChild<QWidget*>("colorNodeActionControls");
        auto* nodes = static_cast<ui::ColorNodeCanvas*>(
            workspace.findChild<QWidget*>("colorNodeCanvas"));
        require(add && add_branch && toggle && remove && new_type && action_controls && nodes,
                "Color graph controls must exist.");
        require(action_controls->isHidden(),
                "Color graph action controls and effect selector must be visually hidden.");
        add->click();
        const auto selected_after_add = nodes->selectedNode();
        const auto* added_node = selected_after_add
            ? color::findNode(graph, *selected_after_add) : nullptr;
        require(color::validate(graph) && graph.nodes.size() == 3 && added_node &&
                added_node->effect && added_node->effect->id == "video.primary_grade" && edits == 1,
                "Adding a correction must create a Color effect node.");
        auto* red = workspace.findChild<QDoubleSpinBox*>("colorWheel_gain_r_value");
        require(red != nullptr, "Primary Grade must expose editable red gain in its wheel.");
        red->setValue(145);
        require(QMetaObject::invokeMethod(red, "editingFinished"),
                "Red gain edit must be committed.");
        const auto selected_after_parameter = nodes->selectedNode();
        const auto* edited_node = selected_after_parameter
            ? color::findNode(graph, *selected_after_parameter) : nullptr;
        require(edited_node && edited_node->effect &&
                    creative_suite::effects::parameterValue(*edited_node->effect, "red_gain") == 145,
                "Primary Grade parameters must update the selected graph node.");
        require(grade_batches_started == 1 && grade_batches_finished == 1,
                "An RGB channel edit must finish one shared edit batch.");
        require(!red->isHidden() && red->value() == 145,
                "Editing an RGB channel must keep the active wheel control visible.");
        auto* gain_disk = workspace.findChild<QWidget*>("colorWheel_gain_disk");
        require(gain_disk != nullptr, "The Primary Grade balance wheel must exist.");
        const QPointF wheel_start(78, 54);
        const QPointF wheel_end(74, 30);
        QMouseEvent press(QEvent::MouseButtonPress, wheel_start, Qt::LeftButton,
            Qt::LeftButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, wheel_end, Qt::NoButton,
            Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, wheel_end, Qt::LeftButton,
            Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(gain_disk, &press);
        QApplication::sendEvent(gain_disk, &move);
        QApplication::sendEvent(gain_disk, &release);
        require(grade_batches_started == 2 && grade_batches_finished == 2 && edits >= 3,
                "Dragging a color wheel must send continuous edits inside one batch.");
        auto* grade_mode = workspace.findChild<QComboBox*>("colorPrimaryGradeMode");
        require(grade_mode && grade_mode->count() == 3 &&
                    grade_mode->currentData().toString() == QStringLiteral("wheels") &&
                    !grade_mode->isHidden(),
                "Primary Grade must default to the local Wheels presentation.");
        const auto edits_before_mode_switch = edits;
        grade_mode->setCurrentIndex(grade_mode->findData(QStringLiteral("bars")));
        require(edits == edits_before_mode_switch,
                "Changing the Primary Grade presentation must not edit the project.");
        auto* gain_bar_green = workspace.findChild<QDoubleSpinBox*>(
            "colorBars_gain_g_value");
        auto* gain_bar_slider = workspace.findChild<QSlider*>("colorBars_gain_r_slider");
        require(gain_bar_green && gain_bar_slider && !gain_bar_green->isHidden(),
                "Bars must expose editable RGB fields and channel sliders.");
        gain_bar_green->setValue(125);
        require(QMetaObject::invokeMethod(gain_bar_green, "editingFinished"),
                "Bars channel edit must be committed.");
        const auto* bars_node = nodes->selectedNode()
            ? color::findNode(graph, *nodes->selectedNode()) : nullptr;
        require(bars_node && bars_node->effect &&
                    creative_suite::effects::parameterValue(*bars_node->effect,
                        "green_gain") == 125.0 &&
                    grade_batches_started == 3 && grade_batches_finished == 3,
                "Bars must edit the same Primary Grade parameters and shared history.");
        const auto batch_start = grade_batches_started;
        const auto batch_end = grade_batches_finished;
        const auto old_master = gain_bar_slider->value();
        require(QMetaObject::invokeMethod(gain_bar_slider, "sliderPressed"),
                "Bars drag must start a shared edit batch.");
        gain_bar_slider->setValue(old_master + 120);
        require(QMetaObject::invokeMethod(gain_bar_slider, "sliderReleased"),
                "Bars drag must finish a shared edit batch.");
        require(grade_batches_started == batch_start + 1 &&
                    grade_batches_finished == batch_end + 1,
                "A Bars drag must create one shared Undo/Redo edit batch.");
        const auto edits_before_log_mode = edits;
        grade_mode->setCurrentIndex(grade_mode->findData(QStringLiteral("log")));
        require(edits == edits_before_log_mode,
                "Changing to Log mode must not create a correction edit.");
        auto* log_shadow_red = workspace.findChild<QDoubleSpinBox*>(
            "colorWheel_log_shadows_r_value");
        auto* log_mid_green = workspace.findChild<QDoubleSpinBox*>(
            "colorWheel_log_midtones_g_value");
        auto* log_high_blue = workspace.findChild<QDoubleSpinBox*>(
            "colorWheel_log_highlights_b_value");
        require(log_shadow_red && log_mid_green && log_high_blue &&
                    !log_shadow_red->isHidden(),
                "Log must expose independent Shadows, Midtones and Highlights controls.");
        log_shadow_red->setValue(30);
        require(QMetaObject::invokeMethod(log_shadow_red, "editingFinished"),
                "Log Shadows edit must be committed.");
        log_mid_green->setValue(-20);
        require(QMetaObject::invokeMethod(log_mid_green, "editingFinished"),
                "Log Midtones edit must be committed.");
        log_high_blue->setValue(15);
        require(QMetaObject::invokeMethod(log_high_blue, "editingFinished"),
                "Log Highlights edit must be committed.");
        const auto* log_node = nodes->selectedNode()
            ? color::findNode(graph, *nodes->selectedNode()) : nullptr;
        require(log_node && log_node->effect &&
                    creative_suite::effects::parameterValue(*log_node->effect,
                        "log_shadows_red") == 30.0 &&
                    creative_suite::effects::parameterValue(*log_node->effect,
                        "log_midtones_green") == -20.0 &&
                    creative_suite::effects::parameterValue(*log_node->effect,
                        "log_highlights_blue") == 15.0 &&
                    edits > edits_before_mode_switch,
                "Log values must persist on the selected shared Primary Grade node.");
        add_branch->click();
        require(color::validate(graph) &&
                    std::count_if(graph.nodes.begin(), graph.nodes.end(), [](const auto& node) {
                        return node.type == color::NodeType::ParallelMixer;
                    }) == 1 && graph.nodes.size() == 5,
                "Adding a parallel correction must create and connect a mixer graph.");
        add->click();
        require(graph.nodes.size() == 6,
                "A correction inserted into a selected branch must add a graph node.");
        toggle->click();
        const auto selected_after_insert = nodes->selectedNode();
        const auto* toggled_node = selected_after_insert
            ? color::findNode(graph, *selected_after_insert) : nullptr;
        require(toggled_node && toggled_node->effect && !toggled_node->effect->enabled,
                "Node enable state must be editable.");
        remove->click();
        require(color::validate(graph) && graph.nodes.size() == 5,
                "Removing a correction must preserve a valid connected graph.");
        new_type->setCurrentIndex(new_type->findData(QStringLiteral("video.saturation")));
        add->click();
        require(std::any_of(graph.nodes.begin(), graph.nodes.end(), [](const auto& node) {
                    return node.effect && node.effect->id == "video.saturation";
                }), "Color must add the chosen effect type to its graph.");
        workspace.resize(1100, 900);
        workspace.show();
        application.processEvents();
        const auto input_graph_node = std::find_if(graph.nodes.begin(), graph.nodes.end(),
            [](const color::Node& node) { return node.type == color::NodeType::Input; });
        require(input_graph_node != graph.nodes.end(),
                "The Color graph input must exist before opening its menu.");
        nodes->setGraph(graph, input_graph_node->id);
        const auto node_position = nodes->mapFromScene(QPointF(
            input_graph_node->x + 24.0, input_graph_node->y + 24.0));
        bool add_curves_triggered = false;
        QTimer::singleShot(0, [nodes, &add_curves_triggered] {
            auto* menu = nodes->findChild<QMenu*>(QStringLiteral("colorNodeCanvasContextMenu"));
            if (!menu || menu->actions().empty()) return;
            menu->setActiveAction(menu->actions().front());
            QKeyEvent activate(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QApplication::sendEvent(menu, &activate);
            add_curves_triggered = true;
        });
        QContextMenuEvent add_curves_menu(QContextMenuEvent::Mouse, node_position,
            nodes->viewport()->mapToGlobal(node_position));
        QApplication::sendEvent(nodes->viewport(), &add_curves_menu);
        require(add_curves_triggered,
                "The Color canvas context menu must present the Curves action.");
        const auto selected_curves = nodes->selectedNode();
        const auto* curves_node = selected_curves
            ? color::findNode(graph, *selected_curves) : nullptr;
        require(curves_node && curves_node->type == color::NodeType::Curves &&
                    curves_node->curves,
                "The Color canvas context menu must insert a Curves node after the selection.");
        const auto curves_id = curves_node->id;
        auto* curve_kind = workspace.findChild<QComboBox*>("colorCurveKind");
        require(curve_kind && curve_kind->count() == 9 &&
                    workspace.findChild<QWidget*>("colorCurveEditor"),
                "The Curves controls must expose all nine curve families.");
        auto* curve_editor = workspace.findChild<QWidget*>("colorCurveEditor");
        require(curve_kind && curve_editor,
                "The selected Curves node must retain its curve controls.");
        curve_editor->resize(280, 210);
        curve_editor->show();
        application.processEvents();
        curve_kind = workspace.findChild<QComboBox*>("colorCurveKind");
        curve_editor = workspace.findChild<QWidget*>("colorCurveEditor");
        require(curve_kind && curve_editor,
                "The curve controls must remain available after layout processing.");
        curve_kind->setCurrentIndex(curve_kind->findData(
            static_cast<int>(color::CurveKind::HueVsHue)));
        require(curve_kind->currentData().toInt() ==
                    static_cast<int>(color::CurveKind::HueVsHue),
                "The requested curve family must remain selected during editing.");
        const QPointF curve_point(14.0 + 128.0 * 252.0 / 255.0,
            10.0 + (1.0 - 200.0 / 255.0) * 174.0);
        const auto curve_batches_before = grade_batches_started;
        const auto curve_finishes_before = grade_batches_finished;
        QTest::mousePress(curve_editor, Qt::LeftButton, Qt::NoModifier,
            curve_point.toPoint());
        QTest::mouseMove(curve_editor, (curve_point + QPointF(25.0, -18.0)).toPoint());
        QTest::mouseRelease(curve_editor, Qt::LeftButton, Qt::NoModifier,
            (curve_point + QPointF(25.0, -18.0)).toPoint());
        const auto* adjusted_curves_node = color::findNode(graph, curves_id);
        require(adjusted_curves_node &&
                    adjusted_curves_node->curves->parameters.targeted[0].size() == 3 &&
                    grade_batches_started == curve_batches_before + 1 &&
                    grade_batches_finished == curve_finishes_before + 1,
                "Editing a curve point must update the selected family in one shared history batch.");
        auto* hue_min = workspace.findChild<QDoubleSpinBox*>("colorQualifier_hue_min");
        auto* hue_max = workspace.findChild<QDoubleSpinBox*>("colorQualifier_hue_max");
        auto* qualifier_enabled = workspace.findChild<QCheckBox*>(
            "colorCurvesQualifierEnabled");
        require(hue_min && hue_max && qualifier_enabled,
                "Curves must expose its tonal qualifier controls.");
        hue_min->setValue(350);
        QMetaObject::invokeMethod(hue_min, "editingFinished");
        application.processEvents();
        hue_max = workspace.findChild<QDoubleSpinBox*>("colorQualifier_hue_max");
        qualifier_enabled = workspace.findChild<QCheckBox*>(
            "colorCurvesQualifierEnabled");
        hue_max->setValue(12);
        QMetaObject::invokeMethod(hue_max, "editingFinished");
        application.processEvents();
        qualifier_enabled = workspace.findChild<QCheckBox*>(
            "colorCurvesQualifierEnabled");
        qualifier_enabled->setChecked(true);
        const auto* qualified_curves_node = color::findNode(graph, curves_id);
        require(qualified_curves_node && qualified_curves_node->curves->parameters.qualifier.enabled &&
                    qualified_curves_node->curves->parameters.qualifier.hue_min == 350 &&
                    qualified_curves_node->curves->parameters.qualifier.hue_max == 12,
                "The UI must preserve a qualifier hue range that wraps across zero.");
        QImage thumbnail(4, 3, QImage::Format_RGBA8888);
        thumbnail.fill(Qt::red);
        workspace.setClips({{7, QStringLiteral("Shot A"), thumbnail}}, 7, graph);
        auto* clip_strip = workspace.findChild<QListWidget*>("colorClipStrip");
        require(clip_strip && !clip_strip->item(0)->icon().isNull(),
                "Color clip strip must display available media thumbnails.");
        int copied_grade_clip = 0;
        int applied_grade_clip = 0;
        workspace.setCopyGradeHandler([&](timeline::ClipId id) {
            copied_grade_clip = static_cast<int>(id);
        });
        workspace.setApplyGradeHandler([&](timeline::ClipId id) {
            applied_grade_clip = static_cast<int>(id);
        });
        workspace.setCanApplyCopiedGrade(true);
        QTimer::singleShot(0, [] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* menu = qobject_cast<QMenu*>(widget);
                    menu != nullptr && menu->isVisible()) {
                    if (!menu->actions().empty()) menu->actions().front()->trigger();
                    menu->close();
                    return;
                }
            }
        });
        require(QMetaObject::invokeMethod(clip_strip, "customContextMenuRequested",
                    Qt::DirectConnection, Q_ARG(QPoint, QPoint(8, 8))) &&
                copied_grade_clip == 7,
                "The Color clip menu must expose Copy Grade for the chosen clip.");
        QTimer::singleShot(0, [] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* menu = qobject_cast<QMenu*>(widget);
                    menu != nullptr && menu->isVisible()) {
                    if (menu->actions().size() > 1) menu->actions()[1]->trigger();
                    menu->close();
                    return;
                }
            }
        });
        require(QMetaObject::invokeMethod(clip_strip, "customContextMenuRequested",
                    Qt::DirectConnection, Q_ARG(QPoint, QPoint(8, 8))) &&
                applied_grade_clip == 7,
                "The Color clip menu must expose Apply Grade for the chosen clip.");

        const std::vector<creative_suite::effects::EffectInstance> version_effects{
            creative_suite::effects::makeDefaultInstance("video.brightness")};
        auto version_graph = color::fromEffectStack(version_effects);
        std::vector<timeline::ColorGradeVersion> grade_versions{
            {"version-1", "Original", version_graph}};
        workspace.setGradeVersionsChangedHandler([&](timeline::ClipId id,
            std::vector<timeline::ColorGradeVersion> updated) {
            require(id == 7, "Grade version actions must keep their clip identity.");
            grade_versions = std::move(updated);
            workspace.setClips({{7, QStringLiteral("Shot A"), thumbnail, grade_versions}},
                7, graph);
        });
        workspace.setClips({{7, QStringLiteral("Shot A"), thumbnail, grade_versions}},
            7, graph);
        auto* versions_list = workspace.findChild<QListWidget*>("colorGradeVersions");
        auto* save_version = workspace.findChild<QPushButton*>("colorSaveGradeVersionButton");
        auto* apply_version = workspace.findChild<QPushButton*>("colorApplyGradeVersionButton");
        auto* rename_version = workspace.findChild<QPushButton*>("colorRenameGradeVersionButton");
        auto* remove_version = workspace.findChild<QPushButton*>("colorRemoveGradeVersionButton");
        require(versions_list && versions_list->count() == 1 && save_version &&
                    apply_version && rename_version && remove_version,
                "The independent Grade Versions panel and actions must be available.");
        versions_list->setCurrentRow(0);
        apply_version->click();
        require(graph == version_graph,
                "Applying a saved grade version must restore its complete graph snapshot.");
        QTimer::singleShot(0, [] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QInputDialog*>(widget)) {
                    dialog->setTextValue(QStringLiteral("Renamed"));
                    dialog->accept();
                }
            }
        });
        rename_version->click();
        require(grade_versions.size() == 1 && grade_versions.front().id == "version-1" &&
                    grade_versions.front().name == "Renamed",
                "Renaming a saved grade must preserve its stable identity.");
        QTimer::singleShot(0, [] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QInputDialog*>(widget)) {
                    dialog->setTextValue(QStringLiteral("Second"));
                    dialog->accept();
                }
            }
        });
        save_version->click();
        require(grade_versions.size() == 2 && grade_versions.back().name == "Second" &&
                    grade_versions.back().graph == graph,
                "Saving a grade version must store a graph snapshot under its chosen name.");
        versions_list->setCurrentRow(0);
        remove_version->click();
        require(grade_versions.size() == 1 && grade_versions.front().name == "Second",
                "Removing a grade version must target only the selected snapshot.");
        versions_list->setCurrentRow(0);
        remove_version->click();
        require(grade_versions.empty(), "All saved grade versions must be removable.");
        workspace.setGalleryStills({{"still-a", QStringLiteral("Still 1"),
            QStringLiteral("Shot A"), 42, thumbnail, true}});
        int capture_requests = 0;
        QString renamed_id;
        QString renamed_value;
        QString removed_id;
        workspace.setCaptureStillHandler([&] { ++capture_requests; });
        workspace.setRenameStillHandler([&](const QString& id, const QString& name) {
            renamed_id = id;
            renamed_value = name;
        });
        workspace.setRemoveStillHandler([&](const QString& id) { removed_id = id; });
        for (int i = 1; i <= 19; ++i)
            require(workspace.findChild<QPushButton*>(QStringLiteral("colorToolButton%1").arg(i)),
                    "Color tool bar must expose its 19 buttons.");
        require(!workspace.findChild<QPushButton*>("colorToolButton20"),
                "Color tool bar must contain exactly 19 buttons.");
        for (const char* dock_name : {"colorMotionEffectsDock", "colorSizingDock", "colorKeyframesDock"}) {
            auto* dock = workspace.findChild<QDockWidget*>(dock_name);
            require(dock && dock->features().testFlag(QDockWidget::DockWidgetClosable) &&
                        dock->features().testFlag(QDockWidget::DockWidgetMovable) &&
                        dock->features().testFlag(QDockWidget::DockWidgetFloatable),
                    "Color bottom dock group must expose its three movable, closable child docks.");
        }
        const std::pair<const char*, std::pair<int, int>> dock_buttons[] = {
            {"colorMotionEffectsDock", {1, 6}},
            {"colorSizingDock", {7, 16}},
            {"colorKeyframesDock", {17, 19}},
        };
        for (const auto& [dock_name, range] : dock_buttons) {
            auto* dock = workspace.findChild<QDockWidget*>(dock_name);
            for (int i = range.first; i <= range.second; ++i)
                require(dock && dock->findChild<QPushButton*>(
                                    QStringLiteral("colorToolButton%1").arg(i)),
                        "Each Color child dock must own its row of placeholder buttons.");
        }
        {
            // Camera Raw page (visual stage): documented controls and defaults.
            auto* raw_page = workspace.findChild<QWidget*>("colorCameraRawPage");
            require(raw_page, "Color Motion Effects dock must open on the Camera Raw page.");
            const std::pair<const char*, double> raw_defaults[] = {
                {"colorCameraRawLift", 0.0}, {"colorCameraRawGain", 0.0},
                {"colorCameraRawContrast", 0.0}, {"colorCameraRawColorTemp", 2000.0},
                {"colorCameraRawTint", 0.0}, {"colorCameraRawExposure", 160.0},
                {"colorCameraRawSharpness", 10.0}, {"colorCameraRawHighlights", 0.0},
                {"colorCameraRawFinetuneRed", 100.0}, {"colorCameraRawFinetuneGreen", 100.0},
                {"colorCameraRawFinetuneBlue", 50.0}, {"colorCameraRawShadows", 0.0},
                {"colorCameraRawColorBoost", 0.0}, {"colorCameraRawSaturation", 0.0},
                {"colorCameraRawMidtoneDetail", 0.0},
            };
            for (const auto& [name, value] : raw_defaults) {
                auto* box = raw_page->findChild<QDoubleSpinBox*>(name);
                require(box && qFuzzyCompare(box->value() + 1.0, value + 1.0),
                        "Camera Raw numeric controls must start at their documented defaults.");
            }
            auto* decode_using = raw_page->findChild<QComboBox*>("colorCameraRawDecodeUsing");
            auto* lift = raw_page->findChild<QDoubleSpinBox*>("colorCameraRawLift");
            auto* mono = raw_page->findChild<QCheckBox*>("colorCameraRawMonochrome");
            auto* tint = raw_page->findChild<QDoubleSpinBox*>("colorCameraRawTint");
            auto* use_settings = raw_page->findChild<QPushButton*>("colorCameraRawUseSettingsButton");
            require(decode_using && lift && mono && tint && use_settings &&
                        raw_page->findChild<QPushButton*>("colorCameraRawUseChangesButton") &&
                        raw_page->findChild<QComboBox*>("colorCameraRawDecodeQuality"),
                    "Camera Raw must expose its decode controls and footer actions.");
            require(!lift->isEnabled() && !mono->isEnabled(),
                    "Camera Raw clip decode overrides must be disabled for the camera default.");
            decode_using->setCurrentIndex(1);
            require(lift->isEnabled() && mono->isEnabled(),
                    "Camera Raw clip decode overrides must enable when decoding with the clip.");
            lift->setValue(12.5);
            tint->setValue(-30.0);
            mono->setChecked(true);
            use_settings->click();
            require(qFuzzyIsNull(lift->value()) && qFuzzyIsNull(tint->value()) && !mono->isChecked() &&
                        decode_using->currentIndex() == 0,
                    "Camera Raw Use Settings must restore the documented defaults.");
            // Color Match page (visual stage): chart, presets and defaults.
            workspace.findChild<QPushButton*>("colorToolButton2")->click();
            auto* match_page = workspace.findChild<QWidget*>("colorColorMatchPage");
            auto* motion_pages = workspace.findChild<QStackedWidget*>("colorMotionEffectsPages");
            require(match_page && motion_pages && motion_pages->currentWidget() == match_page &&
                        motion_pages->currentWidget() != raw_page,
                    "Motion Effects button 2 must select the Color Match page.");
            for (int i = 1; i <= 24; ++i)
                require(match_page->findChild<QWidget*>(
                            QStringLiteral("colorColorMatchPatch%1").arg(i)),
                        "Color Match must show 24 chart patches.");
            require(!match_page->findChild<QWidget*>("colorColorMatchPatch25"),
                    "Color Match must show exactly 24 chart patches.");
            auto* match_preset = match_page->findChild<QComboBox*>("colorColorMatchPreset");
            auto* match_temp = match_page->findChild<QSpinBox*>("colorColorMatchTemperature");
            auto* match_white = match_page->findChild<QDoubleSpinBox*>("colorColorMatchWhiteLevel");
            auto* match_white_on = match_page->findChild<QCheckBox*>("colorColorMatchWhiteLevelEnabled");
            auto* match_reset = match_page->findChild<QPushButton*>("colorColorMatchResetButton");
            require(match_preset && match_temp && match_white && match_white_on && match_reset &&
                        match_page->findChild<QPushButton*>("colorColorMatchMatchButton") &&
                        match_page->findChild<QComboBox*>("colorColorMatchSourceGamma") &&
                        match_page->findChild<QComboBox*>("colorColorMatchTargetGamma") &&
                        match_page->findChild<QComboBox*>("colorColorMatchTargetColorSpace"),
                    "Color Match must expose its preset, gamma, color space and action controls.");
            require(match_temp->value() == 6500 && qFuzzyCompare(match_white->value(), 0.9) &&
                        !match_white->isEnabled(),
                    "Color Match must start at 6500 K with White Level 0.9 disabled.");
            const auto patch_before = match_page->findChild<QWidget*>("colorColorMatchPatch1")
                                          ->styleSheet();
            match_preset->setCurrentIndex(1);
            require(match_page->findChild<QWidget*>("colorColorMatchPatch1")->styleSheet() !=
                        patch_before,
                    "Changing the Color Match preset must relayout the chart patches.");
            match_white_on->setChecked(true);
            require(match_white->isEnabled(), "White Level checkbox must enable the value.");
            match_temp->setValue(5000);
            match_reset->click();
            require(match_preset->currentIndex() == 0 && match_temp->value() == 6500 &&
                        !match_white_on->isChecked() && !match_white->isEnabled(),
                    "Color Match reset must restore the documented defaults.");
            // Primaries - Color Bars page (visual stage): defaults and linked controls.
            workspace.findChild<QPushButton*>("colorToolButton3")->click();
            auto* bars_page = workspace.findChild<QWidget*>("colorPrimariesBarsPage");
            require(bars_page && motion_pages->currentWidget()->isAncestorOf(bars_page),
                    "Motion Effects button 3 must select the Primaries - Color Bars page.");
            const std::pair<const char*, double> bars_defaults[] = {
                {"colorPrimariesBarsTemp", 0.0}, {"colorPrimariesBarsTint", 0.0},
                {"colorPrimariesBarsContrast", 1.0}, {"colorPrimariesBarsPivot", 0.435},
                {"colorPrimariesBarsMidDetail", 0.0}, {"colorPrimariesBarsLiftY", 0.0},
                {"colorPrimariesBarsGammaB", 0.0}, {"colorPrimariesBarsGainY", 1.0},
                {"colorPrimariesBarsGainB", 1.0}, {"colorPrimariesBarsOffsetR", 25.0},
                {"colorPrimariesBarsOffsetB", 25.0}, {"colorPrimariesBarsColBoost", 0.0},
                {"colorPrimariesBarsShad", 0.0}, {"colorPrimariesBarsHiLight", 0.0},
                {"colorPrimariesBarsSat", 50.0}, {"colorPrimariesBarsHue", 50.0},
                {"colorPrimariesBarsLMix", 100.0},
            };
            for (const auto& [name, value] : bars_defaults) {
                auto* box = bars_page->findChild<QDoubleSpinBox*>(
                    QString::fromLatin1(name).replace("Value", QString()));
                if (box == nullptr)
                    box = bars_page->findChild<QDoubleSpinBox*>(QString::fromLatin1(name) + "Value");
                require(box && qFuzzyCompare(box->value() + 1.0, value + 1.0),
                        "Primaries - Color Bars controls must start at their documented defaults.");
            }
            require(!bars_page->findChild<QWidget*>("colorPrimariesBarsOffsetYSlider") &&
                        bars_page->findChild<QWidget*>("colorPrimariesBarsLiftYSlider"),
                    "Offset has three bars; Lift, Gamma and Gain have four.");
            auto* gain_slider = bars_page->findChild<QSlider*>("colorPrimariesBarsGainRSlider");
            auto* gain_value = bars_page->findChild<QDoubleSpinBox*>("colorPrimariesBarsGainRValue");
            auto* lift_value = bars_page->findChild<QDoubleSpinBox*>("colorPrimariesBarsLiftRValue");
            require(gain_slider && gain_value && lift_value,
                    "Primaries - Color Bars must expose slider and value controls per channel.");
            gain_slider->setValue(250);
            require(qFuzzyCompare(gain_value->value(), 2.5),
                    "Moving a bar must update its value field.");
            gain_value->setValue(3.0);
            require(gain_slider->value() == 300, "Typing a value must move its bar.");
            lift_value->setValue(0.4);
            bars_page->findChild<QPushButton*>("colorPrimariesBarsGainResetButton")->click();
            require(qFuzzyCompare(gain_value->value(), 1.0) && qFuzzyCompare(lift_value->value(), 0.4),
                    "A group reset must restore only that group's defaults.");
            bars_page->findChild<QPushButton*>("colorPrimariesBarsResetButton")->click();
            require(qFuzzyIsNull(lift_value->value()),
                    "The page reset must restore every group's defaults.");
            // High Dynamic Range - Color Wheels page (visual stage).
            workspace.findChild<QPushButton*>("colorToolButton4")->click();
            auto* hdr_page = workspace.findChild<QWidget*>("colorHdrWheelsPage");
            require(hdr_page && motion_pages->currentWidget()->isAncestorOf(hdr_page),
                    "Motion Effects button 4 must select the High Dynamic Range page.");
            for (const char* wheel_name : {"Dark", "Shadow", "Light", "Global"}) {
                const QString prefix = QStringLiteral("colorHdrWheels") + QLatin1String(wheel_name);
                require(hdr_page->findChild<QWidget*>(prefix + "Wheel") &&
                            hdr_page->findChild<QPushButton*>(prefix + "ResetButton"),
                        "Each HDR wheel must have its wheel and reset button.");
                require(qFuzzyCompare(hdr_page->findChild<QDoubleSpinBox*>(prefix + "Sat")->value(), 1.0) &&
                            qFuzzyIsNull(hdr_page->findChild<QDoubleSpinBox*>(prefix + "Exp")->value()) &&
                            qFuzzyIsNull(hdr_page->findChild<QDoubleSpinBox*>(prefix + "X")->value()),
                        "Each HDR wheel must start with Exp 0, Sat 1 and X 0.");
            }
            require(!hdr_page->findChild<QWidget*>("colorHdrWheelsGlobalRange") &&
                        !hdr_page->findChild<QWidget*>("colorHdrWheelsGlobalZoneValue") &&
                        qFuzzyCompare(hdr_page->findChild<QDoubleSpinBox*>("colorHdrWheelsDarkZoneValue")->value(), -1.5) &&
                        qFuzzyCompare(hdr_page->findChild<QDoubleSpinBox*>("colorHdrWheelsShadowZoneValue")->value(), 1.0) &&
                        qFuzzyCompare(hdr_page->findChild<QDoubleSpinBox*>("colorHdrWheelsLightZoneValue")->value(), -1.0) &&
                        qFuzzyCompare(hdr_page->findChild<QDoubleSpinBox*>("colorHdrWheelsDarkRange")->value(), 0.2) &&
                        qFuzzyCompare(hdr_page->findChild<QDoubleSpinBox*>("colorHdrWheelsShadowRange")->value(), 0.22),
                    "HDR zone values and ranges must match the documented defaults; Global has none.");
            require(qFuzzyCompare(hdr_page->findChild<QDoubleSpinBox*>("colorHdrWheelsCont")->value(), 1.0) &&
                        qFuzzyIsNull(hdr_page->findChild<QDoubleSpinBox*>("colorHdrWheelsPivot")->value()) &&
                        hdr_page->findChild<QDoubleSpinBox*>("colorHdrWheelsBOfs"),
                    "HDR bottom row must start at its documented defaults.");
            // Dragging the puck moves X/Y; typing X/Y keeps the puck in step.
            auto* dark_wheel = hdr_page->findChild<QWidget*>("colorHdrWheelsDarkWheel");
            auto* dark_x = hdr_page->findChild<QDoubleSpinBox*>("colorHdrWheelsDarkX");
            auto* dark_y = hdr_page->findChild<QDoubleSpinBox*>("colorHdrWheelsDarkY");
            QMouseEvent wheel_press(QEvent::MouseButtonPress,
                QPointF(dark_wheel->width() / 2.0 + 20.0, dark_wheel->height() / 2.0),
                QPointF(), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(dark_wheel, &wheel_press);
            require(dark_x->value() > 0.3 && std::abs(dark_y->value()) < 0.05,
                    "Dragging an HDR wheel puck must update its X and Y fields.");
            hdr_page->findChild<QPushButton*>("colorHdrWheelsDarkResetButton")->click();
            require(qFuzzyIsNull(dark_x->value()) &&
                        qFuzzyCompare(hdr_page->findChild<QDoubleSpinBox*>("colorHdrWheelsDarkZoneValue")->value(), -1.5),
                    "An HDR wheel reset must restore that wheel's defaults.");
            hdr_page->findChild<QPushButton*>("colorHdrWheelsZoneDot3")->click();
            hdr_page->findChild<QPushButton*>("colorHdrWheelsNextZoneButton")->click();
            require(hdr_page->findChild<QPushButton*>("colorHdrWheelsZoneDot4")->isChecked(),
                    "The HDR zone pager Next button must select the following zone set.");
            hdr_page->findChild<QPushButton*>("colorHdrWheelsResetButton")->click();
            require(hdr_page->findChild<QPushButton*>("colorHdrWheelsZoneDot1")->isChecked(),
                    "The HDR page reset must restore the first zone set.");
            // RGB Mixer page (visual stage): identity mix, linked bars and resets.
            workspace.findChild<QPushButton*>("colorToolButton5")->click();
            auto* mixer_page = workspace.findChild<QWidget*>("colorRgbMixerPage");
            require(mixer_page && motion_pages->currentWidget()->isAncestorOf(mixer_page),
                    "Motion Effects button 5 must select the RGB Mixer page.");
            for (const char* output : {"Red", "Green", "Blue"}) {
                for (const char* input : {"R", "G", "B"}) {
                    const QString suffix = QLatin1String(output) + "Output" + QLatin1String(input);
                    auto* box = mixer_page->findChild<QDoubleSpinBox*>("colorRgbMixer" + suffix + "Value");
                    require(box && mixer_page->findChild<QSlider*>("colorRgbMixer" + suffix + "Slider"),
                            "Each RGB Mixer output must have a bar and value per input channel.");
                    const bool identity = QLatin1String(output).startsWith(QLatin1String(input));
                    require(qFuzzyCompare(box->value() + 1.0, (identity ? 1.0 : 0.0) + 1.0),
                            "RGB Mixer must start as an identity mix.");
                }
            }
            for (const char* route : {"colorRgbMixerRouteRedGreenButton",
                     "colorRgbMixerRouteGreenBlueButton", "colorRgbMixerRouteBlueRedButton"})
                require(mixer_page->findChild<QPushButton*>(route),
                        "RGB Mixer must show its three route shortcut buttons.");
            auto* mixer_mono = mixer_page->findChild<QCheckBox*>("colorRgbMixerMonochrome");
            auto* mixer_preserve = mixer_page->findChild<QCheckBox*>("colorRgbMixerPreserveLuminance");
            require(mixer_mono && mixer_preserve && !mixer_mono->isChecked() && mixer_preserve->isChecked(),
                    "RGB Mixer must start with Monochrome off and Preserve Luminance on.");
            auto* red_green = mixer_page->findChild<QDoubleSpinBox*>("colorRgbMixerRedOutputGValue");
            auto* red_green_bar = mixer_page->findChild<QSlider*>("colorRgbMixerRedOutputGSlider");
            auto* green_red = mixer_page->findChild<QDoubleSpinBox*>("colorRgbMixerGreenOutputRValue");
            red_green_bar->setValue(150);
            require(qFuzzyCompare(red_green->value(), 1.5), "Moving a mixer bar must update its value.");
            red_green->setValue(-2.0);
            require(red_green_bar->value() == -200, "Typing a mixer value must move its bar.");
            green_red->setValue(0.7);
            mixer_mono->setChecked(true);
            mixer_page->findChild<QPushButton*>("colorRgbMixerRedResetButton")->click();
            require(qFuzzyIsNull(red_green->value()) && qFuzzyCompare(green_red->value(), 0.7),
                    "An output reset must restore only that output.");
            mixer_page->findChild<QPushButton*>("colorRgbMixerResetButton")->click();
            require(qFuzzyIsNull(green_red->value()) && !mixer_mono->isChecked() && mixer_preserve->isChecked(),
                    "The page reset must restore every output and the options.");
            // Motion Effects page (visual stage): sections, enabling and linking.
            workspace.findChild<QPushButton*>("colorToolButton6")->click();
            auto* effects_page = workspace.findChild<QWidget*>("colorMotionEffectsPage");
            require(effects_page && motion_pages->currentWidget()->isAncestorOf(effects_page),
                    "Motion Effects button 6 must select the Motion Effects page.");
            for (const char* section : {"TemporalNR", "TemporalThreshold", "SpatialNR",
                     "SpatialThreshold", "MotionBlur"})
                require(effects_page->findChild<QPushButton*>(
                            QStringLiteral("colorMotionEffects%1ResetButton").arg(QLatin1String(section))),
                        "Each Motion Effects section must have its own reset button.");
            auto* frames = effects_page->findChild<QComboBox*>("colorMotionEffectsTemporalFrames");
            auto* temporal_range = effects_page->findChild<QComboBox*>("colorMotionEffectsTemporalRange");
            auto* temporal_motion = effects_page->findChild<QDoubleSpinBox*>("colorMotionEffectsTemporalMotion");
            auto* temporal_luma = effects_page->findChild<QDoubleSpinBox*>("colorMotionEffectsTemporalLuma");
            auto* temporal_chroma = effects_page->findChild<QDoubleSpinBox*>("colorMotionEffectsTemporalChroma");
            auto* spatial_luma = effects_page->findChild<QDoubleSpinBox*>("colorMotionEffectsSpatialLuma");
            auto* spatial_chroma = effects_page->findChild<QDoubleSpinBox*>("colorMotionEffectsSpatialChroma");
            auto* spatial_link = effects_page->findChild<QPushButton*>("colorMotionEffectsSpatialLink");
            auto* blur_amount = effects_page->findChild<QDoubleSpinBox*>("colorMotionEffectsBlurAmount");
            require(frames && temporal_range && temporal_motion && temporal_luma && temporal_chroma &&
                        spatial_luma && spatial_chroma && spatial_link && blur_amount,
                    "Motion Effects must expose its temporal, spatial and blur controls.");
            require(frames->currentIndex() == 0 && temporal_range->currentText() == "Medium" &&
                        qFuzzyCompare(temporal_motion->value(), 50.0) && qFuzzyIsNull(blur_amount->value()) &&
                        effects_page->findChild<QComboBox*>("colorMotionEffectsSpatialMode")->currentText() == "Faster" &&
                        effects_page->findChild<QComboBox*>("colorMotionEffectsSpatialRadius")->currentText() == "Small",
                    "Motion Effects must start at its documented defaults.");
            require(!temporal_range->isEnabled() && !temporal_luma->isEnabled() &&
                        !temporal_motion->isEnabled() && spatial_luma->isEnabled(),
                    "Temporal controls must be disabled while Frames is 0; spatial stays enabled.");
            frames->setCurrentIndex(2);
            require(temporal_range->isEnabled() && temporal_luma->isEnabled() && temporal_motion->isEnabled(),
                    "Temporal controls must enable once Frames is above 0.");
            spatial_luma->setValue(12.0);
            require(qFuzzyCompare(spatial_chroma->value(), 12.0),
                    "Linked Luma and Chroma must mirror each other.");
            spatial_link->click();
            spatial_luma->setValue(30.0);
            require(qFuzzyCompare(spatial_chroma->value(), 12.0),
                    "Unlinked Luma and Chroma must be independent.");
            blur_amount->setValue(8.0);
            effects_page->findChild<QPushButton*>("colorMotionEffectsSpatialThresholdResetButton")->click();
            require(qFuzzyIsNull(spatial_luma->value()) && qFuzzyIsNull(spatial_chroma->value()) &&
                        qFuzzyCompare(blur_amount->value(), 8.0) && frames->currentIndex() == 2,
                    "A section reset must restore only that section.");
            effects_page->findChild<QPushButton*>("colorMotionEffectsResetButton")->click();
            require(frames->currentIndex() == 0 && qFuzzyIsNull(blur_amount->value()) &&
                        spatial_link->isChecked() && !temporal_range->isEnabled(),
                    "The page reset must restore every section and re-link Luma and Chroma.");
            // Sizing dock, page 1: Curves - Custom (visual stage).
            auto* sizing_pages = workspace.findChild<QStackedWidget*>("colorSizingPages");
            workspace.findChild<QPushButton*>("colorToolButton7")->click();
            auto* curves_page = workspace.findChild<QWidget*>("colorCurvesPage");
            require(sizing_pages && curves_page && sizing_pages->currentWidget()->isAncestorOf(curves_page),
                    "Sizing dock button 1 must open the Curves - Custom page.");
            for (const char* name : {"colorCurvesCustomModeButton", "colorCurvesHueVsHueModeButton",
                     "colorCurvesSatVsLumModeButton", "colorCurvesExpandButton", "colorCurvesResetButton",
                     "colorCurvesEditLink", "colorCurvesEditY", "colorCurvesEditR", "colorCurvesEditG",
                     "colorCurvesEditB", "colorCurvesSoftClipLink", "colorCurvesSoftClipR",
                     "colorCurvesSoftClipG", "colorCurvesSoftClipB"})
                require(curves_page->findChild<QPushButton*>(name),
                        "Curves page must expose its mode, channel and Soft Clip buttons.");
            for (const char* name : {"colorCurvesEditY", "colorCurvesEditR", "colorCurvesEditG",
                     "colorCurvesEditB", "colorCurvesSoftClipR", "colorCurvesSoftClipLink"})
                require(curves_page->findChild<QPushButton*>(name)->isChecked(),
                        "Curves channel toggles and links must start on.");
            for (const char* channel : {"Y", "R", "G", "B"}) {
                auto* value = curves_page->findChild<QSpinBox*>(
                    QStringLiteral("colorCurvesStrength%1Value").arg(QLatin1String(channel)));
                auto* slider = curves_page->findChild<QSlider*>(
                    QStringLiteral("colorCurvesStrength%1Slider").arg(QLatin1String(channel)));
                require(value && slider && value->value() == 100 && slider->value() == 100,
                        "Curves strength controls must start at 100.");
            }
            auto* strength_r = curves_page->findChild<QSpinBox*>("colorCurvesStrengthRValue");
            auto* strength_r_slider = curves_page->findChild<QSlider*>("colorCurvesStrengthRSlider");
            strength_r->setValue(140);
            require(strength_r_slider->value() == 140, "Typing a Curves strength must move its slider.");
            strength_r_slider->setValue(30);
            require(strength_r->value() == 30, "Moving a Curves slider must update its value.");
            curves_page->findChild<QPushButton*>("colorCurvesStrengthRResetButton")->click();
            require(strength_r->value() == 100, "A Curves strength reset must restore 100.");
            require(qFuzzyCompare(curves_page->findChild<QDoubleSpinBox*>("colorCurvesSoftClipLow")->value(), 50.0) &&
                        qFuzzyCompare(curves_page->findChild<QDoubleSpinBox*>("colorCurvesSoftClipHigh")->value(), 50.0) &&
                        qFuzzyIsNull(curves_page->findChild<QDoubleSpinBox*>("colorCurvesSoftClipLS")->value()) &&
                        qFuzzyIsNull(curves_page->findChild<QDoubleSpinBox*>("colorCurvesSoftClipHS")->value()),
                    "Curves Soft Clip must start at Low 50, High 50, L.S. 0 and H.S. 0.");
            // Graph: two end points; clicking adds a point, double-clicking removes it.
            auto* curve_graph = curves_page->findChild<QWidget*>("colorCurvesGraph");
            require(curve_graph && curve_graph->property("pointCount").toInt() == 2,
                    "The Curves graph must start with its two end points.");
            const double graph_left = 18.0;
            const double graph_width = curve_graph->width() - 26.0;
            const double graph_bottom = curve_graph->height() - 8.0;
            const double graph_height = curve_graph->height() - 16.0;
            // The default curve is the diagonal; click clearly above it at x = 0.5.
            const QPointF add_at(graph_left + graph_width * 0.5, graph_bottom - graph_height * 0.8);
            QMouseEvent add_press(QEvent::MouseButtonPress, add_at, add_at, Qt::LeftButton,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(curve_graph, &add_press);
            QMouseEvent add_release(QEvent::MouseButtonRelease, add_at, add_at, Qt::LeftButton,
                Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(curve_graph, &add_release);
            require(curve_graph->property("pointCount").toInt() == 3,
                    "Clicking the Curves graph must add a point.");
            QMouseEvent remove_click(QEvent::MouseButtonDblClick, add_at, add_at, Qt::LeftButton,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(curve_graph, &remove_click);
            require(curve_graph->property("pointCount").toInt() == 2,
                    "Double-clicking an interior Curves point must remove it.");
            QApplication::sendEvent(curve_graph, &add_press);
            QApplication::sendEvent(curve_graph, &add_release);
            curves_page->findChild<QPushButton*>("colorCurvesEditR")->setChecked(false);
            curves_page->findChild<QDoubleSpinBox*>("colorCurvesSoftClipLow")->setValue(10.0);
            curves_page->findChild<QPushButton*>("colorCurvesResetButton")->click();
            require(curve_graph->property("pointCount").toInt() == 2 &&
                        curves_page->findChild<QPushButton*>("colorCurvesEditR")->isChecked() &&
                        qFuzzyCompare(curves_page->findChild<QDoubleSpinBox*>("colorCurvesSoftClipLow")->value(), 50.0),
                    "The Curves page reset must restore the graph, toggles and fields.");
            // Sizing dock, page 2: Color Slice (visual stage).
            workspace.findChild<QPushButton*>("colorToolButton8")->click();
            auto* slice_page = workspace.findChild<QWidget*>("colorSlicePage");
            require(slice_page && sizing_pages->currentWidget()->isAncestorOf(slice_page) &&
                        !sizing_pages->currentWidget()->isAncestorOf(curves_page),
                    "Sizing dock button 2 must open the Color Slice page.");
            const std::pair<const char*, double> slice_globals[] = {
                {"colorSliceDen", 0.0}, {"colorSliceDenDepth", 0.0}, {"colorSliceSat", 1.0},
                {"colorSliceSatBalance", 0.0}, {"colorSliceSatDepth", 0.0}, {"colorSliceHue", 0.0},
            };
            for (const auto& [name, value] : slice_globals) {
                auto* box = slice_page->findChild<QDoubleSpinBox*>(name);
                require(box && qFuzzyCompare(box->value() + 1.0, value + 1.0),
                        "Color Slice global controls must start at their documented defaults.");
            }
            for (const char* slice : {"Red", "Skin", "Yellow", "Green", "Cyan", "Blue", "Magenta"}) {
                const QString prefix = QStringLiteral("colorSlice") + QLatin1String(slice);
                require(slice_page->findChild<QWidget*>(prefix + "Wheel") &&
                            slice_page->findChild<QPushButton*>(prefix + "ResetButton") &&
                            slice_page->findChild<QPushButton*>(prefix + "ToggleButton")->isChecked() &&
                            slice_page->findChild<QSlider*>(prefix + "SliderA") &&
                            slice_page->findChild<QSlider*>(prefix + "SliderB") &&
                            slice_page->findChild<QSlider*>(prefix + "CenterSlider") &&
                            slice_page->findChild<QSlider*>(prefix + "HueSlider"),
                        "Each Color Slice column must have its wheel, toggle, reset and sliders.");
                require(qFuzzyIsNull(slice_page->findChild<QDoubleSpinBox*>(prefix + "Center")->value()) &&
                            qFuzzyIsNull(slice_page->findChild<QDoubleSpinBox*>(prefix + "Hue")->value()) &&
                            qFuzzyIsNull(slice_page->findChild<QDoubleSpinBox*>(prefix + "ValueA")->value()) &&
                            qFuzzyCompare(slice_page->findChild<QDoubleSpinBox*>(prefix + "ValueB")->value(), 1.0),
                        "Each Color Slice column must start at 0, 0, 0 and 1.");
            }
            require(!slice_page->findChild<QWidget*>("colorSliceOrangeWheel"),
                    "Color Slice must have exactly seven columns.");
            auto* slice_a = slice_page->findChild<QSlider*>("colorSliceGreenSliderA");
            auto* slice_a_value = slice_page->findChild<QDoubleSpinBox*>("colorSliceGreenValueA");
            auto* slice_red_center = slice_page->findChild<QDoubleSpinBox*>("colorSliceRedCenter");
            slice_a->setValue(50);
            require(qFuzzyCompare(slice_a_value->value(), 0.5), "Moving a Color Slice bar must update its value.");
            slice_a_value->setValue(-0.25);
            require(slice_a->value() == -25, "Typing a Color Slice value must move its bar.");
            slice_red_center->setValue(0.3);
            slice_page->findChild<QPushButton*>("colorSliceGreenToggleButton")->setChecked(false);
            slice_page->findChild<QPushButton*>("colorSliceGreenResetButton")->click();
            require(qFuzzyIsNull(slice_a_value->value()) && qFuzzyCompare(slice_red_center->value(), 0.3) &&
                        slice_page->findChild<QPushButton*>("colorSliceGreenToggleButton")->isChecked(),
                    "A Color Slice column reset must restore only that column.");
            slice_page->findChild<QDoubleSpinBox*>("colorSliceSat")->setValue(1.7);
            slice_page->findChild<QPushButton*>("colorSliceResetButton")->click();
            require(qFuzzyIsNull(slice_red_center->value()) &&
                        qFuzzyCompare(slice_page->findChild<QDoubleSpinBox*>("colorSliceSat")->value(), 1.0),
                    "The Color Slice page reset must restore every control.");
            // Sizing dock, page 3: Color Warp - Chroma Warp (visual stage).
            workspace.findChild<QPushButton*>("colorToolButton9")->click();
            auto* warp_page = workspace.findChild<QWidget*>("colorChromaWarpPage");
            require(warp_page && sizing_pages->currentWidget()->isAncestorOf(warp_page) &&
                        !sizing_pages->currentWidget()->isAncestorOf(slice_page),
                    "Sizing dock button 3 must open the Chroma Warp page.");
            for (const char* name : {"colorChromaWarpChromaModeButton", "colorChromaWarpHueSatModeButton",
                     "colorChromaWarpGridButton", "colorChromaWarpExpandButton", "colorChromaWarpResetButton",
                     "colorChromaWarpTool1", "colorChromaWarpTool2", "colorChromaWarpTool3",
                     "colorChromaWarpTool4", "colorChromaWarpDeleteButton", "colorChromaWarpPinResetButton",
                     "colorChromaWarpLinkButton", "colorChromaWarpUnlinkButton"})
                require(warp_page->findChild<QPushButton*>(name),
                        "Chroma Warp must expose its mode, tool, pin and link buttons.");
            auto* warp_graph = warp_page->findChild<QWidget*>("colorChromaWarpGraph");
            auto* warp_range = warp_page->findChild<QDoubleSpinBox*>("colorChromaWarpChromaRange");
            auto* warp_range_slider = warp_page->findChild<QSlider*>("colorChromaWarpChromaRangeSlider");
            auto* warp_delete = warp_page->findChild<QPushButton*>("colorChromaWarpDeleteButton");
            require(warp_graph && warp_range && warp_range_slider &&
                        warp_graph->property("pinCount").toInt() == 1 &&
                        warp_graph->property("selectedPin").toInt() == -1,
                    "Chroma Warp must start with one unselected pin.");
            require(qFuzzyCompare(warp_range->value(), 0.04) &&
                        qFuzzyCompare(warp_page->findChild<QDoubleSpinBox*>("colorChromaWarpTonalLow")->value(), 1.0) &&
                        qFuzzyCompare(warp_page->findChild<QDoubleSpinBox*>("colorChromaWarpTonalHigh")->value(), 1.0) &&
                        qFuzzyCompare(warp_page->findChild<QDoubleSpinBox*>("colorChromaWarpTonalPivot")->value(), 0.5) &&
                        qFuzzyIsNull(warp_page->findChild<QDoubleSpinBox*>("colorChromaWarpExposure")->value()),
                    "Chroma Warp pin parameters must start at their documented defaults.");
            require(!warp_range->isEnabled() && !warp_range_slider->isEnabled() && !warp_delete->isEnabled(),
                    "Chroma Warp pin controls must be disabled until a pin is selected.");
            warp_graph->resize(400, 400);
            // Plane geometry mirrors WarpGraph::planeRect (square, 6 px margin).
            const double warp_side = std::min(warp_graph->width(), warp_graph->height()) - 12.0;
            const double warp_left = (warp_graph->width() - warp_side) / 2.0;
            const double warp_top = (warp_graph->height() - warp_side) / 2.0;
            const auto warp_point = [&](double x, double y) {
                return QPointF(warp_left + x * warp_side, warp_top + y * warp_side);
            };
            const auto warp_click = [&](const QPointF& at) {
                QMouseEvent press(QEvent::MouseButtonPress, at, at, Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);
                QApplication::sendEvent(warp_graph, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, at, at, Qt::LeftButton, Qt::NoButton,
                    Qt::NoModifier);
                QApplication::sendEvent(warp_graph, &release);
            };
            warp_click(warp_point(0.5, 0.5));
            require(warp_graph->property("selectedPin").toInt() == 0 && warp_range->isEnabled() &&
                        warp_range_slider->isEnabled() && warp_delete->isEnabled(),
                    "Clicking a pin must select it and enable the pin controls.");
            warp_range->setValue(0.1);
            require(warp_range_slider->value() == 100, "Typing a pin value must move its slider.");
            warp_page->findChild<QPushButton*>("colorChromaWarpPinResetButton")->click();
            require(qFuzzyCompare(warp_range->value(), 0.04), "The pin reset must restore the defaults.");
            warp_click(warp_point(0.35, 0.5));
            require(warp_graph->property("pinCount").toInt() == 2 &&
                        warp_graph->property("selectedPin").toInt() == 1,
                    "With the Add pin tool, clicking empty plane must add and select a pin.");
            warp_delete->click();
            require(warp_graph->property("pinCount").toInt() == 1 &&
                        warp_graph->property("selectedPin").toInt() == -1 && !warp_range->isEnabled(),
                    "Delete must remove the selected pin and disable the pin controls.");
            warp_page->findChild<QPushButton*>("colorChromaWarpTool4")->click();
            warp_click(warp_point(0.35, 0.5));
            require(warp_graph->property("pinCount").toInt() == 1,
                    "With the Select tool, clicking empty plane must not add a pin.");
            warp_page->findChild<QPushButton*>("colorChromaWarpTool1")->click();
            warp_click(warp_point(0.35, 0.5));
            warp_page->findChild<QPushButton*>("colorChromaWarpResetButton")->click();
            require(warp_graph->property("pinCount").toInt() == 1 &&
                        warp_graph->property("selectedPin").toInt() == -1 &&
                        warp_page->findChild<QPushButton*>("colorChromaWarpTool1")->isChecked(),
                    "The Chroma Warp page reset must restore one unselected pin and the Add pin tool.");
            // Sizing dock, page 4: Qualifier - HSL (visual stage).
            workspace.findChild<QPushButton*>("colorToolButton10")->click();
            auto* qual_page = workspace.findChild<QWidget*>("colorQualifierPage");
            require(qual_page && sizing_pages->currentWidget()->isAncestorOf(qual_page) &&
                        !sizing_pages->currentWidget()->isAncestorOf(warp_page),
                    "Sizing dock button 4 must open the Qualifier - HSL page.");
            for (const char* name : {"colorQualifierHslModeButton", "colorQualifierRgbModeButton",
                     "colorQualifierLumModeButton", "colorQualifierThreeDModeButton",
                     "colorQualifierResetButton", "colorQualifierTool1", "colorQualifierTool5",
                     "colorQualifierInvertButton", "colorQualifierHueToggle",
                     "colorQualifierSaturationToggle", "colorQualifierLuminanceToggle",
                     "colorQualifierMatteTab1", "colorQualifierMatteTab2"})
                require(qual_page->findChild<QPushButton*>(name),
                        "Qualifier must expose its mode, tool, toggle and Matte Finesse buttons.");
            const std::pair<const char*, double> qual_defaults[] = {
                {"colorQualifierHueCenter", 50.0}, {"colorQualifierHueWidth", 100.0},
                {"colorQualifierHueSoft", 0.0}, {"colorQualifierHueSym", 50.0},
                {"colorQualifierSaturationLow", 0.0}, {"colorQualifierSaturationHigh", 100.0},
                {"colorQualifierSaturationLSoft", 0.0}, {"colorQualifierSaturationHSoft", 0.0},
                {"colorQualifierLuminanceLow", 0.0}, {"colorQualifierLuminanceHigh", 100.0},
                {"colorQualifierPreFilter", 0.0}, {"colorQualifierCleanBlack", 0.0},
                {"colorQualifierCleanWhite", 0.0}, {"colorQualifierBlackClip", 0.0},
                {"colorQualifierWhiteClip", 100.0}, {"colorQualifierBlurRadius", 0.0},
                {"colorQualifierInOutRatio", 0.0},
            };
            for (const auto& [name, value] : qual_defaults) {
                auto* box = qual_page->findChild<QDoubleSpinBox*>(name);
                require(box && qFuzzyCompare(box->value() + 1.0, value + 1.0),
                        "Qualifier controls must start at their documented defaults.");
            }
            require(qual_page->findChild<QPushButton*>("colorQualifierHueToggle")->isChecked() &&
                        qual_page->findChild<QPushButton*>("colorQualifierTool1")->isChecked() &&
                        qual_page->findChild<QPushButton*>("colorQualifierMatteTab1")->isChecked() &&
                        !qual_page->findChild<QPushButton*>("colorQualifierInvertButton")->isChecked(),
                    "Qualifier must start with its ranges on, the Pick tool and tab 1 selected.");
            // Hue bar shows Center +/- Width / 2.
            auto* hue_bar = qual_page->findChild<QWidget*>("colorQualifierHueBar");
            auto* hue_width = qual_page->findChild<QDoubleSpinBox*>("colorQualifierHueWidth");
            require(hue_bar && qFuzzyCompare(hue_bar->property("rangeLow").toDouble() + 1.0, 1.0) &&
                        qFuzzyCompare(hue_bar->property("rangeHigh").toDouble(), 100.0),
                    "The Hue bar must start spanning the full range.");
            hue_width->setValue(40.0);
            require(qFuzzyCompare(hue_bar->property("rangeLow").toDouble(), 30.0) &&
                        qFuzzyCompare(hue_bar->property("rangeHigh").toDouble(), 70.0),
                    "Changing Hue Width must resize the bar about Center.");
            // Saturation bar: dragging the high handle updates High.
            auto* sat_bar = qual_page->findChild<QWidget*>("colorQualifierSaturationBar");
            auto* sat_high = qual_page->findChild<QDoubleSpinBox*>("colorQualifierSaturationHigh");
            sat_bar->resize(406, 26);
            const auto sat_x = [&](double percent) { return 3.0 + (sat_bar->width() - 6.0) * percent / 100.0; };
            QMouseEvent sat_press(QEvent::MouseButtonPress, QPointF(sat_x(100.0), 13.0),
                QPointF(sat_x(100.0), 13.0), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(sat_bar, &sat_press);
            QMouseEvent sat_move(QEvent::MouseMove, QPointF(sat_x(60.0), 13.0),
                QPointF(sat_x(60.0), 13.0), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(sat_bar, &sat_move);
            QMouseEvent sat_release(QEvent::MouseButtonRelease, QPointF(sat_x(60.0), 13.0),
                QPointF(sat_x(60.0), 13.0), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(sat_bar, &sat_release);
            require(std::abs(sat_high->value() - 60.0) < 0.5 &&
                        std::abs(sat_bar->property("rangeHigh").toDouble() - 60.0) < 0.5 &&
                        qFuzzyIsNull(sat_bar->property("rangeLow").toDouble()),
                    "Dragging the Saturation high handle must update High and keep Low.");
            qual_page->findChild<QDoubleSpinBox*>("colorQualifierSaturationLow")->setValue(20.0);
            require(qFuzzyCompare(sat_bar->property("rangeLow").toDouble(), 20.0),
                    "Typing Saturation Low must move the bar's low handle.");
            qual_page->findChild<QPushButton*>("colorQualifierSaturationResetButton")->click();
            require(qFuzzyCompare(sat_high->value(), 100.0) && qFuzzyCompare(hue_width->value(), 40.0),
                    "A range reset must restore only that range.");
            auto* white_clip = qual_page->findChild<QDoubleSpinBox*>("colorQualifierWhiteClip");
            auto* white_clip_slider = qual_page->findChild<QSlider*>("colorQualifierWhiteClipSlider");
            white_clip->setValue(75.0);
            require(white_clip_slider->value() == 750, "Typing a Matte Finesse value must move its slider.");
            qual_page->findChild<QPushButton*>("colorQualifierMatteTab2")->click();
            qual_page->findChild<QPushButton*>("colorQualifierInvertButton")->click();
            qual_page->findChild<QPushButton*>("colorQualifierResetButton")->click();
            require(qFuzzyCompare(hue_width->value(), 100.0) && qFuzzyCompare(white_clip->value(), 100.0) &&
                        qual_page->findChild<QPushButton*>("colorQualifierMatteTab1")->isChecked() &&
                        !qual_page->findChild<QPushButton*>("colorQualifierInvertButton")->isChecked(),
                    "The Qualifier page reset must restore every control.");
            // Sizing dock, page 5: Window (visual stage).
            workspace.findChild<QPushButton*>("colorToolButton11")->click();
            auto* window_page = workspace.findChild<QWidget*>("colorWindowPage");
            require(window_page && sizing_pages->currentWidget()->isAncestorOf(window_page) &&
                        !sizing_pages->currentWidget()->isAncestorOf(qual_page),
                    "Sizing dock button 5 must open the Window page.");
            for (const char* name : {"colorWindowAddLinearButton", "colorWindowAddCircleButton",
                     "colorWindowAddPolygonButton", "colorWindowAddCurveButton",
                     "colorWindowAddGradientButton", "colorWindowDeleteButton", "colorWindowResetButton"})
                require(window_page->findChild<QPushButton*>(name),
                        "Window must expose its add, Delete and reset buttons.");
            auto* window_list = window_page->findChild<QWidget*>("colorWindowList");
            auto* window_delete = window_page->findChild<QPushButton*>("colorWindowDeleteButton");
            require(window_list && window_list->property("rowCount").toInt() == 5 &&
                        window_list->property("selectedRow").toInt() == -1 && !window_delete->isEnabled(),
                    "Window must start with five rows, none selected, and Delete disabled.");
            const std::pair<const char*, double> window_defaults[] = {
                {"colorWindowSize", 50.0}, {"colorWindowAspect", 50.0}, {"colorWindowPan", 50.0},
                {"colorWindowTilt", 50.0}, {"colorWindowRotate", 0.0}, {"colorWindowOpacity", 100.0},
                {"colorWindowSoft1", 2.07}, {"colorWindowSoft2", 50.0}, {"colorWindowSoft3", 50.0},
                {"colorWindowSoft4", 50.0}, {"colorWindowInside", 50.0}, {"colorWindowOutside", 50.0},
            };
            for (const auto& [name, value] : window_defaults) {
                auto* box = window_page->findChild<QDoubleSpinBox*>(name);
                require(box && qFuzzyCompare(box->value() + 1.0, value + 1.0) && !box->isEnabled(),
                        "Window fields must start at their documented defaults and disabled.");
            }
            auto window_rows = [&] { return window_list->findChildren<QFrame*>("colorWindowRow"); };
            require(window_rows().size() == 5, "Window must list five sample rows.");
            const auto window_press = [&](QWidget* target) {
                QMouseEvent press(QEvent::MouseButtonPress, QPointF(4, 4), QPointF(4, 4), Qt::LeftButton,
                    Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(target, &press);
            };
            window_press(window_rows().at(1));
            require(window_list->property("selectedRow").toInt() == 1 &&
                        window_page->findChild<QDoubleSpinBox*>("colorWindowSize")->isEnabled() &&
                        window_delete->isEnabled() &&
                        window_rows().at(1)->findChild<QPushButton*>("colorWindowRowEnableButton")->isEnabled() &&
                        !window_rows().at(0)->findChild<QPushButton*>("colorWindowRowEnableButton")->isEnabled(),
                    "Selecting a Window row must enable its toggles, the fields and Delete.");
            window_page->findChild<QPushButton*>("colorWindowAddCircleButton")->click();
            require(window_list->property("rowCount").toInt() == 6 &&
                        window_list->property("selectedRow").toInt() == 5,
                    "Adding a window must append and select a row.");
            window_delete->click();
            require(window_list->property("rowCount").toInt() == 5 &&
                        window_list->property("selectedRow").toInt() == -1 && !window_delete->isEnabled() &&
                        !window_page->findChild<QDoubleSpinBox*>("colorWindowSize")->isEnabled(),
                    "Delete must remove the selected row and disable the fields.");
            window_page->findChild<QPushButton*>("colorWindowAddGradientButton")->click();
            window_page->findChild<QDoubleSpinBox*>("colorWindowOpacity")->setValue(40.0);
            window_page->findChild<QPushButton*>("colorWindowResetButton")->click();
            require(window_list->property("rowCount").toInt() == 5 &&
                        window_list->property("selectedRow").toInt() == -1 &&
                        qFuzzyCompare(window_page->findChild<QDoubleSpinBox*>("colorWindowOpacity")->value(), 100.0),
                    "The Window page reset must restore the five rows, no selection and the defaults.");
            // Sizing dock, page 6: Tracker - Window (visual stage).
            workspace.findChild<QPushButton*>("colorToolButton12")->click();
            auto* tracker_page = workspace.findChild<QWidget*>("colorTrackerPage");
            require(tracker_page && sizing_pages->currentWidget()->isAncestorOf(tracker_page) &&
                        !sizing_pages->currentWidget()->isAncestorOf(window_page),
                    "Sizing dock button 6 must open the Tracker - Window page.");
            for (const char* name : {"colorTrackerWindowModeButton", "colorTrackerThreeDModeButton",
                     "colorTrackerFxModeButton", "colorTrackerResetButton", "colorTrackerToStartButton",
                     "colorTrackerTrackBackwardButton", "colorTrackerStopButton", "colorTrackerLoopButton",
                     "colorTrackerTrackForwardButton", "colorTrackerToEndButton", "colorTrackerClipButton",
                     "colorTrackerFrameButton", "colorTrackerRegionButton", "colorTrackerPointsButton"})
                require(tracker_page->findChild<QPushButton*>(name),
                        "Tracker must expose its mode, transport, scope and tool buttons.");
            for (const char* name : {"colorTrackerPan", "colorTrackerTilt", "colorTrackerZoom",
                     "colorTrackerRotate", "colorTrackerThreeD"})
                require(tracker_page->findChild<QCheckBox*>(name) &&
                            tracker_page->findChild<QCheckBox*>(name)->isChecked(),
                        "Tracker channel toggles must start on.");
            auto* tracker_interactive = tracker_page->findChild<QCheckBox*>("colorTrackerInteractiveMode");
            auto* tracker_region = tracker_page->findChild<QPushButton*>("colorTrackerRegionButton");
            auto* tracker_method = tracker_page->findChild<QComboBox*>("colorTrackerMethod");
            require(tracker_interactive && !tracker_interactive->isChecked() && !tracker_region->isEnabled() &&
                        !tracker_page->findChild<QPushButton*>("colorTrackerPointsButton")->isEnabled() &&
                        tracker_page->findChild<QPushButton*>("colorTrackerClipButton")->isChecked() &&
                        tracker_page->findChild<QPushButton*>("colorTrackerWindowModeButton")->isChecked() &&
                        tracker_method && tracker_method->currentIndex() == 0,
                    "Tracker must start with Interactive Mode off, its tools disabled and Clip selected.");
            tracker_interactive->setChecked(true);
            require(tracker_region->isEnabled() &&
                        tracker_page->findChild<QPushButton*>("colorTrackerPointsButton")->isEnabled(),
                    "Interactive Mode must enable the tracker tools.");
            tracker_page->findChild<QPushButton*>("colorTrackerFrameButton")->click();
            require(!tracker_page->findChild<QPushButton*>("colorTrackerClipButton")->isChecked(),
                    "Choosing Frame must deselect Clip.");
            tracker_page->findChild<QCheckBox*>("colorTrackerZoom")->setChecked(false);
            // The playhead follows clicks on the timeline: 8 px margin on both sides.
            auto* tracker_graph = tracker_page->findChild<QWidget*>("colorTrackerGraph");
            require(tracker_graph && qFuzzyIsNull(tracker_graph->property("playhead").toDouble()),
                    "The Tracker playhead must start at the beginning.");
            tracker_graph->resize(400, 200);
            const QPointF playhead_at(8.0 + (400.0 - 16.0) * 0.5, 60.0);
            QMouseEvent playhead_press(QEvent::MouseButtonPress, playhead_at, playhead_at, Qt::LeftButton,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(tracker_graph, &playhead_press);
            require(std::abs(tracker_graph->property("playhead").toDouble() - 0.5) < 0.01,
                    "Clicking the Tracker timeline must move the playhead.");
            tracker_page->findChild<QSlider*>("colorTrackerVerticalSlider")->setValue(40);
            tracker_page->findChild<QPushButton*>("colorTrackerLoopButton")->setChecked(true);
            tracker_method->setCurrentIndex(1);
            tracker_page->findChild<QPushButton*>("colorTrackerResetButton")->click();
            require(qFuzzyIsNull(tracker_graph->property("playhead").toDouble()) &&
                        tracker_page->findChild<QCheckBox*>("colorTrackerZoom")->isChecked() &&
                        !tracker_interactive->isChecked() && !tracker_region->isEnabled() &&
                        tracker_page->findChild<QPushButton*>("colorTrackerClipButton")->isChecked() &&
                        !tracker_page->findChild<QPushButton*>("colorTrackerLoopButton")->isChecked() &&
                        tracker_method->currentIndex() == 0 &&
                        tracker_page->findChild<QSlider*>("colorTrackerVerticalSlider")->value() == 0,
                    "The Tracker page reset must restore every control and the playhead.");
            // Sizing dock, page 7: Smart Mask (visual stage).
            workspace.findChild<QPushButton*>("colorToolButton13")->click();
            auto* mask_page = workspace.findChild<QWidget*>("colorSmartMaskPage");
            require(mask_page && sizing_pages->currentWidget()->isAncestorOf(mask_page) &&
                        !sizing_pages->currentWidget()->isAncestorOf(tracker_page),
                    "Sizing dock button 7 must open the Smart Mask page.");
            for (const char* name : {"colorSmartMaskSetInButton", "colorSmartMaskSetOutButton",
                     "colorSmartMaskToStartButton", "colorSmartMaskTrackBackwardButton",
                     "colorSmartMaskStopButton", "colorSmartMaskLoopButton",
                     "colorSmartMaskTrackForwardButton", "colorSmartMaskToEndButton",
                     "colorSmartMaskViewButton", "colorSmartMaskOverlayButton",
                     "colorSmartMaskSettingsButton", "colorSmartMaskResetButton",
                     "colorSmartMaskQualityResetButton", "colorSmartMaskQualityFaster",
                     "colorSmartMaskQualityBetter"})
                require(mask_page->findChild<QPushButton*>(name),
                        "Smart Mask must expose its toolbar, quality and reset buttons.");
            require(mask_page->findChild<QPushButton*>("colorSmartMaskTool1")->isChecked() &&
                        mask_page->findChild<QPushButton*>("colorSmartMaskTool1")->isEnabled() &&
                        mask_page->findChild<QPushButton*>("colorSmartMaskTool2")->isEnabled() &&
                        !mask_page->findChild<QPushButton*>("colorSmartMaskTool3")->isEnabled() &&
                        !mask_page->findChild<QPushButton*>("colorSmartMaskTool4")->isEnabled() &&
                        !mask_page->findChild<QPushButton*>("colorSmartMaskTool5")->isEnabled(),
                    "Smart Mask must start with the point tools on and the stroke tools disabled.");
            const std::pair<const char*, double> mask_defaults[] = {
                {"colorSmartMaskSmartRefine", 30.0}, {"colorSmartMaskRadius", 0.0},
                {"colorSmartMaskIterations", 1.0}, {"colorSmartMaskSmoothing", 0.0},
                {"colorSmartMaskDenoise", 0.0}, {"colorSmartMaskBlurRadius", 0.0},
                {"colorSmartMaskInOutRatio", 0.0}, {"colorSmartMaskCleanBlack", 0.0},
                {"colorSmartMaskBlackClip", 0.0}, {"colorSmartMaskCleanWhite", 0.0},
                {"colorSmartMaskWhiteClip", 100.0}, {"colorSmartMaskPostFilter", 0.0},
            };
            for (const auto& [name, value] : mask_defaults) {
                auto* box = mask_page->findChild<QDoubleSpinBox*>(name);
                require(box && qFuzzyCompare(box->value() + 1.0, value + 1.0),
                        "Smart Mask refinement fields must start at their documented defaults.");
            }
            require(mask_page->findChild<QComboBox*>("colorSmartMaskMode")->currentText() == "Shrink" &&
                        mask_page->findChild<QComboBox*>("colorSmartMaskShape")->currentText() == "Circle" &&
                        mask_page->findChild<QPushButton*>("colorSmartMaskQualityFaster")->isChecked(),
                    "Smart Mask must start with Shrink, Circle and the Faster quality.");
            auto* mask_refine = mask_page->findChild<QDoubleSpinBox*>("colorSmartMaskSmartRefine");
            auto* mask_refine_slider = mask_page->findChild<QSlider*>("colorSmartMaskSmartRefineSlider");
            require(!mask_refine->isEnabled() && !mask_refine_slider->isEnabled(),
                    "Smart Refine must be disabled while the quality is Faster.");
            mask_page->findChild<QPushButton*>("colorSmartMaskQualityBetter")->click();
            require(mask_refine->isEnabled() && mask_refine_slider->isEnabled(),
                    "Smart Refine must enable once the quality is Better.");
            auto* mask_white_clip = mask_page->findChild<QDoubleSpinBox*>("colorSmartMaskWhiteClip");
            auto* mask_white_clip_slider = mask_page->findChild<QSlider*>("colorSmartMaskWhiteClipSlider");
            mask_white_clip->setValue(75.0);
            require(mask_white_clip_slider->value() == 750, "Typing a Smart Mask value must move its slider.");
            mask_white_clip_slider->setValue(400);
            require(qFuzzyCompare(mask_white_clip->value(), 40.0),
                    "Moving a Smart Mask slider must update its value.");
            // The playhead follows clicks on the timeline: 8 px margin on both sides.
            auto* mask_timeline = mask_page->findChild<QWidget*>("colorSmartMaskTimeline");
            require(mask_timeline && qFuzzyIsNull(mask_timeline->property("playhead").toDouble()),
                    "The Smart Mask playhead must start at the beginning.");
            mask_timeline->resize(400, 200);
            const QPointF mask_playhead_at(8.0 + (400.0 - 16.0) * 0.25, 60.0);
            QMouseEvent mask_press(QEvent::MouseButtonPress, mask_playhead_at, mask_playhead_at,
                Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(mask_timeline, &mask_press);
            require(std::abs(mask_timeline->property("playhead").toDouble() - 0.25) < 0.01,
                    "Clicking the Smart Mask timeline must move the playhead.");
            mask_page->findChild<QPushButton*>("colorSmartMaskTool2")->click();
            mask_page->findChild<QComboBox*>("colorSmartMaskMode")->setCurrentIndex(1);
            mask_page->findChild<QPushButton*>("colorSmartMaskQualityResetButton")->click();
            require(mask_page->findChild<QPushButton*>("colorSmartMaskQualityFaster")->isChecked() &&
                        !mask_refine->isEnabled() && qFuzzyCompare(mask_white_clip->value(), 100.0) &&
                        mask_page->findChild<QPushButton*>("colorSmartMaskTool1")->isChecked() &&
                        mask_page->findChild<QComboBox*>("colorSmartMaskMode")->currentIndex() == 0 &&
                        qFuzzyIsNull(mask_timeline->property("playhead").toDouble()),
                    "The Smart Mask reset must restore the quality, fields, tool and playhead.");
            // Sizing dock, page 8: Blur - Blur (visual stage).
            workspace.findChild<QPushButton*>("colorToolButton14")->click();
            auto* blur_page = workspace.findChild<QWidget*>("colorBlurPage");
            require(blur_page && sizing_pages->currentWidget()->isAncestorOf(blur_page) &&
                        !sizing_pages->currentWidget()->isAncestorOf(mask_page),
                    "Sizing dock button 8 must open the Blur - Blur page.");
            for (const char* name : {"colorBlurBlurModeButton", "colorBlurSharpenModeButton",
                     "colorBlurMistModeButton", "colorBlurResetButton", "colorBlurRadiusLink",
                     "colorBlurHVRatioLink", "colorBlurScalingLink", "colorBlurRadiusResetButton",
                     "colorBlurHVRatioResetButton", "colorBlurScalingResetButton"})
                require(blur_page->findChild<QPushButton*>(name),
                        "Blur must expose its mode, link and reset buttons.");
            require(blur_page->findChild<QPushButton*>("colorBlurBlurModeButton")->isChecked() &&
                        blur_page->findChild<QPushButton*>("colorBlurRadiusLink")->isChecked() &&
                        blur_page->findChild<QPushButton*>("colorBlurHVRatioLink")->isChecked() &&
                        !blur_page->findChild<QPushButton*>("colorBlurScalingLink")->isChecked() &&
                        !blur_page->findChild<QPushButton*>("colorBlurScalingLink")->isEnabled(),
                    "Blur must start with Radius and H/V Ratio linked and Scaling's link disabled.");
            for (const char* group : {"Radius", "HVRatio", "Scaling"}) {
                const double expected = QLatin1String(group) == QLatin1String("Scaling") ? 0.25 : 0.5;
                for (const char* channel : {"R", "G", "B"}) {
                    const QString stem = QStringLiteral("colorBlur") + QLatin1String(group) + QLatin1String(channel);
                    auto* box = blur_page->findChild<QDoubleSpinBox*>(stem + "Value");
                    require(box && blur_page->findChild<QSlider*>(stem + "Slider") &&
                                qFuzzyCompare(box->value(), expected),
                            "Each Blur group must start with its documented value on every channel.");
                }
            }
            require(!blur_page->findChild<QDoubleSpinBox*>("colorBlurCoringSoftness")->isEnabled() &&
                        !blur_page->findChild<QDoubleSpinBox*>("colorBlurLevel")->isEnabled() &&
                        !blur_page->findChild<QDoubleSpinBox*>("colorBlurMix")->isEnabled() &&
                        qFuzzyIsNull(blur_page->findChild<QDoubleSpinBox*>("colorBlurCoringSoftness")->value()) &&
                        qFuzzyCompare(blur_page->findChild<QDoubleSpinBox*>("colorBlurMix")->value(), 100.0),
                    "The Blur footer fields must be disabled with their documented defaults.");
            auto* blur_radius_r = blur_page->findChild<QDoubleSpinBox*>("colorBlurRadiusRValue");
            auto* blur_radius_g = blur_page->findChild<QDoubleSpinBox*>("colorBlurRadiusGValue");
            auto* blur_radius_b = blur_page->findChild<QDoubleSpinBox*>("colorBlurRadiusBValue");
            auto* blur_radius_slider = blur_page->findChild<QSlider*>("colorBlurRadiusBSlider");
            blur_radius_r->setValue(0.8);
            require(qFuzzyCompare(blur_radius_g->value(), 0.8) && qFuzzyCompare(blur_radius_b->value(), 0.8) &&
                        blur_radius_slider->value() == 80,
                    "While linked, changing one Blur channel must move all three and their bars.");
            auto* blur_scaling_r = blur_page->findChild<QDoubleSpinBox*>("colorBlurScalingRValue");
            auto* blur_scaling_g = blur_page->findChild<QDoubleSpinBox*>("colorBlurScalingGValue");
            blur_scaling_r->setValue(0.6);
            require(qFuzzyCompare(blur_scaling_g->value(), 0.25),
                    "An unlinked Blur group must keep its channels independent.");
            blur_page->findChild<QPushButton*>("colorBlurRadiusLink")->click();
            blur_radius_r->setValue(0.2);
            require(qFuzzyCompare(blur_radius_g->value(), 0.8),
                    "Turning a Blur link off must make its channels independent.");
            blur_page->findChild<QPushButton*>("colorBlurRadiusResetButton")->click();
            require(qFuzzyCompare(blur_radius_r->value(), 0.5) && qFuzzyCompare(blur_radius_b->value(), 0.5) &&
                        blur_page->findChild<QPushButton*>("colorBlurRadiusLink")->isChecked() &&
                        qFuzzyCompare(blur_scaling_r->value(), 0.6),
                    "A Blur group reset must restore only that group and turn its link on.");
            blur_page->findChild<QPushButton*>("colorBlurSharpenModeButton")->click();
            blur_page->findChild<QPushButton*>("colorBlurResetButton")->click();
            require(qFuzzyCompare(blur_scaling_r->value(), 0.25) &&
                        blur_page->findChild<QPushButton*>("colorBlurBlurModeButton")->isChecked(),
                    "The Blur page reset must restore every group and the Blur kind.");
            // Sizing dock, page 9: Key (visual stage).
            workspace.findChild<QPushButton*>("colorToolButton15")->click();
            auto* key_page = workspace.findChild<QWidget*>("colorKeyPage");
            require(key_page && sizing_pages->currentWidget()->isAncestorOf(key_page) &&
                        !sizing_pages->currentWidget()->isAncestorOf(blur_page),
                    "Sizing dock button 9 must open the Key page.");
            require(key_page->findChild<QWidget*>("colorKeyPreview") &&
                        key_page->findChild<QPushButton*>("colorKeyResetButton") &&
                        key_page->findChild<QLabel*>("colorKeyNodeLabel"),
                    "Key must expose its matte preview, node label and reset button.");
            const std::pair<const char*, double> key_defaults[] = {
                {"colorKeyInputGain", 1.0}, {"colorKeyInputOffset", 0.0},
                {"colorKeyInputBlurRadius", 0.0}, {"colorKeyInputBlurHV", 0.0},
                {"colorKeyOutputGain", 1.0}, {"colorKeyOutputOffset", 0.0},
                {"colorKeyQualifierGain", 1.0}, {"colorKeyQualifierOffset", 0.0},
            };
            for (const auto& [name, value] : key_defaults) {
                auto* box = key_page->findChild<QDoubleSpinBox*>(name);
                require(box && qFuzzyCompare(box->value() + 1.0, value + 1.0),
                        "Key fields must start at their documented defaults.");
            }
            const std::pair<const char*, bool> key_toggles[] = {
                {"colorKeyInputEnable", true}, {"colorKeyInputView", true}, {"colorKeyOutputEnable", false},
                {"colorKeyQualifierEnable", false}, {"colorKeyQualifierView", true},
            };
            for (const auto& [name, checked] : key_toggles) {
                auto* toggle = key_page->findChild<QPushButton*>(name);
                require(toggle && toggle->isChecked() == checked,
                        "Key toggles must start at their documented states.");
            }
            key_page->findChild<QDoubleSpinBox*>("colorKeyInputGain")->setValue(2.5);
            key_page->findChild<QDoubleSpinBox*>("colorKeyOutputOffset")->setValue(-0.25);
            key_page->findChild<QPushButton*>("colorKeyOutputEnable")->click();
            key_page->findChild<QPushButton*>("colorKeyInputEnable")->click();
            require(qFuzzyCompare(key_page->findChild<QDoubleSpinBox*>("colorKeyInputGain")->value(), 2.5) &&
                        key_page->findChild<QPushButton*>("colorKeyOutputEnable")->isChecked() &&
                        !key_page->findChild<QPushButton*>("colorKeyInputEnable")->isChecked(),
                    "Key fields and toggles must hold the values set on them.");
            key_page->findChild<QPushButton*>("colorKeyResetButton")->click();
            require(qFuzzyCompare(key_page->findChild<QDoubleSpinBox*>("colorKeyInputGain")->value(), 1.0) &&
                        qFuzzyIsNull(key_page->findChild<QDoubleSpinBox*>("colorKeyOutputOffset")->value()) &&
                        !key_page->findChild<QPushButton*>("colorKeyOutputEnable")->isChecked() &&
                        key_page->findChild<QPushButton*>("colorKeyInputEnable")->isChecked(),
                    "The Key reset must restore every field and toggle.");
            // Sizing dock, page 10: Sizing - Input Sizing (visual stage).
            workspace.findChild<QPushButton*>("colorToolButton16")->click();
            auto* input_page = workspace.findChild<QWidget*>("colorInputSizingPage");
            require(input_page && sizing_pages->currentWidget()->isAncestorOf(input_page) &&
                        !sizing_pages->currentWidget()->isAncestorOf(key_page),
                    "Sizing dock button 10 must open the Sizing - Input Sizing page.");
            for (int i = 1; i <= 5; ++i)
                require(input_page->findChild<QPushButton*>(
                            QStringLiteral("colorInputSizingModeButton%1").arg(i)),
                        "Input Sizing must show its five sizing-mode buttons.");
            require(input_page->findChild<QPushButton*>("colorInputSizingModeButton2")->isChecked() &&
                        input_page->findChild<QPushButton*>("colorInputSizingResetButton") &&
                        input_page->findChild<QPushButton*>("colorInputSizingSizingResetButton") &&
                        input_page->findChild<QPushButton*>("colorInputSizingBlankingResetButton"),
                    "Input Sizing must start on its second mode with its reset buttons present.");
            const std::pair<const char*, double> input_defaults[] = {
                {"colorInputSizingPan", 0.0}, {"colorInputSizingTilt", 0.0}, {"colorInputSizingZoom", 1.0},
                {"colorInputSizingRotate", 0.0}, {"colorInputSizingWidth", 1.0},
                {"colorInputSizingHeight", 1.0}, {"colorInputSizingPitch", 0.0}, {"colorInputSizingYaw", 0.0},
            };
            for (const auto& [name, value] : input_defaults) {
                auto* box = input_page->findChild<QDoubleSpinBox*>(name);
                require(box && qFuzzyCompare(box->value() + 1.0, value + 1.0) && box->isEnabled(),
                        "Input Sizing fields must start at their documented defaults.");
            }
            auto* flip_h = input_page->findChild<QPushButton*>("colorInputSizingFlipHorizontal");
            auto* flip_v = input_page->findChild<QPushButton*>("colorInputSizingFlipVertical");
            require(flip_h && flip_v && !flip_h->isChecked() && !flip_v->isChecked(),
                    "Input Sizing flips must start off.");
            for (const char* name : {"colorInputSizingBlankingTop", "colorInputSizingBlankingRight",
                     "colorInputSizingBlankingBottom", "colorInputSizingBlankingLeft"})
                require(input_page->findChild<QDoubleSpinBox*>(name) &&
                            !input_page->findChild<QDoubleSpinBox*>(name)->isEnabled(),
                        "The Blanking fields must be present and disabled.");
            require(input_page->findChild<QCheckBox*>("colorInputSizingBlankingSmooth") &&
                        !input_page->findChild<QCheckBox*>("colorInputSizingBlankingSmooth")->isEnabled(),
                    "The Blanking Smooth checkbox must be present and disabled.");
            auto* input_zoom = input_page->findChild<QDoubleSpinBox*>("colorInputSizingZoom");
            auto* input_pan = input_page->findChild<QDoubleSpinBox*>("colorInputSizingPan");
            input_zoom->setValue(2.5);
            input_pan->setValue(-1.25);
            flip_h->click();
            require(qFuzzyCompare(input_zoom->value(), 2.5) && flip_h->isChecked(),
                    "Input Sizing fields and flips must hold the values set on them.");
            input_page->findChild<QPushButton*>("colorInputSizingModeButton4")->click();
            input_page->findChild<QPushButton*>("colorInputSizingSizingResetButton")->click();
            require(qFuzzyCompare(input_zoom->value(), 1.0) && qFuzzyIsNull(input_pan->value()) &&
                        !flip_h->isChecked() &&
                        input_page->findChild<QPushButton*>("colorInputSizingModeButton4")->isChecked(),
                    "The Sizing reset must restore its fields and flips but not the mode.");
            input_zoom->setValue(3.0);
            input_page->findChild<QPushButton*>("colorInputSizingResetButton")->click();
            require(qFuzzyCompare(input_zoom->value(), 1.0) &&
                        input_page->findChild<QPushButton*>("colorInputSizingModeButton2")->isChecked(),
                    "The Input Sizing page reset must restore every control and the mode.");
            // Keyframes dock, page 1: Keyframes (visual stage).
            auto* keyframes_pages = workspace.findChild<QStackedWidget*>("colorKeyframesPages");
            workspace.findChild<QPushButton*>("colorToolButton17")->click();
            auto* keyframes_page = workspace.findChild<QWidget*>("colorKeyframesPage");
            require(keyframes_pages && keyframes_page &&
                        keyframes_pages->currentWidget()->isAncestorOf(keyframes_page),
                    "Keyframes dock button 1 must open the Keyframes page.");
            require(keyframes_page->findChild<QComboBox*>("colorKeyframesFilter") &&
                        keyframes_page->findChild<QComboBox*>("colorKeyframesFilter")->currentText() == "All" &&
                        keyframes_page->findChild<QSlider*>("colorKeyframesZoomSlider") &&
                        keyframes_page->findChild<QSlider*>("colorKeyframesZoomSlider")->value() == 0 &&
                        keyframes_page->findChild<QPushButton*>("colorKeyframesCollapseButton") &&
                        keyframes_page->findChild<QLabel*>("colorKeyframesTrackMaster"),
                    "Keyframes must expose its filter, zoom slider, collapse button and Master row.");
            require(keyframes_page->findChild<QLabel*>("colorKeyframesTimecode")->text() == "00:00:00:00",
                    "The Keyframes timecode must start at zero.");
            for (const char* track : {"Corrector", "Sizing"}) {
                const QString stem = QStringLiteral("colorKeyframesTrack") + QLatin1String(track);
                require(keyframes_page->findChild<QPushButton*>(stem + "Enable") &&
                            keyframes_page->findChild<QPushButton*>(stem + "Enable")->isChecked() &&
                            keyframes_page->findChild<QPushButton*>(stem + "Lock") &&
                            !keyframes_page->findChild<QPushButton*>(stem + "Lock")->isChecked() &&
                            keyframes_page->findChild<QPushButton*>(stem + "Diamond") &&
                            keyframes_page->findChild<QPushButton*>(stem + "Expand") &&
                            !keyframes_page->findChild<QPushButton*>(stem + "Expand")->isChecked(),
                        "Each Keyframes track must have its enable (on), lock (off), diamond and expander.");
            }
            auto* keyframes_graph = keyframes_page->findChild<QWidget*>("colorKeyframesGraph");
            require(keyframes_graph && keyframes_graph->property("keyframeCountCorrector").toInt() == 1 &&
                        keyframes_graph->property("keyframeCountSizing").toInt() == 1 &&
                        keyframes_graph->property("keyframeCountMaster").toInt() == 1 &&
                        qFuzzyIsNull(keyframes_graph->property("playhead").toDouble()),
                    "Keyframes must start with one sample keyframe per track and the playhead at zero.");
            keyframes_graph->resize(400, 200);
            const QPointF keyframes_at(400.0 * 0.5, 60.0);
            QMouseEvent keyframes_press(QEvent::MouseButtonPress, keyframes_at, keyframes_at, Qt::LeftButton,
                Qt::LeftButton, Qt::NoModifier);
            QApplication::sendEvent(keyframes_graph, &keyframes_press);
            require(std::abs(keyframes_graph->property("playhead").toDouble() - 0.5) < 0.01 &&
                        keyframes_page->findChild<QLabel*>("colorKeyframesTimecode")->text().startsWith("00:00:19:"),
                    "Clicking the Keyframes timeline must move the playhead and update the timecode.");
            keyframes_page->findChild<QPushButton*>("colorKeyframesTrackSizingDiamond")->click();
            require(keyframes_graph->property("keyframeCountSizing").toInt() == 2 &&
                        keyframes_graph->property("keyframeCountCorrector").toInt() == 1 &&
                        keyframes_graph->property("keyframeCountMaster").toInt() == 2,
                    "The diamond must add a keyframe at the playhead; Master must show the union.");
            keyframes_page->findChild<QPushButton*>("colorKeyframesTrackSizingDiamond")->click();
            require(keyframes_graph->property("keyframeCountSizing").toInt() == 2,
                    "A second diamond click at the same position must not duplicate the keyframe.");
            keyframes_page->findChild<QPushButton*>("colorKeyframesTrackCorrectorDiamond")->click();
            require(keyframes_graph->property("keyframeCountMaster").toInt() == 2 &&
                        keyframes_graph->property("keyframeCountCorrector").toInt() == 2,
                    "Master must merge keyframes that share a position.");
            keyframes_page->findChild<QPushButton*>("colorKeyframesTrackSizingLock")->click();
            keyframes_page->findChild<QPushButton*>("colorKeyframesTrackCorrectorExpand")->click();
            keyframes_page->findChild<QComboBox*>("colorKeyframesFilter")->setCurrentIndex(2);
            keyframes_page->findChild<QSlider*>("colorKeyframesZoomSlider")->setValue(40);
            // The page has no reset button; the dock's page change restores nothing, so
            // the controls keep what was set on them.
            require(keyframes_page->findChild<QPushButton*>("colorKeyframesTrackSizingLock")->isChecked() &&
                        keyframes_page->findChild<QPushButton*>("colorKeyframesTrackCorrectorExpand")->isChecked() &&
                        keyframes_page->findChild<QComboBox*>("colorKeyframesFilter")->currentIndex() == 2,
                    "Keyframes controls must hold the values set on them.");
            // Keyframes dock, page 2: Scopes (visual stage).
            workspace.findChild<QPushButton*>("colorToolButton18")->click();
            auto* scopes_page = workspace.findChild<QWidget*>("colorScopesPage");
            require(scopes_page && keyframes_pages->currentWidget()->isAncestorOf(scopes_page) &&
                        !keyframes_pages->currentWidget()->isAncestorOf(keyframes_page),
                    "Keyframes dock button 2 must open the Scopes page.");
            for (const char* name : {"colorScopesSettingsButton", "colorScopesExpandButton",
                     "colorScopesMoreButton"})
                require(scopes_page->findChild<QPushButton*>(name),
                        "Scopes must expose its settings, expand and more buttons.");
            auto* scopes_mode = scopes_page->findChild<QComboBox*>("colorScopesMode");
            auto* scopes_graph = scopes_page->findChild<QWidget*>("colorScopesGraph");
            require(scopes_mode && scopes_graph && scopes_mode->currentText() == "Parade" &&
                        scopes_mode->count() == 4 && scopes_graph->property("mode").toInt() == 1,
                    "Scopes must start on Parade with four scope kinds.");
            scopes_mode->setCurrentIndex(3);
            require(scopes_graph->property("mode").toInt() == 3 &&
                        scopes_mode->currentText() == "Vectorscope",
                    "Choosing a scope kind must switch the graticule.");
            scopes_mode->setCurrentIndex(0);
            require(scopes_graph->property("mode").toInt() == 0, "Waveform must be selectable.");
            workspace.findChild<QPushButton*>("colorToolButton1")->click();
        }
        auto* capture_button = workspace.findChild<QPushButton*>("colorCaptureStillButton");
        auto* rename_button = workspace.findChild<QPushButton*>("colorRenameStillButton");
        auto* remove_button = workspace.findChild<QPushButton*>("colorRemoveStillButton");
        auto* gallery_list = workspace.findChild<QListWidget*>("colorGalleryStills");
        require(capture_button && rename_button && remove_button && gallery_list &&
                    gallery_list->count() == 1 && !gallery_list->item(0)->icon().isNull(),
                "Color Gallery must show captured stills and its actions.");
        capture_button->click();
        require(capture_requests == 1, "Color Gallery did not request a current-frame capture.");
        gallery_list->setCurrentRow(0);
        QTimer::singleShot(0, [] {
            for (auto* widget : QApplication::topLevelWidgets()) {
                if (auto* dialog = qobject_cast<QInputDialog*>(widget)) {
                    dialog->setTextValue(QStringLiteral("Warm Look"));
                    dialog->accept();
                }
            }
        });
        rename_button->click();
        require(renamed_id == QStringLiteral("still-a") &&
                    renamed_value == QStringLiteral("Warm Look"),
                "Color Gallery rename did not mixer_preserve the selected still identity.");
        remove_button->click();
        require(removed_id == QStringLiteral("still-a"),
                "Color Gallery removal did not target the selected still.");
        auto scope_curves_graph = color::makePassthroughGraph();
        const auto upstream_grayscale = creative_suite::effects::makeDefaultInstance(
            "video.grayscale");
        require(color::insertEffectAfter(scope_curves_graph, 1, upstream_grayscale),
                "Could not create the upstream Curves preview correction.");
        const auto upstream_node = std::find_if(scope_curves_graph.nodes.begin(),
            scope_curves_graph.nodes.end(), [](const color::Node& node) {
                return node.effect && node.effect->id == "video.grayscale";
            });
        require(upstream_node != scope_curves_graph.nodes.end() &&
                    color::insertCurvesAfter(scope_curves_graph, upstream_node->id),
                "Could not create the scope matte-preview Curves fixture.");
        const auto scope_curves_node = std::find_if(scope_curves_graph.nodes.begin(),
            scope_curves_graph.nodes.end(), [](const color::Node& node) {
                return node.type == color::NodeType::Curves;
            });
        require(scope_curves_node != scope_curves_graph.nodes.end(),
                "The scope matte-preview Curves node is missing.");
        const auto scope_curves_id = scope_curves_node->id;
        auto scope_qualifier = scope_curves_node->curves->parameters.qualifier;
        scope_qualifier.enabled = true;
        require(color::setCurvesQualifier(scope_curves_graph, scope_curves_id,
                    scope_qualifier), "Could not enable the scope matte preview qualifier.");
        workspace.setClips({{7, QStringLiteral("Shot A"), thumbnail}}, 7,
            scope_curves_graph);
        const QPoint curves_canvas_position = nodes->mapFromScene(QPointF(
            scope_curves_node->x + 77.0, scope_curves_node->y + 20.0));
        QTest::mouseClick(nodes->viewport(), Qt::LeftButton, Qt::NoModifier,
            curves_canvas_position);
        qputenv("CREATIVE_SUITE_DISABLE_GPU_PREVIEW", "1");
        auto* viewer = new PreviewWidget(&workspace);
        auto frame = std::make_shared<media::VideoFrame>();
        frame->width = 2; frame->height = 2; frame->stride = 8;
        frame->rgba_pixels = {255, 0, 0, 255, 0, 255, 0, 255,
                              0, 0, 255, 255, 255, 255, 255, 255};
        viewer->setFrame(frame);
        workspace.setPreviewWidget(viewer);
        std::vector<std::pair<quint64, QString>> gallery_reference_requests;
        workspace.setGalleryReferenceRequestHandler(
            [&](quint64 request_id, const QString& still_id) {
                gallery_reference_requests.emplace_back(request_id, still_id);
            });
        const ui::ColorWorkspace::StillItem still_a{
            QStringLiteral("still-a"), QStringLiteral("Still 1"), QStringLiteral("Shot A"),
            42, thumbnail, true};
        const ui::ColorWorkspace::StillItem still_b{
            QStringLiteral("still-b"), QStringLiteral("Still 2"), QStringLiteral("Shot A"),
            84, thumbnail, true};
        const ui::ColorWorkspace::StillItem missing_still{
            QStringLiteral("still-missing"), QStringLiteral("Missing"),
            QStringLiteral("Shot A"), 126, {}, false};
        workspace.setGalleryStills({still_a, still_b}, 1);
        gallery_list->setCurrentRow(0);
        require(gallery_reference_requests.size() == 1 &&
                    gallery_reference_requests.back().second == QStringLiteral("still-a"),
                "Selecting a Gallery still must request its reference image.");
        const auto stale_reference_request = gallery_reference_requests.back().first;
        gallery_list->setCurrentRow(1);
        require(gallery_reference_requests.size() == 2 &&
                    gallery_reference_requests.back().second == QStringLiteral("still-b"),
                "Selecting another Gallery still must replace the pending reference request.");
        auto reference = QImage(8, 4, QImage::Format_RGBA8888);
        reference.fill(QColor(15, 35, 220));
        const auto active_reference_request = gallery_reference_requests.back().first;
        workspace.acceptGalleryReferenceImage(stale_reference_request,
            QStringLiteral("still-a"), reference);
        auto* comparison_mode = workspace.findChild<QComboBox*>("colorGalleryComparisonMode");
        auto* wipe_position = workspace.findChild<QSlider*>("colorGalleryWipePosition");
        auto* reference_label = workspace.findChild<QLabel*>("colorGalleryReferenceName");
        require(comparison_mode && wipe_position && reference_label &&
                    comparison_mode->currentData().toInt() == 0 &&
                    !comparison_mode->isEnabled(),
                "A stale reference response enabled comparison for the wrong still.");
        const auto viewer_payload_before_comparison = viewer->currentPayload();
        const auto edits_before_comparison = edits;
        workspace.acceptGalleryReferenceImage(active_reference_request,
            QStringLiteral("still-b"), reference);
        require(comparison_mode->isEnabled() && comparison_mode->currentData().toInt() == 0 &&
                    reference_label->text().contains(QStringLiteral("Still 2")),
                "A selected still must load as a reference while comparison remains Off.");
        workspace.setActive(true);
        comparison_mode->setCurrentIndex(1);
        require(comparison_mode->currentData().toInt() == 1 && !wipe_position->isEnabled(),
                "Split-screen comparison should not enable the wipe control.");
        comparison_mode->setCurrentIndex(2);
        require(comparison_mode->currentData().toInt() == 2 && wipe_position->isEnabled() &&
                    wipe_position->value() == 50,
                "Wipe comparison must start at the accessible center position.");
        wipe_position->setValue(0);
        wipe_position->setValue(100);
        require(viewer->currentPayload().timeline_frame ==
                    viewer_payload_before_comparison.timeline_frame &&
                    viewer->currentPayload().composition_revision ==
                    viewer_payload_before_comparison.composition_revision &&
                    viewer->currentPayload().playback_generation ==
                    viewer_payload_before_comparison.playback_generation &&
                    viewer->currentPayload().rgba == viewer_payload_before_comparison.rgba &&
                    edits == edits_before_comparison,
                "Gallery comparison must not change the Viewer frame or Color project history.");
        workspace.setActive(false);
        workspace.setActive(true);
        require(comparison_mode->currentData().toInt() == 2 && wipe_position->isEnabled(),
                "Returning to Color must restore its temporary comparison mode.");

        workspace.setGalleryStills({still_a, still_b, missing_still}, 1);
        gallery_list->setCurrentRow(2);
        require(comparison_mode->currentData().toInt() == 0 &&
                    !comparison_mode->isEnabled() && !wipe_position->isEnabled() &&
                    reference_label->text().contains(QStringLiteral("unavailable"),
                        Qt::CaseInsensitive),
                "An unavailable Gallery still must disable comparison and return to Off.");
        workspace.setGalleryStills({still_a, still_b}, 1);
        require(comparison_mode->currentData().toInt() == 0 &&
                    !comparison_mode->isEnabled() &&
                    reference_label->text().contains(QStringLiteral("No comparison")),
                "Removing the active Gallery still must clear its reference and reset Off.");
        gallery_list->setCurrentRow(0);
        require(gallery_reference_requests.size() == 3,
                "Selecting a replacement still must issue a new async request.");
        const auto prior_project_request = gallery_reference_requests.back().first;
        workspace.setGalleryStills({still_a}, 2);
        workspace.acceptGalleryReferenceImage(prior_project_request,
            QStringLiteral("still-a"), reference);
        require(comparison_mode->currentData().toInt() == 0 &&
                    !comparison_mode->isEnabled() &&
                    reference_label->text().contains(QStringLiteral("No comparison")),
                "A response from the previous project must be discarded.");
        workspace.setGalleryStills({}, 2);

        const std::vector<creative_suite::effects::EffectInstance> saved_effects{
            creative_suite::effects::makeDefaultInstance("video.brightness")};
        auto saved_comparison_graph = color::fromEffectStack(saved_effects);
        grade_versions = {
            {"saved-grade-a", "Reference Grade", saved_comparison_graph},
            {"saved-grade-b", "Second Grade", version_graph}};
        workspace.setClips({{7, QStringLiteral("Shot A"), thumbnail, grade_versions}},
            7, graph);
        versions_list = workspace.findChild<QListWidget*>("colorGradeVersions");
        auto* compare_version = workspace.findChild<QPushButton*>(
            "colorCompareGradeVersionButton");
        require(versions_list && compare_version && versions_list->count() == 2 &&
                    !compare_version->isEnabled(),
                "Compare Version must be a separate action enabled by list selection.");
        std::vector<std::optional<playback::PlaybackColorGradeComparisonRequest>>
            grade_comparison_requests;
        workspace.setGradeComparisonRequestHandler([&](auto request) {
            grade_comparison_requests.push_back(std::move(request));
        });
        versions_list->setCurrentRow(0);
        require(compare_version->isEnabled() && grade_comparison_requests.empty() &&
                    comparison_mode->currentData().toInt() == 0,
                "Selecting a saved grade must enable Compare Version without starting comparison.");
        const auto graph_before_grade_comparison = graph;
        const auto edits_before_grade_comparison = edits;
        const auto viewer_before_grade_comparison = viewer->currentPayload();
        workspace.setActive(true);
        compare_version->click();
        require(comparison_mode->currentData().toInt() == 1 &&
                    !grade_comparison_requests.empty() &&
                    grade_comparison_requests.back().has_value() &&
                    grade_comparison_requests.back()->clip_id == 7 &&
                    grade_comparison_requests.back()->version_id == "saved-grade-a" &&
                    grade_comparison_requests.back()->graph == saved_comparison_graph &&
                    reference_label->text().contains(QStringLiteral("Reference Grade")),
                "Compare Version must request the selected snapshot and start Split-screen from Off.");
        const auto first_grade_request = grade_comparison_requests.back()->request_id;
        auto alternate_frame = viewer_before_grade_comparison;
        alternate_frame.rgba = std::make_shared<const media::VideoFrame>(*frame);
        workspace.acceptGradeComparisonFrame(first_grade_request + 1, 2, 7,
            "saved-grade-a", alternate_frame);
        require(comparison_mode->currentData().toInt() == 1,
                "A stale alternate frame must not change comparison mode.");
        workspace.acceptGradeComparisonFrame(first_grade_request, 2, 7,
            "saved-grade-a", alternate_frame);
        workspace.acceptGradeComparisonFrame(first_grade_request, 2, 7,
            "saved-grade-a", {});
        require(comparison_mode->currentData().toInt() == 1 && graph ==
                    graph_before_grade_comparison && edits == edits_before_grade_comparison &&
                    viewer->currentPayload().timeline_frame ==
                        viewer_before_grade_comparison.timeline_frame &&
                    viewer->currentPayload().composition_revision ==
                        viewer_before_grade_comparison.composition_revision &&
                    viewer->currentPayload().rgba == viewer_before_grade_comparison.rgba,
                "Compare requests and missing alternate frames must leave the active grade and Viewer state unchanged.");
        comparison_mode->setCurrentIndex(0);
        require(!grade_comparison_requests.back().has_value(),
                "Off must stop alternate grade renders.");
        comparison_mode->setCurrentIndex(2);
        wipe_position->setValue(37);
        compare_version->click();
        require(comparison_mode->currentData().toInt() == 2 &&
                    wipe_position->value() == 37 &&
                    grade_comparison_requests.back().has_value() &&
                    grade_comparison_requests.back()->request_id != first_grade_request,
                "Compare Version must mixer_preserve Wipe position and reject responses from the prior request.");
        const auto grade_request_before_gallery =
            grade_comparison_requests.back()->request_id;
        workspace.setGalleryStills({still_a}, 2);
        gallery_list->setCurrentRow(0);
        require(!grade_comparison_requests.back().has_value() &&
                    gallery_reference_requests.back().second == QStringLiteral("still-a"),
                "Selecting a Gallery still must replace the active saved-grade reference.");
        const auto gallery_reentry_request = gallery_reference_requests.back().first;
        workspace.acceptGalleryReferenceImage(gallery_reentry_request,
            QStringLiteral("still-a"), reference);
        require(comparison_mode->currentData().toInt() == 2 &&
                    wipe_position->value() == 37,
                "Switching reference sources must mixer_preserve the selected Wipe mode and divider.");
        versions_list->setCurrentRow(0);
        compare_version->click();
        require(comparison_mode->currentData().toInt() == 2 &&
                    grade_comparison_requests.back().has_value() &&
                    grade_comparison_requests.back()->request_id != grade_request_before_gallery,
                "Compare Version must switch back from Gallery while preserving Wipe.");
        workspace.setActive(false);
        require(!grade_comparison_requests.back().has_value(),
                "Leaving Color must stop temporary alternate renders.");
        workspace.setActive(true);
        require(grade_comparison_requests.back().has_value(),
                "Re-entering Color must restore the selected saved-grade comparison.");
        workspace.setClips({{8, QStringLiteral("Shot B"), thumbnail, grade_versions}},
            8, graph);
        require(comparison_mode->currentData().toInt() == 0 &&
                    !grade_comparison_requests.back().has_value(),
                "Changing clips must clear the active grade reference and reset Off.");
        workspace.setClips({{8, QStringLiteral("Shot B"), thumbnail, grade_versions}},
            8, graph);
        versions_list->setCurrentRow(0);
        compare_version->click();
        require(comparison_mode->currentData().toInt() == 1,
                "A new clip's version can be compared independently.");
        workspace.setClips({{8, QStringLiteral("Shot B"), thumbnail, {}}}, 8, graph);
        require(comparison_mode->currentData().toInt() == 0 &&
                    !grade_comparison_requests.back().has_value(),
                "Removing the active saved version must clear the reference and reset Off.");
        workspace.setClips({{8, QStringLiteral("Shot B"), thumbnail, grade_versions}},
            8, graph);
        versions_list->setCurrentRow(0);
        compare_version->click();
        workspace.setGalleryStills({still_a}, 3);
        require(comparison_mode->currentData().toInt() == 0 &&
                    !grade_comparison_requests.back().has_value(),
                "Changing projects must clear the active saved-grade reference.");
        workspace.setActive(false);
        workspace.setClips({{7, QStringLiteral("Shot A"), thumbnail}}, 7,
            scope_curves_graph);
        QTest::mouseClick(nodes->viewport(), Qt::LeftButton, Qt::NoModifier,
            curves_canvas_position);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        int curves_input_requests = 0;
        quint64 last_curves_input_request = 0;
        media::VideoFramePtr captured_curves_input;
        workspace.setColorInputFrameRequestHandler(
            [&](quint64 request_id, timeline::ClipId clip_id, color::NodeId node_id,
                qint64 timeline_frame, quint64 delivery_epoch, quint64 revision,
                quint64 playback_generation, color::Graph request_graph) {
                require(clip_id == 7 && node_id == scope_curves_id,
                    "Curves input requests must identify the active clip and node.");
                ++curves_input_requests;
                last_curves_input_request = request_id;
                auto evaluated = color::evaluateInput(request_graph, node_id, *frame);
                if (!evaluated) return;
                captured_curves_input =
                    std::make_shared<const media::VideoFrame>(*evaluated);
                workspace.acceptColorInputFrame(request_id, clip_id, node_id,
                    captured_curves_input, timeline_frame, delivery_epoch, revision,
                    playback_generation);
            });
        workspace.setActive(true);
        auto* waveform = workspace.findChild<QLabel*>("colorWaveform");
        require(waveform != nullptr, "Color waveform display must exist.");
        QElapsedTimer timeout;
        timeout.start();
        while (waveform->pixmap().isNull() && timeout.elapsed() < 2000) {
            application.processEvents();
            QThread::msleep(10);
        }
        require(!waveform->pixmap().isNull() && waveform->pixmap().size() == QSize(480, 256),
                "Waveform must process the composed RGBA frame asynchronously at bounded resolution.");
        auto* matte_preview = workspace.findChild<QLabel*>("colorQualifierMattePreview");
        require(matte_preview && !matte_preview->pixmap().isNull() &&
                    curves_input_requests > 0 && captured_curves_input &&
                    captured_curves_input->rgba_pixels[0] ==
                        captured_curves_input->rgba_pixels[1] &&
                    captured_curves_input->rgba_pixels[1] ==
                        captured_curves_input->rgba_pixels[2],
                "The Curves qualifier preview must use its upstream corrected input frame.");
        const auto matte_before_stale_input = matte_preview->pixmap().toImage();
        auto later_frame = std::make_shared<media::VideoFrame>(*frame);
        later_frame->rgba_pixels[0] = 17;
        viewer->setFrame(later_frame);
        workspace.acceptColorInputFrame(last_curves_input_request, 7, scope_curves_id,
            captured_curves_input, 0, 0, 0, 0);
        require(matte_preview->pixmap().toImage() == matte_before_stale_input,
                "A delayed Curves input response must be ignored after the Viewer frame changes.");
        viewer->setFrame(frame);
        const std::array<const char*, 5> matte_controls{
            "colorQualifier_denoise_percent", "colorQualifier_clean_black_percent",
            "colorQualifier_clean_white_percent", "colorQualifier_in_out_percent",
            "colorQualifier_blur_radius_pixels"};
        for (const auto* name : matte_controls)
            require(workspace.findChild<QDoubleSpinBox*>(name) != nullptr,
                "Curves must expose each persisted matte refinement control.");
        auto* preview_mode = workspace.findChild<QComboBox*>("colorQualifierPreviewMode");
        require(preview_mode && preview_mode->count() == 2 &&
                    preview_mode->currentData().toString() == QStringLiteral("matte"),
                "The Inspector matte preview must default to grayscale Matte mode.");
        const auto viewer_frame = viewer->currentPayload();
        const auto preview_edits_before = edits;
        auto* denoise = workspace.findChild<QDoubleSpinBox*>("colorQualifier_denoise_percent");
        denoise->setValue(30);
        require(QMetaObject::invokeMethod(denoise, "editingFinished"),
                "Denoise edit must commit through the graph history handler.");
        const auto* refined_node = color::findNode(graph, scope_curves_id);
        require(refined_node && refined_node->curves->parameters.qualifier.denoise_percent == 30,
                "Denoise must update the selected Curves node qualifier.");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        preview_mode = workspace.findChild<QComboBox*>("colorQualifierPreviewMode");
        matte_preview = workspace.findChild<QLabel*>("colorQualifierMattePreview");
        const auto matte_image = matte_preview->pixmap().toImage();
        preview_mode->setCurrentIndex(preview_mode->findData(QStringLiteral("highlight")));
        const auto highlight_image = matte_preview->pixmap().toImage();
        require(preview_mode->currentData().toString() == QStringLiteral("highlight"),
                "The Inspector preview mode selector must enter Highlight mode.");
        require(!matte_image.isNull() && !highlight_image.isNull() && matte_image != highlight_image,
                "Highlight mode must change the Inspector matte preview.");
        require(edits == preview_edits_before + 1,
                "Switching Matte/Highlight modes must not create a project history edit.");
        require(viewer->currentPayload().rgba == viewer_frame.rgba &&
                    viewer->currentPayload().gpu == viewer_frame.gpu,
                "The Inspector matte preview must leave Viewer pixels unchanged.");
        auto* scope_mode = workspace.findChild<QComboBox*>("colorScopeMode");
        const std::array<QString, 4> scope_modes{QStringLiteral("waveform"),
            QStringLiteral("parade"), QStringLiteral("vectorscope"),
            QStringLiteral("histogram")};
        const auto edits_before_scopes = edits;
        require(scope_mode && scope_mode->count() == 4,
                "Scopes must provide Waveform, Parade, Vectorscope and RGB Histogram.");
        for (const auto& mode : scope_modes) {
            const auto item = scope_mode->findData(mode);
            require(item >= 0, "A documented Color scope mode is missing.");
            scope_mode->setCurrentIndex(item);
            timeout.restart();
            while (waveform->pixmap().isNull() && timeout.elapsed() < 2000) {
                application.processEvents();
                QThread::msleep(10);
            }
            require(!waveform->pixmap().isNull() &&
                        waveform->pixmap().size() == QSize(480, 256),
                    "Each Color scope mode must render the current composed frame.");
        }
        require(edits == edits_before_scopes,
                "Changing the scope display mode must not edit the project.");
        viewer->setFrame(rendering::PreviewFramePayload{});
        timeout.restart();
        while (waveform->text() != QStringLiteral("No frame") && timeout.elapsed() < 500) {
            application.processEvents();
            QThread::msleep(10);
        }
        require(waveform->text() == QStringLiteral("No frame"),
                "Scopes must show their empty state when no composed frame is available.");
        workspace.setActive(false);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
