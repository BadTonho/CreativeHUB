#pragma once

#include "../media/video_playback.h"
#include "../media/video_metadata.h"
#include "../media/audio_playback.h"
#include "../media/timeline_audio_mix.h"
#include "../rendering/frame_compositor.h"
#include "../rendering/preview_performance_metrics.h"
#include "../rendering/preview_frame_payload.h"
#include "../timeline/timeline_model.h"
#include "audio_output.h"
#include <creative_suite/composition/opengl_frame_compositor.h>
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
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <unordered_set>

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
    qint64 source_duration_frames = 0;
    timeline::FrameRate timeline_frame_rate;
    bool has_audio_stream = false;
    double track_audio_gain = 1.0;
    bool track_audio_muted = false;
    double clip_audio_gain = 1.0;
    bool clip_audio_muted = false;
    qint64 source_start_time_us = 0;
    qint64 source_duration_time_us = 0;
    bool audio_extracted = false;
    std::vector<timeline::AudioGainKeyframe> audio_gain_keyframes;
    std::vector<creative_suite::effects::EffectInstance> effects;
    std::optional<fusion::nodes::NodeGraph> node_graph;
    // Ephemeral viewer routing; never serialized to the project document.
    std::optional<fusion::nodes::NodeId> fusion_preview_node_id;
    QString linked_render_path;
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
    using GpuCompose = std::function<creative_suite::composition::OpenGlCompositionResult(
        int, int, const std::vector<rendering::CompositionLayer>&,
        const creative_suite::composition::OpenGlFrameCompositor::CancellationPredicate&,
        creative_suite::composition::OpenGlCompositionTimings*)>;
    explicit PlaybackWorker(QObject* parent = nullptr, GpuCompose gpu_compose = {});
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
    virtual void setCompositionAudioParameters(
        qint64 track_index,
        qint64 clip_index,
        double track_audio_gain,
        bool track_audio_muted,
        double clip_audio_gain,
        bool clip_audio_muted);
    virtual void setMonitorVolume(double gain);
    virtual void setPreviewQuality(PreviewQuality quality);
    virtual void setCompositionCanvasSize(int width, int height);
    virtual void setGpuCompositionEnabled(bool enabled, QOffscreenSurface* surface);
    virtual void setGpuTextureDelivery(bool enabled, quint64 epoch, QOpenGLContext* share_context);
    virtual void recoverPreviewFrame(rendering::PreviewFramePayload frame, qint64 local_frame,
        quint64 generation, quint64 epoch);
    virtual void setComposition(
        QVector<CompositionLayerSpec> layers,
        QVector<CompositionTransitionSpec> transitions,
        quint64 generation);
    virtual void setActiveCompositionClip(
        qint64 track_index,
        qint64 clip_index,
        qint64 global_timeline_frame = std::numeric_limits<qint64>::min());
    virtual void cancelActivation(quint64 generation);
    virtual void renderCompositionFrame(
        qint64 global_frame,
        qint64 frame_index,
        quint64 generation);
    virtual void stepForward();
    virtual void stepBackward();
    virtual void seekToFrame(qint64 frame_index, quint64 generation);

signals:
    void previewFrameReady(rendering::PreviewFramePayload frame, qint64 frame_index,
        quint64 generation, quint64 delivery_trace_id);
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
    void compositionWarning(QString message, qint64 error_code, quint64 generation);

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
        std::size_t composition_session_index = 0;
    };

    bool ensureSessionAtCurrentFrame();
    void ensureTimer();
    void finishPlayback();
    void emitFrame(std::optional<media::VideoFramePtr> frame);
    void publishPreviewFrame(rendering::PreviewFramePayload frame, qint64 index,
        quint64 generation, quint64 trace);
    void retireGpuCompositor();
    void ensureGpuMaintenance();
    void collectGpuResources();
    void emitComposedFrame();
    [[nodiscard]] std::optional<std::vector<DecodedCompositionLayer>>
        decodeCompositionLayers(
        std::int64_t global_frame,
        const media::VideoPlaybackSession::CancellationPredicate& should_cancel);
    [[nodiscard]] std::optional<media::VideoFrame> composeCompositionLayers(
        const std::vector<DecodedCompositionLayer>& layers,
        const media::VideoPlaybackSession::CancellationPredicate& should_cancel,
        rendering::FrameCompositionTimings* timings = nullptr);
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
    [[nodiscard]] std::optional<std::int64_t> timelineFrameForDiagnostics(
        std::optional<std::int64_t> requested_clip_local_frame = std::nullopt) const noexcept;
    void configureAudio();
    void configureCompositionAudio();
    void restartCompositionAudioOutput();
    void fillAudioOutput();
    void fillCompositionAudioOutput();
    void reportCompositionAudioFailure(
        const CompositionLayerSpec& spec,
        const std::exception& error,
        const char* operation,
        qint64 error_code = -1);
    void updateAudioBufferMetric() noexcept;
    void disableAudioOutput() noexcept;
    [[nodiscard]] std::optional<std::int64_t> sourceFrameForLocal(
        std::int64_t local_frame) const noexcept;
    [[nodiscard]] double playbackFrameRate() const noexcept;
    void updateFrameRateMetrics() noexcept;
    void setFallbackTimelineFrameRate(double source_frame_rate) noexcept;
    [[nodiscard]] bool isLocalFrameInRange(std::int64_t local_frame) const noexcept;
    [[nodiscard]] bool isSourceFrameInRange(std::int64_t source_frame) const noexcept;
    [[nodiscard]] bool isSeekCurrent(quint64 sequence) const noexcept;
    void scheduleNextPlaybackTick();
    void updateTransitionPreroll();
    void collectTransitionPreroll();
    void cancelTransitionPreroll() noexcept;
    void startPlaybackClock() noexcept;
    void resetPlaybackClock() noexcept;

    QTimer* timer_ = nullptr;
    std::unique_ptr<media::VideoPlaybackSession> session_;
    std::unique_ptr<media::AudioPlaybackSession> audio_session_;
    std::unique_ptr<AudioOutput> audio_output_;
    QByteArray pending_audio_bytes_;
    std::filesystem::path source_path_;
    timeline::FrameRate timeline_frame_rate_{30, 1};
    double source_frame_rate_ = 30.0;
    bool composition_timeline_frame_rate_valid_ = false;
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
    bool composition_audio_configured_ = false;
    bool composition_audio_cursor_valid_ = false;
    std::int64_t next_composition_audio_sample_ = 0;
    qint64 audio_clock_origin_usecs_ = 0;
    std::int64_t audio_clock_origin_frame_ = 0;
    quint64 generation_ = 0;
    bool playing_ = false;
    bool diagnostics_logged_ = false;
    PreviewQuality preview_quality_ = PreviewQuality::Full;
    int composition_canvas_width_ = 1920;
    int composition_canvas_height_ = 1080;
    GpuCompose gpu_compose_;
    std::unique_ptr<creative_suite::composition::OpenGlFrameCompositor> gpu_compositor_;
    QOffscreenSurface* gpu_surface_ = nullptr;
    bool gpu_composition_enabled_ = false;
    bool gpu_composition_failed_ = false;
    bool gpu_warning_reported_ = false;
    bool last_composition_gpu_ = false;
    bool last_composition_cancelled_ = false;
    bool last_composition_busy_ = false;
    creative_suite::composition::OpenGlTextureFramePtr last_composition_texture_;
    bool gpu_texture_delivery_enabled_ = false;
    bool gpu_texture_delivery_failed_ = false;
    bool gpu_texture_warning_reported_ = false;
    quint64 delivery_epoch_ = 0;
    QOpenGLContext* gpu_share_context_ = nullptr;
    std::shared_ptr<creative_suite::composition::OpenGlTexturePoolBudget> gpu_texture_budget_ =
        std::make_shared<creative_suite::composition::OpenGlTexturePoolBudget>();
    std::vector<std::unique_ptr<creative_suite::composition::OpenGlFrameCompositor>> retiring_gpu_compositors_;
    std::unordered_set<const void*> failed_gpu_collectors_;
    QTimer* gpu_maintenance_timer_ = nullptr;
    QTimer* gpu_busy_timer_ = nullptr;
    struct PendingGpuFrame {
        qint64 global_frame, local_frame;
        quint64 generation, epoch, revision, seek_sequence;
    };
    std::optional<PendingGpuFrame> pending_gpu_frame_;
    using Clock = std::chrono::steady_clock;
    detail::PlaybackDeadlineScheduler playback_scheduler_;
    detail::AudioPacingPolicy audio_pacing_policy_;
    std::atomic<qint64> pending_seek_frame_{std::numeric_limits<qint64>::min()};
    std::atomic<quint64> pending_seek_generation_{0};
    std::atomic<quint64> pending_seek_sequence_{0};
    std::atomic_bool seek_dispatch_scheduled_{false};
    struct CompositionSession {
        struct GraphInputSession {
            fusion::nodes::NodeId node_id = 0;
            double frame_rate = 30.0;
            bool still_source = false;
            VideoFramePtr still_frame;
            std::unique_ptr<media::VideoPlaybackSession> video_session;
        };
        CompositionLayerSpec spec;
        std::unique_ptr<media::VideoPlaybackSession> session;
        std::unique_ptr<media::AudioPlaybackSession> audio_session;
        bool audio_open_attempted = false;
        bool audio_open_failed = false;
        VideoFramePtr static_frame;
        std::shared_ptr<const media::VideoFrame> cached_text_frame;
        std::vector<GraphInputSession> graph_inputs;
        rendering::AlphaCoveragePtr cached_text_alpha_coverage;
        rendering::PreparedAlphaCoverageGeometryPtr cached_text_geometry;
    };
    [[nodiscard]] std::optional<media::VideoFrame> decodeFusionNodePreview(
        CompositionSession& composition,
        qint64 global_frame,
        const media::VideoPlaybackSession::CancellationPredicate& should_cancel);
    struct TransitionPrerollResult {
        quint64 composition_revision = 0;
        std::size_t session_index = 0;
        qint64 track_index = -1;
        qint64 clip_index = -1;
        qint64 transition_start_frame = 0;
        qint64 source_frame = 0;
        std::filesystem::path source_path;
        std::unique_ptr<media::VideoPlaybackSession> session;
        std::string error_message;
        qint64 error_code = -1;
        bool cancelled = false;
    };
    struct TransitionPrerollState {
        quint64 composition_revision = 0;
        qint64 transition_start_frame = 0;
        std::atomic_bool cancel_requested{false};
        std::atomic_bool finished{false};
        std::mutex result_mutex;
        std::optional<TransitionPrerollResult> result;
    };
    QVector<CompositionLayerSpec> composition_specs_;
    QVector<CompositionTransitionSpec> composition_transitions_;
    std::vector<CompositionSession> composition_sessions_;
    std::vector<media::TimelineAudioMixClip> composition_audio_mix_clips_;
    std::vector<media::TimelineAudioMixTransition> composition_audio_mix_transitions_;
    std::shared_ptr<TransitionPrerollState> transition_preroll_state_;
    std::thread transition_preroll_thread_;
    std::size_t last_transition_preroll_session_index_ =
        std::numeric_limits<std::size_t>::max();
    qint64 last_transition_preroll_start_frame_ = -1;
    quint64 composition_revision_ = 0;
    rendering::FrameCompositionTimings composition_timings_scratch_;
    bool composition_enabled_ = false;
    std::int64_t primary_timeline_start_frame_ = 0;
    std::int64_t composition_start_frame_ = 0;
    std::int64_t composition_end_frame_ = 0;
    std::int64_t current_timeline_frame_ = 0;
    bool composition_position_initialized_ = false;
    quint64 cached_composition_generation_ = 0;
    std::int64_t cached_composition_global_frame_ = -1;
    rendering::PreviewFramePayload cached_composition_frame_;
};

} // namespace playback

Q_DECLARE_METATYPE(playback::VideoFramePtr)
Q_DECLARE_METATYPE(playback::CompositionLayerSpec)
Q_DECLARE_METATYPE(QVector<playback::CompositionLayerSpec>)
Q_DECLARE_METATYPE(playback::CompositionTransitionSpec)
Q_DECLARE_METATYPE(QVector<playback::CompositionTransitionSpec>)
