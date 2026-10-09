#include "main_window/main_window.h"
#include "main_window_support.h"

#include "logging/logger.h"
#include "ui/preview/preview_widget.h"
#include "project/project_file.h"
#include "settings/settings_dialog.h"
#include "settings/shortcut_manager.h"
#include "timeline/timeline_widget.h"
#include "ui/functions/function_palette.h"
#include "ui/media_browser/media_browser_list_widget.h"
#include "ui/timeline/timeline_end_buttons.h"
#include "ui/workspace/workspace_host.h"
#include "ui/workspace/workspace_transition_controller.h"
#include "workspaces/fusion/ui/fusion_workspace.h"
#include "workspaces/render/ui/render_workspace.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QDesktopServices>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
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
#include <QSignalBlocker>
#include <QScrollArea>
#include <QSettings>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QSlider>
#include <QStatusBar>
#include <QToolBar>
#include <QTextEdit>
#include <QUrl>
#include <QWidget>
#include <QTreeWidget>
#include <QTreeWidgetItem>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>


using namespace main_window_detail;

namespace {

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
        &ui::EditWorkspaceController::timelineFusionClipOpenRequested,
        this,
        [this](timeline::ClipId clip_id) {
            if (editor_session_.selection().active_clip_id != clip_id) return;
            const auto location = editor_session_.timeline().locateClip(clip_id);
            if (!location.has_value()) return;
            const auto& track = editor_session_.timeline().tracks()[location->track_index];
            if (location->clip_index >= track.clips.size()) return;
            const auto kind = track.clips[location->clip_index].kind;
            if (kind != timeline::ClipKind::Video &&
                kind != timeline::ClipKind::Image) return;
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
                ? "Direct GPU preview unavailable; using RGBA delivery."
                : "GPU preview unavailable; using CPU preview.", 5000);
        });

    bins_dock_ = createDock(
        "Bins",
        "binsDock",
        createMediaBins());
    media_dock_ = createDock(
        "Media",
        "mediaDock",
        createMediaPanel());
    addDockWidget(Qt::LeftDockWidgetArea, bins_dock_);
    addDockWidget(Qt::LeftDockWidgetArea, media_dock_);
    splitDockWidget(bins_dock_, media_dock_, Qt::Vertical);
    resizeDocks({bins_dock_, media_dock_}, {300, 700}, Qt::Vertical);

    toolbox_dock_ = createDock(
        "Toolbox",
        "toolboxDock",
        createEffectsToolbox());
    effects_dock_ = createDock(
        "Effects",
        "effectsDock",
        createEffectsPanel());
    favorites_dock_ = createDock(
        "Favorites",
        "favoritesDock",
        createEffectsFavorites());
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
    preview_dock_layout->addWidget(preview_widget_, 1);
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
    render_workspace_button_ = workspace_buttons.render;
    if (auto* navigation_layout = qobject_cast<QHBoxLayout*>(
            edit_workspace_->ui().workspace_navigation_slot->layout())) {
        navigation_layout->addWidget(workspace_buttons_container_);
    }
    workspace_host_ = new ui::WorkspaceHost(
        edit_workspace_, fusion_workspace_, render_workspace_,
        preview_dock_contents, this);
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
    addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    splitDockWidget(preview_dock_, inspector_dock_, Qt::Horizontal);

    timeline_dock_ = createDock(
        "Timeline",
        "timelineDock",
        workspace_host_->lowerWorkspacePanel());
    addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);

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
                timeline_dock_},
            {
                edit_workspace_button_,
                fusion_workspace_button_,
                render_workspace_button_},
            this);
    workspace_transition_controller_->setPageChangedHandler(
        [this](ui::WorkspacePageId page) { handleWorkspacePageChanged(page); });

    function_palette_ = new ui::FunctionPalette(this, *shortcut_manager_);
    connect(function_palette_, &ui::FunctionPalette::effectAddRequested,
            edit_workspace_->controller(),
            &ui::EditWorkspaceController::addEffectToSelectedClip);
    connect(edit_workspace_->controller(),
            &ui::EditWorkspaceController::effectTargetAvailabilityChanged,
            function_palette_, &ui::FunctionPalette::setEffectTargetAvailable);
    function_palette_->setEffectTargetAvailable(
        edit_workspace_->controller()->selectedClipSupportsEffects());

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
}

void MainWindow::setWorkspacePage(ui::WorkspacePageId page) {
    if (workspace_transition_controller_ != nullptr) {
        workspace_transition_controller_->setPage(page);
    }
}

void MainWindow::handleWorkspacePageChanged(ui::WorkspacePageId page) {
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
    if (menuBar() == nullptr) return;
    for (auto* action : menuBar()->actions()) {
        if (action != nullptr && action->menu() != nullptr) {
            refreshMenuVisibility(action->menu());
        }
    }
}

void MainWindow::showSettingsDialog() {
    settings::SettingsDialog dialog(this, *shortcut_manager_);
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
    new_project_action_ = file_menu->addAction("&New Project");
    register_menu_action(new_project_action_, settings::ShortcutScope::Application);
    disableDuringProjectLoad(new_project_action_);
    new_project_action_->setShortcut(QKeySequence("Ctrl+N"));
    new_project_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.new_project"), QStringLiteral("New Project"),
        new_project_action_, settings::ShortcutScope::Application,
        QStringLiteral("All workspaces"));
    connect(new_project_action_, &QAction::triggered, this, &MainWindow::newProject);
    project_settings_action_ = file_menu->addAction("Project Settings...");
    register_menu_action(project_settings_action_, settings::ShortcutScope::Application);
    project_settings_action_->setObjectName(QStringLiteral("projectSettingsAction"));
    disableDuringProjectLoad(project_settings_action_);
    connect(project_settings_action_, &QAction::triggered,
            this, &MainWindow::showProjectSettingsDialog);
    auto* open_media_action = file_menu->addAction("Open &Media...");
    register_menu_action(open_media_action, settings::ShortcutScope::Application);
    disableDuringProjectLoad(open_media_action);
    connect(open_media_action, &QAction::triggered, this, &MainWindow::openMedia);
    open_project_action_ = file_menu->addAction("&Open Project...");
    register_menu_action(open_project_action_, settings::ShortcutScope::Application);
    disableDuringProjectLoad(open_project_action_);
    open_project_action_->setShortcut(QKeySequence("Ctrl+O"));
    open_project_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.open_project"), QStringLiteral("Open Project"),
        open_project_action_, settings::ShortcutScope::Application,
        QStringLiteral("All workspaces"));
    connect(open_project_action_, &QAction::triggered, this, &MainWindow::openProject);
    save_project_action_ = file_menu->addAction("&Save Project");
    register_menu_action(save_project_action_, settings::ShortcutScope::Application);
    disableDuringProjectLoad(save_project_action_);
    save_project_action_->setShortcut(QKeySequence("Ctrl+S"));
    save_project_action_->setShortcutContext(Qt::WindowShortcut);
    register_shortcut(
        QStringLiteral("file.save_project"), QStringLiteral("Save Project"),
        save_project_action_, settings::ShortcutScope::Application,
        QStringLiteral("All workspaces"));
    connect(save_project_action_, &QAction::triggered, this, &MainWindow::saveProject);
    save_project_as_action_ = file_menu->addAction("Save Project &As...");
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
    auto* exit_action = file_menu->addAction("E&xit");
    register_menu_action(exit_action, settings::ShortcutScope::Application);
    connect(exit_action, &QAction::triggered, this, &QWidget::close);

    auto* edit_menu = menuBar()->addMenu("&Edit");
    edit_menu->setObjectName(QStringLiteral("editMenu"));
    auto* undo_action = edit_menu->addAction("&Undo");
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
    auto* redo_action = edit_menu->addAction("&Redo");
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
    auto* effects_view_action = effects_dock_->toggleViewAction();
    effects_menu->addAction(toolbox_view_action);
    effects_menu->addAction(favorites_view_action);
    effects_menu->addAction(effects_view_action);
    register_menu_action(toolbox_view_action, settings::ShortcutScope::Shared);
    register_menu_action(favorites_view_action, settings::ShortcutScope::Shared);
    register_menu_action(effects_view_action, settings::ShortcutScope::Shared);
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
    auto* grayscale_action = view_menu->addAction("Grayscale Preview");
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
    auto* restore_layout_action = view_menu->addAction("Restore &Default Layout");
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
    media_pool_action_ = media_pool_toolbar->addAction("Media Pool");
    register_menu_action(media_pool_action_, settings::ShortcutScope::Shared);
    media_pool_action_->setCheckable(true);
    media_pool_action_->setToolTip("Show the Media Pool docks");
    effects_action_ = media_pool_toolbar->addAction("Effects");
    register_menu_action(effects_action_, settings::ShortcutScope::Shared);
    effects_action_->setCheckable(true);
    effects_action_->setToolTip("Show the Effects docks");
    auto* workspace_toolbar_spacer = new QWidget(media_pool_toolbar);
    workspace_toolbar_spacer->setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Preferred);
    media_pool_toolbar->addWidget(workspace_toolbar_spacer);
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
            this, [this](bool) { activateEffectsGroup(); });
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

    auto* settings_action = menuBar()->addAction("&Settings");
    settings_action->setObjectName(QStringLiteral("settingsMenuAction"));
    register_menu_action(settings_action, settings::ShortcutScope::Application);
    settings_action->setToolTip("Open editor settings");
    connect(settings_action, &QAction::triggered,
            this, &MainWindow::showSettingsDialog);

    auto* help_menu = menuBar()->addMenu("&Help");
    help_menu->setObjectName(QStringLiteral("helpMenu"));
    auto* system_action = help_menu->addAction("&System");
    register_menu_action(system_action, settings::ShortcutScope::Application);
    connect(system_action, &QAction::triggered, this, [this]() {
        const auto version = QCoreApplication::applicationVersion();
        const auto executable_path = QCoreApplication::applicationFilePath();
        QMessageBox::information(
            this,
            "System",
            QString("Video Editor\n\nVersion: %1\nExecutable: %2")
                .arg(version.isEmpty() ? QStringLiteral("Beta 0.1.0") : version,
                     executable_path.isEmpty()
                         ? QStringLiteral("N/A")
                         : executable_path));
    });
    auto* open_log_folder_action = help_menu->addAction("Open &Log Folder");
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
            statusBar()->showMessage("Could not open the log folder.");
            return;
        }

        logger.log(
            logging::Level::Info,
            "ui",
            "open_log_folder",
            "Opened the log directory.",
            {{"path", directory_text}});
        statusBar()->showMessage("Log folder opened.");
    });
    help_menu->addSeparator();
    auto* about_action = help_menu->addAction("&About Video Editor");
    register_menu_action(about_action, settings::ShortcutScope::Application);
    connect(about_action, &QAction::triggered, this, [this]() {
        QMessageBox::about(
            this,
            "About Video Editor",
            "Video Editor application shell\n\n"
            "This is an early open-source creative suite workspace.");
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
                    : QStringLiteral("%1 (%2)").arg(tooltip, shortcut_text));
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
    const auto saved_state = settings.value(
        "workspace/dock_layout_state").toByteArray();
    if (!saved_state.isEmpty() && restoreState(saved_state, 9)) {
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
        return;
    }
    if (!saved_state.isEmpty() && restoreState(saved_state, 7)) {
        // Version 7 predates the Preview dock. Keep its existing panel layout
        // and add Preview as the large panel beside Inspector.
        placePreviewBesideInspector();
        preview_dock_->show();
        initial_window_layout_pending_ = false;
        saveWorkspaceLayout();
        return;
    }

    restoreDefaultLayout();
}

void MainWindow::saveWorkspaceLayout() {
    QSettings settings;
    settings.setValue("workspace/dock_layout_state", saveState(9));
    settings.sync();
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
}

void MainWindow::activateEffectsGroup() {
    bins_dock_->hide();
    media_dock_->hide();
    toolbox_dock_->show();
    favorites_dock_->show();
    effects_dock_->show();
    updateMediaPoolActionState();
    updateEffectsActionState();
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
    effects_action_->setChecked(
        toolbox_dock_->isVisible() &&
        favorites_dock_->isVisible() &&
        effects_dock_->isVisible() &&
        !bins_dock_->isVisible() && !media_dock_->isVisible());
}

void MainWindow::restoreDefaultLayout() {
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
    updateMediaPoolActionState();
    updateEffectsActionState();
    saveWorkspaceLayout();
}
