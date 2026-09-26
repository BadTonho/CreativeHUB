#pragma once

#include "../media/video_playback.h"
#include "../media/video_metadata.h"
#include "../media/audio_playback.h"
#include "../rendering/frame_compositor.h"
#include "../rendering/preview_performance_metrics.h"
#include "../timeline/timeline_model.h"
#include "audio_output.h"
#include "playback_audio_pacing.h"
#include "playback_deadline_scheduler.h"

#include <QMetaType>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

class QTimer;

namespace playback {

using VideoFramePtr = media::VideoFramePtr;

namespace detail {

inline constexpr qint64 maximum_sequential_decode_gap_frames = 8;

[[nodiscard]] constexpr bool shouldUseSequentialDecode(
    qint64 current_source_frame,
    qint64 requested_source_frame) noexcept {
    return current_source_frame >= 0 &&
        requested_source_frame > current_source_frame &&
        requested_source_frame - current_source_frame <=
            maximum_sequential_decode_gap_frames;
}

} // namespace detail

enum class PreviewQuality {
    Full,
    Half,
    Quarter,
};

struct CompositionLayerSpec {
    QString source_path;
    double frame_rate = 30.0;
    qint64 timeline_start_frame = 0;
    qint64 source_start_frame = 0;
    qint64 segment_frame_count = 0;
    qint64 track_index = -1;
    qint64 clip_index = -1;
    timeline::Transform2D transform;
    timeline::TransformKeyframes keyframes;
    timeline::ClipKind kind = timeline::ClipKind::Video;
    timeline::TextStyle text;
    VideoFramePtr still_frame;
    timeline::TrackId track_id = 0;
    timeline::ClipId clip_id = 0;
};

struct CompositionTransitionSpec {
    qint64 track_index = -1;
    qint64 from_clip_index = -1;
    qint64 to_clip_index = -1;
    qint64 boundary_frame = 0;
    qint64 duration_frames = 0;
    timeline::TransitionKind kind = timeline::TransitionKind::CrossDissolve;
};

class PlaybackWorker : public QObject {
    Q_OBJECT

public:
    explicit PlaybackWorker(QObject* parent = nullptr);
    ~PlaybackWorker() override;

    // Thread-safe entry point used by the UI to replace an older pending seek.
    virtual void requestSeek(qint64 frame_index, quint64 generation);

public slots:
    virtual void initializeDiagnostics();
    virtual void setMedia(
        QString source_path,
        double frame_rate,
        qint64 source_start_frame,
        qint64 segment_frame_count,
        double track_audio_gain,
        bool track_audio_muted,
        double clip_audio_gain,
        bool clip_audio_muted,
        qint64 track_index,
        qint64 clip_index,
        quint64 generation);
    virtual void play();
    virtual void pause();
    virtual void stop();
    virtual void setAudioParameters(
        double track_audio_gain,
        bool track_audio_muted,
        double clip_audio_gain,
        bool clip_audio_muted);
    virtual void setMonitorVolume(double gain);
    virtual void setPreviewQuality(PreviewQuality quality);
    virtual void setComposition(
        QVector<CompositionLayerSpec> layers,
        QVector<CompositionTransitionSpec> transitions,
        quint64 generation);
    virtual void setActiveCompositionClip(qint64 track_index, qint64 clip_index);
    virtual void cancelActivation(quint64 generation);
    virtual void renderCompositionFrame(
        qint64 global_frame,
        qint64 frame_index,
        quint64 generation);
    virtual void stepForward();
    virtual void stepBackward();
    virtual void seekToFrame(qint64 frame_index, quint64 generation);

signals:
    void frameReady(
        VideoFramePtr frame,
        qint64 frame_index,
        quint64 generation,
        quint64 delivery_trace_id);
    void mediaReady(quint64 generation);
    void playbackStateChanged(bool playing, quint64 generation);
    void playbackFinished(quint64 generation, bool during_playback);
    void playbackError(QString message, qint64 error_code, quint64 generation);
    void audioWarning(QString message, qint64 error_code, quint64 generation);

private slots:
    void decodeTick();
    void processPendingSeek();

private:
    struct DecodedCompositionLayer {
        std::shared_ptr<const media::VideoFrame> frame;
        timeline::Transform2D transform;
        rendering::AlphaCoveragePtr alpha_coverage;
        timeline::TrackId track_id = 0;
        timeline::ClipId clip_id = 0;
        qint64 track_index = -1;
        qint64 clip_index = -1;
        qint64 source_frame = -1;
        timeline::ClipKind kind = timeline::ClipKind::Video;
        rendering::SlowFrameDecodePath decode_path =
            rendering::SlowFrameDecodePath::None;
        std::uint64_t decode_nanoseconds = 0;
        media::ForwardDecodeDiagnostics forward_decode;
    };

    bool ensureSessionAtCurrentFrame();
    void ensureTimer();
    void finishPlayback();
    void emitFrame(std::optional<media::VideoFramePtr> frame);
    void emitComposedFrame();
    [[nodiscard]] std::optional<std::vector<DecodedCompositionLayer>>
        decodeCompositionLayers(
        std::int64_t global_frame,
        const media::VideoPlaybackSession::CancellationPredicate& should_cancel);
    [[nodiscard]] std::optional<media::VideoFrame> composeCompositionLayers(
        const std::vector<DecodedCompositionLayer>& layers,
        rendering::FrameCompositionTimings* timings = nullptr) const;
    void clearCompositionCache() noexcept;
    void reportFailure(
        const media::MediaError& error,
        const char* operation,
        std::optional<std::int64_t> requested_frame = std::nullopt);
    void reportFailure(
        const std::exception& error,
        const char* operation,
        std::optional<std::int64_t> requested_frame = std::nullopt);
    void reportAudioFailure(
        const media::MediaError& error,
        const char* operation);
    void reportAudioFailure(
        const std::exception& error,
        const char* operation,
        qint64 error_code = -1);
    void configureAudio();
    void fillAudioOutput();
    void updateAudioBufferMetric() noexcept;
    void disableAudioOutput() noexcept;
    [[nodiscard]] std::optional<std::int64_t> sourceFrameForLocal(
        std::int64_t local_frame) const noexcept;
    [[nodiscard]] bool isLocalFrameInRange(std::int64_t local_frame) const noexcept;
    [[nodiscard]] bool isSourceFrameInRange(std::int64_t source_frame) const noexcept;
    [[nodiscard]] bool isSeekCurrent(quint64 sequence) const noexcept;
    void scheduleNextPlaybackTick();
    void startPlaybackClock() noexcept;
    void resetPlaybackClock() noexcept;

    QTimer* timer_ = nullptr;
    std::unique_ptr<media::VideoPlaybackSession> session_;
    std::unique_ptr<media::AudioPlaybackSession> audio_session_;
    std::unique_ptr<AudioOutput> audio_output_;
    QByteArray pending_audio_bytes_;
    std::filesystem::path source_path_;
    double frame_rate_ = 30.0;
    std::int64_t source_start_frame_ = 0;
    std::int64_t segment_frame_count_ = 0;
    std::int64_t current_frame_index_ = 0;
    double track_audio_gain_ = 1.0;
    bool track_audio_muted_ = false;
    double clip_audio_gain_ = 1.0;
    bool clip_audio_muted_ = false;
    double monitor_volume_gain_ = 1.0;
    qint64 track_index_ = -1;
    qint64 clip_index_ = -1;
    bool audio_enabled_ = false;
    bool audio_failure_reported_ = false;
    bool audio_position_valid_ = false;
    qint64 audio_clock_origin_usecs_ = 0;
    std::int64_t audio_clock_origin_frame_ = 0;
    quint64 generation_ = 0;
    bool playing_ = false;
    bool diagnostics_logged_ = false;
    PreviewQuality preview_quality_ = PreviewQuality::Full;
    using Clock = std::chrono::steady_clock;
    detail::PlaybackDeadlineScheduler playback_scheduler_;
    detail::AudioPacingPolicy audio_pacing_policy_;
    std::atomic<qint64> pending_seek_frame_{std::numeric_limits<qint64>::min()};
    std::atomic<quint64> pending_seek_generation_{0};
    std::atomic<quint64> pending_seek_sequence_{0};
    std::atomic_bool seek_dispatch_scheduled_{false};
    struct CompositionSession {
        CompositionLayerSpec spec;
        std::unique_ptr<media::VideoPlaybackSession> session;
        VideoFramePtr static_frame;
        std::shared_ptr<const media::VideoFrame> cached_text_frame;
        rendering::AlphaCoveragePtr cached_text_alpha_coverage;
    };
    QVector<CompositionLayerSpec> composition_specs_;
    QVector<CompositionTransitionSpec> composition_transitions_;
    std::vector<CompositionSession> composition_sessions_;
    rendering::FrameCompositionTimings composition_timings_scratch_;
    bool composition_enabled_ = false;
    std::int64_t primary_timeline_start_frame_ = 0;
    quint64 cached_composition_generation_ = 0;
    std::int64_t cached_composition_global_frame_ = -1;
    VideoFramePtr cached_composition_frame_;
};

} // namespace playback

Q_DECLARE_METATYPE(playback::VideoFramePtr)
Q_DECLARE_METATYPE(playback::CompositionLayerSpec)
Q_DECLARE_METATYPE(QVector<playback::CompositionLayerSpec>)
Q_DECLARE_METATYPE(playback::CompositionTransitionSpec)
Q_DECLARE_METATYPE(QVector<playback::CompositionTransitionSpec>)
