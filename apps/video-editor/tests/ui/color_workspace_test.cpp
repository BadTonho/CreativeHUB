#include "workspaces/color/ui/color_workspace.h"
#include "workspaces/color/ui/color_node_canvas.h"
#include "workspaces/color/evaluation/color_graph_evaluator.h"
#include "ui/preview/preview_widget.h"

#include <QApplication>
#include <QColor>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QMetaObject>
#include <QPushButton>
#include <QSlider>
#include <QThread>
#include <QTimer>
#include <QTest>

#include <creative_suite/effects/effects.h>

#include <algorithm>
#include <array>
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
                    reference_label->text().contains(QStringLiteral("No Gallery")),
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
                    reference_label->text().contains(QStringLiteral("No Gallery")),
                "A response from the previous project must be discarded.");
        workspace.setGalleryStills({}, 2);
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
