#include "playback_worker.h"

#include "../logging/logger.h"

#include <QFileInfo>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <utility>

namespace playback {
namespace {

constexpr double default_frame_rate = 30.0;

std::string pathToUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::string safePathForLog(const std::filesystem::path& path) noexcept {
    try {
        return pathToUtf8(path);
    } catch (...) {
        return "<unavailable>";
    }
}

} // namespace

PlaybackWorker::PlaybackWorker(QObject* parent)
    : QObject(parent) {}

PlaybackWorker::~PlaybackWorker() {
    if (timer_ != nullptr) timer_->stop();
    session_.reset();
}

void PlaybackWorker::setMedia(QString source_path, double frame_rate, quint64 generation) {
    if (timer_ != nullptr) timer_->stop();
    if (playing_) {
        playing_ = false;
        emit playbackStateChanged(false, generation_);
    }

    generation_ = generation;
    source_path_ = QFileInfo(source_path).filesystemFilePath();
    frame_rate_ = std::isfinite(frame_rate) && frame_rate > 0.0
        ? frame_rate
        : default_frame_rate;
    current_frame_index_ = 0;
    session_.reset();

    try {
        session_ = media::VideoPlaybackSession::open(source_path_);
    } catch (const media::MediaError& error) {
        reportFailure(error, "set_media");
    } catch (const std::exception& error) {
        reportFailure(error, "set_media");
    }
}

void PlaybackWorker::play() {
    if (source_path_.empty()) return;

    try {
        if (!session_) session_ = media::VideoPlaybackSession::open(source_path_);
        if (session_->at_end()) {
            session_->reset();
            current_frame_index_ = 0;
        }
        if (!ensureSessionAtCurrentFrame()) {
            finishPlayback();
            return;
        }

        ensureTimer();
        timer_->start(frameIntervalMilliseconds());
        if (!playing_) {
            playing_ = true;
            emit playbackStateChanged(true, generation_);
        }
    } catch (const media::MediaError& error) {
        reportFailure(error, "play");
    } catch (const std::exception& error) {
        reportFailure(error, "play");
    }
}

void PlaybackWorker::pause() {
    if (timer_ != nullptr) timer_->stop();
    if (!playing_) return;

    playing_ = false;
    emit playbackStateChanged(false, generation_);
}

void PlaybackWorker::stop() {
    pause();
}

void PlaybackWorker::stepForward() {
    pause();
    if (source_path_.empty()) return;

    try {
        if (!session_) session_ = media::VideoPlaybackSession::open(source_path_);
        if (!ensureSessionAtCurrentFrame()) {
            finishPlayback();
            return;
        }
        emitFrame(session_->decode_next_frame());
    } catch (const media::MediaError& error) {
        reportFailure(error, "step_forward");
    } catch (const std::exception& error) {
        reportFailure(error, "step_forward");
    }
}

void PlaybackWorker::stepBackward() {
    pause();
    if (source_path_.empty()) return;

    try {
        if (!session_) session_ = media::VideoPlaybackSession::open(source_path_);
        const auto target_frame = std::max<std::int64_t>(0, current_frame_index_ - 1);
        emitFrame(session_->decode_frame_at(target_frame));
    } catch (const media::MediaError& error) {
        reportFailure(error, "step_backward");
    } catch (const std::exception& error) {
        reportFailure(error, "step_backward");
    }
}

void PlaybackWorker::decodeTick() {
    if (!playing_) return;

    try {
        if (!session_) {
            throw media::MediaError("Playback session is not available.");
        }
        const auto frame = session_->decode_next_frame();
        if (!frame.has_value()) {
            finishPlayback();
            return;
        }
        emitFrame(frame);
    } catch (const media::MediaError& error) {
        reportFailure(error, "decode_tick");
    } catch (const std::exception& error) {
        reportFailure(error, "decode_tick");
    }
}

bool PlaybackWorker::ensureSessionAtCurrentFrame() {
    while (session_->current_frame_index() < current_frame_index_) {
        const auto frame = session_->decode_next_frame();
        if (!frame.has_value()) return false;
    }
    return true;
}

void PlaybackWorker::ensureTimer() {
    if (timer_ != nullptr) return;

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &PlaybackWorker::decodeTick);
}

void PlaybackWorker::finishPlayback() {
    if (timer_ != nullptr) timer_->stop();
    const bool was_playing = playing_;
    playing_ = false;
    if (was_playing) emit playbackStateChanged(false, generation_);
    emit playbackFinished(generation_);
}

void PlaybackWorker::emitFrame(std::optional<media::VideoFrame> frame) {
    if (!frame.has_value()) {
        finishPlayback();
        return;
    }

    current_frame_index_ = session_->current_frame_index();
    auto payload = std::make_shared<const media::VideoFrame>(std::move(*frame));
    emit frameReady(std::move(payload), current_frame_index_, generation_);
}

void PlaybackWorker::reportFailure(const media::MediaError& error, const char* operation) {
    try {
        logging::Context context{
            {"path", safePathForLog(source_path_)},
            {"frame_index", std::to_string(current_frame_index_)}};
        if (error.error_code().has_value()) {
            context.emplace_back("error_code", std::to_string(*error.error_code()));
        }
        logging::Logger::instance().log(
            logging::Level::Error,
            "playback",
            operation,
            error.what(),
            context);
    } catch (...) {
        // The UI still receives the original short error when diagnostic logging fails.
    }

    if (timer_ != nullptr) timer_->stop();
    playing_ = false;
    session_.reset();
    emit playbackError(QString::fromUtf8(error.what()), generation_);
}

void PlaybackWorker::reportFailure(const std::exception& error, const char* operation) {
    try {
        logging::Logger::instance().log(
            logging::Level::Error,
            "playback",
            operation,
            error.what(),
            {{"path", safePathForLog(source_path_)},
             {"frame_index", std::to_string(current_frame_index_)}});
    } catch (...) {
        // The UI still receives the original short error when diagnostic logging fails.
    }

    if (timer_ != nullptr) timer_->stop();
    playing_ = false;
    session_.reset();
    emit playbackError(QString::fromUtf8(error.what()), generation_);
}

int PlaybackWorker::frameIntervalMilliseconds() const noexcept {
    const double interval = 1000.0 / frame_rate_;
    const auto rounded = static_cast<int>(std::lround(interval));
    return std::clamp(rounded, 1, 1000);
}

} // namespace playback
