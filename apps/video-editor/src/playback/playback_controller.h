#pragma once

#include "application/editor_session.h"
#include "playback_frame_mailbox.h"
#include "playback_worker.h"

#include <QThread>
#include <QObject>
#include <QString>
#include <QtGlobal>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <variant>

namespace playback {

enum class PlaybackStepDirection { Forward, Backward };
enum class PlaybackCommand { Play, Pause, StepForward, StepBackward };
enum class PlaybackCommandResult {
    Applied,
    Pending,
    NoClip,
    Gap,
    Beginning,
    End,
    Unavailable,
    Rejected,
};

enum class PlaybackActivationPhase { Pending, Committed, Discarded };

struct PlaybackActivationEvent {
    timeline::ClipId clip_id = 0;
    PlaybackActivationPhase phase = PlaybackActivationPhase::Pending;
    std::int64_t frame_index = 0;
    bool show_cached_frame = false;
    bool preserve_timeline_playhead = false;
    bool clear_selection = false;
};

struct PlaybackFrameEvent {
    VideoFramePtr frame;
    std::int64_t frame_index = 0;
    timeline::ClipId clip_id = 0;
};

struct PlaybackStateEvent {
    bool playing = false;
};

struct PlaybackFinishedEvent {
    bool during_playback = false;
    bool gap = false;
};

struct PlaybackErrorEvent {
    QString message;
    qint64 error_code = -1;
    quint64 generation = 0;
    std::optional<timeline::ClipId> activation_clip_id;
    std::filesystem::path source_path;
    std::int64_t target_frame = 0;
    std::int64_t source_start_frame = 0;
    std::int64_t segment_frame_count = 0;
};

struct PlaybackAudioWarningEvent {
    QString message;
    qint64 error_code = -1;
};

using PlaybackControllerEvent = std::variant<
    PlaybackActivationEvent,
    PlaybackFrameEvent,
    PlaybackStateEvent,
    PlaybackFinishedEvent,
    PlaybackErrorEvent,
    PlaybackAudioWarningEvent>;

class PlaybackController final : public QObject {
public:
    using EventHandler = std::function<void(const PlaybackControllerEvent&)>;
    using WorkerFactory = std::function<PlaybackWorker*()>;

    explicit PlaybackController(
        application::EditorSession& session,
        QObject* parent = nullptr,
        WorkerFactory worker_factory = {});
    ~PlaybackController() override;

    PlaybackController(const PlaybackController&) = delete;
    PlaybackController& operator=(const PlaybackController&) = delete;

    void setEventHandler(EventHandler handler);
    void shutdown() noexcept;

    [[nodiscard]] bool available() const noexcept;
    [[nodiscard]] bool isPlaying() const noexcept;

    void refreshComposition();
    void setMonitorVolume(double gain);
    void setAudioParametersForActiveClip();
    void invalidate(bool stop_worker);
    void pause();
    void stop();
    void seekActiveClip(std::int64_t local_frame);
    void renderCompositionFrame(
        std::int64_t global_frame,
        std::int64_t local_frame);

    [[nodiscard]] PlaybackCommandResult activateClip(
        timeline::ClipId clip_id,
        std::int64_t local_frame,
        bool resume_playback,
        bool preserve_timeline_playhead = false);
    [[nodiscard]] PlaybackCommandResult play();
    [[nodiscard]] PlaybackCommandResult execute(PlaybackCommand command);
    [[nodiscard]] PlaybackCommandResult step(PlaybackStepDirection direction);
    [[nodiscard]] PlaybackCommandResult seekTimeline(std::int64_t global_frame);

private:
    struct PendingActivation {
        timeline::ClipId clip_id = 0;
        std::filesystem::path canonical_source_path;
        std::int64_t target_frame = 0;
        std::int64_t source_start_frame = 0;
        std::int64_t segment_frame_count = 0;
        bool resume_playback = false;
        bool preserve_timeline_playhead = false;
        quint64 generation = 0;
    };

    void queueWorker(
        std::function<void(PlaybackWorker&)> operation,
        std::optional<quint64> request_generation = std::nullopt);
    void requestSeekForGeneration(qint64 frame, quint64 request_generation);
    void handleWorkerMediaReady(quint64 generation);
    void handleWorkerStateChanged(bool playing, quint64 generation);
    void handleWorkerFinished(quint64 generation, bool during_playback);
    void handleWorkerError(
        const QString& message,
        qint64 error_code,
        quint64 generation);
    void handleWorkerAudioWarning(
        const QString& message,
        qint64 error_code,
        quint64 generation);
    void queueFrame(
        VideoFramePtr frame,
        qint64 frame_index,
        quint64 generation);
    void drainFrameMailbox();
    void emitEvent(PlaybackControllerEvent event);
    void setGeneration(quint64 generation) noexcept;
    void discardPendingActivation(bool clear_selection);
    [[nodiscard]] bool validatePendingActivation() const;
    [[nodiscard]] std::optional<timeline::ClipLocation> activeClipLocation() const noexcept;
    [[nodiscard]] std::int64_t timelineFrame() const noexcept;
    [[nodiscard]] bool clipCanPlay(const timeline::TimelineClip& clip) const;
    [[nodiscard]] bool hasFutureClip(std::int64_t frame) const;

    application::EditorSession& session_;
    WorkerFactory worker_factory_;
    QThread worker_thread_;
    PlaybackWorker* worker_ = nullptr;
    PlaybackFrameMailbox frame_mailbox_;
    EventHandler event_handler_;
    std::optional<PendingActivation> pending_activation_;
    quint64 generation_ = 0;
    std::atomic<quint64> published_generation_{0};
    std::atomic_bool shutting_down_{false};
    bool playing_ = false;
};

} // namespace playback
