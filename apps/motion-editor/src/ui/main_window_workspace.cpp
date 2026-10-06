#include "main_window.h"
#include "main_window_support.h"

#include "composition_viewer.h"
#include "inspector/inspector_widget.h"
#include "media_pool_widget.h"
#include "property_curve_editor.h"
#include "preview_renderer.h"
#include "timeline_navigator.h"
#include "workspace/motion_workspace.h"

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/media_importer.h>
#include <creative_suite/media/media_library.h>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <utility>
#include <vector>

namespace motion::ui {
using creative_suite::animation::CubicBezierEasing;
using creative_suite::animation::TransformProperty;
using detail::pathFromQString;

void MainWindow::createWorkspace()
{
    const bool was_maximized = isMaximized();
    const bool was_full_screen = isFullScreen();
    const auto previous_geometry = geometry();

    media_pool_ = new MediaPoolWidget(this);
    media_pool_->setMinimumWidth(220);
    media_pool_->setImportRequestedHandler([this] { openMedia(); });
    media_pool_->setSelectionChangedHandler([this] { updateMediaDetails(); });
    media_pool_->setContentChangedHandler([this] { updateDocumentState(); });
    viewer_ = new CompositionViewer(this);
    viewer_->setObjectName(QStringLiteral("motion-composition-viewer"));
    inspector_ = new InspectorWidget(this);
    connect(inspector_, &InspectorWidget::layerContentEdited,
            this, &MainWindow::editSelectedLayerContent);
    connect(inspector_, &InspectorWidget::layerContentEditFinished,
            this, &MainWindow::finishPendingContentEdit);
    connect(inspector_, &InspectorWidget::layerColorSelectionRequested,
            this, &MainWindow::chooseSelectedLayerColor);
    connect(inspector_, &InspectorWidget::transformValueEdited, this,
            [this](int index) { editSelectedLayerTransform(static_cast<std::size_t>(index)); });
    connect(inspector_, &InspectorWidget::transformEditFinished,
            this, &MainWindow::finishPendingTransformEdit);
    connect(inspector_, &InspectorWidget::keyframeToggleRequested, this,
            [this](int index) { toggleSelectedLayerKeyframe(static_cast<std::size_t>(index)); });
    connect(inspector_, &InspectorWidget::effectAddRequested,
            this, &MainWindow::addLayerEffect);
    connect(inspector_, &InspectorWidget::effectMoveRequested,
            this, &MainWindow::moveSelectedEffect);
    connect(inspector_, &InspectorWidget::effectRemoveRequested,
            this, &MainWindow::removeSelectedEffect);
    connect(inspector_, &InspectorWidget::effectRowSelected,
            this, &MainWindow::selectEffectRow);
    connect(inspector_, &InspectorWidget::effectParametersEdited,
            this, &MainWindow::editSelectedEffectParameters);
    connect(inspector_, &InspectorWidget::effectParametersEditFinished,
            this, &MainWindow::finishPendingEffectEdit);
    connect(inspector_, &InspectorWidget::effectReorderRequested, this,
            [this](qulonglong source, int insertion) {
                reorderEffectsFromList(static_cast<std::size_t>(source), insertion);
            });
    connect(inspector_, &InspectorWidget::effectEnabledChanged, this,
            [this](int row, bool enabled) {
                if (!document_ || selected_layer_id_ == 0 || row < 0) return;
                const auto found = std::find_if(document_->layers().begin(),
                    document_->layers().end(), [this](const auto& layer) {
                        return layer.id == selected_layer_id_;
                    });
                if (found == document_->layers().end() ||
                    static_cast<std::size_t>(row) >= found->effects.size()) return;
                auto effects = found->effects;
                std::visit([enabled](auto& effect) { effect.enabled = enabled; },
                           effects[static_cast<std::size_t>(row)]);
                applySelectedEffectStack(std::move(effects));
            });
    timeline_ = new TimelineNavigator(this);
    timeline_->setShortcutActions(
        play_pause_action_, previous_frame_action_, next_frame_action_, loop_action_,
        zoom_in_action_, zoom_out_action_);
    timeline_->setMediaDropHandler([this](const std::filesystem::path& path,
                                          std::int64_t frame,
                                          model::LayerId before) {
        handleMediaDrop(path, frame, before);
    });
    timeline_->setLayerSelectedHandler([this](model::LayerId id) { selectLayer(id); });
    timeline_->setKeyframeSelectedHandler(
        [this](model::LayerId id, TransformProperty property, std::int64_t local_frame) {
            if (!document_ || timeline_ == nullptr) return;
            const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
                [id](const auto& layer) { return layer.id == id; });
            if (found == document_->layers().end() || local_frame < 0 ||
                local_frame >= found->duration_frames) return;
            selectLayer(id);
            selectCurveSegment(id, property, local_frame);
            const auto composition_frame = found->timeline_start_frame + local_frame;
            timeline_->setCurrentFrame(composition_frame);
        });
    timeline_->setCurveSegmentSelectedHandler(
        [this](model::LayerId id, TransformProperty property, std::int64_t local_frame) {
            selectLayer(id);
            selectCurveSegment(id, property, local_frame);
        });
    timeline_->setKeyframeMoveHandler(
        [this](model::LayerId id, TransformProperty property,
               std::int64_t from_local_frame, std::int64_t to_local_frame) {
            finishPendingTransformEdit();
            if (!document_) return false;
            auto before = captureEditState();
            if (!document_ || !document_->moveLayerKeyframe(
                    id, property, from_local_frame, to_local_frame)) {
                if (statusBar() != nullptr) {
                    statusBar()->showMessage(
                        QStringLiteral("A keyframe already exists at that frame."), 4000);
                }
                return false;
            }
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            syncTransformInspector();
            requestPreview();
            return true;
        });
    timeline_->setLayerMoveHandler([this](model::LayerId id, std::int64_t frame) {
        finishPendingTransformEdit();
        if (!document_) return;
        auto before = captureEditState();
        if (document_ && document_->moveLayerInTimeline(id, frame)) {
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            requestPreview();
        }
    });
    timeline_->setLayerResizeHandler([this](model::LayerId id, std::int64_t duration) {
        finishPendingTransformEdit();
        if (!document_) return;
        auto before = captureEditState();
        if (document_ && document_->resizeLayerDuration(id, duration)) {
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            requestPreview();
        }
    });
    timeline_->setLayerReorderHandler([this](model::LayerId id, std::size_t front_index) {
        finishPendingTransformEdit();
        if (!document_ || front_index >= document_->layers().size()) return;
        auto before = captureEditState();
        const auto model_index = document_->layers().size() - 1 - front_index;
        if (document_->moveLayer(id, model_index)) {
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            requestPreview();
        }
    });
    timeline_->setLayerVisibilityHandler([this](model::LayerId id, bool visible) {
        finishPendingTransformEdit();
        if (!document_) return;
        auto before = captureEditState();
        if (document_ && document_->setLayerVisible(id, visible)) {
            (void)recordCompositionEdit(std::move(before));
            updateDocumentState();
            refreshTimeline();
            if (selected_layer_id_ == id) syncTransformInspector();
            requestPreview();
        }
    });
    timeline_->setLayerRemoveHandler([this](model::LayerId id) {
        finishPendingTransformEdit();
        if (!document_) return;
        auto before = captureEditState();
        if (!document_->removeLayer(id)) return;
        (void)recordCompositionEdit(std::move(before));
        updateDocumentState();
        if (selected_layer_id_ == id) selected_layer_id_ = 0;
        refreshTimeline();
        syncTransformInspector();
        requestPreview();
    });
    connect(timeline_, &TimelineNavigator::currentFrameChanged,
            this, [this] {
                finishPendingTransformEdit();
                syncTransformInspector();
                requestPreview(timeline_ != nullptr && timeline_->isPlaying());
            });
    curve_editor_panel_ = new QWidget(this);
    curve_editor_panel_->setObjectName(QStringLiteral("motion-graph-editor-panel"));
    curve_editor_panel_->setMinimumHeight(168);
    auto* curve_panel_layout = new QVBoxLayout(curve_editor_panel_);
    curve_panel_layout->setContentsMargins(6, 4, 6, 4);
    curve_panel_layout->setSpacing(3);
    auto* curve_panel_controls = new QHBoxLayout();
    auto* curve_panel_title = new QLabel(QStringLiteral("Graph Editor"), curve_editor_panel_);
    curve_panel_title->setObjectName(QStringLiteral("motion-graph-editor-title"));
    curve_panel_controls->addWidget(curve_panel_title);
    curve_panel_controls->addStretch(1);
    curve_custom_label_ = new QLabel(QStringLiteral("Custom Bézier"), curve_editor_panel_);
    curve_custom_label_->setObjectName(QStringLiteral("motion-curve-editor-custom-status"));
    curve_custom_label_->hide();
    curve_panel_controls->addWidget(curve_custom_label_);
    curve_preset_combo_ = new QComboBox(curve_editor_panel_);
    curve_preset_combo_->setObjectName(QStringLiteral("motion-curve-editor-preset"));
    curve_preset_combo_->addItem(QStringLiteral("Linear"));
    curve_preset_combo_->addItem(QStringLiteral("Ease In"));
    curve_preset_combo_->addItem(QStringLiteral("Ease Out"));
    curve_preset_combo_->addItem(QStringLiteral("Ease In/Out"));
    curve_preset_combo_->setToolTip(
        QStringLiteral("Set interpolation for the selected keyframe segment"));
    curve_panel_controls->addWidget(curve_preset_combo_);
    curve_panel_layout->addLayout(curve_panel_controls);
    curve_editor_ = new PropertyCurveEditor(curve_editor_panel_);
    curve_editor_->setSelectionHandler([this](std::int64_t local_start_frame) {
        if (!curve_selection_) return;
        curve_selection_->segment_start_frame = local_start_frame;
        refreshCurveEditor();
    });
    curve_editor_->setEasingEditedHandler(
        [this](std::int64_t local_start_frame, CubicBezierEasing easing) {
            if (!curve_selection_) return;
            curve_selection_->segment_start_frame = local_start_frame;
            applyCurveEasing(local_start_frame, easing);
        });
    connect(curve_preset_combo_, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int index) { applyCurvePreset(index); });
    curve_panel_layout->addWidget(curve_editor_, 1);

    workspace_ = new MotionWorkspace(
        this, viewer_, media_pool_, inspector_, timeline_, curve_editor_panel_, timeline_);
    media_pool_dock_ = workspace_->mediaPoolDock();
    inspector_dock_ = workspace_->inspectorDock();
    timeline_dock_ = workspace_->timelineDock();
    graph_editor_dock_ = workspace_->graphEditorDock();
    workspace_->setGraphEditorRefreshHandler([this] { refreshCurveEditor(); });

    const auto add_panel_action = [this](QDockWidget* dock,
                                         const QString& action_object_name) {
        auto* action = dock->toggleViewAction();
        action->setObjectName(action_object_name);
        view_menu_->addAction(action);
    };
    add_panel_action(media_pool_dock_, QStringLiteral("motion-view-media-pool-action"));
    add_panel_action(inspector_dock_, QStringLiteral("motion-view-inspector-action"));
    add_panel_action(timeline_dock_, QStringLiteral("motion-view-timeline-action"));
    add_panel_action(graph_editor_dock_, QStringLiteral("motion-view-graph-editor-action"));
    connect(graph_editor_dock_->toggleViewAction(), &QAction::triggered,
            this, [this](bool visible) {
                if (visible && graph_editor_dock_ != nullptr) {
                    graph_editor_dock_->show();
                    graph_editor_dock_->raise();
                    timeline_->setGraphEditorOpen(true);
                    refreshCurveEditor();
                }
            });
    view_menu_->addSeparator();
    reset_panel_layout_action_->setEnabled(true);
    restoreWorkspaceLayout();
    empty_state_ = nullptr;
    empty_state_new_composition_button_ = nullptr;

    preview_renderer_ = std::make_unique<PreviewRenderer>(this,
        [this](std::uint64_t generation,
               PreviewRequestMode mode,
               std::uint64_t cancellation_generation,
               creative_suite::media::RgbaFramePtr frame) {
            const bool may_present = preview_renderer_ &&
                preview_renderer_->canPresentResult(
                    generation, mode, cancellation_generation) &&
                (mode != PreviewRequestMode::Playback ||
                 (timeline_ != nullptr && timeline_->isPlaying()));
            if (may_present && viewer_ != nullptr) {
                if (frame != nullptr) {
                    viewer_->setRenderedFrame(std::move(frame), generation);
                } else {
                    diagnostics::PerformanceMetrics::instance().discardRequest(generation);
                }
            } else {
                diagnostics::PerformanceMetrics::instance().recordStaleResult(generation);
            }
        });
    if (!was_maximized && !was_full_screen) setGeometry(previous_geometry);
}
void MainWindow::restoreWorkspaceLayout()
{
    if (workspace_ != nullptr) workspace_->restoreLayout();
}
void MainWindow::saveWorkspaceLayout()
{
    if (workspace_ != nullptr) workspace_->saveLayout();
}
void MainWindow::restoreDefaultPanelLayout()
{
    if (workspace_ != nullptr) workspace_->restoreDefaultLayout();
}
void MainWindow::openMedia()
{
    if (!document_.has_value() || media_pool_ == nullptr) return;
    QFileDialog dialog(this, QStringLiteral("Import Media"));
    dialog.setObjectName(QStringLiteral("motion-import-media-dialog"));
    dialog.setFileMode(QFileDialog::ExistingFiles);
    dialog.setNameFilters({
        QStringLiteral("Supported media (*.avi *.mkv *.mov *.mp4 *.mxf *.webm *.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff)"),
        QStringLiteral("Video files (*.avi *.mkv *.mov *.mp4 *.mxf *.webm)"),
        QStringLiteral("Image files (*.png *.jpg *.jpeg *.bmp *.webp *.tif *.tiff)"),
        QStringLiteral("All files (*)"),
    });
    if (dialog.exec() != QDialog::Accepted) return;
    const auto files = dialog.selectedFiles();
    std::vector<std::filesystem::path> paths;
    paths.reserve(static_cast<std::size_t>(files.size()));
    for (const auto& file : files) paths.push_back(pathFromQString(file));
    media_pool_->importFiles(std::move(paths));
}
void MainWindow::updateMediaDetails()
{
    if (media_pool_ == nullptr || inspector_ == nullptr) return;
    inspector_->setMedia(media_pool_->selectedMedia());
    inspector_->selectMediaTab();
}
void MainWindow::refreshTimeline()
{
    if (timeline_ == nullptr || !document_) return;
    timeline_->setLayers(document_->layers());
    timeline_->setSelectedLayerId(selected_layer_id_);
    refreshCurveEditor();
}

} // namespace motion::ui
