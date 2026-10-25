#include "main_window/main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "ui/preview/preview_widget.h"
#include "project/project_file.h"
#include "settings/settings_dialog.h"
#include "settings/shortcut_manager.h"
#include "timeline/timeline_widget.h"
#include "ui/effects/effects_list_widget.h"
#include "ui/functions/function_palette.h"
#include "ui/media_browser/media_browser_list_widget.h"
#include "ui/timeline/timeline_end_buttons.h"
#include "ui/workspace/workspace_host.h"
#include "ui/workspace/workspace_transition_controller.h"
#include "workspaces/fusion/ui/fusion_workspace.h"
#include "workspaces/audio/ui/audio_workspace.h"
#include "workspaces/color/ui/color_workspace.h"
#include "workspaces/render/ui/render_workspace.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QCloseEvent>
#include <QEvent>
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLayout>
#include <QLayoutItem>
#include <QAbstractItemView>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMetaObject>
#include <QMessageBox>
#include <QPushButton>
#include <QPointer>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QScopeGuard>
#include <QSettings>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QSlider>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QStyle>
#include <QTextEdit>
#include <QUrl>
#include <QWidget>
#include <QUuid>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <limits>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>


using namespace main_window_detail;

namespace {

class PreviewAspectLayout final : public QLayout {
public:
    explicit PreviewAspectLayout(QWidget* parent) : QLayout(parent) {
        setContentsMargins(0, 0, 0, 0);
        setSpacing(0);
    }

    void addItem(QLayoutItem* item) override { item_ = item; }

    int count() const override { return item_ != nullptr ? 1 : 0; }

    QLayoutItem* itemAt(int index) const override {
        return index == 0 ? item_ : nullptr;
    }

    QLayoutItem* takeAt(int index) override {
        if (index != 0) return nullptr;
        auto* item = item_;
        item_ = nullptr;
        return item;
    }

    QSize sizeHint() const override { return QSize(640, 360); }

    QSize minimumSize() const override { return QSize(0, 0); }

    void setGeometry(const QRect& geometry) override {
        QLayout::setGeometry(geometry);
        if (item_ == nullptr) return;

        auto canvas = geometry;
        const auto* preview = qobject_cast<const PreviewWidget*>(item_->widget());
        const auto* host = parentWidget();
        if (preview == nullptr || host == nullptr ||
            !host->property("previewAspectConstrained").toBool()) {
            item_->setGeometry(canvas);
            return;
        }

        const auto payload = preview->currentPayload();
        if (!payload.valid() || payload.width() <= 0 || payload.height() <= 0 ||
            canvas.width() <= 0 || canvas.height() <= 0) {
            item_->setGeometry(canvas);
            return;
        }

        const double aspect = static_cast<double>(payload.width()) /
            static_cast<double>(payload.height());
        int width = canvas.width();
        int height = qRound(static_cast<double>(width) / aspect);
        if (height > canvas.height()) {
            height = canvas.height();
            width = qRound(static_cast<double>(height) * aspect);
        }
        canvas.setRect(
            canvas.x() + (canvas.width() - width) / 2,
            canvas.y() + (canvas.height() - height) / 2,
            width,
            height);
        item_->setGeometry(canvas);
    }

private:
    QLayoutItem* item_ = nullptr;
};

void refreshMenuVisibility(QMenu* menu) {
    if (menu == nullptr) return;

    bool has_visible_content = false;
    QAction* pending_separator = nullptr;
    const auto actions = menu->actions();
    for (auto* action : actions) {
        if (action == nullptr) continue;
        if (action->menu() != nullptr) refreshMenuVisibility(action->menu());
        if (action->isSeparator()) {
            action->setVisible(false);
            if (has_visible_content) pending_separator = action;
            continue;
        }
        if (!action->isVisible()) continue;
        if (pending_separator != nullptr) {
            pending_separator->setVisible(true);
            pending_separator = nullptr;
        }
        has_visible_content = true;
    }
    menu->menuAction()->setVisible(has_visible_content);
}

} // namespace

void MainWindow::createWorkspace() {
    preview_widget_ = new PreviewWidget(this);
    preview_widget_->setObjectName("sharedPreviewWidget");
    edit_workspace_ = new ui::EditWorkspace(
        editor_session_, timeline_command_service_, preview_widget_,
        *shortcut_manager_, this,
        [this](const QString& message) {
            statusBar()->showMessage(message);
        },
        this);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::historyStateChanged,
        this,
        [this](bool can_undo, bool can_redo) {
            if (undo_action_ != nullptr) undo_action_->setEnabled(can_undo);
            if (redo_action_ != nullptr) redo_action_->setEnabled(can_redo);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::statusMessageRequested,
        this,
        [this](const QString& message) {
            statusBar()->showMessage(message);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::warningMessageRequested,
        this,
        [this](const QString& title, const QString& message) {
            QMessageBox::warning(this, title, message);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::projectDirtyStateUpdateRequested,
        this,
        &MainWindow::updateProjectDirtyState);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::playbackInvalidateRequested,
        this,
        [this](bool stop_playback) {
            if (playback_controller_ != nullptr) {
                playback_controller_->invalidate(stop_playback);
            }
            playback_is_playing_ = false;
            updatePlaybackControls();
            updatePlaybackStatus();
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::refreshPlaybackCompositionRequested,
        this,
        &MainWindow::refreshPlaybackComposition);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::renderCompositionFrameRequested,
        this,
        [this](qint64 timeline_frame, qint64 clip_frame) {
            if (playback_controller_ != nullptr) {
                playback_controller_->renderCompositionFrame(
                    timeline_frame, clip_frame);
            }
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::seekActiveClipRequested,
        this,
        [this](qint64 clip_frame) {
            if (playback_controller_ != nullptr) {
                playback_controller_->seekActiveClip(clip_frame);
            }
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::playbackAudioParametersRequested,
        this,
        &MainWindow::updatePlaybackAudioParameters);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::playbackCommandRequested,
        this,
        &MainWindow::sendPlaybackCommand);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::monitorVolumeChangedRequested,
        this,
        &MainWindow::applyMonitorVolumePercent);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineImageClipEditRequested,
        this,
        &MainWindow::editTimelineImageClip);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineMotionClipOpenRequested,
        this,
        &MainWindow::editTimelineVideoClipInMotionStudio);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineFusionClipOpenRequested,
        this,
        [this](timeline::ClipId clip_id) {
            if (editor_session_.selection().active_clip_id != clip_id) return;
            const auto location = editor_session_.timeline().locateClip(clip_id);
            if (!location.has_value()) return;
            const auto& track = editor_session_.timeline().tracks()[location->track_index];
            if (location->clip_index >= track.clips.size()) return;
            const auto kind = track.clips[location->clip_index].kind;
            if (!timeline::supportsNodeGraph(kind)) return;
            refreshFusionSelection();
            setWorkspacePage(ui::WorkspacePageId::Fusion);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineMediaDropRequested,
        this,
        &MainWindow::handleMediaDropAt);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineExternalFilesDropRequested,
        this,
        &MainWindow::handleExternalTimelineFilesDrop);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineMediaGroupDropRequested,
        this,
        &MainWindow::handleMediaGroupDropAt);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineExternalFilesGroupDropRequested,
        this,
        &MainWindow::handleExternalTimelineFilesGroupDrop);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::seekTimelineRequested,
        this,
        [this](qint64 global_frame) {
            const auto result = playback_controller_ != nullptr
                ? playback_controller_->seekTimeline(global_frame)
                : playback::PlaybackCommandResult::Unavailable;
            edit_workspace_->controller()->handleTimelineSeekResult(
                global_frame, result);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::clearPreviewRequested,
        this,
        [this](const QString& message) {
            if (preview_widget_ != nullptr) preview_widget_->clearFrame(message);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::selectMediaBrowserClipRequested,
        this,
        [this](timeline::ClipId clip_id) {
            const auto location = timeline_model_.locateClip(clip_id);
            if (!location.has_value()) return;
            const auto& clip = timeline_model_.tracks()[location->track_index]
                .clips[location->clip_index];
            populateMediaBrowser(clip.source_path);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::activateTimelineClipRequested,
        this,
        [this](timeline::ClipId clip_id,
               qint64 clip_frame,
               bool resume_playback,
               bool preserve_timeline_playhead) {
            const auto location = timeline_model_.locateClip(clip_id);
            if (!location.has_value()) return;
            activateTimelineClipAt(
                location->track_index,
                location->clip_index,
                clip_frame,
                resume_playback,
                preserve_timeline_playhead);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::showTimelineClipPreviewRequested,
        this,
        [this](timeline::ClipId clip_id) {
            const auto location = timeline_model_.locateClip(clip_id);
            if (!location.has_value()) return;
            const auto& clip = timeline_model_.tracks()[location->track_index]
                .clips[location->clip_index];
            populateMediaBrowser(clip.source_path);
            const auto media = std::find_if(
                media_items_.begin(), media_items_.end(),
                [&clip](const ImportedMedia& item) {
                    return normalizedPath(item.metadata.source_path) ==
                        normalizedPath(clip.source_path);
                });
            if (media == media_items_.end()) return;
            if (media->offline) {
                preview_widget_->clearFrame(
                    "Preview area\n\nThe selected media is offline.");
            } else if (clip.kind == timeline::ClipKind::Audio) {
                preview_widget_->clearFrame(
                    "Audio clip\n\nAudio is mixed during timeline playback.");
            } else {
                preview_widget_->setFrame(media->first_frame);
            }
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::clearMediaBrowserSelectionRequested,
        this,
        [this]() {
            if (media_list_ == nullptr) return;
            const QSignalBlocker blocker(media_list_);
            media_list_->clearSelection();
            media_list_->setCurrentRow(-1);
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineSelectionPresentationChanged,
        this,
        &MainWindow::synchronizeActiveTimelineSelection);
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::refreshPlaybackUiRequested,
        this,
        [this]() {
            updatePlaybackControls();
            updatePlaybackStatus();
        });
    connect(
        edit_workspace_->controller(),
        &ui::EditWorkspaceController::timelineEditCommitted,
        this,
        [this](const application::TimelineEditResult& result,
               bool stop_playback,
               bool refresh_composition,
               const QString& status_message) {
            applyTimelineEditResult(result, stop_playback);
            updateTimelineState();
            updatePlaybackControls();
            updatePlaybackStatus();
            if (refresh_composition) refreshPlaybackComposition();
            if (!status_message.isEmpty()) {
                statusBar()->showMessage(status_message);
            }
        });
    connect(
        preview_widget_,
        &PreviewWidget::gpuFallbackRequested,
        this,
        [this](const QString& reason, qint64 error_code) {
            logging::Context context{{"cause", reason.toUtf8().toStdString()}};
            const auto payload = preview_widget_->currentPayload();
            context.emplace_back("delivery_epoch", std::to_string(payload.delivery_epoch));
            context.emplace_back("generation", std::to_string(payload.playback_generation));
            context.emplace_back("timeline_frame", std::to_string(payload.timeline_frame));
            context.emplace_back("gpu_session", payload.gpu ? std::to_string(payload.gpu->session()) : "none");
            if (error_code != 0) {
                context.emplace_back("error_code", std::to_string(error_code));
            }
            logging::Logger::instance().log(
                logging::Level::Error,
                "rendering",
                "gpu_preview",
                reason.toUtf8().toStdString(),
                context);
            if (playback_controller_ && preview_widget_->currentPayload().gpu)
                playback_controller_->recoverPreviewFrame(preview_widget_->currentPayload());
            statusBar()->showMessage(preview_widget_->usesGpuPreview()
                ? QCoreApplication::translate("MainWindow", "Direct GPU preview unavailable; using RGBA delivery.")
                : QCoreApplication::translate("MainWindow", "GPU preview unavailable; using CPU preview."), 5000);
        });
    connect(preview_widget_, &PreviewWidget::comparisonUnavailable, this,
        [this](const QString& reason, qint64 error_code) {
            logging::Logger::instance().log(
                error_code != 0 ? logging::Level::Error : logging::Level::Warning,
                "color-gallery", "prepare_comparison_reference",
                reason.toUtf8().toStdString(),
                {{"error_code", std::to_string(error_code)}});
            statusBar()->showMessage(QCoreApplication::translate(
                "MainWindow", "Gallery comparison is unavailable; the Viewer remains unchanged."), 5000);
        });

    bins_dock_ = createDock(
        "Bins",
        "binsDock",
        createMediaBins());
    bins_dock_->installEventFilter(this);
    media_dock_ = createDock(
        "Media",
        "mediaDock",
        createMediaPanel());
    media_dock_->installEventFilter(this);
    media_pool_width_save_timer_ = new QTimer(this);
    media_pool_width_save_timer_->setSingleShot(true);
    media_pool_width_save_timer_->setInterval(120);
    connect(
        media_pool_width_save_timer_, &QTimer::timeout,
        this, &MainWindow::persistMediaPoolWidth);
    preview_side_panel_width_save_timer_ = new QTimer(this);
    preview_side_panel_width_save_timer_->setSingleShot(true);
    preview_side_panel_width_save_timer_->setInterval(150);
    connect(
        preview_side_panel_width_save_timer_, &QTimer::timeout,
        this, &MainWindow::persistPreviewSidePanelWidths);
    media_pool_expand_button_ = addDockTitleBarAction(
        media_dock_,
        DockTitleBarAction{
            "mediaPoolExpandButton",
            "Expand Media Pool to full window height",
            "Expand Media Pool to full window height",
            style()->standardIcon(QStyle::SP_TitleBarMaxButton),
            true,
            [this](bool expanded) { setMediaPoolExpanded(expanded); }});
    addDockWidget(Qt::LeftDockWidgetArea, bins_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, media_dock_);
    splitDockWidget(bins_dock_, media_dock_, Qt::Vertical);
    resizeDocks({bins_dock_, media_dock_}, {300, 700}, Qt::Vertical);

    toolbox_dock_ = createDock(
        "Toolbox",
        "toolboxDock",
        createEffectsToolbox());
    toolbox_dock_->installEventFilter(this);
    effects_dock_ = createDock(
        "Effects",
        "effectsDock",
        createEffectsPanel());
    effects_dock_->installEventFilter(this);
    favorites_dock_ = createDock(
        "Favorites",
        "favoritesDock",
        createEffectsFavorites());
    favorites_dock_->installEventFilter(this);
    toolbox_dock_->setMinimumWidth(20);
    favorites_dock_->setMinimumWidth(20);
    effects_dock_->setMinimumWidth(30);
    addDockWidget(Qt::LeftDockWidgetArea, toolbox_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, effects_dock_);
    splitDockWidget(toolbox_dock_, effects_dock_, Qt::Horizontal);
    resizeDocks({toolbox_dock_, effects_dock_}, {180, 420}, Qt::Horizontal);
    addDockWidget(Qt::LeftDockWidgetArea, favorites_dock_);
    splitDockWidget(toolbox_dock_, favorites_dock_, Qt::Vertical);
    resizeDocks({toolbox_dock_, favorites_dock_}, {300, 300}, Qt::Vertical);
    toolbox_dock_->hide();
    favorites_dock_->hide();
    effects_dock_->hide();

    populateMediaBrowser();
    updateTimelineState();
    edit_workspace_->createPanels(this);
    fusion_workspace_ = new ui::FusionWorkspace(this);
    fusion_workspace_->createPanels(this);
    auto* edit_controller = edit_workspace_->controller();
    connect(fusion_workspace_, &ui::FusionWorkspace::graphEditRequested,
        edit_controller, [this, edit_controller](timeline::ClipId clip_id,
            const fusion::nodes::NodeGraph& graph) {
            edit_controller->applyFusionNodeGraph(clip_id, graph);
            refreshFusionSelection();
        });
    connect(fusion_workspace_, &ui::FusionWorkspace::playbackPauseRequested,
        this, [this] {
            if (playback_controller_ != nullptr) playback_controller_->pause();
        });
    connect(fusion_workspace_, &ui::FusionWorkspace::previewTargetRequested,
        this, [this](timeline::ClipId clip_id, fusion::nodes::NodeId node_id) {
            if (playback_controller_ != nullptr) {
                playback_controller_->setFusionNodePreviewTarget(
                    playback::FusionNodePreviewTarget{clip_id, node_id});
            }
        });
    connect(fusion_workspace_, &ui::FusionWorkspace::previewTargetCleared,
        this, [this] {
            if (playback_controller_ != nullptr)
                playback_controller_->setFusionNodePreviewTarget(std::nullopt);
        });
    connect(fusion_workspace_,
        &ui::FusionWorkspace::playbackClipActivationRequested,
        this, [this](timeline::ClipId clip_id, std::int64_t local_frame) {
            if (playback_controller_ != nullptr) {
                static_cast<void>(playback_controller_->activateClip(
                    clip_id, local_frame, false));
            }
        });
    connect(edit_controller, &ui::EditWorkspaceController::timelineSelectionPresentationChanged,
        this, &MainWindow::refreshFusionSelection);
    connect(edit_controller, &ui::EditWorkspaceController::timelineEditCommitted,
        this, [this](const application::TimelineEditResult&,
            bool, bool, const QString&) { refreshFusionSelection(); });
    refreshFusionSelection();
    render_workspace_ = new ui::RenderWorkspace(
        [edit_controller](bool active) {
            if (edit_controller != nullptr) {
                edit_controller->setTimelineReadOnly(active);
            }
        },
        [this] { return currentProjectDocument(); },
        [this] {
            return editor_session_.timeline().frameRate().asDouble();
        },
        this);
    render_workspace_->createPanels(this);
    audio_workspace_ = new ui::AudioWorkspace(this);
    connect(audio_workspace_, &ui::AudioWorkspace::gainEditStarted,
            this, [this] {
                if (!audio_gain_batch_id_.has_value())
                    audio_gain_batch_id_ = timeline_command_service_.beginEditBatch();
            });
    connect(audio_workspace_, &ui::AudioWorkspace::gainEditFinished,
            this, [this] {
                if (!audio_gain_batch_id_.has_value()) return;
                const auto result = timeline_command_service_.finishEditBatch(
                    *audio_gain_batch_id_);
                audio_gain_batch_id_.reset();
                if (result.changed()) {
                    updateHistoryActions();
                    updateProjectDirtyState();
                }
            });
    connect(audio_workspace_, &ui::AudioWorkspace::trackMixChanged,
            this, &MainWindow::applyAudioTrackMix);
    connect(audio_workspace_, &ui::AudioWorkspace::trackAudioEffectsChanged,
            this, &MainWindow::applyAudioTrackEffects);
    connect(audio_workspace_, &ui::AudioWorkspace::busMixChanged,
            this, &MainWindow::applyAudioBusMix);
    connect(audio_workspace_, &ui::AudioWorkspace::masterMixChanged,
            this, &MainWindow::applyAudioMasterMix);
    connect(audio_workspace_, &ui::AudioWorkspace::createBusRequested,
            this, &MainWindow::createAudioBus);
    connect(audio_workspace_, &ui::AudioWorkspace::renameBusRequested,
            this, &MainWindow::renameAudioBus);
    connect(audio_workspace_, &ui::AudioWorkspace::removeBusRequested,
            this, &MainWindow::removeAudioBus);
    connect(audio_workspace_, &ui::AudioWorkspace::soloStateChanged,
            this, &MainWindow::setAudioSoloState);
    applyMonitorVolumePercent(edit_workspace_->ui().monitor_volume->value());

    auto* preview_dock_contents = new QWidget(this);
    preview_dock_contents->setObjectName("previewDockContents");
    preview_dock_contents->setMinimumSize(0, 0);
    preview_dock_contents->setSizePolicy(
        QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto* preview_dock_layout = new QVBoxLayout(preview_dock_contents);
    preview_dock_layout->setContentsMargins(0, 0, 0, 0);
    preview_dock_layout->setSpacing(0);
    if (fusion_workspace_->viewerTitle() != nullptr) {
        preview_dock_layout->addWidget(fusion_workspace_->viewerTitle());
    }
    auto* preview_canvas_area = new QWidget(preview_dock_contents);
    preview_canvas_area->setObjectName("previewCanvasArea");
    preview_canvas_area->setMinimumSize(0, 0);
    preview_canvas_area->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto* preview_canvas_layout = new PreviewAspectLayout(preview_canvas_area);
    preview_canvas_layout->addWidget(preview_widget_);
    preview_dock_layout->addWidget(preview_canvas_area, 1);
    preview_dock_ = createDock(
        "Preview",
        "previewDock",
        preview_dock_contents);
    addDockWidget(Qt::RightDockWidgetArea, preview_dock_);

    const auto workspace_buttons = ui::createTimelineEndButtons(
        edit_workspace_->ui().workspace_navigation_slot);
    workspace_buttons_container_ = workspace_buttons.container;
    edit_workspace_button_ = workspace_buttons.edit;
    fusion_workspace_button_ = workspace_buttons.fusion;
    color_workspace_button_ = workspace_buttons.color;
    audio_workspace_button_ = workspace_buttons.audio;
    render_workspace_button_ = workspace_buttons.render;
    if (auto* navigation_layout = qobject_cast<QHBoxLayout*>(
            edit_workspace_->ui().workspace_navigation_slot->layout())) {
        navigation_layout->addWidget(workspace_buttons_container_);
    }
    workspace_host_ = new ui::WorkspaceHost(
        edit_workspace_, fusion_workspace_, render_workspace_,
        preview_canvas_area, this);
    setCentralWidget(workspace_host_);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setFixedHeight(26);
    statusBar()->addPermanentWidget(
        edit_workspace_->ui().workspace_footer, 1);
    statusBar()->setVisible(true);

    inspector_dock_ = createDock(
        "Inspector",
        "inspectorDock",
        workspace_host_->inspectorPanel());
    inspector_dock_->installEventFilter(this);
    inspector_width_save_timer_ = new QTimer(this);
    inspector_width_save_timer_->setSingleShot(true);
    inspector_width_save_timer_->setInterval(120);
    connect(
        inspector_width_save_timer_, &QTimer::timeout,
        this, &MainWindow::persistInspectorWidth);
    inspector_expand_button_ = addDockTitleBarAction(
        inspector_dock_,
        DockTitleBarAction{
            "inspectorExpandButton",
            "Expand Inspector to full window height",
            "Expand Inspector to full window height",
            style()->standardIcon(QStyle::SP_TitleBarMaxButton),
            true,
            [this](bool expanded) { setInspectorExpanded(expanded); }});
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    splitDockWidget(preview_dock_, inspector_dock_, Qt::Horizontal);

    timeline_dock_ = createDock(
        "Timeline",
        "timelineDock",
        workspace_host_->lowerWorkspacePanel());
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);

    render_settings_dock_ = new QDockWidget(
        QCoreApplication::translate("RenderWorkspace", "Render Settings"),
        this);
    render_settings_dock_->setObjectName("renderSettingsDock");
    render_settings_dock_->setAllowedAreas(Qt::LeftDockWidgetArea);
    render_settings_dock_->setFeatures(QDockWidget::NoDockWidgetFeatures);
    render_settings_dock_->setMinimumWidth(0);
    render_settings_dock_->setWidget(render_workspace_->settingsPanel());
    render_settings_dock_->hide();

    workspace_transition_controller_ =
        new ui::WorkspaceTransitionController(
            workspace_host_,
            {
                bins_dock_,
                media_dock_,
                toolbox_dock_,
                favorites_dock_,
                effects_dock_,
                inspector_dock_,
                preview_dock_,
                timeline_dock_,
                render_settings_dock_},
            {
                edit_workspace_button_,
                fusion_workspace_button_,
                color_workspace_button_,
                audio_workspace_button_,
                render_workspace_button_},
            this);
    workspace_transition_controller_->setPageChangedHandler(
        [this](ui::WorkspacePageId page) { handleWorkspacePageChanged(page); });
    workspace_transition_controller_->setAudioPagePreparationHandler([this] {
        if (audio_workspace_ == nullptr || workspace_host_ == nullptr ||
            audio_workspace_->meterPage() != nullptr) return;
        audio_workspace_->createPanels(this);
        audio_workspace_->refreshTracks(timeline_model_.tracks(),
            timeline_model_.audioBuses(), timeline_model_.masterAudioMix());
        workspace_host_->setAudioPanels(
            audio_workspace_->meterPage(), audio_workspace_->mixerPanel());
    });
    workspace_transition_controller_->setColorPagePreparationHandler([this] {
        if (color_workspace_ != nullptr || workspace_host_ == nullptr) return;
        color_workspace_ = new ui::ColorWorkspace(this);
        color_workspace_->setPlaybackHandler([this](playback::PlaybackCommand command) {
            if (command == playback::PlaybackCommand::Play && playback_controller_ &&
                playback_controller_->isPlaying()) command = playback::PlaybackCommand::Pause;
            sendPlaybackCommand(command);
        });
        color_workspace_->setGradeEditBatchHandlers(
            [this] {
                if (!color_grade_edit_batch_id_.has_value())
                    color_grade_edit_batch_id_ = timeline_command_service_.beginEditBatch();
            },
            [this] {
                if (!color_grade_edit_batch_id_.has_value()) return;
                const auto result = timeline_command_service_.finishEditBatch(
                    *color_grade_edit_batch_id_);
                color_grade_edit_batch_id_.reset();
                if (result.changed()) {
                    updateHistoryActions();
                    updateProjectDirtyState();
                    updateTimelineState();
                }
            });
        color_workspace_->setScopeFrameRequestHandler(
            [this](quint64 request_id, qint64 frame, quint64 revision,
                   quint64 generation) {
                if (playback_controller_)
                    playback_controller_->requestScopeFrame(request_id, frame,
                        revision, generation);
            });
        color_workspace_->setColorInputFrameRequestHandler(
            [this](quint64 request_id, timeline::ClipId clip_id,
                   color::NodeId node_id, qint64 frame, quint64 delivery_epoch,
                   quint64 revision, quint64 generation, color::Graph graph) {
                if (playback_controller_)
                    playback_controller_->requestColorInputFrame(request_id,
                        clip_id, node_id, frame, project_generation_, delivery_epoch,
                        revision, generation,
                        std::move(graph));
            });
        color_workspace_->setPowerWindowTrackRequestHandler(
            [this](quint64 request_id, timeline::ClipId clip_id,
                   color::NodeId node_id, color::PowerWindowId window_id,
                   qint64 frame, int direction, quint64 project_generation,
                   quint64 revision, quint64 playback_generation,
                   color::Graph graph,
                   std::shared_ptr<std::atomic_bool> canceled) {
                if (playback_controller_)
                    playback_controller_->trackColorPowerWindow(request_id,
                        clip_id, node_id, window_id, frame, direction,
                        project_generation, revision, playback_generation,
                        std::move(graph), std::move(canceled));
            });
        color_workspace_->setGradeComparisonRequestHandler(
            [this](std::optional<playback::PlaybackColorGradeComparisonRequest> request) {
                if (playback_controller_)
                    playback_controller_->setColorGradeComparisonRequest(std::move(request));
            });
        color_workspace_->setCaptureStillHandler([this] { captureColorStill(); });
        color_workspace_->setRenameStillHandler([this](const QString& id, const QString& name) {
            renameColorStill(id, name);
        });
        color_workspace_->setRemoveStillHandler([this](const QString& id) {
            removeColorStill(id);
        });
        color_workspace_->setGalleryReferenceRequestHandler(
            [this](quint64 request_id, const QString& id) {
                requestColorGalleryReference(request_id, id);
            });
        color_workspace_->setClipSelectedHandler([this](timeline::ClipId id) {
            if (const auto location = timeline_model_.locateClip(id)) {
                activateTimelineClipAt(location->track_index, location->clip_index,
                                       0, false);
                refreshColorSelection();
            }
        });
        color_workspace_->setCopyGradeHandler([this](timeline::ClipId id) {
            const auto location = timeline_model_.locateClip(id);
            if (!location) return;
            const auto& clip = timeline_model_.tracks()[location->track_index]
                .clips[location->clip_index];
            if (!timeline::supportsClipEffects(clip.kind)) return;
            copied_color_grade_ = timeline::colorGraphForClip(clip);
            color_workspace_->setCanApplyCopiedGrade(true);
        });
        color_workspace_->setApplyGradeHandler([this](timeline::ClipId id) {
            if (!copied_color_grade_) return;
            try {
                const auto result = timeline_command_service_.execute(
                    application::SetClipColorGraphCommand{id, *copied_color_grade_});
                if (!result.changed()) return;
                applyTimelineEditResult(result, false);
                updateTimelineState();
                refreshPlaybackComposition();
            } catch (const std::exception& error) {
                logging::Logger::instance().log(logging::Level::Error,
                    "color", "apply_copied_grade", error.what(),
                    {{"clip_id", std::to_string(id)}});
                statusBar()->showMessage(QCoreApplication::translate(
                    "MainWindow", "Could not apply the copied color grade."));
            }
        });
        color_workspace_->setGradeVersionsChangedHandler([this](timeline::ClipId id,
            std::vector<timeline::ColorGradeVersion> versions) {
            try {
                const auto result = timeline_command_service_.execute(
                    application::SetClipColorGradeVersionsCommand{id,
                        std::move(versions)});
                if (!result.changed()) return;
                applyTimelineEditResult(result, false);
                updateTimelineState();
            } catch (const std::exception& error) {
                logging::Logger::instance().log(logging::Level::Error,
                    "color", "set_grade_versions", error.what(),
                    {{"clip_id", std::to_string(id)}});
                statusBar()->showMessage(QCoreApplication::translate(
                    "MainWindow", "Could not update grade versions."));
            }
        });
        color_workspace_->setEffectsChangedHandler([this](timeline::ClipId id,
            color::Graph graph) {
            try {
                const auto result = timeline_command_service_.execute(
                    application::SetClipColorGraphCommand{id, std::move(graph)});
                if (!result.changed()) return;
                applyTimelineEditResult(result, false);
                const bool edit_batch_active = color_grade_edit_batch_id_.has_value();
                if (!edit_batch_active) updateTimelineState();
                refreshPlaybackComposition();
            } catch (const std::exception& error) {
                logging::Logger::instance().log(logging::Level::Error,
                    "color", "set_color_graph", error.what(),
                    {{"clip_id", std::to_string(id)}});
                statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Could not update the color grade."));
            }
        });
        workspace_host_->setColorWorkspace(color_workspace_);
        refreshColorSelection();
    });
    workspace_transition_controller_->setColorPageEffectsCatalogHandler(
        [this](bool embed_in_color) {
            if (color_workspace_ == nullptr || effects_list_ == nullptr ||
                effects_catalog_layout_ == nullptr) {
                return;
            }
            if (embed_in_color) {
                effects_catalog_layout_->removeWidget(effects_list_);
                effects_list_->hide();
                color_workspace_->setEffectsCatalogWidget(effects_list_);
                color_workspace_->setEffectsPanelVisible(false);
                return;
            }
            color_workspace_->setEffectsPanelVisible(false);
            color_workspace_->setEffectsCatalogWidget(nullptr);
            effects_catalog_layout_->addWidget(effects_list_);
            effects_list_->show();
        });

    function_palette_ = new ui::FunctionPalette(this, *shortcut_manager_);
    function_palette_->setAddHandler([this](const QString& id) {
        if (!workspace_host_) return false;
        if (workspace_host_->currentPage() == ui::WorkspacePageId::Fusion)
            return fusion_workspace_ && fusion_workspace_->addCatalogNode(id);
        if (workspace_host_->currentPage() == ui::WorkspacePageId::Edit &&
            effects::actionFor(id, effects::Workspace::Edit) == effects::Action::VisualEffect)
            return edit_workspace_->controller()->addEffectToSelectedClip(id);
        return false;
    });
    connect(edit_workspace_->controller(),
            &ui::EditWorkspaceController::effectTargetAvailabilityChanged,
            this, [this](bool) { refreshEffectsContext(); });
    refreshEffectsContext();

    setWorkspacePage(ui::WorkspacePageId::Edit);
}

void MainWindow::refreshFusionSelection() {
    if (fusion_workspace_ == nullptr || edit_workspace_ == nullptr) return;
    std::vector<ui::FusionWorkspace::MediaChoice> choices;
    for (const auto& item : editor_session_.mediaLibrary().items()) {
        if (item.offline || (item.metadata.kind != media::MediaKind::Video &&
                             item.metadata.kind != media::MediaKind::Image)) continue;
        choices.push_back({
            QString::fromUtf8(item.display_name.data(),
                              static_cast<qsizetype>(item.display_name.size())),
            item.metadata.source_path,
            item.metadata.frame_rate.value_or(30.0),
            item.metadata.frame_count.value_or(0),
            item.metadata.kind == media::MediaKind::Image});
    }
    const timeline::TimelineClip* selected = nullptr;
    if (const auto location = edit_workspace_->controller()->selectedTimelineClipLocation();
        location.has_value()) {
        const auto& model = editor_session_.timeline();
        if (location->track_index < model.tracks().size() &&
            location->clip_index < model.tracks()[location->track_index].clips.size())
            selected = &model.tracks()[location->track_index].clips[location->clip_index];
    }
    fusion_workspace_->setSelection(selected, std::move(choices));
    refreshEffectsContext();
}

void MainWindow::refreshColorSelection() {
    if (color_workspace_ == nullptr) return;
    color_workspace_->setProjectGeneration(project_generation_);
    struct OrderedClip {
        std::int64_t start = 0;
        std::size_t track = 0;
        ui::ColorWorkspace::ClipItem item;
    };
    std::vector<OrderedClip> ordered;
    const auto& tracks = timeline_model_.tracks();
    for (std::size_t track_index = 0; track_index < tracks.size(); ++track_index) {
        for (const auto& clip : tracks[track_index].clips) {
            if (!timeline::supportsClipEffects(clip.kind)) continue;
            QString name = QString::fromUtf8(clip.display_name.data(),
                static_cast<qsizetype>(clip.display_name.size()));
            if (name.isEmpty()) name = QString::fromStdString(clip.source_path.filename().string());
            if (name.isEmpty()) name = QCoreApplication::translate("MainWindow", "Generated clip");
            QImage thumbnail;
            const auto media = std::find_if(media_items_.begin(), media_items_.end(),
                [&clip](const ImportedMedia& item) {
                    return normalizedPath(item.metadata.source_path) == normalizedPath(clip.source_path);
                });
            if (media != media_items_.end()) {
                const auto& frame = media->first_frame;
                if (frame.width > 0 && frame.height > 0 && frame.stride >= frame.width * 4 &&
                    frame.rgba_pixels.size() >= static_cast<std::size_t>(frame.stride) * frame.height) {
                    thumbnail = QImage(frame.rgba_pixels.data(), frame.width, frame.height,
                        frame.stride, QImage::Format_RGBA8888).scaled(
                            112, 63, Qt::KeepAspectRatio, Qt::FastTransformation);
                }
            }
            ordered.push_back({clip.timeline_start_frame, track_index,
                {clip.clip_id, std::move(name), std::move(thumbnail),
                    clip.color_grade_versions, clip.timeline_start_frame,
                    clip.timeline_duration_frames}});
        }
    }
    std::sort(ordered.begin(), ordered.end(), [](const OrderedClip& a, const OrderedClip& b) {
        return a.start < b.start || (a.start == b.start && a.track < b.track);
    });
    std::vector<ui::ColorWorkspace::ClipItem> clips;
    clips.reserve(ordered.size());
    for (auto& item : ordered) clips.push_back(std::move(item.item));
    timeline::ClipId selected_id = 0;
    color::Graph graph = color::makePassthroughGraph();
    if (active_timeline_clip_id_) {
        if (const auto location = timeline_model_.locateClip(*active_timeline_clip_id_)) {
            const auto& clip = tracks[location->track_index].clips[location->clip_index];
            if (timeline::supportsClipEffects(clip.kind)) {
                selected_id = clip.clip_id;
                graph = timeline::colorGraphForClip(clip);
            }
        }
    }
    color_workspace_->setCanApplyCopiedGrade(copied_color_grade_.has_value());
    color_workspace_->setClips(std::move(clips), selected_id, std::move(graph));

    std::vector<ui::ColorWorkspace::StillItem> stills;
    stills.reserve(editor_session_.colorGalleryStills().size());
    for (const auto& still : editor_session_.colorGalleryStills()) {
        QString source = QCoreApplication::translate("ColorWorkspace", "Clip %1")
            .arg(static_cast<qulonglong>(still.source_clip_id));
        if (const auto location = timeline_model_.locateClip(still.source_clip_id)) {
            const auto& source_clip = tracks[location->track_index].clips[location->clip_index];
            if (!source_clip.display_name.empty()) {
                source = QString::fromUtf8(source_clip.display_name.data(),
                    static_cast<qsizetype>(source_clip.display_name.size()));
            } else if (!source_clip.source_path.empty()) {
                source = QString::fromStdString(source_clip.source_path.filename().string());
            }
        }
        QImage thumbnail;
        if (!still.image_unavailable && still.image_png && !still.image_png->empty() &&
            still.image_png->size() <= static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            QByteArray encoded(reinterpret_cast<const char*>(still.image_png->data()),
                static_cast<qsizetype>(still.image_png->size()));
            QBuffer buffer(&encoded);
            if (buffer.open(QIODevice::ReadOnly)) {
                QImageReader reader(&buffer, "PNG");
                const QSize source_size = reader.size();
                if (source_size.isValid()) {
                    reader.setScaledSize(source_size.scaled(QSize(112, 63),
                        Qt::KeepAspectRatio));
                    thumbnail = reader.read();
                }
            }
        }
        const bool available = !still.image_unavailable && !thumbnail.isNull();
        stills.push_back({
            QString::fromUtf8(still.id.data(), static_cast<qsizetype>(still.id.size())),
            QString::fromUtf8(still.name.data(), static_cast<qsizetype>(still.name.size())),
            source,
            static_cast<qint64>(still.timeline_frame),
            std::move(thumbnail),
            available});
    }
    color_workspace_->setGalleryStills(std::move(stills),
        static_cast<quint64>(project_generation_));
    refreshEffectsContext();
}

void MainWindow::captureColorStill() {
    if (pending_color_still_capture_ || preview_widget_ == nullptr) return;
    const auto payload = preview_widget_->currentPayload();
    if (!payload.valid() || payload.width() <= 0 || payload.height() <= 0) return;
    const auto frame = payload.timeline_frame;
    const auto source_location = timeline_model_.topClipAt(frame);
    if (!source_location) return;
    const auto& source_clip = timeline_model_.tracks()[source_location->track_index]
        .clips[source_location->clip_index];
    auto request_id = next_color_still_request_id_++;
    if (next_color_still_request_id_ == 0) next_color_still_request_id_ = 1;
    PendingColorStillCapture capture{request_id, project_generation_, frame,
        payload.composition_revision, payload.playback_generation,
        source_clip.clip_id};
    pending_color_still_capture_ = capture;
    color_workspace_->setCapturePending(true);
    if (payload.rgba && payload.width() == editor_session_.canvasWidth() &&
        payload.height() == editor_session_.canvasHeight()) {
        encodeColorStill(capture, payload.rgba);
        return;
    }
    if (playback_controller_ != nullptr) {
        playback_controller_->captureStill(request_id, project_generation_, frame,
                                           payload.composition_revision);
    } else {
        pending_color_still_capture_.reset();
        color_workspace_->setCapturePending(false);
    }
}

void MainWindow::requestColorGalleryReference(
    quint64 request_id, const QString& still_id) {
    const auto id_utf8 = still_id.toUtf8();
    const std::string id(id_utf8.constData(), static_cast<std::size_t>(id_utf8.size()));
    const auto& stills = editor_session_.colorGalleryStills();
    const auto found = std::find_if(stills.begin(), stills.end(), [&id](const auto& still) {
        return still.id == id;
    });
    if (found == stills.end() || found->image_unavailable || !found->image_png ||
        found->image_png->empty()) {
        if (color_workspace_ != nullptr)
            color_workspace_->acceptGalleryReferenceImage(request_id, still_id, {});
        return;
    }
    if (found->image_png->size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        logging::Logger::instance().log(logging::Level::Error,
            "color-gallery", "decode_comparison_reference",
            "The Color Gallery PNG exceeds the supported in-memory image size.",
            {{"still_id", id}, {"encoded_bytes", std::to_string(found->image_png->size())}});
        if (color_workspace_ != nullptr)
            color_workspace_->acceptGalleryReferenceImage(request_id, still_id, {});
        return;
    }
    const auto bytes = found->image_png;
    const auto generation = project_generation_;
    const QPointer<MainWindow> target(this);
    QThreadPool::globalInstance()->start([target, request_id, still_id,
        generation, bytes] {
        QByteArray encoded(reinterpret_cast<const char*>(bytes->data()),
            static_cast<qsizetype>(bytes->size()));
        QBuffer buffer(&encoded);
        QImage image;
        QString failure;
        if (!buffer.open(QIODevice::ReadOnly)) {
            failure = QStringLiteral("The Gallery PNG buffer could not be opened.");
        } else {
            QImageReader reader(&buffer, "PNG");
            image = reader.read();
            if (image.isNull()) failure = reader.errorString();
        }
        if (!target) return;
        QMetaObject::invokeMethod(target, [target, request_id, still_id,
            generation, image = std::move(image), failure = std::move(failure)]() mutable {
            if (!target || target->project_generation_ != generation ||
                target->color_workspace_ == nullptr) return;
            if (image.isNull()) {
                logging::Logger::instance().log(logging::Level::Error,
                    "color-gallery", "decode_comparison_reference",
                    failure.isEmpty() ? "The selected Gallery still could not be decoded."
                                      : failure.toUtf8().toStdString(),
                    {{"still_id", still_id.toUtf8().toStdString()}});
            }
            target->color_workspace_->acceptGalleryReferenceImage(
                request_id, still_id, std::move(image));
        }, Qt::QueuedConnection);
    });
}

void MainWindow::handleColorStillCapture(
    const playback::PlaybackStillCaptureEvent& event) {
    if (!pending_color_still_capture_ ||
        pending_color_still_capture_->request_id != event.request_id) return;
    const auto capture = *pending_color_still_capture_;
    pending_color_still_capture_.reset();
    if (color_workspace_ != nullptr) color_workspace_->setCapturePending(false);
    if (!event.frame || event.project_generation != capture.project_generation ||
        event.project_generation != project_generation_ ||
        event.timeline_frame != capture.timeline_frame ||
        event.composition_revision != capture.composition_revision ||
        event.playback_generation != capture.playback_generation) return;
    const auto current = preview_widget_ != nullptr
        ? preview_widget_->currentPayload() : rendering::PreviewFramePayload{};
    if (!current.valid() || current.timeline_frame != capture.timeline_frame ||
        current.composition_revision != capture.composition_revision ||
        current.playback_generation != capture.playback_generation ||
        timelinePlayheadFrame() != capture.timeline_frame) return;
    encodeColorStill(capture, event.frame);
}

void MainWindow::encodeColorStill(
    PendingColorStillCapture capture,
    media::VideoFramePtr frame) {
    if (!frame || frame->width != editor_session_.canvasWidth() ||
        frame->height != editor_session_.canvasHeight() || frame->stride < frame->width * 4 ||
        frame->rgba_pixels.size() < static_cast<std::size_t>(frame->stride) * frame->height) {
        pending_color_still_capture_.reset();
        if (color_workspace_ != nullptr) color_workspace_->setCapturePending(false);
        logging::Logger::instance().log(logging::Level::Error,
            "color-gallery", "validate_capture_frame",
            "The captured still did not match the project canvas RGBA contract.",
            {{"width", frame ? std::to_string(frame->width) : "missing"},
             {"height", frame ? std::to_string(frame->height) : "missing"},
             {"expected_width", std::to_string(editor_session_.canvasWidth())},
             {"expected_height", std::to_string(editor_session_.canvasHeight())}});
        return;
    }
    const QImage source(reinterpret_cast<const uchar*>(frame->rgba_pixels.data()),
        frame->width, frame->height, frame->stride, QImage::Format_RGBA8888);
    const QPointer<MainWindow> target(this);
    QThreadPool::globalInstance()->start([target, capture = std::move(capture),
                                           frame = std::move(frame), source] {
        QByteArray png;
        QBuffer output(&png);
        const bool encoded = output.open(QIODevice::WriteOnly) && source.save(&output, "PNG");
        if (!target) return;
        QMetaObject::invokeMethod(target, [target, capture, frame, png = std::move(png), encoded] {
            if (!target || !target->pending_color_still_capture_ ||
                target->pending_color_still_capture_->request_id != capture.request_id) return;
            target->pending_color_still_capture_.reset();
            if (target->color_workspace_ != nullptr)
                target->color_workspace_->setCapturePending(false);
            const auto current = target->preview_widget_ != nullptr
                ? target->preview_widget_->currentPayload() : rendering::PreviewFramePayload{};
            if (capture.project_generation != target->project_generation_ ||
                target->timelinePlayheadFrame() != capture.timeline_frame ||
                !current.valid() || current.timeline_frame != capture.timeline_frame ||
                current.composition_revision != capture.composition_revision ||
                current.playback_generation != capture.playback_generation) return;
            if (!encoded || png.isEmpty()) {
                logging::Logger::instance().log(logging::Level::Error,
                    "color-gallery", "encode_still", "Could not encode a Color Gallery PNG.",
                    {{"timeline_frame", std::to_string(capture.timeline_frame)},
                     {"width", std::to_string(frame->width)},
                     {"height", std::to_string(frame->height)}});
                target->statusBar()->showMessage(QCoreApplication::translate(
                    "MainWindow", "Could not save the Color Gallery still."), 5000);
                return;
            }
            const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toUtf8();
            project::ColorGalleryStill still;
            still.id.assign(id.constData(), static_cast<std::size_t>(id.size()));
            const auto name = QCoreApplication::translate("ColorWorkspace", "Still %1")
                .arg(target->editor_session_.colorGalleryStills().size() + 1).toUtf8();
            still.name.assign(name.constData(), static_cast<std::size_t>(name.size()));
            still.source_clip_id = capture.source_clip_id;
            still.timeline_frame = capture.timeline_frame;
            still.image_file = still.id + ".png";
            still.image_png = std::make_shared<const std::vector<std::uint8_t>>(
                reinterpret_cast<const std::uint8_t*>(png.constData()),
                reinterpret_cast<const std::uint8_t*>(png.constData()) + png.size());
            target->editor_session_.colorGalleryStillsForUi().push_back(std::move(still));
            target->updateProjectDirtyState();
            target->refreshColorSelection();
        }, Qt::QueuedConnection);
    });
}

void MainWindow::renameColorStill(const QString& id, const QString& name) {
    const auto id_utf8 = id.toUtf8();
    const auto name_utf8 = name.trimmed().toUtf8();
    if (name_utf8.isEmpty()) return;
    auto& stills = editor_session_.colorGalleryStillsForUi();
    const auto found = std::find_if(stills.begin(), stills.end(), [&id_utf8](const auto& still) {
        return still.id == std::string(id_utf8.constData(), static_cast<std::size_t>(id_utf8.size()));
    });
    if (found == stills.end() || found->name == std::string(name_utf8.constData(),
            static_cast<std::size_t>(name_utf8.size()))) return;
    found->name.assign(name_utf8.constData(), static_cast<std::size_t>(name_utf8.size()));
    updateProjectDirtyState();
    refreshColorSelection();
}

void MainWindow::removeColorStill(const QString& id) {
    const auto id_utf8 = id.toUtf8();
    auto& stills = editor_session_.colorGalleryStillsForUi();
    const auto found = std::find_if(stills.begin(), stills.end(), [&id_utf8](const auto& still) {
        return still.id == std::string(id_utf8.constData(), static_cast<std::size_t>(id_utf8.size()));
    });
    if (found == stills.end()) return;
    stills.erase(found);
    updateProjectDirtyState();
    refreshColorSelection();
}

void MainWindow::setWorkspacePage(ui::WorkspacePageId page) {
    if (workspace_transition_controller_ != nullptr) {
        workspace_transition_controller_->setPage(page);
    }
}

void MainWindow::handleWorkspacePageChanged(ui::WorkspacePageId page) {
    if (page == ui::WorkspacePageId::Color) refreshColorSelection();
    if (playback_controller_ != nullptr) {
        playback_controller_->setAudioMeteringEnabled(
            page == ui::WorkspacePageId::Audio);
    }
    if (editUi().timeline != nullptr) {
        editUi().timeline->setAudioOnlyDisplayEnabled(
            page == ui::WorkspacePageId::Audio);
    }
    if (audio_workspace_ != nullptr) {
        audio_workspace_->refreshTracks(timeline_model_.tracks(),
            timeline_model_.audioBuses(), timeline_model_.masterAudioMix());
        if (page != ui::WorkspacePageId::Audio) audio_workspace_->clearPeaks();
    }
    refreshEffectsContext();
    if (shortcut_manager_ != nullptr) {
        shortcut_manager_->setWorkspace(page);
    }
    refreshWorkspaceMenuVisibility();
}

void MainWindow::registerWorkspaceMenuAction(
    QAction* action,
    settings::ShortcutScope scope) {
    if (action == nullptr) return;
    const auto scope_index = static_cast<std::size_t>(scope);
    if (scope_index >= workspace_menu_actions_.size()) return;
    for (const auto& actions : workspace_menu_actions_) {
        if (std::find(actions.cbegin(), actions.cend(), action) != actions.cend()) {
            return;
        }
    }
    workspace_menu_actions_[scope_index].push_back(action);
    if (shortcut_manager_ != nullptr) {
        action->setVisible(shortcut_manager_->isScopeActive(scope));
    }
}

void MainWindow::refreshWorkspaceMenuVisibility() {
    if (shortcut_manager_ != nullptr) {
        for (std::size_t scope_index = 0;
             scope_index < workspace_menu_actions_.size(); ++scope_index) {
            const auto scope = static_cast<settings::ShortcutScope>(scope_index);
            const bool active = shortcut_manager_->isScopeActive(scope);
            for (auto* action : workspace_menu_actions_[scope_index]) {
                if (action != nullptr) action->setVisible(active);
            }
        }
    }
    // The Effects toolbar action also controls the dock from Color. It shares
    // its toolbar with Media Pool, whose actions remain scoped to Edit/Fusion.
    if (effects_action_ != nullptr && workspace_host_ != nullptr) {
        const auto page = workspace_host_->currentPage();
        effects_action_->setVisible(
            page == ui::WorkspacePageId::Edit ||
            page == ui::WorkspacePageId::Fusion ||
            page == ui::WorkspacePageId::Color);
        if (effects_dock_view_action_ != nullptr) {
            effects_dock_view_action_->setVisible(
                page != ui::WorkspacePageId::Color);
        }
    }
    if (menuBar() == nullptr) return;
    for (auto* action : menuBar()->actions()) {
        if (action != nullptr && action->menu() != nullptr) {
            refreshMenuVisibility(action->menu());
        }
    }
}

void MainWindow::showSettingsDialog() {
    settings::SettingsDialog dialog(this, *shortcut_manager_);
    connect(&dialog, &settings::SettingsDialog::hardwareDecodingEnabledChanged,
        this, [this](bool enabled) {
            if (playback_controller_) playback_controller_->setHardwareDecodingEnabled(enabled);
        });
    connect(&dialog, &settings::SettingsDialog::gpuCompositionEnabledChanged,
        this, [this](bool enabled) {
            if (playback_controller_) playback_controller_->setGpuCompositionEnabled(enabled);
        });
    connect(&dialog, &settings::SettingsDialog::audioWaveformStereoModeChanged,
        this, [this](bool enabled) {
            if (editUi().timeline != nullptr) {
                editUi().timeline->setStereoWaveformDisplayEnabled(enabled);
            }
        });
    connect(&dialog,
        &settings::SettingsDialog::timelineTrackRowHeightAdjustmentModeChanged,
        this, [this](timeline::TrackRowHeightAdjustmentMode mode) {
            if (editUi().timeline != nullptr) {
                editUi().timeline->setTrackRowHeightAdjustmentMode(mode);
            }
        });
    dialog.setAutosaveSnapshots(autosaveSnapshotsForSettings());
    connect(
        &dialog,
        &settings::SettingsDialog::previewPerformanceMetricsEnabledChanged,
        this,
        &MainWindow::configurePreviewPerformanceMetrics);
    connect(
        &dialog,
        &settings::SettingsDialog::projectAutosaveSettingsChanged,
        this,
        [this](bool enabled, int interval_seconds, int) {
            configureProjectAutosave(enabled, interval_seconds);
        });
    connect(
        &dialog,
        &settings::SettingsDialog::autosaveRefreshRequested,
        &dialog,
        [this, &dialog]() {
            dialog.setAutosaveSnapshots(autosaveSnapshotsForSettings());
        });
    connect(
        &dialog,
        &settings::SettingsDialog::autosaveRestoreRequested,
        &dialog,
        [this, &dialog](const QString& snapshot_path,
                        const QString& project_path) {
            if (restoreAutosaveSnapshot(snapshot_path, project_path)) {
                dialog.accept();
            }
        });
    connect(
        &dialog,
        &settings::SettingsDialog::autosaveDeleteRequested,
        &dialog,
        [this, &dialog](const QString& snapshot_path) {
            deleteAutosaveSnapshot(snapshot_path);
            dialog.setAutosaveSnapshots(autosaveSnapshotsForSettings());
        });
    connect(
        &dialog,
        &settings::SettingsDialog::autosaveOpenFolderRequested,
        this,
        &MainWindow::openAutosaveFolder);
    dialog.exec();
}
void MainWindow::createMenus() {
    const auto disableDuringProjectLoad = [](QAction* action) {
        if (action != nullptr) {
            action->setProperty("disabledDuringProjectLoad", true);
        }
    };
    const auto register_shortcut =
        [this](const QString& id,
               const QString& label,
               QAction* action,
               settings::ShortcutScope scope,
               const QString& availability) {
            shortcut_manager_->registerAction(
                id, label, action, scope, availability);
        };
    const auto register_menu_action =
        [this](QAction* action, settings::ShortcutScope scope) {
            registerWorkspaceMenuAction(action, scope);
        };

    auto* file_menu = menuBar()->addMenu("&File");
    file_menu->setObjectName(QStringLiteral("fileMenu"));
    auto* import_interchange = file_menu->addAction(QCoreApplication::translate("MainWindow", "Import Composition Interchange..."));
    import_interchange->setObjectName("importCompositionInterchangeAction");
    connect(import_interchange, &QAction::triggered, this, &MainWindow::importCompositionInterchange);
    auto* export_interchange = file_menu->addAction(QCoreApplication::translate("MainWindow", "Export Composition Interchange..."));
    export_interchange->setObjectName("exportCompositionInterchangeAction");
    disableDuringProjectLoad(import_interchange);
    disableDuringProjectLoad(export_interchange);
    register_menu_action(import_interchange, settings::ShortcutScope::Application);
    register_menu_action(export_interchange, settings::ShortcutScope::Application);
    connect(export_interchange, &QAction::triggered, this, &MainWindow::exportCompositionInterchange);
    file_menu->addSeparator();
    new_project_action_ = file_menu->addAction(QCoreApplication::translate("MainWindow", "&New Project"));
    register_menu_action(new_project_action_, settings::ShortcutScope::Application);
    disableDuringProjectLoad(new_project_action_);
    new_project_action_->setShortcut(QKeySequence("Ctrl+N"));
    new_project_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.new_project"), QStringLiteral("New Project"),
        new_project_action_, settings::ShortcutScope::Application,
        QStringLiteral("All workspaces"));
    connect(new_project_action_, &QAction::triggered, this, &MainWindow::newProject);
    project_settings_action_ = file_menu->addAction(QCoreApplication::translate("MainWindow", "Project Settings..."));
    register_menu_action(project_settings_action_, settings::ShortcutScope::Application);
    project_settings_action_->setObjectName(QStringLiteral("projectSettingsAction"));
    disableDuringProjectLoad(project_settings_action_);
    connect(project_settings_action_, &QAction::triggered,
            this, &MainWindow::showProjectSettingsDialog);
    auto* open_media_action = file_menu->addAction(QCoreApplication::translate("MainWindow", "Open &Media..."));
    register_menu_action(open_media_action, settings::ShortcutScope::Application);
    disableDuringProjectLoad(open_media_action);
    connect(open_media_action, &QAction::triggered, this, &MainWindow::openMedia);
    open_project_action_ = file_menu->addAction(QCoreApplication::translate("MainWindow", "&Open Project..."));
    register_menu_action(open_project_action_, settings::ShortcutScope::Application);
    disableDuringProjectLoad(open_project_action_);
    open_project_action_->setShortcut(QKeySequence("Ctrl+O"));
    open_project_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.open_project"), QStringLiteral("Open Project"),
        open_project_action_, settings::ShortcutScope::Application,
        QStringLiteral("All workspaces"));
    connect(open_project_action_, &QAction::triggered, this, &MainWindow::openProject);
    save_project_action_ = file_menu->addAction(QCoreApplication::translate("MainWindow", "&Save Project"));
    register_menu_action(save_project_action_, settings::ShortcutScope::Application);
    disableDuringProjectLoad(save_project_action_);
    save_project_action_->setShortcut(QKeySequence("Ctrl+S"));
    save_project_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.save_project"), QStringLiteral("Save Project"),
        save_project_action_, settings::ShortcutScope::Application,
        QStringLiteral("All workspaces"));
    connect(save_project_action_, &QAction::triggered, this, &MainWindow::saveProject);
    save_project_as_action_ = file_menu->addAction(QCoreApplication::translate("MainWindow", "Save Project &As..."));
    register_menu_action(save_project_as_action_, settings::ShortcutScope::Application);
    disableDuringProjectLoad(save_project_as_action_);
    save_project_as_action_->setShortcut(QKeySequence("Ctrl+Shift+S"));
    save_project_as_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.save_project_as"),
        QStringLiteral("Save Project As"), save_project_as_action_,
        settings::ShortcutScope::Application,
        QStringLiteral("All workspaces"));
    connect(save_project_as_action_, &QAction::triggered, this, &MainWindow::saveProjectAs);
    file_menu->addSeparator();
    auto* exit_action = file_menu->addAction(QCoreApplication::translate("MainWindow", "E&xit"));
    register_menu_action(exit_action, settings::ShortcutScope::Application);
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    auto* edit_menu = menuBar()->addMenu("&Edit");
    edit_menu->setObjectName(QStringLiteral("editMenu"));
    auto* undo_action = edit_menu->addAction(QCoreApplication::translate("MainWindow", "&Undo"));
    register_menu_action(undo_action, settings::ShortcutScope::Shared);
    disableDuringProjectLoad(undo_action);
    undo_action->setShortcut(QKeySequence::Undo);
    undo_action->setShortcutContext(Qt::WindowShortcut);
    undo_action_ = undo_action;
    register_shortcut(
        QStringLiteral("edit.undo"), QStringLiteral("Undo"), undo_action_,
        settings::ShortcutScope::Shared,
        QStringLiteral("Edit and Fusion workspaces"));
    connect(undo_action_, &QAction::triggered, this, [this]() {
        static_cast<void>(edit_workspace_->controller()->undo());
    });
    auto* redo_action = edit_menu->addAction(QCoreApplication::translate("MainWindow", "&Redo"));
    register_menu_action(redo_action, settings::ShortcutScope::Shared);
    disableDuringProjectLoad(redo_action);
    redo_action->setShortcut(QKeySequence::Redo);
    redo_action->setShortcutContext(Qt::WindowShortcut);
    redo_action_ = redo_action;
    register_shortcut(
        QStringLiteral("edit.redo"), QStringLiteral("Redo"), redo_action_,
        settings::ShortcutScope::Shared,
        QStringLiteral("Edit and Fusion workspaces"));
    connect(redo_action_, &QAction::triggered, this, [this]() {
        static_cast<void>(edit_workspace_->controller()->redo());
    });
    edit_menu->addSeparator();
    for (auto* action : edit_workspace_->menuActions()) {
        if (action == nullptr) {
            edit_menu->addSeparator();
            continue;
        }
        edit_menu->addAction(action);
        register_menu_action(action, settings::ShortcutScope::Edit);
    }
    for (auto* action : edit_workspace_->shortcutOnlyActions()) {
        addAction(action);
    }
    auto* view_menu = menuBar()->addMenu("&View");
    view_menu->setObjectName(QStringLiteral("viewMenu"));
    auto* media_pool_menu = view_menu->addMenu("Media Pool");
    media_pool_menu->setObjectName(QStringLiteral("mediaPoolViewMenu"));
    auto* bins_view_action = bins_dock_->toggleViewAction();
    auto* media_view_action = media_dock_->toggleViewAction();
    media_pool_menu->addAction(bins_view_action);
    media_pool_menu->addAction(media_view_action);
    register_menu_action(bins_view_action, settings::ShortcutScope::Shared);
    register_menu_action(media_view_action, settings::ShortcutScope::Shared);
    auto* effects_menu = view_menu->addMenu("Effects");
    effects_menu->setObjectName(QStringLiteral("effectsViewMenu"));
    auto* toolbox_view_action = toolbox_dock_->toggleViewAction();
    auto* favorites_view_action = favorites_dock_->toggleViewAction();
    effects_dock_view_action_ = effects_dock_->toggleViewAction();
    effects_menu->addAction(toolbox_view_action);
    effects_menu->addAction(favorites_view_action);
    effects_menu->addAction(effects_dock_view_action_);
    register_menu_action(toolbox_view_action, settings::ShortcutScope::Shared);
    register_menu_action(favorites_view_action, settings::ShortcutScope::Shared);
    register_menu_action(effects_dock_view_action_, settings::ShortcutScope::Shared);
    auto* preview_view_action = preview_dock_->toggleViewAction();
    preview_view_action->setObjectName(QStringLiteral("previewViewAction"));
    view_menu->addAction(preview_view_action);
    register_menu_action(preview_view_action, settings::ShortcutScope::Shared);
    auto* inspector_view_action = inspector_dock_->toggleViewAction();
    auto* timeline_view_action = timeline_dock_->toggleViewAction();
    view_menu->addAction(inspector_view_action);
    view_menu->addAction(timeline_view_action);
    register_menu_action(inspector_view_action, settings::ShortcutScope::Application);
    register_menu_action(timeline_view_action, settings::ShortcutScope::Application);
    view_menu->addSeparator();
    auto* grayscale_action = view_menu->addAction(QCoreApplication::translate("MainWindow", "Grayscale Preview"));
    register_menu_action(grayscale_action, settings::ShortcutScope::Application);
    grayscale_action->setCheckable(true);
    grayscale_action->setChecked(false);
    connect(grayscale_action, &QAction::toggled, this, [this](bool enabled) {
        if (preview_widget_ != nullptr) preview_widget_->setGrayscaleEnabled(enabled);
    });
    auto* playback_quality_menu = view_menu->addMenu("Playback Preview Quality");
    playback_quality_menu->setObjectName("playbackPreviewQualityMenu");
    auto* playback_quality_group = new QActionGroup(playback_quality_menu);
    playback_quality_group->setExclusive(true);
    const auto add_playback_quality_action =
        [this, playback_quality_menu, playback_quality_group,
         &register_menu_action](
            const QString& label,
            const QString& object_name,
            playback::PreviewQuality quality) {
            auto* action = playback_quality_menu->addAction(label);
            register_menu_action(action, settings::ShortcutScope::Shared);
            action->setObjectName(object_name);
            action->setCheckable(true);
            action->setChecked(playback_preview_quality_ == quality);
            playback_quality_group->addAction(action);
            connect(action, &QAction::triggered, this, [this, quality]() {
                playback_preview_quality_ = quality;
                QSettings settings;
                settings.setValue(
                    "preview/playback_quality",
                    static_cast<int>(quality));
                if (playback_controller_ != nullptr) {
                    playback_controller_->setPreviewQuality(quality);
                }
            });
        };
    add_playback_quality_action(
        "Full (Project Canvas)",
        "playbackPreviewQualityFull",
        playback::PreviewQuality::Full);
    add_playback_quality_action(
        "Half (Project Canvas)",
        "playbackPreviewQualityHalf",
        playback::PreviewQuality::Half);
    add_playback_quality_action(
        "Quarter (Project Canvas)",
        "playbackPreviewQualityQuarter",
        playback::PreviewQuality::Quarter);
    view_menu->addSeparator();
    auto* layouts_view_action = view_menu->addAction(
        QCoreApplication::translate("MainWindow", "Layouts..."));
    layouts_view_action->setObjectName(QStringLiteral("layoutsViewAction"));
    register_menu_action(layouts_view_action, settings::ShortcutScope::Shared);
    connect(layouts_view_action, &QAction::triggered,
            this, &MainWindow::showWorkspaceLayouts);
    auto* restore_layout_action = view_menu->addAction(QCoreApplication::translate("MainWindow", "Restore &Default Layout"));
    register_menu_action(restore_layout_action, settings::ShortcutScope::Application);
    connect(restore_layout_action, &QAction::triggered, this, &MainWindow::restoreDefaultLayout);

    auto* media_pool_toolbar = new QToolBar("Media Pool", this);
    media_pool_toolbar->setObjectName("mediaPoolToolbar");
    media_pool_toolbar->setMovable(false);
    media_pool_toolbar->setFloatable(false);
    media_pool_toolbar->setAllowedAreas(Qt::TopToolBarArea);
    media_pool_toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);
    media_pool_toolbar->setStyleSheet(
        "QToolButton {"
        " color: #f2f2f2;"
        " background-color: #343434;"
        " border: 1px solid #5c5c5c;"
        " border-radius: 3px;"
        " padding: 3px 10px;"
        " font-weight: 600;"
        "}"
        "QToolButton:hover {"
        " background-color: #414141;"
        "}"
        "QToolButton:checked {"
        " color: #ffffff;"
        " background-color: #2878b8;"
        " border-color: #66b7ed;"
        "}"
        "QToolButton:pressed {"
        " background-color: #1f5f91;"
        "}");
    addToolBar(Qt::TopToolBarArea, media_pool_toolbar);
    media_pool_action_ = media_pool_toolbar->addAction(QCoreApplication::translate("MainWindow", "Media Pool"));
    media_pool_action_->setObjectName(QStringLiteral("mediaPoolGroupAction"));
    register_menu_action(media_pool_action_, settings::ShortcutScope::Shared);
    media_pool_action_->setCheckable(true);
    media_pool_action_->setToolTip(QCoreApplication::translate("MainWindow", "Show the Media Pool docks"));
    auto* workspace_toolbar_spacer = new QWidget(media_pool_toolbar);
    workspace_toolbar_spacer->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Preferred);
    media_pool_toolbar->addWidget(workspace_toolbar_spacer);
    effects_action_ = media_pool_toolbar->addAction(QCoreApplication::translate("MainWindow", "Effects"));
    effects_action_->setObjectName(QStringLiteral("effectsGroupAction"));
    register_menu_action(effects_action_, settings::ShortcutScope::Shared);
    effects_action_->setCheckable(true);
    effects_action_->setToolTip(QCoreApplication::translate("MainWindow", "Show the Effects docks"));
    auto* layouts_toolbar_action = media_pool_toolbar->addAction(
        QCoreApplication::translate("MainWindow", "Layouts"));
    layouts_toolbar_action->setObjectName(QStringLiteral("layoutsToolbarAction"));
    layouts_toolbar_action->setToolTip(
        QCoreApplication::translate("MainWindow", "Open panel layouts"));
    register_menu_action(layouts_toolbar_action, settings::ShortcutScope::Shared);
    connect(layouts_toolbar_action, &QAction::triggered,
            this, &MainWindow::showWorkspaceLayouts);
    connect(media_pool_action_, &QAction::triggered,
            this, [this](bool) { activateMediaPoolGroup(); });
    connect(bins_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool) {
                updateMediaPoolActionState();
                updateEffectsActionState();
            });
    connect(media_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool) {
                updateMediaPoolActionState();
                updateEffectsActionState();
            });
    updateMediaPoolActionState();

    connect(effects_action_, &QAction::triggered,
            this, [this](bool checked) {
                if (workspace_host_ != nullptr &&
                    workspace_host_->currentPage() == ui::WorkspacePageId::Color) {
                    if (color_workspace_ != nullptr)
                        color_workspace_->setEffectsPanelVisible(checked);
                    updateMediaPoolActionState();
                    updateEffectsActionState();
                    return;
                }
                activateEffectsGroup();
            });
    connect(toolbox_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool) {
                updateMediaPoolActionState();
                updateEffectsActionState();
            });
    connect(favorites_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool) {
                updateMediaPoolActionState();
                updateEffectsActionState();
            });
    connect(effects_dock_, &QDockWidget::visibilityChanged, this,
            [this](bool) {
                updateMediaPoolActionState();
                updateEffectsActionState();
            });
    updateEffectsActionState();

    auto* settings_action = menuBar()->addAction(QCoreApplication::translate("MainWindow", "&Settings"));
    settings_action->setObjectName(QStringLiteral("settingsMenuAction"));
    register_menu_action(settings_action, settings::ShortcutScope::Application);
    settings_action->setToolTip(QCoreApplication::translate("MainWindow", "Open editor settings"));
    connect(settings_action, &QAction::triggered,
            this, &MainWindow::showSettingsDialog);

    auto* help_menu = menuBar()->addMenu("&Help");
    help_menu->setObjectName(QStringLiteral("helpMenu"));
    auto* system_action = help_menu->addAction(QCoreApplication::translate("MainWindow", "&System"));
    register_menu_action(system_action, settings::ShortcutScope::Application);
    connect(system_action, &QAction::triggered, this, [this]() {
        const auto version = QCoreApplication::applicationVersion();
        const auto executable_path = QCoreApplication::applicationFilePath();
        QMessageBox::information(
            this,
            QCoreApplication::translate("MainWindow", "System"),
            QString(QCoreApplication::translate("MainWindow", "Video Editor\n\nVersion: %1\nExecutable: %2"))
                .arg(version.isEmpty() ? QCoreApplication::translate("MainWindow", "Beta 0.1.0") : version,
                     executable_path.isEmpty()
                         ? QCoreApplication::translate("MainWindow", "N/A")
                         : executable_path));
    });
    auto* open_log_folder_action = help_menu->addAction(QCoreApplication::translate("MainWindow", "Open &Log Folder"));
    register_menu_action(open_log_folder_action, settings::ShortcutScope::Application);
    connect(open_log_folder_action, &QAction::triggered, this, [this]() {
        auto& logger = logging::Logger::instance();
        const auto directory = logger.log_directory();
        const auto directory_text = pathToUtf8(directory);
        const bool opened = !directory.empty() &&
            QDesktopServices::openUrl(QUrl::fromLocalFile(fromUtf8(directory_text)));
        if (!opened) {
            logger.log(
                logging::Level::Warning,
                "ui",
                "open_log_folder",
                "Could not open the log directory.",
                {{"path", directory_text}});
            statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Could not open the log folder."));
            return;
        }

        logger.log(
            logging::Level::Info,
            "ui",
            "open_log_folder",
            "Opened the log directory.",
            {{"path", directory_text}});
        statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Log folder opened."));
    });
    help_menu->addSeparator();
    auto* about_action = help_menu->addAction(QCoreApplication::translate("MainWindow", "&About Video Editor"));
    register_menu_action(about_action, settings::ShortcutScope::Application);
    connect(about_action, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this,
            QCoreApplication::translate("MainWindow", "About Video Editor"),
            QCoreApplication::translate("MainWindow", "Video Editor application shell\n\n") +
            QCoreApplication::translate("MainWindow", "This is an early open-source creative suite workspace."));
    });

    auto* play_action = new QAction(this);
    play_action->setShortcut(QKeySequence(Qt::Key_Space));
    play_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("playback.play_pause"),
        QStringLiteral("Play or Pause"), play_action,
        settings::ShortcutScope::Shared,
        QStringLiteral("Edit and Fusion workspaces"));
    connect(play_action, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->togglePlayback();
    });
    addAction(play_action);

    auto* previous_frame_action = new QAction(this);
    previous_frame_action->setShortcut(QKeySequence(Qt::Key_Left));
    previous_frame_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("playback.previous_frame"),
        QStringLiteral("Previous Frame"), previous_frame_action,
        settings::ShortcutScope::Shared,
        QStringLiteral("Edit and Fusion workspaces"));
    connect(previous_frame_action, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->requestPlaybackCommand(
            playback::PlaybackCommand::StepBackward);
    });
    addAction(previous_frame_action);

    auto* next_frame_action = new QAction(this);
    next_frame_action->setShortcut(QKeySequence(Qt::Key_Right));
    next_frame_action->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("playback.next_frame"),
        QStringLiteral("Next Frame"), next_frame_action,
        settings::ShortcutScope::Shared,
        QStringLiteral("Edit and Fusion workspaces"));
    connect(next_frame_action, &QAction::triggered, this, [this]() {
        edit_workspace_->controller()->requestPlaybackCommand(
            playback::PlaybackCommand::StepForward);
    });
    addAction(next_frame_action);

    const auto register_workspace_shortcut =
        [this, &register_shortcut](
            const QString& id,
            const QString& label,
            const QString& object_name,
            const QString& default_sequence,
            const QString& tooltip,
            QPushButton* button,
            ui::WorkspacePageId page) {
            auto* action = new QAction(label, this);
            action->setObjectName(object_name);
            action->setShortcut(QKeySequence(default_sequence));
            action->setShortcutContext(Qt::WindowShortcut);
            register_shortcut(
                id,
                label,
                action,
                settings::ShortcutScope::Application,
                QStringLiteral("All workspaces"));
            connect(action, &QAction::triggered, this, [this, page]() {
                setWorkspacePage(page);
            });
            addAction(action);

            const auto update_button_tooltip = [action, button, tooltip]() {
                const auto shortcut_text = action->shortcut().toString(
                    QKeySequence::NativeText);
                button->setToolTip(shortcut_text.isEmpty()
                    ? tooltip
                    : QCoreApplication::translate("MainWindow", "%1 (%2)").arg(tooltip, shortcut_text));
            };
            connect(action, &QAction::changed,
                    this, update_button_tooltip);
            update_button_tooltip();
        };
    register_workspace_shortcut(
        QStringLiteral("workspace.switch_edit"),
        QStringLiteral("Switch to Edit workspace"),
        QStringLiteral("workspaceSwitchEditAction"),
        QStringLiteral("Alt+1"),
        QStringLiteral("Switch to the Edit workspace"),
        edit_workspace_button_,
        ui::WorkspacePageId::Edit);
    register_workspace_shortcut(
        QStringLiteral("workspace.switch_fusion"),
        QStringLiteral("Switch to Fusion workspace"),
        QStringLiteral("workspaceSwitchFusionAction"),
        QStringLiteral("Alt+2"),
        QStringLiteral("Switch to the Fusion workspace"),
        fusion_workspace_button_,
        ui::WorkspacePageId::Fusion);
    register_workspace_shortcut(
        QStringLiteral("workspace.switch_color"),
        QStringLiteral("Switch to Color workspace"),
        QStringLiteral("workspaceSwitchColorAction"),
        QStringLiteral("Alt+5"),
        QStringLiteral("Switch to the Color workspace"),
        color_workspace_button_,
        ui::WorkspacePageId::Color);
    register_workspace_shortcut(
        QStringLiteral("workspace.switch_audio"),
        QStringLiteral("Switch to Audio workspace"),
        QStringLiteral("workspaceSwitchAudioAction"),
        QStringLiteral("Alt+4"),
        QStringLiteral("Switch to the Audio workspace"),
        audio_workspace_button_,
        ui::WorkspacePageId::Audio);
    register_workspace_shortcut(
        QStringLiteral("workspace.switch_render"),
        QStringLiteral("Switch to Render workspace"),
        QStringLiteral("workspaceSwitchRenderAction"),
        QStringLiteral("Alt+3"),
        QStringLiteral("Switch to the Render workspace"),
        render_workspace_button_,
        ui::WorkspacePageId::Render);

    shortcut_manager_->load();
    if (workspace_host_ != nullptr) {
        shortcut_manager_->setWorkspace(workspace_host_->currentPage());
    }
    refreshWorkspaceMenuVisibility();
    updateHistoryActions();
}

void MainWindow::restoreWorkspaceLayout() {
    QSettings settings;
    // Start with the ordinary lower Timeline corners. The persisted marker is
    // only a hint from older layouts; the restored dock geometry determines
    // whether Preview is truly on an outer edge.
    preview_side_dock_full_height_ = false;
    preview_side_panel_widths_active_ = settings.value(
        "workspace/preview_side_panel_widths_active", false).toBool();
    preview_side_layout_restore_pending_ = preview_side_panel_widths_active_;
    preview_side_width_restore_pending_ = preview_side_panel_widths_active_;
    settings.remove("workspace/preview_side_dock_area");
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    const auto saved_state = settings.value(
        "workspace/dock_layout_state").toByteArray();
    const auto restoreMediaPoolExpansion = [this, &settings] {
        media_pool_expanded_ = settings.value(
            "workspace/media_pool_expanded", false).toBool();
        inspector_expanded_ = settings.value(
            "workspace/inspector_expanded", false).toBool();
        workspace_expansion_restore_layout_state_ = settings.value(
            "workspace/dock_expansion_restore_layout_state",
            settings.value(
                "workspace/media_pool_restore_layout_state")).toByteArray();
        updateMediaPoolExpandButton();
        updateInspectorExpandButton();
    };
    if (!saved_state.isEmpty() && restoreState(saved_state, 9)) {
        restoreMediaPoolExpansion();
        if (workspace_host_ != nullptr) {
            workspace_host_->refreshCentralWorkspaceVisibility();
        }
        initial_window_layout_pending_ = false;
        return;
    }

    const auto placePreviewBesideInspector = [this]() {
        if (preview_dock_ == nullptr || inspector_dock_ == nullptr) return;
        preview_dock_->setFloating(false);
        addDockWidget(Qt::RightDockWidgetArea, preview_dock_);
        splitDockWidget(preview_dock_, inspector_dock_, Qt::Horizontal);
        const auto window_width = std::max(1, width());
        resizeDocks(
            {preview_dock_, inspector_dock_},
            {std::max(640, static_cast<int>(std::lround(window_width * 0.62))),
             std::max(280, static_cast<int>(std::lround(window_width * 0.19)))},
            Qt::Horizontal);
    };

    if (!saved_state.isEmpty() && restoreState(saved_state, 8)) {
        restoreMediaPoolExpansion();
        // Version 8 introduced Preview, but could save it in a narrow
        // vertical split after restoring a pre-dock layout. Repair that
        // default placement once while retaining intentional tab/floating,
        // hidden, and other-area arrangements.
        const bool has_preview_tabs = !tabifiedDockWidgets(preview_dock_).isEmpty();
        if (!preview_dock_->isHidden() && !preview_dock_->isFloating() &&
            !has_preview_tabs &&
            dockWidgetArea(preview_dock_) == Qt::RightDockWidgetArea) {
            placePreviewBesideInspector();
        }
        initial_window_layout_pending_ = false;
        saveWorkspaceLayout();
        if (workspace_host_ != nullptr) {
            workspace_host_->refreshCentralWorkspaceVisibility();
        }
        return;
    }
    if (!saved_state.isEmpty() && restoreState(saved_state, 7)) {
        restoreMediaPoolExpansion();
        // Version 7 predates the Preview dock. Keep its existing panel layout
        // and add Preview as the large panel beside Inspector.
        placePreviewBesideInspector();
        preview_dock_->show();
        initial_window_layout_pending_ = false;
        saveWorkspaceLayout();
        if (workspace_host_ != nullptr) {
            workspace_host_->refreshCentralWorkspaceVisibility();
        }
        return;
    }

    restoreDefaultLayout();
    if (workspace_host_ != nullptr) {
        workspace_host_->refreshCentralWorkspaceVisibility();
    }
}

void MainWindow::saveWorkspaceLayout() {
    QSettings settings;
    settings.setValue("workspace/dock_layout_state", saveState(9));
    if (preview_side_dock_full_height_ && preview_dock_ != nullptr &&
        !preview_dock_->isFloating()) {
        const auto area = dockWidgetArea(preview_dock_);
        if (area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea) {
            settings.setValue("workspace/preview_side_dock_area",
                              static_cast<int>(area));
        } else {
            preview_side_dock_full_height_ = false;
            settings.remove("workspace/preview_side_dock_area");
        }
    } else if (!preview_side_dock_full_height_) {
        settings.remove("workspace/preview_side_dock_area");
    }
    settings.setValue("workspace/media_pool_expanded", media_pool_expanded_);
    settings.setValue("workspace/inspector_expanded", inspector_expanded_);
    if ((media_pool_expanded_ || inspector_expanded_) &&
        !workspace_expansion_restore_layout_state_.isEmpty()) {
        settings.setValue(
            "workspace/dock_expansion_restore_layout_state",
            workspace_expansion_restore_layout_state_);
    } else if (!media_pool_expanded_ && !inspector_expanded_) {
        settings.remove("workspace/dock_expansion_restore_layout_state");
        settings.remove("workspace/media_pool_restore_layout_state");
    }
    settings.sync();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
    if (watched == preview_dock_ && event != nullptr &&
        (event->type() == QEvent::Move || event->type() == QEvent::Resize ||
         event->type() == QEvent::Show || event->type() == QEvent::Hide)) {
        schedulePreviewDockLayoutUpdate();
    }
    if (event != nullptr && event->type() == QEvent::Resize &&
        !expanded_dock_layout_change_in_progress_ &&
        !preview_side_panel_layout_change_in_progress_) {
        const bool is_side_panel = watched == bins_dock_ ||
            watched == media_dock_ || watched == toolbox_dock_ ||
            watched == favorites_dock_ || watched == effects_dock_;
        if (is_side_panel && preview_side_panel_width_save_timer_ != nullptr) {
            preview_side_panel_width_save_timer_->start();
        } else if (watched == media_dock_ && media_pool_width_save_timer_ != nullptr) {
            media_pool_width_save_timer_->start();
        } else if (watched == inspector_dock_ &&
                   inspector_width_save_timer_ != nullptr) {
            inspector_width_save_timer_->start();
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::schedulePreviewDockLayoutUpdate() {
    if (preview_dock_layout_update_pending_ ||
        restoring_default_dock_layout_ || preview_dock_ == nullptr) {
        return;
    }
    preview_dock_layout_update_pending_ = true;
    QTimer::singleShot(0, this, [this] {
        preview_dock_layout_update_pending_ = false;
        updatePreviewDockLayout();
    });
}

void MainWindow::updatePreviewDockLayout() {
    if (restoring_default_dock_layout_ || preview_dock_ == nullptr ||
        !isVisible() || preview_side_layout_restore_pending_ ||
        (workspace_host_ != nullptr &&
         workspace_host_->currentPage() == ui::WorkspacePageId::Render)) {
        return;
    }

    const auto area = dockWidgetArea(preview_dock_);
    auto full_height_side = Qt::NoDockWidgetArea;
    if (!preview_dock_->isHidden() && !preview_dock_->isFloating()) {
        const auto dock_origin = preview_dock_->mapTo(this, QPoint(0, 0));
        const auto client_rect = contentsRect();
        const auto dock_rect = QRect(dock_origin, preview_dock_->size());
        constexpr int kOuterEdgeTolerance = 64;
        if (area == Qt::LeftDockWidgetArea &&
            dock_rect.left() <= client_rect.left() + kOuterEdgeTolerance) {
            full_height_side = Qt::LeftDockWidgetArea;
        } else if (area == Qt::RightDockWidgetArea &&
                   dock_rect.right() >= client_rect.right() - kOuterEdgeTolerance) {
            full_height_side = Qt::RightDockWidgetArea;
        }
    }

    const bool should_be_full_height = full_height_side != Qt::NoDockWidgetArea;
    const auto client_rect = contentsRect();
    const auto preview_rect = QRect(
        preview_dock_->mapTo(this, QPoint(0, 0)), preview_dock_->size());
    constexpr int kFullHeightTolerance = 16;
    const int dock_workspace_bottom = statusBar() != nullptr &&
            statusBar()->isVisible()
        ? statusBar()->geometry().top() - 1
        : client_rect.bottom();
    int dock_workspace_top = menuBar() != nullptr && menuBar()->isVisible()
        ? menuBar()->geometry().bottom() + 1
        : client_rect.top();
    for (auto* toolbar : findChildren<QToolBar*>()) {
        if (toolbar == nullptr || toolbar->isHidden() ||
            toolBarArea(toolbar) != Qt::TopToolBarArea) {
            continue;
        }
        dock_workspace_top = std::max(
            dock_workspace_top,
            toolbar->mapTo(this, QPoint(0, toolbar->height())).y());
    }
    const bool preview_fills_workspace_height = should_be_full_height &&
        preview_rect.top() <= dock_workspace_top + kFullHeightTolerance &&
        preview_rect.bottom() >= dock_workspace_bottom - kFullHeightTolerance;
    const bool layout_changed =
        preview_side_dock_full_height_ != should_be_full_height ||
        corner(Qt::BottomLeftCorner) !=
            (full_height_side == Qt::LeftDockWidgetArea
                 ? Qt::LeftDockWidgetArea : Qt::BottomDockWidgetArea) ||
        corner(Qt::BottomRightCorner) !=
            (full_height_side == Qt::RightDockWidgetArea
                 ? Qt::RightDockWidgetArea : Qt::BottomDockWidgetArea);

    preview_side_dock_full_height_ = should_be_full_height;
    setCorner(Qt::BottomLeftCorner,
              full_height_side == Qt::LeftDockWidgetArea
                  ? Qt::LeftDockWidgetArea : Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner,
              full_height_side == Qt::RightDockWidgetArea
                  ? Qt::RightDockWidgetArea : Qt::BottomDockWidgetArea);
    if (layout_changed) {
        saveWorkspaceLayout();
        schedulePreviewDockLayoutUpdate();
        return;
    }

    QSettings settings;
    constexpr auto kMediaPoolWidthKey =
        "workspace/preview_side_saved_media_pool_width";
    constexpr auto kToolboxWidthKey =
        "workspace/preview_side_saved_toolbox_width";
    constexpr auto kEffectsWidthKey =
        "workspace/preview_side_saved_effects_width";
    constexpr auto kSideMediaPoolWidthKey =
        "workspace/preview_side_media_pool_width";
    constexpr auto kSideToolboxWidthKey =
        "workspace/preview_side_toolbox_width";
    constexpr auto kSideEffectsWidthKey =
        "workspace/preview_side_effects_width";
    constexpr auto kSideWidthsActiveKey =
        "workspace/preview_side_panel_widths_active";
    const auto dockRect = [this](QDockWidget* dock) {
        return dock == nullptr
            ? QRect{}
            : QRect(dock->mapTo(this, QPoint(0, 0)), dock->size());
    };
    const auto nearestCompanion = [this, &dockRect](
        const QRect& group_rect,
        const std::vector<QDockWidget*>& group_docks) -> QDockWidget* {
        const std::vector<QDockWidget*> candidates{
            bins_dock_, media_dock_, toolbox_dock_, favorites_dock_,
            effects_dock_, preview_dock_, inspector_dock_, timeline_dock_};
        QDockWidget* result = nullptr;
        auto best_gap = std::numeric_limits<int>::max();
        for (auto* candidate : candidates) {
            if (candidate == nullptr || !candidate->isVisible() ||
                std::find(group_docks.begin(), group_docks.end(), candidate) !=
                    group_docks.end()) {
                continue;
            }
            const auto candidate_rect = dockRect(candidate);
            const int overlap_height = std::max(
                0, std::min(group_rect.bottom(), candidate_rect.bottom()) -
                       std::max(group_rect.top(), candidate_rect.top()) + 1);
            if (overlap_height < std::min(group_rect.height(),
                                          candidate_rect.height()) / 3) {
                continue;
            }
            const int gap = std::max(
                0, std::max(group_rect.left() - candidate_rect.right(),
                            candidate_rect.left() - group_rect.right()) - 1);
            if (gap < best_gap) {
                best_gap = gap;
                result = candidate;
            }
        }
        return result;
    };

    const auto clearSavedPanelWidths = [&settings] {
        settings.remove("workspace/preview_side_saved_media_pool_width");
        settings.remove("workspace/preview_side_saved_toolbox_width");
        settings.remove("workspace/preview_side_saved_effects_width");
        settings.remove("workspace/preview_side_panel_widths_active");
    };
    const auto restoreSavedPanelWidths = [this, &settings, &dockRect, &nearestCompanion] {
        const int media_width = settings.value(
            "workspace/preview_side_saved_media_pool_width", 0).toInt();
        const int toolbox_width = settings.value(
            "workspace/preview_side_saved_toolbox_width", 0).toInt();
        const int effects_width = settings.value(
            "workspace/preview_side_saved_effects_width", 0).toInt();
        preview_side_panel_layout_change_in_progress_ = true;
        if (media_width > 0 && bins_dock_ != nullptr && media_dock_ != nullptr) {
            const std::vector<QDockWidget*> group{bins_dock_, media_dock_};
            const auto rect = dockRect(bins_dock_).united(dockRect(media_dock_));
            if (auto* companion = nearestCompanion(rect, group)) {
                const int current = std::max(bins_dock_->width(), media_dock_->width());
                resizeDocks(
                    {bins_dock_, companion},
                    {media_width, std::max(companion->minimumWidth(),
                        companion->width() + current - media_width)},
                    Qt::Horizontal);
            }
            settings.setValue("workspace/media_pool_width", media_width);
        }
        if (toolbox_width > 0 && effects_width > 0 &&
            toolbox_dock_ != nullptr && effects_dock_ != nullptr) {
            const std::vector<QDockWidget*> group{
                toolbox_dock_, favorites_dock_, effects_dock_};
            const auto rect = dockRect(toolbox_dock_)
                .united(dockRect(favorites_dock_))
                .united(dockRect(effects_dock_));
            if (auto* companion = nearestCompanion(rect, group)) {
                const int current = toolbox_dock_->width() + effects_dock_->width();
                const int saved = toolbox_width + effects_width;
                resizeDocks(
                    {toolbox_dock_, effects_dock_, companion},
                    {toolbox_width, effects_width,
                     std::max(companion->minimumWidth(),
                         companion->width() + current - saved)},
                    Qt::Horizontal);
            }
        }
        preview_side_panel_layout_change_in_progress_ = false;
    };

    const bool entering_side_layout =
        preview_fills_workspace_height && !preview_side_panel_widths_active_;
    if (entering_side_layout) {
        // A previous session may have saved the external-edge layout while
        // these widths were already compacted. Keep that original snapshot.
        if (!settings.value(kSideWidthsActiveKey, false).toBool()) {
            settings.setValue(kMediaPoolWidthKey, settings.value(
                "workspace/media_pool_width",
                std::max(0, media_dock_ ? media_dock_->width() : 0)).toInt());
            settings.setValue(kToolboxWidthKey, settings.value(
                "workspace/preview_normal_toolbox_width",
                std::max(0, toolbox_dock_ ? toolbox_dock_->width() : 0)).toInt());
            settings.setValue(kEffectsWidthKey, settings.value(
                "workspace/preview_normal_effects_width",
                std::max(0, effects_dock_ ? effects_dock_->width() : 0)).toInt());
            settings.setValue(kSideWidthsActiveKey, true);
        }
        preview_side_panel_widths_active_ = true;
    }

    if (preview_fills_workspace_height) {
        const bool media_pool_active = media_pool_action_ != nullptr &&
            media_pool_action_->isChecked();
        const bool effects_active = effects_action_ != nullptr &&
            effects_action_->isChecked();
        const int initial_group_width = std::max(
            1, contentsRect().width() / 4 - 8);
        if (media_pool_active && media_dock_ != nullptr && bins_dock_ != nullptr &&
            !settings.contains(kSideMediaPoolWidthKey)) {
            const int current_width = std::max(bins_dock_->width(), media_dock_->width());
            const int target_width = std::min(current_width, initial_group_width);
            if (current_width > target_width) {
                if (media_pool_width_save_timer_ != nullptr) {
                    media_pool_width_save_timer_->stop();
                }
                const std::vector<QDockWidget*> group{bins_dock_, media_dock_};
                const auto companion = nearestCompanion(
                    dockRect(bins_dock_).united(dockRect(media_dock_)), group);
                if (companion != nullptr) {
                    preview_side_panel_layout_change_in_progress_ = true;
                    resizeDocks(
                        {bins_dock_, companion},
                        {target_width,
                         std::max(companion->minimumWidth(),
                                  companion->width() + current_width - target_width)},
                        Qt::Horizontal);
                    layout()->activate();
                    preview_side_panel_layout_change_in_progress_ = false;
                }
            }
            settings.setValue(
                kSideMediaPoolWidthKey,
                std::max(bins_dock_->width(), media_dock_->width()));
            if (preview_side_panel_width_save_timer_ != nullptr) {
                preview_side_panel_width_save_timer_->start();
            }
        } else if (effects_active && toolbox_dock_ != nullptr &&
                   effects_dock_ != nullptr &&
                   (!settings.contains(kSideToolboxWidthKey) ||
                    !settings.contains(kSideEffectsWidthKey))) {
            const int current_toolbox_width = toolbox_dock_->width();
            const int current_effects_width = effects_dock_->width();
            const int current_group_width =
                current_toolbox_width + current_effects_width;
            const int target_group_width =
                std::min(current_group_width, initial_group_width);
            if (current_group_width > target_group_width) {
                if (media_pool_width_save_timer_ != nullptr) {
                    media_pool_width_save_timer_->stop();
                }
                const int toolbox_target = std::max(
                    20, static_cast<int>(std::lround(
                            static_cast<double>(target_group_width) *
                            current_toolbox_width /
                                std::max(1, current_group_width))));
                const int effects_target = std::max(
                    30, target_group_width - toolbox_target);
                const std::vector<QDockWidget*> group{
                    toolbox_dock_, favorites_dock_, effects_dock_};
                const auto group_rect = dockRect(toolbox_dock_)
                    .united(dockRect(favorites_dock_))
                    .united(dockRect(effects_dock_));
                const auto companion = nearestCompanion(group_rect, group);
                if (companion != nullptr) {
                    preview_side_panel_layout_change_in_progress_ = true;
                    resizeDocks(
                        {toolbox_dock_, effects_dock_, companion},
                        {toolbox_target, effects_target,
                         std::max(companion->minimumWidth(),
                                  companion->width() + current_group_width -
                                      target_group_width)},
                        Qt::Horizontal);
                    layout()->activate();
                    preview_side_panel_layout_change_in_progress_ = false;
                }
            }
            settings.setValue(kSideToolboxWidthKey, toolbox_dock_->width());
            settings.setValue(kSideEffectsWidthKey, effects_dock_->width());
            if (preview_side_panel_width_save_timer_ != nullptr) {
                preview_side_panel_width_save_timer_->start();
            }
        }
    } else if (!should_be_full_height &&
               (preview_side_panel_widths_active_ ||
                settings.value(kSideWidthsActiveKey, false).toBool())) {
        if (preview_side_panel_width_save_timer_ != nullptr) {
            preview_side_panel_width_save_timer_->stop();
        }
        restoreSavedPanelWidths();
        preview_side_panel_widths_active_ = false;
        clearSavedPanelWidths();
    }

    if (layout_changed) saveWorkspaceLayout();
}

void MainWindow::persistMediaPoolWidth() {
    if (preview_side_panel_widths_active_ || media_dock_ == nullptr ||
        media_dock_->width() <= 0) {
        return;
    }
    QSettings settings;
    settings.setValue("workspace/media_pool_width", media_dock_->width());
    settings.sync();
}

void MainWindow::persistPreviewSidePanelWidths() {
    if (preview_side_panel_layout_change_in_progress_) {
        return;
    }
    QSettings settings;
    if (!preview_side_panel_widths_active_) {
        if (media_dock_ != nullptr && media_dock_->isVisible() &&
            media_dock_->width() > 0) {
            settings.setValue("workspace/media_pool_width", media_dock_->width());
        }
        if (toolbox_dock_ != nullptr && effects_dock_ != nullptr &&
            toolbox_dock_->isVisible() && effects_dock_->isVisible()) {
            settings.setValue(
                "workspace/preview_normal_toolbox_width", toolbox_dock_->width());
            settings.setValue(
                "workspace/preview_normal_effects_width", effects_dock_->width());
            settings.sync();
        }
        return;
    }
    if (bins_dock_ != nullptr && media_dock_ != nullptr) {
        settings.setValue(
            "workspace/preview_side_media_pool_width",
            std::max(bins_dock_->width(), media_dock_->width()));
    }
    if (toolbox_dock_ != nullptr && effects_dock_ != nullptr) {
        settings.setValue(
            "workspace/preview_side_toolbox_width", toolbox_dock_->width());
        settings.setValue(
            "workspace/preview_side_effects_width", effects_dock_->width());
    }
    saveWorkspaceLayout();
    settings.sync();
}

void MainWindow::persistInspectorWidth() {
    if (inspector_dock_ == nullptr || inspector_dock_->width() <= 0) {
        return;
    }
    QSettings settings;
    settings.setValue("workspace/inspector_width", inspector_dock_->width());
    settings.sync();
}

void MainWindow::restoreExpandedDockLayout() {
    if (media_dock_ == nullptr || inspector_dock_ == nullptr) return;

    // restoreState already restored the side-docked arrangement. Rebuilding
    // the normal expansion layout here would overwrite its saved side widths.
    if (preview_side_panel_widths_active_ && preview_dock_ != nullptr &&
        !preview_dock_->isHidden() && !preview_dock_->isFloating()) {
        const auto area = dockWidgetArea(preview_dock_);
        const auto rect = QRect(
            preview_dock_->mapTo(this, QPoint(0, 0)), preview_dock_->size());
        const auto client = contentsRect();
        constexpr int kOuterEdgeTolerance = 64;
        const bool restored_on_outer_edge =
            (area == Qt::LeftDockWidgetArea &&
             rect.left() <= client.left() + kOuterEdgeTolerance) ||
            (area == Qt::RightDockWidgetArea &&
             rect.right() >= client.right() - kOuterEdgeTolerance);
        if (restored_on_outer_edge) return;
    }

    QSettings settings;
    const int saved_media_width = settings.value(
        "workspace/media_pool_width",
        settings.value(
            "workspace/media_pool_expanded_width",
            settings.value("workspace/media_pool_normal_width", 280)))
        .toInt();
    const int saved_inspector_width = settings.value(
        "workspace/inspector_width", inspector_dock_->width()).toInt();
    applyExpandedDockLayout(
        media_pool_expanded_, inspector_expanded_,
        saved_media_width, saved_inspector_width, true);
}

void MainWindow::setMediaPoolExpanded(
    bool expanded,
    std::optional<int> expanded_width_override) {
    applyExpandedDockLayout(
        expanded, inspector_expanded_, expanded_width_override,
        inspector_expanded_
            ? std::optional<int>(inspector_dock_->width())
            : std::nullopt);
}

void MainWindow::setInspectorExpanded(bool expanded) {
    applyExpandedDockLayout(
        media_pool_expanded_, expanded,
        media_pool_expanded_
            ? std::optional<int>(media_dock_->width())
            : std::nullopt,
        expanded ? std::optional<int>(inspector_dock_->width())
                 : std::nullopt);
}

void MainWindow::applyExpandedDockLayout(
    bool media_pool_expanded,
    bool inspector_expanded,
    std::optional<int> media_pool_width_override,
    std::optional<int> inspector_width_override,
    bool force_rebuild) {
    if (!force_rebuild &&
            media_pool_expanded_ == media_pool_expanded &&
            inspector_expanded_ == inspector_expanded ||
        bins_dock_ == nullptr || media_dock_ == nullptr ||
        preview_dock_ == nullptr || inspector_dock_ == nullptr ||
        timeline_dock_ == nullptr) {
        updateMediaPoolExpandButton();
        updateInspectorExpandButton();
        return;
    }

    QSettings settings;
    const bool was_expanded = media_pool_expanded_ || inspector_expanded_;
    const bool will_be_expanded = media_pool_expanded || inspector_expanded;
    const QRect preview_geometry = preview_dock_->geometry();
    const QRect timeline_geometry = timeline_dock_->geometry();
    const int current_media_width = media_dock_->width();
    const int current_inspector_width = inspector_dock_->width();
    const int current_bins_height = bins_dock_->height();
    if (!was_expanded && will_be_expanded) {
        workspace_expansion_restore_layout_state_ = saveState(9);
    }

    // Apply the baseline synchronously before measuring its normal heights.
    // The current geometry can belong to a previously expanded Media Pool.
    const auto original_dock_options = dockOptions();
    setDockOptions(original_dock_options & ~QMainWindow::AnimatedDocks);
    const auto restore_dock_options = qScopeGuard([this, original_dock_options] {
        setDockOptions(original_dock_options);
    });

    const int media_pool_width = std::clamp(
        media_pool_width_override.value_or(current_media_width),
        280, std::max(280, width() / 2));
    const int inspector_width = std::clamp(
        inspector_width_override.value_or(current_inspector_width),
        280, std::max(280, width() / 2));

    if (media_pool_width_save_timer_ != nullptr) {
        media_pool_width_save_timer_->stop();
    }
    if (inspector_width_save_timer_ != nullptr) {
        inspector_width_save_timer_->stop();
    }
    bins_dock_->setMinimumWidth(0);
    bins_dock_->setMaximumHeight(QWIDGETSIZE_MAX);
    media_dock_->setMinimumWidth(0);
    media_dock_->setMaximumHeight(QWIDGETSIZE_MAX);
    preview_dock_->setMinimumWidth(0);
    preview_dock_->setMaximumWidth(QWIDGETSIZE_MAX);
    preview_dock_->setMinimumHeight(0);
    preview_dock_->setMaximumHeight(QWIDGETSIZE_MAX);
    inspector_dock_->setMinimumWidth(0);
    expanded_dock_layout_change_in_progress_ = true;

    const bool restored_base_layout =
        workspace_expansion_restore_layout_state_.isEmpty() ||
        restoreState(workspace_expansion_restore_layout_state_, 9);
    if (!restored_base_layout) {
        restoreDefaultLayout();
    }
    layout()->activate();
    const int normal_bins_height = bins_dock_->height();
    const int normal_media_height = media_dock_->height();

    media_pool_expanded_ = false;
    inspector_expanded_ = false;
    if (workspace_host_ != nullptr) {
        workspace_host_->refreshCentralWorkspaceVisibility();
    }

    if (!will_be_expanded) {
        const int restored_preview_width = preview_dock_->width();
        resizeDocks(
            {bins_dock_, preview_dock_},
            {media_pool_width,
             std::max(280, restored_preview_width + current_media_width -
                               media_pool_width + current_inspector_width -
                               inspector_width)},
            Qt::Horizontal);
        resizeDocks(
            {preview_dock_, inspector_dock_},
            {std::max(280, restored_preview_width + current_media_width -
                               media_pool_width + current_inspector_width -
                               inspector_width),
             inspector_width},
            Qt::Horizontal);
        workspace_expansion_restore_layout_state_.clear();
        settings.remove("workspace/dock_expansion_restore_layout_state");
    } else {
        bins_dock_->setFloating(false);
        media_dock_->setFloating(false);
        preview_dock_->setFloating(false);
        inspector_dock_->setFloating(false);
        timeline_dock_->setFloating(false);
        addDockWidget(Qt::LeftDockWidgetArea, bins_dock_);
        addDockWidget(Qt::LeftDockWidgetArea, media_dock_);
        splitDockWidget(bins_dock_, media_dock_, Qt::Vertical);
        addDockWidget(Qt::RightDockWidgetArea, preview_dock_);

        if (media_pool_expanded && !inspector_expanded) {
            addDockWidget(Qt::RightDockWidgetArea, timeline_dock_);
            splitDockWidget(preview_dock_, timeline_dock_, Qt::Vertical);
            addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
            splitDockWidget(preview_dock_, inspector_dock_, Qt::Horizontal);
        } else if (inspector_expanded && !media_pool_expanded) {
            // Build an upper Bins/Media + Preview row above Timeline in the
            // left dock area, with Inspector alone on the right. Keeping
            // Timeline under Preview in the right area reserves an empty
            // full-height column below Media regardless of height constraints.
            removeDockWidget(bins_dock_);
            removeDockWidget(media_dock_);
            removeDockWidget(preview_dock_);
            removeDockWidget(inspector_dock_);
            removeDockWidget(timeline_dock_);
            addDockWidget(Qt::LeftDockWidgetArea, bins_dock_);
            addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
            addDockWidget(Qt::LeftDockWidgetArea, timeline_dock_);
            splitDockWidget(bins_dock_, timeline_dock_, Qt::Vertical);
            addDockWidget(Qt::LeftDockWidgetArea, preview_dock_);
            splitDockWidget(bins_dock_, preview_dock_, Qt::Horizontal);
            addDockWidget(Qt::LeftDockWidgetArea, media_dock_);
            splitDockWidget(bins_dock_, media_dock_, Qt::Vertical);
            bins_dock_->show();
            media_dock_->show();
            preview_dock_->show();
            inspector_dock_->show();
            timeline_dock_->show();
            resizeDocks({bins_dock_, media_dock_},
                        {normal_bins_height, normal_media_height}, Qt::Vertical);
            resizeDocks({bins_dock_, preview_dock_, inspector_dock_},
                        {media_pool_width, std::max(280, preview_geometry.width()),
                         inspector_width}, Qt::Horizontal);
        } else if (inspector_expanded) {
            addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
            addDockWidget(Qt::RightDockWidgetArea, preview_dock_);
            splitDockWidget(preview_dock_, inspector_dock_, Qt::Horizontal);
            resizeDocks(
                {preview_dock_, inspector_dock_},
                {std::max(280, preview_geometry.width()), inspector_width},
                Qt::Horizontal);
            addDockWidget(Qt::RightDockWidgetArea, timeline_dock_);
            splitDockWidget(preview_dock_, timeline_dock_, Qt::Vertical);
        } else {
            addDockWidget(Qt::RightDockWidgetArea, timeline_dock_);
            splitDockWidget(preview_dock_, timeline_dock_, Qt::Vertical);
            addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
            splitDockWidget(preview_dock_, inspector_dock_, Qt::Horizontal);
        }
        if (media_pool_expanded) {
            resizeDocks(
                {bins_dock_, media_dock_},
                {std::max(140, current_bins_height),
                 std::max(260, height() - current_bins_height)},
                Qt::Vertical);
        }
        const int target_preview_width = std::max(
            280, preview_geometry.width() + current_media_width -
                     media_pool_width + current_inspector_width -
                     inspector_width);
        if (!inspector_expanded) {
            resizeDocks(
                {bins_dock_, preview_dock_},
                {media_pool_width, target_preview_width}, Qt::Horizontal);
            resizeDocks(
                {preview_dock_, inspector_dock_},
                {target_preview_width, inspector_width}, Qt::Horizontal);
        }
        if (inspector_expanded) {
            bins_dock_->setMinimumWidth(media_pool_width);
            media_dock_->setMinimumWidth(media_pool_width);
        }
        resizeDocks(
            {preview_dock_, timeline_dock_},
            {std::max(260, preview_geometry.height()),
             std::max(220, timeline_geometry.height())},
            Qt::Vertical);
        if (inspector_expanded) {
            // Keep the preview's captured geometry through the dock-tree
            // rebuild. The constraints are released after Qt settles so the
            // Inspector splitter remains user-resizable.
            preview_dock_->setFixedWidth(target_preview_width);
            preview_dock_->setFixedHeight(
                preview_geometry.height() + (media_pool_expanded ? 3 : 0));
        }

        media_pool_expanded_ = media_pool_expanded;
        inspector_expanded_ = inspector_expanded;
    }

    layout()->activate();
    settings.setValue("workspace/media_pool_width", media_dock_->width());
    settings.setValue("workspace/inspector_width", inspector_dock_->width());
    settings.setValue("workspace/media_pool_expanded", media_pool_expanded_);
    settings.setValue("workspace/inspector_expanded", inspector_expanded_);
    if (will_be_expanded) {
        settings.setValue(
            "workspace/dock_expansion_restore_layout_state",
            workspace_expansion_restore_layout_state_);
    }

    QTimer::singleShot(
        150, this, [this, normal_bins_height, normal_media_height] {
            if (bins_dock_ != nullptr) {
                bins_dock_->setMinimumWidth(0);
                bins_dock_->setMaximumHeight(QWIDGETSIZE_MAX);
            }
            if (media_dock_ != nullptr) {
                media_dock_->setMinimumWidth(0);
                media_dock_->setMaximumHeight(QWIDGETSIZE_MAX);
            }
            if (preview_dock_ != nullptr) {
                preview_dock_->setMinimumWidth(0);
                preview_dock_->setMaximumWidth(QWIDGETSIZE_MAX);
                preview_dock_->setMinimumHeight(0);
                preview_dock_->setMaximumHeight(QWIDGETSIZE_MAX);
            }
            if (inspector_expanded_ && !media_pool_expanded_ &&
                bins_dock_ != nullptr && media_dock_ != nullptr) {
                resizeDocks(
                    {bins_dock_, media_dock_},
                    {normal_bins_height, normal_media_height}, Qt::Vertical);
            }
            expanded_dock_layout_change_in_progress_ = false;
            persistMediaPoolWidth();
            persistInspectorWidth();
            saveWorkspaceLayout();
        });

    updateMediaPoolExpandButton();
    updateInspectorExpandButton();
    saveWorkspaceLayout();
    if (!will_be_expanded) {
        QTimer::singleShot(0, this, [this, media_pool_width, inspector_width] {
            if (media_pool_expanded_ || inspector_expanded_ ||
                media_dock_ == nullptr || preview_dock_ == nullptr ||
                inspector_dock_ == nullptr) {
                return;
            }
            const int available_width = std::max(840, width() - 16);
            const int preview_width = std::max(
                280, available_width - media_pool_width - inspector_width);
            resizeDocks(
                {bins_dock_, preview_dock_, inspector_dock_},
                {media_pool_width, preview_width, inspector_width},
                Qt::Horizontal);
            saveWorkspaceLayout();
        });
    }
}

void MainWindow::updateInspectorExpandButton() {
    if (inspector_expand_button_ == nullptr) return;
    const QSignalBlocker blocker(inspector_expand_button_);
    inspector_expand_button_->setChecked(inspector_expanded_);
    inspector_expand_button_->setIcon(style()->standardIcon(
        inspector_expanded_ ? QStyle::SP_TitleBarNormalButton
                            : QStyle::SP_TitleBarMaxButton));
    inspector_expand_button_->setToolTip(
        inspector_expanded_
            ? QCoreApplication::translate("MainWindow", "Restore Inspector layout")
            : QCoreApplication::translate("MainWindow", "Expand Inspector to full window height"));
    inspector_expand_button_->setAccessibleName(
        inspector_expand_button_->toolTip());
}

void MainWindow::updateMediaPoolExpandButton() {
    if (media_pool_expand_button_ == nullptr) return;
    const QSignalBlocker blocker(media_pool_expand_button_);
    media_pool_expand_button_->setChecked(media_pool_expanded_);
    media_pool_expand_button_->setIcon(style()->standardIcon(
        media_pool_expanded_ ? QStyle::SP_TitleBarNormalButton
                             : QStyle::SP_TitleBarMaxButton));
    media_pool_expand_button_->setToolTip(
        media_pool_expanded_
            ? QCoreApplication::translate("MainWindow", "Restore Media Pool layout")
            : QCoreApplication::translate("MainWindow", "Expand Media Pool to full window height"));
    media_pool_expand_button_->setAccessibleName(
        media_pool_expand_button_->toolTip());
}

void MainWindow::applyInitialWindowLayout() {
    if (!initial_window_layout_pending_) return;
    initial_window_layout_pending_ = false;

    const auto window_width = std::max(1, width());
    const auto window_height = std::max(1, height());
    resizeDocks(
        {bins_dock_},
        {std::max(280, static_cast<int>(std::lround(window_width * 0.18)))},
        Qt::Horizontal);
    resizeDocks(
        {inspector_dock_},
        {std::max(280, static_cast<int>(std::lround(window_width * 0.19)))},
        Qt::Horizontal);
    resizeDocks(
        {preview_dock_, inspector_dock_},
        {std::max(640, static_cast<int>(std::lround(window_width * 0.62))),
         std::max(280, static_cast<int>(std::lround(window_width * 0.19)))},
        Qt::Horizontal);
    resizeDocks(
        {timeline_dock_},
        {std::max(260, static_cast<int>(std::lround(window_height * 0.38)))},
        Qt::Vertical);
    resizeDocks({bins_dock_, media_dock_}, {300, 700}, Qt::Vertical);
    saveWorkspaceLayout();
}

void MainWindow::saveWindowGeometry() {
    QSettings settings;
    settings.setValue("workspace/window_geometry", saveGeometry());
    settings.setValue("workspace/window_maximized", isMaximized());
    if (media_dock_ != nullptr) {
        persistMediaPoolWidth();
    }
    if (inspector_dock_ != nullptr) {
        persistInspectorWidth();
    }
    settings.sync();
}

void MainWindow::activateMediaPoolGroup() {
    toolbox_dock_->hide();
    favorites_dock_->hide();
    effects_dock_->hide();
    bins_dock_->show();
    media_dock_->show();
    updateMediaPoolActionState();
    updateEffectsActionState();
    schedulePreviewDockLayoutUpdate();
}

void MainWindow::activateEffectsGroup() {
    bins_dock_->hide();
    media_dock_->hide();
    toolbox_dock_->show();
    favorites_dock_->show();
    effects_dock_->show();
    updateMediaPoolActionState();
    updateEffectsActionState();
    schedulePreviewDockLayoutUpdate();
}

void MainWindow::updateMediaPoolActionState() {
    if (media_pool_action_ == nullptr ||
        bins_dock_ == nullptr ||
        media_dock_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(media_pool_action_);
    media_pool_action_->setChecked(
        bins_dock_->isVisible() && media_dock_->isVisible() &&
        !toolbox_dock_->isVisible() &&
        !favorites_dock_->isVisible() &&
        !effects_dock_->isVisible());
}

void MainWindow::updateEffectsActionState() {
    if (effects_action_ == nullptr ||
        toolbox_dock_ == nullptr ||
        effects_dock_ == nullptr) {
        return;
    }
    const QSignalBlocker blocker(effects_action_);
    if (workspace_host_ != nullptr &&
        workspace_host_->currentPage() == ui::WorkspacePageId::Color) {
        effects_action_->setChecked(
            color_workspace_ != nullptr &&
            color_workspace_->effectsPanelVisible());
        return;
    }
    effects_action_->setChecked(
        toolbox_dock_->isVisible() &&
        favorites_dock_->isVisible() &&
        effects_dock_->isVisible() &&
        !bins_dock_->isVisible() && !media_dock_->isVisible());
}

void MainWindow::restoreDefaultLayout() {
    [[maybe_unused]] const QSignalBlocker preview_dock_signals(preview_dock_);
    restoring_default_dock_layout_ = true;
    preview_side_dock_full_height_ = false;
    preview_side_panel_widths_active_ = false;
    preview_side_layout_restore_pending_ = false;
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    media_pool_expanded_ = false;
    inspector_expanded_ = false;
    workspace_expansion_restore_layout_state_.clear();
    QSettings settings;
    settings.remove("workspace/preview_side_saved_media_pool_width");
    settings.remove("workspace/preview_side_saved_toolbox_width");
    settings.remove("workspace/preview_side_saved_effects_width");
    settings.remove("workspace/preview_side_panel_widths_active");
    settings.remove("workspace/preview_side_media_pool_width");
    settings.remove("workspace/preview_side_toolbox_width");
    settings.remove("workspace/preview_side_effects_width");
    settings.remove("workspace/dock_expansion_restore_layout_state");
    settings.remove("workspace/media_pool_restore_layout_state");
    updateMediaPoolExpandButton();
    updateInspectorExpandButton();

    bins_dock_->setFloating(false);
    media_dock_->setFloating(false);
    toolbox_dock_->setFloating(false);
    favorites_dock_->setFloating(false);
    effects_dock_->setFloating(false);
    preview_dock_->setFloating(false);
    inspector_dock_->setFloating(false);
    timeline_dock_->setFloating(false);

    addDockWidget(Qt::LeftDockWidgetArea, bins_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, media_dock_);
    splitDockWidget(bins_dock_, media_dock_, Qt::Vertical);
    resizeDocks({bins_dock_, media_dock_}, {300, 700}, Qt::Vertical);

    addDockWidget(Qt::LeftDockWidgetArea, toolbox_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, effects_dock_);
    splitDockWidget(toolbox_dock_, effects_dock_, Qt::Horizontal);
    resizeDocks({toolbox_dock_, effects_dock_}, {180, 420}, Qt::Horizontal);
    addDockWidget(Qt::LeftDockWidgetArea, favorites_dock_);
    splitDockWidget(toolbox_dock_, favorites_dock_, Qt::Vertical);
    resizeDocks({toolbox_dock_, favorites_dock_}, {300, 300}, Qt::Vertical);
    addDockWidget(Qt::RightDockWidgetArea, preview_dock_);
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    splitDockWidget(preview_dock_, inspector_dock_, Qt::Horizontal);
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);

    bins_dock_->show();
    media_dock_->show();
    toolbox_dock_->hide();
    favorites_dock_->hide();
    effects_dock_->hide();
    preview_dock_->show();
    inspector_dock_->show();
    timeline_dock_->show();
    updateMediaPoolExpandButton();
    updateMediaPoolActionState();
    updateEffectsActionState();
    restoring_default_dock_layout_ = false;
    saveWorkspaceLayout();
}
