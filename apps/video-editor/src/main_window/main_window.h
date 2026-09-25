#pragma once

#include "media/media_library.h"
#include "media/video_metadata.h"
#include "application/editor_session.h"
#include "application/media_controller.h"
#include "application/media_import_service.h"
#include "application/project_controller.h"
#include "application/project_open_service.h"
#include "application/timeline_command_service.h"
#include "playback/playback_controller.h"
#include "project/autosave_manager.h"
#include "project/project_document.h"
#include "system/performance_usage.h"
#include "timeline/timeline_history.h"
#include "timeline/timeline_model.h"

#include <QMainWindow>
#include <QString>
#include <QThreadPool>
#include <QtGlobal>

#include <cstdint>
#include <atomic>
#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <functional>
#include <utility>
#include <vector>

class QDockWidget;
class QAction;
class QCloseEvent;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QProgressDialog;
class QPoint;
class QSlider;
class QSpinBox;
class QTabWidget;
class QScrollArea;
class QTimer;
class MediaBrowserBinTreeWidget;
class MediaBrowserListWidget;
class QListWidgetItem;
class QTreeWidgetItem;
class QVBoxLayout;
class PreviewWidget;
class SystemMemoryIndicator;
class EffectsToolboxWidget;
class EffectsListWidget;
class EffectsFavoritesWidget;
class QWidget;
namespace ui {
class FunctionPalette;
class WorkspacePageView;
}

namespace timeline {
class TimelineWidget;
class TimelineTrackHeaderOverlay;
}

namespace settings {
class ShortcutManager;
struct AutosaveSnapshotItem;
}

class MainWindowIntegrationTest;

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    friend class MainWindowIntegrationTest;

    using ImportedMedia = application::ImportedMedia;
    using ActiveTransition = timeline::TransitionSelection;
    struct TimelineControls;

    enum class WorkspacePage {
        Edit,
        Fusion,
        Render
    };

    void createMenus();
    void createWorkspace();
    void setWorkspacePage(WorkspacePage page);
    void showSettingsDialog();
    void restoreDefaultLayout();
    void restoreWorkspaceLayout();
    void saveWorkspaceLayout();
    void applyInitialWindowLayout();
    void saveWindowGeometry();
    void activateMediaPoolGroup();
    void activateEffectsGroup();
    void updateMediaPoolActionState();
    void updateEffectsActionState();
    QWidget* createMediaBins();
    QWidget* createMediaPanel();
    QWidget* createEffectsToolbox();
    QWidget* createEffectsPanel();
    QWidget* createEffectsFavorites();
    QWidget* createInspector();
    QWidget* createTimeline();
    TimelineControls createTimelineControls(QWidget* container, QVBoxLayout* layout);
    void createTimelineViewport(QWidget* container, QVBoxLayout* layout);
    void createTimelineFooter(QWidget* container, QVBoxLayout* layout);
    void connectTimelineSignals(const TimelineControls& controls);
    void initializePlayback();
    void shutdownPlayback();
    void configurePreviewPerformanceMetrics(bool enabled);
    void flushPreviewPerformanceMetrics();
    void configureProjectAutosave(bool enabled, int interval_seconds);
    void autosaveProject();
    [[nodiscard]] std::vector<settings::AutosaveSnapshotItem>
    autosaveSnapshotsForSettings() const;
    [[nodiscard]] bool restoreAutosaveSnapshot(
        const QString& snapshot_path,
        const QString& project_path);
    void deleteAutosaveSnapshot(const QString& snapshot_path);
    void openAutosaveFolder(const QString& folder_path);
    void offerUnsavedProjectRecovery();
    [[nodiscard]] std::optional<std::filesystem::path> chooseRecoverySnapshot(
        const std::vector<project::AutosaveSnapshot>& snapshots,
        const QString& project_name);
    void openMedia();
    [[nodiscard]] bool startMediaImport(
        std::vector<std::filesystem::path> paths);
    void finishMediaImport(application::MediaImportBatchResult result);
    void updateMediaDetails(int row);
    void populateMediaBrowser(
        const std::filesystem::path& selected_path = {},
        std::optional<std::string> selected_bin = std::nullopt);
    void selectMediaBrowserBin(const QString& path);
    void showMediaContextMenu(const QPoint& position);
    void editSelectedMediaInImageEditor();
    void editTimelineImageClip(timeline::ClipId clip_id);
    void handleMediaBrowserMediaDrop(
        const QString& source_path,
        const QString& destination_bin);
    void handleMediaBrowserBinDrop(
        const QString& source_bin,
        const QString& destination_bin);
    void handleMediaBrowserListItemChanged(QListWidgetItem* item);
    void handleMediaBrowserBinItemChanged(QTreeWidgetItem* item, int column);
    void selectMediaBrowserListBin(const QString& path);
    void beginMediaBrowserBinEdit(const QString& path);
    void createBin();
    void moveSelectedMediaToBin();
    void removeSelectedMedia();
    void restoreSelectedMedia();
    void addVideoTrack();
    void renameActiveTrack();
    void moveActiveTrack(int direction);
    void removeActiveTrack();
    void addTextClipAt(timeline::TrackId track_id, qint64 timeline_frame);
    void handleEffectDropAt(
        const QString& effect_id,
        timeline::TrackId track_id,
        qint64 timeline_frame);
    [[nodiscard]] std::optional<std::size_t> selectedMediaIndex() const noexcept;
    [[nodiscard]] std::string selectedBinPath() const;
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    [[nodiscard]] bool openProjectPath(
        const std::filesystem::path& source_path,
        std::optional<std::filesystem::path> active_project_path = std::nullopt,
        std::optional<project::ProjectDocument> saved_baseline = std::nullopt,
        std::function<void(bool)> completion = {});
    void finishProjectOpen(
        std::uint64_t work_id,
        std::uint64_t project_generation,
        application::ProjectOpenResult result);
    void setProjectLoadingState(bool loading);
    [[nodiscard]] bool confirmProjectChange();
    [[nodiscard]] bool saveProjectTo(
        const std::filesystem::path& project_path,
        const char* operation);
    [[nodiscard]] project::ProjectDocument currentProjectDocument() const;
    void updateProjectDirtyState();
    void initializeLinkedImageCompatibility();
    void refreshLinkedImageTargets();
    void pollLinkedImageOutputs();
    void refreshLinkedImageOutput(
        const media::LinkedImageReference& link,
        const std::filesystem::path& source_path,
        std::vector<timeline::ClipId> clip_ids,
        bool media_asset,
        std::uint64_t project_generation,
        std::uintmax_t size,
        std::filesystem::file_time_type modified);
    void applyLinkedImageRefresh(
        const media::LinkedImageReference& link,
        const std::filesystem::path& source_path,
        std::vector<timeline::ClipId> clip_ids,
        bool media_asset,
        std::uint64_t project_generation,
        std::uintmax_t size,
        std::filesystem::file_time_type modified,
        media::VideoMetadata metadata,
        media::VideoFrame frame,
        std::string failure);
    [[nodiscard]] bool launchLinkedImageEditor(
        const media::LinkedImageReference& link,
        const std::filesystem::path& source_path,
        std::optional<timeline::ClipId> clip_id = std::nullopt);
    void clearProjectState();
    void applyLoadedProject(application::PreparedProject prepared);
    void addSelectedMediaToTimeline();
    void handleMediaDrop(const QString& source_path);
    void handleMediaDropAt(
        const QString& source_path,
        timeline::TrackId track_id,
        qint64 timeline_frame);
    void clearTimeline();
    void moveActiveTimelineClip(int direction);
    void deleteActiveTimelineClip();
    void splitActiveClipAtPlayhead();
    void handleTimelineClipSelectionChanged(
        timeline::TrackId track_id,
        timeline::ClipId clip_id);
    void handleTimelineClipSelectionCleared();
    void handleTimelineClipMove(
        timeline::ClipId clip_id,
        timeline::TrackId target_track_id,
        qint64 timeline_start_frame);
    void handleTimelineClipSplit(timeline::ClipId clip_id, qint64 local_frame);
    void handleTimelineTrimStarted();
    void handleTimelineClipTrim(
        timeline::ClipId clip_id,
        qint64 edge,
        qint64 boundary_frame,
        qint64 mode);
    void handleTimelineTransitionSelected(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id);
    void handleTimelineTransitionSelectionCleared();
    void handleTimelineTransitionAddRequested(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id,
        qint64 kind);
    void handleTimelineTransitionRemoveRequested(
        timeline::TrackId track_id,
        timeline::ClipId from_clip_id,
        timeline::ClipId to_clip_id);
    void applyTransitionSettings();
    void removeSelectedTransition();
    void undoTimelineEdit();
    void redoTimelineEdit();
    void synchronizeTimelineSessionSelection();
    [[nodiscard]] timeline::EditState captureTimelineEditState();
    void recordTimelineEdit(timeline::EditState state);
    void applyTimelineEditResult(
        const application::TimelineEditResult& result,
        bool stop_playback = true);
    template <typename Command>
    [[nodiscard]] application::TimelineEditResult executeTimelineCommand(
        const Command& command) {
        synchronizeTimelineSessionSelection();
        return timeline_command_service_.execute(command);
    }
    void updateHistoryActions();
    void updateTimelineState();
    void applyTimelineZoom(double factor);
    void beginAudioEdit();
    void finishAudioEdit();
    void applyClipAudioControls();
    void applyTrackAudioControls();
    void updatePlaybackAudioParameters();
    void applyMonitorVolumePercent(int percent);
    void refreshPlaybackComposition();
    void updateInspector();
    void beginTransformEdit();
    void finishTransformEdit();
    void applyTransformProperty(int property_index, double value);
    void toggleTransformKeyframe(int property_index);
    void applyTextStyle();
    [[nodiscard]] bool hasSelectedMedia() const noexcept;
    [[nodiscard]] std::optional<timeline::ClipLocation>
    selectedTimelineClipLocation() const noexcept;
    void synchronizeActiveTimelineSelection() noexcept;
    void setActiveTimelineSelection(timeline::ClipLocation location) noexcept;
    void clearActiveTimelineSelection() noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedTimelineClipIndex() const noexcept;
    [[nodiscard]] bool selectedMediaMatchesTimeline() const noexcept;
    [[nodiscard]] bool canPreviewSelectedMedia() const noexcept;
    [[nodiscard]] bool canPlaybackSelectedMedia() const noexcept;
    [[nodiscard]] std::optional<timeline::ClipLocation>
    timelineClipAtPlayhead() const noexcept;
    [[nodiscard]] bool canPlaybackTimelineAtPlayhead() const noexcept;
    [[nodiscard]] std::int64_t timelinePlayheadFrame() const noexcept;
    void sendPlaybackCommand(playback::PlaybackCommand command);
    void updatePlaybackControls();
    void updatePlaybackStatus();
    void handlePlaybackEvent(const playback::PlaybackControllerEvent& event);
    void handlePlaybackFrame(const playback::PlaybackFrameEvent& event);
    void handlePlaybackActivation(const playback::PlaybackActivationEvent& event);
    void handlePlaybackStateChanged(bool playing);
    void handlePlaybackFinished(bool during_playback, bool gap);
    void handlePlaybackError(const playback::PlaybackErrorEvent& event);
    void handleTimelineClipSelected(timeline::ClipId clip_id);
    void handleTimelineSeekStarted();
    void handleTimelineSeek(qint64 global_frame);
    void activateTimelineClip(
        std::size_t clip_index,
        std::int64_t target_frame,
        bool resume_playback);
    void activateTimelineClipAt(
        std::size_t track_index,
        std::size_t clip_index,
        std::int64_t target_frame,
        bool resume_playback,
        bool preserve_timeline_playhead = false);
    void commitTimelineClipActivation(
        timeline::ClipId clip_id,
        std::int64_t frame_index,
        bool show_cached_frame,
        bool preserve_timeline_playhead = false);

    QDockWidget* bins_dock_ = nullptr;
    QDockWidget* media_dock_ = nullptr;
    QDockWidget* toolbox_dock_ = nullptr;
    QDockWidget* favorites_dock_ = nullptr;
    QDockWidget* effects_dock_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* timeline_dock_ = nullptr;
    PreviewWidget* preview_widget_ = nullptr;
    ui::FunctionPalette* function_palette_ = nullptr;
    ui::WorkspacePageView* workspace_page_view_ = nullptr;
    QWidget* workspace_buttons_container_ = nullptr;
    QWidget* timeline_controls_container_ = nullptr;
    QWidget* timeline_footer_ = nullptr;
    QPushButton* edit_workspace_button_ = nullptr;
    QPushButton* fusion_workspace_button_ = nullptr;
    QPushButton* render_workspace_button_ = nullptr;
    MediaBrowserListWidget* media_list_ = nullptr;
    MediaBrowserBinTreeWidget* bin_tree_ = nullptr;
    EffectsToolboxWidget* effects_toolbox_ = nullptr;
    EffectsFavoritesWidget* effects_favorites_ = nullptr;
    EffectsListWidget* effects_list_ = nullptr;
    QPushButton* previous_frame_button_ = nullptr;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* next_frame_button_ = nullptr;
    QPushButton* clear_timeline_button_ = nullptr;
    QPushButton* selection_button_ = nullptr;
    QPushButton* razor_button_ = nullptr;
    QPushButton* snap_button_ = nullptr;
    QSlider* monitor_volume_slider_ = nullptr;
    QLabel* monitor_volume_indicator_ = nullptr;
    QSlider* clip_volume_slider_ = nullptr;
    QSlider* track_volume_slider_ = nullptr;
    QCheckBox* clip_mute_check_ = nullptr;
    QCheckBox* track_mute_check_ = nullptr;
    QLabel* playback_status_label_ = nullptr;
    QLabel* timeline_message_label_ = nullptr;
    SystemMemoryIndicator* system_memory_indicator_ = nullptr;
    QTimer* preview_metrics_timer_ = nullptr;
    QTimer* autosave_timer_ = nullptr;
    QTimer* linked_image_poll_timer_ = nullptr;
    system_monitor::PerformanceSampler performance_sampler_;
    bool media_browser_inline_rename_pending_ = false;
    std::array<QDoubleSpinBox*, 5> transform_spin_boxes_{};
    std::array<QSlider*, 5> transform_sliders_{};
    std::array<QPushButton*, 5> transform_key_buttons_{};
    QWidget* text_controls_ = nullptr;
    QPlainTextEdit* text_content_editor_ = nullptr;
    QFontComboBox* text_font_combo_ = nullptr;
    QSpinBox* text_font_size_spin_ = nullptr;
    QComboBox* text_alignment_combo_ = nullptr;
    QPushButton* text_color_button_ = nullptr;
    QPushButton* apply_text_button_ = nullptr;
    QWidget* transition_controls_ = nullptr;
    QComboBox* transition_type_combo_ = nullptr;
    QSpinBox* transition_duration_spin_ = nullptr;
    QPushButton* apply_transition_button_ = nullptr;
    QPushButton* remove_transition_button_ = nullptr;
    QTabWidget* inspector_tabs_ = nullptr;
    QAction* new_project_action_ = nullptr;
    QAction* open_project_action_ = nullptr;
    QAction* save_project_action_ = nullptr;
    QAction* save_project_as_action_ = nullptr;
    QAction* delete_clip_action_ = nullptr;
    QAction* undo_action_ = nullptr;
    QAction* redo_action_ = nullptr;
    QAction* razor_tool_action_ = nullptr;
    QAction* require_alt_to_move_action_ = nullptr;
    QAction* move_playhead_on_clip_selection_action_ = nullptr;
    QAction* media_pool_action_ = nullptr;
    QAction* effects_action_ = nullptr;
    QAction* add_video_track_action_ = nullptr;
    QAction* rename_track_action_ = nullptr;
    QAction* move_track_up_action_ = nullptr;
    QAction* move_track_down_action_ = nullptr;
    QAction* remove_track_action_ = nullptr;
    std::unique_ptr<settings::ShortcutManager> shortcut_manager_;
    timeline::TimelineWidget* timeline_widget_ = nullptr;
    timeline::TimelineTrackHeaderOverlay* timeline_header_overlay_ = nullptr;
    QScrollArea* timeline_scroll_ = nullptr;
    application::EditorSession editor_session_;
    std::unique_ptr<playback::PlaybackController> playback_controller_;
    application::TimelineCommandService timeline_command_service_{editor_session_};
    application::MediaController media_controller_{editor_session_};
    application::ProjectController project_controller_{editor_session_};
    const std::vector<ImportedMedia>& media_items_ = editor_session_.mediaItems();
    const std::vector<std::string>& bin_paths_ = editor_session_.binPaths();
    const timeline::TimelineModel& timeline_model_ = editor_session_.timeline();
    // Stable identities are the source of truth for selection. The index
    // fields below remain as short-lived presentation/worker coordinates.
    std::optional<timeline::TrackId>& active_timeline_track_id_ =
        editor_session_.selectionForUi().active_track_id;
    std::optional<timeline::ClipId>& active_timeline_clip_id_ =
        editor_session_.selectionForUi().active_clip_id;
    std::optional<std::size_t> active_timeline_track_index_cache_;
    std::optional<std::size_t> active_timeline_clip_index_cache_;
    std::optional<std::int64_t>& preserved_timeline_playhead_frame_ =
        editor_session_.preservedPlayheadFrameForUi();
    std::optional<ActiveTransition>& active_transition_ =
        editor_session_.selectionForUi().active_transition;
    const std::optional<std::filesystem::path>& project_path_ =
        editor_session_.projectPath();
    const std::optional<project::ProjectDocument>& saved_project_document_ =
        editor_session_.savedProjectDocument();
    std::optional<application::TimelineCommandService::EditBatchId>
        pending_audio_edit_batch_id_;
    std::optional<application::TimelineCommandService::EditBatchId>
        pending_transform_edit_batch_id_;
    const bool& project_dirty_ = editor_session_.projectDirtyState();
    WorkspacePage workspace_page_ = WorkspacePage::Edit;
    std::array<bool, 7> dock_visibility_before_render_{};
    bool has_render_dock_visibility_snapshot_ = false;
    bool initial_window_layout_pending_ = false;
    bool playback_activation_loading_ = false;
    QThreadPool media_task_pool_;
    QProgressDialog* media_import_progress_ = nullptr;
    std::shared_ptr<std::atomic_bool> active_media_import_cancel_;
    std::uint64_t project_generation_ = 0;
    std::uint64_t selection_generation_ = 0;
    std::uint64_t next_media_work_id_ = 1;
    std::uint64_t active_media_work_id_ = 0;
    std::uint64_t next_project_work_id_ = 1;
    std::uint64_t active_project_work_id_ = 0;
    struct LinkedImageWatchTarget {
        media::LinkedImageReference link;
        std::filesystem::path source_path;
        std::vector<timeline::ClipId> clip_ids;
        bool media_asset = false;
        bool has_signature = false;
        std::uintmax_t size = 0;
        std::filesystem::file_time_type modified{};
        bool refresh_pending = false;
    };
    std::vector<LinkedImageWatchTarget> linked_image_watch_targets_;
    std::filesystem::path active_project_source_path_;
    bool project_load_pending_ = false;
    QProgressDialog* project_load_progress_ = nullptr;
    std::shared_ptr<std::atomic_bool> project_load_cancel_;
    std::function<void(bool)> project_open_completion_;
    std::vector<std::pair<QWidget*, bool>> project_loading_widget_states_;
    std::vector<std::pair<QAction*, bool>> project_loading_action_states_;
    std::int64_t& playback_frame_index_ = editor_session_.playheadFrameForUi();
    bool playback_is_playing_ = false;
    std::array<std::uint8_t, 4> text_color_{255, 255, 255, 255};
};
