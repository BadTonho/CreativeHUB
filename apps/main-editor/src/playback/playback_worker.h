#pragma once

#include "../media/video_playback.h"
#include "../media/video_metadata.h"
#include "../media/audio_playback.h"
#include "../rendering/frame_compositor.h"
#include "audio_output.h"

#include <QMetaType>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QVector>
#include <QtGlobal>

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

class QTimer;

namespace playback {

using VideoFramePtr = std::shared_ptr<const media::VideoFrame>;

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
};

class PlaybackWorker final : public QObject {
    Q_OBJECT

public:
    explicit PlaybackWorker(QObject* parent = nullptr);
    ~PlaybackWorker() override;

    // Thread-safe entry point used by the UI to replace an older pending seek.
    void requestSeek(qint64 frame_index, quint64 generation);

public slots:
    void setMedia(
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
    void play();
    void pause();
    void stop();
    void setAudioParameters(
        double track_audio_gain,
        bool track_audio_muted,
        double clip_audio_gain,
        bool clip_audio_muted);
    void setComposition(QVector<CompositionLayerSpec> layers, quint64 generation);
    void stepForward();
    void stepBackward();
    void seekToFrame(qint64 frame_index, quint64 generation);

signals:
    void frameReady(VideoFramePtr frame, qint64 frame_index, quint64 generation);
    void mediaReady(quint64 generation);
    void playbackStateChanged(bool playing, quint64 generation);
    void playbackFinished(quint64 generation, bool during_playback);
    void playbackError(QString message, qint64 error_code, quint64 generation);
    void audioWarning(QString message, qint64 error_code, quint64 generation);

private slots:
    void decodeTick();
    void processPendingSeek();

private:
    bool ensureSessionAtCurrentFrame();
    void ensureTimer();
    void finishPlayback();
    void emitFrame(std::optional<media::VideoFrame> frame);
    void emitComposedFrame();
    [[nodiscard]] std::optional<media::VideoFrame> decodeCompositionAt(
        std::int64_t global_frame);
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
    void disableAudioOutput() noexcept;
    [[nodiscard]] std::optional<std::int64_t> sourceFrameForLocal(
        std::int64_t local_frame) const noexcept;
    [[nodiscard]] bool isLocalFrameInRange(std::int64_t local_frame) const noexcept;
    [[nodiscard]] bool isSourceFrameInRange(std::int64_t source_frame) const noexcept;
    [[nodiscard]] bool isSeekCurrent(quint64 sequence) const noexcept;
    [[nodiscard]] int frameIntervalMilliseconds() const noexcept;

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
    qint64 track_index_ = -1;
    qint64 clip_index_ = -1;
    bool audio_enabled_ = false;
    bool audio_failure_reported_ = false;
    bool audio_position_valid_ = false;
    qint64 audio_clock_origin_usecs_ = 0;
    std::int64_t audio_clock_origin_frame_ = 0;
    quint64 generation_ = 0;
    bool playing_ = false;
    std::atomic<qint64> pending_seek_frame_{std::numeric_limits<qint64>::min()};
    std::atomic<quint64> pending_seek_generation_{0};
    std::atomic<quint64> pending_seek_sequence_{0};
    std::atomic_bool seek_dispatch_scheduled_{false};
    struct CompositionSession {
        CompositionLayerSpec spec;
        std::unique_ptr<media::VideoPlaybackSession> session;
    };
    QVector<CompositionLayerSpec> composition_specs_;
    std::vector<CompositionSession> composition_sessions_;
    bool composition_enabled_ = false;
    std::int64_t primary_timeline_start_frame_ = 0;
};

} // namespace playback

Q_DECLARE_METATYPE(playback::VideoFramePtr)
Q_DECLARE_METATYPE(playback::CompositionLayerSpec)
Q_DECLARE_METATYPE(QVector<playback::CompositionLayerSpec>)
