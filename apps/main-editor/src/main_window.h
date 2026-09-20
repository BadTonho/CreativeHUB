#pragma once

#include "media/video_decoder.h"
#include "media/video_metadata.h"
#include "media/video_probe.h"
#include "playback/playback_worker.h"
#include "timeline/timeline_model.h"

#include <QMainWindow>
#include <QString>
#include <QThread>
#include <QtGlobal>

#include <cstdint>
#include <optional>
#include <vector>

class QDockWidget;
class QLabel;
class QListWidget;
class QPushButton;
class PreviewWidget;
class QWidget;

namespace timeline {
class TimelineWidget;
}

class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private:
    void createMenus();
    void createWorkspace();
    void restoreDefaultLayout();
    QWidget* createMediaBrowser();
    QWidget* createTimeline();
    void initializePlayback();
    void shutdownPlayback();
    void openMedia();
    void updateMediaDetails(int row);
    void addMediaItem(media::VideoMetadata metadata, media::VideoFrame first_frame);
    void addSelectedMediaToTimeline();
    void handleMediaDrop(const QString& source_path);
    void clearTimeline();
    void updateTimelineState();
    [[nodiscard]] bool hasSelectedMedia() const noexcept;
    [[nodiscard]] std::optional<std::size_t> selectedTimelineClipIndex() const noexcept;
    [[nodiscard]] bool selectedMediaMatchesTimeline() const noexcept;
    [[nodiscard]] bool canPreviewSelectedMedia() const noexcept;
    void sendPlaybackCommand(const char* command);
    void updatePlaybackControls();
    void updatePlaybackStatus();
    void handlePlaybackFrame(
        playback::VideoFramePtr frame,
        qint64 frame_index,
        quint64 generation);
    void handlePlaybackStateChanged(bool playing, quint64 generation);
    void handlePlaybackFinished(quint64 generation);
    void handlePlaybackError(const QString& message, quint64 generation);
    void handleTimelineClipSelected(qint64 clip_index);
    void handleTimelineSeekStarted();
    void handleTimelineSeek(qint64 frame_index);

    struct ImportedMedia {
        media::VideoMetadata metadata;
        media::VideoFrame first_frame;
    };

    QDockWidget* media_browser_dock_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* timeline_dock_ = nullptr;
    PreviewWidget* preview_widget_ = nullptr;
    QListWidget* media_list_ = nullptr;
    QLabel* media_details_ = nullptr;
    QPushButton* add_to_timeline_button_ = nullptr;
    QPushButton* previous_frame_button_ = nullptr;
    QPushButton* play_pause_button_ = nullptr;
    QPushButton* next_frame_button_ = nullptr;
    QPushButton* clear_timeline_button_ = nullptr;
    QLabel* playback_status_label_ = nullptr;
    timeline::TimelineWidget* timeline_widget_ = nullptr;
    std::vector<ImportedMedia> media_items_;
    timeline::TimelineModel timeline_model_;
    std::optional<std::size_t> active_timeline_clip_index_;
    media::VideoProbe video_probe_;
    media::VideoDecoder video_decoder_;
    QThread playback_thread_;
    playback::PlaybackWorker* playback_worker_ = nullptr;
    std::int64_t playback_frame_index_ = 0;
    quint64 playback_generation_ = 0;
    bool playback_is_playing_ = false;
};
