#include "workspaces/color/ui/color_workspace.h"
#include "workspaces/color/ui/color_node_canvas.h"
#include "ui/preview/preview_widget.h"

#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QMetaObject>
#include <QPushButton>
#include <QSlider>
#include <QThread>
#include <QTimer>

#include <creative_suite/effects/effects.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    try {
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
        QImage thumbnail(4, 3, QImage::Format_RGBA8888);
        thumbnail.fill(Qt::red);
        workspace.setClips({{7, QStringLiteral("Shot A"), thumbnail}}, 7, graph);
        auto* clip_strip = workspace.findChild<QListWidget*>("colorClipStrip");
        require(clip_strip && !clip_strip->item(0)->icon().isNull(),
                "Color clip strip must display available media thumbnails.");
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
                "Color Gallery rename did not preserve the selected still identity.");
        remove_button->click();
        require(removed_id == QStringLiteral("still-a"),
                "Color Gallery removal did not target the selected still.");
        auto* viewer = new PreviewWidget(&workspace);
        auto frame = std::make_shared<media::VideoFrame>();
        frame->width = 2; frame->height = 2; frame->stride = 8;
        frame->rgba_pixels = {255, 0, 0, 255, 0, 255, 0, 255,
                              0, 0, 255, 255, 255, 255, 255, 255};
        viewer->setFrame(frame);
        workspace.setPreviewWidget(viewer);
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
