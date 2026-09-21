#pragma once

#include "media/video_decoder.h"
#include "media/media_library.h"
#include "media/video_metadata.h"
#include "media/video_probe.h"
#include "playback/playback_worker.h"
#include "project/project_document.h"
#include "timeline/timeline_history.h"
#include "timeline/timeline_model.h"

#include <QMainWindow>
#include <QString>
#include <QThread>
#include <QtGlobal>

#include <cstdint>
#include <array>
#include <filesystem>
#include <optional>
#include <vector>

class QDockWidget;
class QScrollArea;
class QAction;
class QCloseEvent;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QFontComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QPoint;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;
class PreviewWidget;
class QWidget;

namespace timeline {
class TimelineWidget;
}

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct ImportedMedia;

    void createMenus();
    void createWorkspace();
    void restoreDefaultLayout();
    QWidget* createMediaBrowser();
    QWidget* createInspector();
    QWidget* createTimeline();
    void initializePlayback();
    void shutdownPlayback();
    void openMedia();
    void updateMediaDetails(int row);
    void populateMediaBrowser(const std::filesystem::path& selected_path = {});
    void updateMediaBrowserFilter();
    void showMediaContextMenu(const QPoint& position);
    void createBin();
    void renameSelectedBin();
    void renameSelectedMedia();
    void moveSelectedMediaToBin();
    void removeSelectedMedia();
    void restoreSelectedMedia();
    void addVideoTrack();
    void renameActiveTrack();
    void moveActiveTrack(int direction);
    void removeActiveTrack();
    void addTextClip();
    [[nodiscard]] std::optional<std::size_t> selectedMediaIndex() const noexcept;
    [[nodiscard]] std::string selectedBinPath() const;
    void addMediaItem(
        media::VideoMetadata metadata,
        media::VideoFrame first_frame,
        std::string display_name = {},
        std::string bin_path = "Unsorted",
        bool offline = false,
        bool mark_dirty = true);
    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();
    [[nodiscard]] bool confirmProjectChange();
    [[nodiscard]] bool saveProjectTo(
        const std::filesystem::path& project_path,
        const char* operation);
    [[nodiscard]] project::ProjectDocument currentProjectDocument() const;
    void updateProjectDirtyState();
    void clearProjectState();
    void applyLoadedProject(
        std::vector<ImportedMedia> media_items,
        timeline::TimelineModel::Snapshot timeline_snapshot,
        const std::filesystem::path& project_path,
        const project::ProjectDocument& saved_document);
    void addSelectedMediaToTimeline();
    void handleMediaDrop(const QString& source_path);
    void handleMediaDropAt(
        const QString& source_path,
        qint64 track_index,
        qint64 timeline_frame);
    void clearTimeline();
    void handleTimelineClipMove(qint64 from_index, qint64 to_index);
    void moveActiveTimelineClip(int direction);
    void deleteActiveTimelineClip();
    void splitActiveClipAtPlayhead();
    void handleTimelineClipSplit(qint64 clip_index, qint64 local_frame);
    void handleTimelineClipSelectedAt(qint64 track_index, qint64 clip_index);
    void handleTimelineClipMoveAt(
        qint64 from_track,
        qint64 from_clip,
        qint64 to_track,
        qint64 timeline_start_frame);
    void handleTimelineClipSplitAt(
        qint64 track_index,
        qint64 clip_index,
        qint64 local_frame);
    void handleTimelineTrimStarted();
    void handleTimelineClipTrim(
        qint64 clip_index,
        qint64 local_start_frame,
        qint64 local_end_frame);
    void handleTimelineClipTrimAt(
        qint64 track_index,
        qint64 clip_index,
        qint64 local_start_frame,
        qint64 local_end_frame);
    void handleTimelineTransitionSelectedAt(
        qint64 track_index,
        qint64 from_clip_index,
        qint64 to_clip_index);
    void handleTimelineTransitionAddRequestedAt(
        qint64 track_index,
        qint64 from_clip_index,
        qint64 to_clip_index,
        qint64 kind);
    void handleTimelineTransitionRemoveRequestedAt(
        qint64 track_index,
        qint64 from_clip_index,
        qint64 to_clip_index);
    void applyTransitionSettings();
    void removeSelectedTransition();
    void undoTimelineEdit();
    void redoTimelineEdit();
    [[nodiscard]] timeline::EditState captureTimelineEditState() const;
    void restoreTimelineEditState(timeline::EditState state, const char* operation);
    void recordTimelineEdit(timeline::EditState state);
    void updateHistoryActions();
    void updateTimelineState();
    void applyTimelineZoom(double factor);
    void beginAudioEdit();
    void finishAudioEdit();
    void applyClipAudioControls();
    void applyTrackAudioControls();
    void updatePlaybackAudioParameters();
    void sendCompositionToWorker();
    void updateInspector();
    void beginTransformEdit();
    void finishTransformEdit();
    void applyTransformProperty(int property_index, double value);
    void toggleTransformKeyframe(int property_index);
    void applyTextStyle();
    [[nodiscard]] bool hasSelectedMedia() const noexcept;
    [[nodiscard]] std::optional<timeline::ClipLocation>
    selectedTimelineClipLocation() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedTimelineClipIndex() const noexcept;
    [[nodiscard]] bool selectedMediaMatchesTimeline() const noexcept;
    [[nodiscard]] bool canPreviewSelectedMedia() const noexcept;
    [[nodiscard]] bool canPlaybackSelectedMedia() const noexcept;
    [[nodiscard]] std::int64_t timelinePlayheadFrame() const noexcept;
    void sendPlaybackCommand(const char* command);
    void updatePlaybackControls();
    void updatePlaybackStatus();
    void handlePlaybackFrame(
        playback::VideoFramePtr frame,
        qint64 frame_index,
        quint64 generation);
    void handlePlaybackMediaReady(quint64 generation);
    void handlePlaybackStateChanged(bool playing, quint64 generation);
    void handlePlaybackFinished(quint64 generation, bool during_playback);
    void handlePlaybackError(
        const QString& message,
        qint64 error_code,
        quint64 generation);
    void handleTimelineClipSelected(qint64 clip_index);
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
        std::size_t track_index,
        std::size_t clip_index,
        std::size_t media_index,
        std::int64_t frame_index,
        bool show_cached_frame,
        bool preserve_timeline_playhead = false);

    struct ImportedMedia {
        media::VideoMetadata metadata;
        media::VideoFrame first_frame;
        std::string display_name;
        std::string bin_path = "Unsorted";
        bool offline = false;
    };

    struct PendingClipActivation {
        std::size_t clip_index = 0;
        std::size_t media_index = 0;
        std::int64_t target_frame = 0;
        std::int64_t source_start_frame = 0;
        std::int64_t segment_frame_count = 0;
        bool resume_playback = false;
        quint64 generation = 0;
        std::size_t track_index = 0;
        bool preserve_timeline_playhead = false;
    };

    QDockWidget* media_browser_dock_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* timeline_dock_ = nullptr;
    PreviewWidget* preview_widget_ = nullptr;
    QListWidget* media_list_ = nullptr;
    QTreeWidget* bin_tree_ = nullptr;
    QLabel* media_details_ = nullptr;
    QPushButton* add_to_timeline_button_ = nullptr;
    QPushButton* new_bin_button_ = nullptr;
    QPushButton* previous_frame_button_ = nullptr;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* next_frame_button_ = nullptr;
    QPushButton* clear_timeline_button_ = nullptr;
    QPushButton* razor_button_ = nullptr;
    QPushButton* add_text_button_ = nullptr;
    QSlider* clip_volume_slider_ = nullptr;
    QSlider* track_volume_slider_ = nullptr;
    QCheckBox* clip_mute_check_ = nullptr;
    QCheckBox* track_mute_check_ = nullptr;
    QLabel* timeline_interaction_hint_ = nullptr;
    QLabel* playback_status_label_ = nullptr;
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
    QAction* add_video_track_action_ = nullptr;
    QAction* rename_track_action_ = nullptr;
    QAction* move_track_up_action_ = nullptr;
    QAction* move_track_down_action_ = nullptr;
    QAction* remove_track_action_ = nullptr;
    timeline::TimelineWidget* timeline_widget_ = nullptr;
    QScrollArea* timeline_scroll_ = nullptr;
    std::vector<ImportedMedia> media_items_;
    std::vector<std::string> bin_paths_{"Unsorted"};
    timeline::TimelineModel timeline_model_;
    timeline::TimelineHistory timeline_history_;
    std::optional<std::size_t> active_timeline_track_index_;
    std::optional<std::size_t> active_timeline_clip_index_;
    std::optional<std::int64_t> preserved_timeline_playhead_frame_;
    struct ActiveTransition {
        std::size_t track_index = 0;
        std::size_t from_clip_index = 0;
        std::size_t to_clip_index = 0;

        friend bool operator==(const ActiveTransition&, const ActiveTransition&) = default;
    };
    std::optional<ActiveTransition> active_transition_;
    std::optional<PendingClipActivation> pending_clip_activation_;
    std::optional<std::filesystem::path> project_path_;
    std::optional<project::ProjectDocument> saved_project_document_;
    std::optional<timeline::EditState> pending_audio_edit_;
    std::optional<timeline::EditState> pending_transform_edit_;
    bool project_dirty_ = false;
    media::VideoProbe video_probe_;
    media::VideoDecoder video_decoder_;
    QThread playback_thread_;
    playback::PlaybackWorker* playback_worker_ = nullptr;
    std::int64_t playback_frame_index_ = 0;
    quint64 playback_generation_ = 0;
    bool playback_is_playing_ = false;
    std::array<std::uint8_t, 4> text_color_{255, 255, 255, 255};
};
