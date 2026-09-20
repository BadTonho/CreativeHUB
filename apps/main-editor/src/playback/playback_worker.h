#pragma once

#include "../media/video_playback.h"
#include "../media/video_metadata.h"

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QtGlobal>

#include <atomic>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>

class QTimer;

namespace playback {

using VideoFramePtr = std::shared_ptr<const media::VideoFrame>;

class PlaybackWorker final : public QObject {
    Q_OBJECT

public:
    explicit PlaybackWorker(QObject* parent = nullptr);
    ~PlaybackWorker() override;

    // Thread-safe entry point used by the UI to replace an older pending seek.
    void requestSeek(qint64 frame_index, quint64 generation);

public slots:
    void setMedia(QString source_path, double frame_rate, quint64 generation);
    void play();
    void pause();
    void stop();
    void stepForward();
    void stepBackward();
    void seekToFrame(qint64 frame_index, quint64 generation);

signals:
    void frameReady(VideoFramePtr frame, qint64 frame_index, quint64 generation);
    void mediaReady(quint64 generation);
    void playbackStateChanged(bool playing, quint64 generation);
    void playbackFinished(quint64 generation, bool during_playback);
    void playbackError(QString message, qint64 error_code, quint64 generation);

private slots:
    void decodeTick();
    void processPendingSeek();

private:
    bool ensureSessionAtCurrentFrame();
    void ensureTimer();
    void finishPlayback();
    void emitFrame(std::optional<media::VideoFrame> frame);
    void reportFailure(
        const media::MediaError& error,
        const char* operation,
        std::optional<std::int64_t> requested_frame = std::nullopt);
    void reportFailure(
        const std::exception& error,
        const char* operation,
        std::optional<std::int64_t> requested_frame = std::nullopt);
    [[nodiscard]] bool isSeekCurrent(quint64 sequence) const noexcept;
    [[nodiscard]] int frameIntervalMilliseconds() const noexcept;

    QTimer* timer_ = nullptr;
    std::unique_ptr<media::VideoPlaybackSession> session_;
    std::filesystem::path source_path_;
    double frame_rate_ = 30.0;
    std::int64_t current_frame_index_ = 0;
    quint64 generation_ = 0;
    bool playing_ = false;
    std::atomic<qint64> pending_seek_frame_{std::numeric_limits<qint64>::min()};
    std::atomic<quint64> pending_seek_generation_{0};
    std::atomic<quint64> pending_seek_sequence_{0};
    std::atomic_bool seek_dispatch_scheduled_{false};
};

} // namespace playback

Q_DECLARE_METATYPE(playback::VideoFramePtr)
