#include "playback_worker.h"

#include "../logging/logger.h"
#include "../rendering/text_renderer.h"

#include <QFileInfo>
#include <QByteArray>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <exception>
#include <string>
#include <stdexcept>
#include <utility>

namespace playback {
namespace {

constexpr double default_frame_rate = 30.0;
constexpr qint64 no_pending_seek = std::numeric_limits<qint64>::min();

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
    disableAudioOutput();
    audio_session_.reset();
    session_.reset();
    composition_sessions_.clear();
    composition_specs_.clear();
    composition_transitions_.clear();
}

void PlaybackWorker::requestSeek(qint64 frame_index, quint64 generation) {
    pending_seek_frame_.store(frame_index, std::memory_order_relaxed);
    pending_seek_generation_.store(generation, std::memory_order_relaxed);
    pending_seek_sequence_.fetch_add(1, std::memory_order_release);

    if (!seek_dispatch_scheduled_.exchange(true, std::memory_order_acq_rel)) {
        QMetaObject::invokeMethod(
            this,
            "processPendingSeek",
            Qt::QueuedConnection);
    }
}

void PlaybackWorker::setMedia(
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
    quint64 generation) {
    pending_seek_frame_.store(no_pending_seek, std::memory_order_relaxed);
    pending_seek_generation_.store(generation, std::memory_order_relaxed);
    pending_seek_sequence_.fetch_add(1, std::memory_order_release);

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
    source_start_frame_ = source_start_frame;
    segment_frame_count_ = segment_frame_count;
    current_frame_index_ = 0;
    track_audio_gain_ = std::isfinite(track_audio_gain) && track_audio_gain >= 0.0 && track_audio_gain <= 2.0
        ? track_audio_gain : 1.0;
    track_audio_muted_ = track_audio_muted;
    clip_audio_gain_ = std::isfinite(clip_audio_gain) && clip_audio_gain >= 0.0 && clip_audio_gain <= 2.0
        ? clip_audio_gain : 1.0;
    clip_audio_muted_ = clip_audio_muted;
    track_index_ = track_index;
    clip_index_ = clip_index;
    audio_position_valid_ = false;
    audio_failure_reported_ = false;
    audio_clock_origin_usecs_ = 0;
    audio_clock_origin_frame_ = 0;
    pending_audio_bytes_.clear();
    disableAudioOutput();
    audio_session_.reset();
    session_.reset();
    composition_sessions_.clear();
    composition_specs_.clear();
    composition_transitions_.clear();
    composition_enabled_ = false;
    primary_timeline_start_frame_ = 0;

    try {
        if (source_start_frame_ < 0 || segment_frame_count_ < 0) {
            throw media::MediaError("The playback segment range is invalid.");
        }
        session_ = media::VideoPlaybackSession::open(source_path_);
        configureAudio();
        emit mediaReady(generation_);
    } catch (const media::MediaError& error) {
        reportFailure(error, "set_media");
    } catch (const std::exception& error) {
        reportFailure(error, "set_media");
    }
}

void PlaybackWorker::play() {
    if (source_path_.empty()) {
        if (!composition_enabled_ || segment_frame_count_ <= 0) return;

        ensureTimer();
        timer_->start(frameIntervalMilliseconds());
        if (!playing_) {
            playing_ = true;
            emit playbackStateChanged(true, generation_);
        }
        return;
    }

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

        if (audio_enabled_ && audio_session_ != nullptr && audio_output_ != nullptr) {
            try {
                if (!audio_position_valid_) {
                    const auto source_frame = sourceFrameForLocal(current_frame_index_);
                    if (!source_frame.has_value()) {
                        throw media::MediaError("The requested audio source frame is invalid.");
                    }
                    audio_session_->seek_to_source_frame(*source_frame, frame_rate_);
                    audio_position_valid_ = true;
                    pending_audio_bytes_.clear();
                }
            } catch (const media::MediaError& error) {
                reportAudioFailure(error, "seek");
            } catch (const std::exception& error) {
                reportAudioFailure(error, "seek");
            }
            if (audio_enabled_) {
                try {
                    QString audio_error;
                    qint64 audio_code = 0;
                    const bool resumed = playing_
                        ? true
                        : audio_output_->resume(&audio_error, &audio_code);
                    if (!resumed && !audio_output_->start(&audio_error, &audio_code)) {
                        throw media::MediaError(
                            audio_error.isEmpty()
                                ? "The audio output could not be started."
                                : audio_error.toUtf8().toStdString(),
                            audio_code > 0
                                ? std::optional<int>(static_cast<int>(audio_code))
                                : std::nullopt);
                    }
                    audio_clock_origin_frame_ = current_frame_index_;
                    audio_clock_origin_usecs_ = audio_output_->processedUsecs();
                } catch (const media::MediaError& error) {
                    reportAudioFailure(error, "output");
                } catch (const std::exception& error) {
                    reportAudioFailure(error, "output");
                }
            }
            if (audio_enabled_) {
                try {
                    fillAudioOutput();
                } catch (const media::MediaError& error) {
                    reportAudioFailure(error, "decode");
                } catch (const std::exception& error) {
                    reportAudioFailure(error, "decode");
                }
            }
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
    if (audio_output_ != nullptr) audio_output_->pause();
    if (!playing_) return;

    playing_ = false;
    emit playbackStateChanged(false, generation_);
}

void PlaybackWorker::stop() {
    pause();
    if (audio_output_ != nullptr) audio_output_->stop();
    audio_position_valid_ = false;
    pending_audio_bytes_.clear();
}

void PlaybackWorker::setAudioParameters(
    double track_audio_gain,
    bool track_audio_muted,
    double clip_audio_gain,
    bool clip_audio_muted) {
    const bool was_playing = playing_;
    track_audio_gain_ = std::isfinite(track_audio_gain) &&
            track_audio_gain >= 0.0 && track_audio_gain <= 2.0
        ? track_audio_gain
        : 1.0;
    track_audio_muted_ = track_audio_muted;
    clip_audio_gain_ = std::isfinite(clip_audio_gain) &&
            clip_audio_gain >= 0.0 && clip_audio_gain <= 2.0
        ? clip_audio_gain
        : 1.0;
    clip_audio_muted_ = clip_audio_muted;

    // Discard already-scaled samples so the next audio buffer uses the new
    // parameters. Video keeps its current frame and playback position.
    pending_audio_bytes_.clear();
    audio_position_valid_ = false;
    if (audio_output_ != nullptr) audio_output_->stop();
    if (was_playing) play();
}

void PlaybackWorker::setComposition(
    QVector<CompositionLayerSpec> layers,
    QVector<CompositionTransitionSpec> transitions,
    quint64 generation) {
    if (generation < generation_) return;
    generation_ = generation;

    composition_specs_ = std::move(layers);
    composition_transitions_ = std::move(transitions);
    composition_sessions_.clear();
    composition_enabled_ = !composition_specs_.isEmpty();
    primary_timeline_start_frame_ = 0;
    if (!composition_enabled_) return;

    std::int64_t composition_start = std::numeric_limits<std::int64_t>::max();
    std::int64_t composition_end = 0;

    try {
        for (const auto& spec : composition_specs_) {
            if (spec.timeline_start_frame < 0 ||
                spec.source_start_frame < 0 || spec.segment_frame_count <= 0) {
                continue;
            }
            composition_start = std::min(composition_start, spec.timeline_start_frame);
            if (spec.timeline_start_frame <=
                    std::numeric_limits<std::int64_t>::max() -
                        spec.segment_frame_count) {
                composition_end = std::max(
                    composition_end,
                    spec.timeline_start_frame + spec.segment_frame_count);
            }
            CompositionSession composition_session;
            composition_session.spec = spec;
            if (spec.kind == timeline::ClipKind::Video) {
                if (spec.source_path.isEmpty()) continue;
                composition_session.session = media::VideoPlaybackSession::open(
                    QFileInfo(spec.source_path).filesystemFilePath());
            }
            if (spec.track_index == track_index_ && spec.clip_index == clip_index_) {
                primary_timeline_start_frame_ = spec.timeline_start_frame;
            }
            composition_sessions_.push_back(std::move(composition_session));
        }

        if (source_path_.empty() && composition_start !=
                std::numeric_limits<std::int64_t>::max() &&
            composition_end > composition_start) {
            primary_timeline_start_frame_ = composition_start;
            current_frame_index_ = 0;
            segment_frame_count_ = composition_end - composition_start;
            frame_rate_ = default_frame_rate;
        }
    } catch (const media::MediaError& error) {
        composition_sessions_.clear();
        composition_transitions_.clear();
        composition_enabled_ = false;
        reportFailure(error, "compose");
    } catch (const std::exception& error) {
        composition_sessions_.clear();
        composition_transitions_.clear();
        composition_enabled_ = false;
        reportFailure(error, "compose");
    }
}

void PlaybackWorker::renderCompositionFrame(
    qint64 global_frame,
    qint64 frame_index,
    quint64 generation) {
    if (generation < generation_ || !composition_enabled_) return;
    generation_ = generation;
    try {
        if (source_path_.empty() && segment_frame_count_ > 0) {
            current_frame_index_ = std::clamp<std::int64_t>(
                frame_index,
                0,
                segment_frame_count_ - 1);
        }
        const auto composed = decodeCompositionAt(global_frame);
        if (!composed.has_value()) {
            throw media::MediaError("The timeline composition could not produce a frame.");
        }
        auto payload = std::make_shared<const media::VideoFrame>(*composed);
        emit frameReady(std::move(payload), frame_index, generation_);
    } catch (const media::MediaError& error) {
        reportFailure(error, "compose", frame_index);
    } catch (const std::exception& error) {
        reportFailure(error, "compose", frame_index);
    }
}

void PlaybackWorker::stepForward() {
    pause();
    if (source_path_.empty()) {
        if (!composition_enabled_ || segment_frame_count_ <= 0) return;
        if (current_frame_index_ >= segment_frame_count_ - 1) {
            finishPlayback();
            return;
        }
        ++current_frame_index_;
        emitComposedFrame();
        return;
    }

    try {
        if (!session_) session_ = media::VideoPlaybackSession::open(source_path_);
        if (segment_frame_count_ > 0 &&
            current_frame_index_ >= segment_frame_count_ - 1) {
            finishPlayback();
            return;
        }
        if (!ensureSessionAtCurrentFrame()) {
            finishPlayback();
            return;
        }
        const auto frame = session_->decode_next_frame();
        if (!frame.has_value() ||
            !isSourceFrameInRange(session_->current_frame_index())) {
            finishPlayback();
            return;
        }
        emitFrame(frame);
        audio_position_valid_ = false;
    } catch (const media::MediaError& error) {
        reportFailure(error, "step_forward");
    } catch (const std::exception& error) {
        reportFailure(error, "step_forward");
    }
}

void PlaybackWorker::stepBackward() {
    pause();
    if (source_path_.empty()) {
        if (!composition_enabled_ || segment_frame_count_ <= 0) return;
        current_frame_index_ = std::max<std::int64_t>(0, current_frame_index_ - 1);
        emitComposedFrame();
        return;
    }

    try {
        if (!session_) session_ = media::VideoPlaybackSession::open(source_path_);
        const auto target_frame = std::max<std::int64_t>(0, current_frame_index_ - 1);
        const auto source_frame = sourceFrameForLocal(target_frame);
        if (!source_frame.has_value()) {
            throw media::MediaError("The requested previous frame is outside the playback segment.");
        }
        emitFrame(session_->decode_frame_at(*source_frame));
        audio_position_valid_ = false;
    } catch (const media::MediaError& error) {
        reportFailure(error, "step_backward");
    } catch (const std::exception& error) {
        reportFailure(error, "step_backward");
    }
}

void PlaybackWorker::seekToFrame(qint64 frame_index, quint64 generation) {
    requestSeek(frame_index, generation);
}

void PlaybackWorker::processPendingSeek() {
    while (true) {
        const auto sequence = pending_seek_sequence_.load(std::memory_order_acquire);
        const auto frame_index = pending_seek_frame_.load(std::memory_order_relaxed);
        const auto generation = pending_seek_generation_.load(std::memory_order_relaxed);
        if (frame_index == no_pending_seek) {
            seek_dispatch_scheduled_.store(false, std::memory_order_release);
            if (pending_seek_frame_.load(std::memory_order_relaxed) != no_pending_seek &&
                !seek_dispatch_scheduled_.exchange(true, std::memory_order_acq_rel)) {
                continue;
            }
            return;
        }
        generation_ = generation;
        pause();

        const auto seek_is_current = [this, sequence]() {
            return !isSeekCurrent(sequence);
        };

        try {
            if (frame_index < 0) {
                throw media::MediaError("The requested frame index is negative.");
            }
            if (!isLocalFrameInRange(frame_index)) {
                throw media::MediaError("The requested frame is outside the playback segment.");
            }
            // The timeline can be scrubbed before a video source is selected.
            // There is no frame to decode in that state, but it is not a
            // playback error and the Main Window keeps the visual playhead.
            if (!source_path_.empty()) {
                if (!session_) {
                    session_ = media::VideoPlaybackSession::open(source_path_);
                }

                const auto source_frame = sourceFrameForLocal(frame_index);
                if (!source_frame.has_value()) {
                    throw media::MediaError("The requested source frame is outside the media range.");
                }
                auto frame = session_->decode_frame_at(source_frame.value(), seek_is_current);
                if (!isSeekCurrent(sequence)) continue;
                if (!frame.has_value()) {
                    throw media::MediaError("The requested frame is outside the media range.");
                }
                emitFrame(std::move(frame));
                audio_position_valid_ = false;
                pending_audio_bytes_.clear();
                if (audio_output_ != nullptr) audio_output_->stop();
            } else if (composition_enabled_) {
                current_frame_index_ = frame_index;
                emitComposedFrame();
            }
        } catch (const media::MediaError& error) {
            if (!isSeekCurrent(sequence)) continue;
            reportFailure(error, "seek", frame_index);
        } catch (const std::exception& error) {
            if (!isSeekCurrent(sequence)) continue;
            reportFailure(error, "seek", frame_index);
        }

        if (isSeekCurrent(sequence)) {
            seek_dispatch_scheduled_.store(false, std::memory_order_release);
            if (pending_seek_sequence_.load(std::memory_order_acquire) != sequence &&
                !seek_dispatch_scheduled_.exchange(true, std::memory_order_acq_rel)) {
                continue;
            }
            return;
        }
    }
}

void PlaybackWorker::decodeTick() {
    if (!playing_) return;

    try {
        if (composition_enabled_ && source_path_.empty()) {
            if (segment_frame_count_ > 0 &&
                current_frame_index_ >= segment_frame_count_ - 1) {
                finishPlayback();
                return;
            }
            ++current_frame_index_;
            emitComposedFrame();
            return;
        }

        if (!session_) {
            throw media::MediaError("Playback session is not available.");
        }
        if (audio_enabled_) {
            try {
                fillAudioOutput();
            } catch (const media::MediaError& error) {
                reportAudioFailure(error, "decode");
            } catch (const std::exception& error) {
                reportAudioFailure(error, "decode");
            }
        }
        if (segment_frame_count_ > 0 &&
            current_frame_index_ >= segment_frame_count_ - 1) {
            finishPlayback();
            return;
        }
        if (audio_enabled_ && audio_output_ != nullptr) {
            const auto elapsed_usecs = std::max<qint64>(
                0,
                audio_output_->processedUsecs() - audio_clock_origin_usecs_);
            const auto target_frame = std::min<std::int64_t>(
                segment_frame_count_ > 0 ? segment_frame_count_ - 1 : current_frame_index_ + 1,
                audio_clock_origin_frame_ + static_cast<std::int64_t>(
                    std::floor(static_cast<double>(elapsed_usecs) * frame_rate_ / 1000000.0)));
            if (target_frame <= current_frame_index_) return;
            while (current_frame_index_ < target_frame) {
                const auto frame = session_->decode_next_frame();
                if (!frame.has_value() ||
                    !isSourceFrameInRange(session_->current_frame_index())) {
                    finishPlayback();
                    return;
                }
                emitFrame(frame);
            }
            return;
        }
        const auto frame = session_->decode_next_frame();
        if (!frame.has_value() ||
            !isSourceFrameInRange(session_->current_frame_index())) {
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

bool PlaybackWorker::isSeekCurrent(quint64 sequence) const noexcept {
    return pending_seek_sequence_.load(std::memory_order_acquire) == sequence;
}

bool PlaybackWorker::ensureSessionAtCurrentFrame() {
    const auto source_frame = sourceFrameForLocal(current_frame_index_);
    if (!source_frame.has_value()) return false;
    if (session_->current_frame_index() == *source_frame) return true;

    const auto frame = session_->decode_frame_at(*source_frame);
    return frame.has_value() &&
        session_->current_frame_index() == *source_frame;
}

void PlaybackWorker::configureAudio() {
    audio_enabled_ = false;
    audio_output_ = std::make_unique<AudioOutput>();

    // Probe the media before touching the system audio device. A video-only
    // source is an expected case and must remain silent in the diagnostics.
    try {
        auto media_audio_probe = media::AudioPlaybackSession::open(source_path_);
        if (!media_audio_probe->has_audio()) return;
    } catch (const media::MediaError& error) {
        reportAudioFailure(error, "probe");
        return;
    } catch (const std::exception& error) {
        reportAudioFailure(error, "probe");
        return;
    }

    QString output_error;
    qint64 output_code = 0;
    if (!audio_output_->initialize(&output_error, &output_code)) {
        if (!audio_output_->disabledByEnvironment()) {
            reportAudioFailure(
                std::runtime_error(output_error.isEmpty()
                    ? "The audio output could not be initialized."
                    : output_error.toUtf8().toStdString()),
                "output",
                output_code);
        }
        return;
    }

    try {
        audio_session_ = media::AudioPlaybackSession::open(
            source_path_,
            media::AudioPlaybackSession::OutputSpec{
                audio_output_->sampleRate(),
                audio_output_->channelCount()});
        if (!audio_session_->has_audio()) {
            audio_session_.reset();
            audio_output_->stop();
            return;
        }
        const auto source_frame = sourceFrameForLocal(0);
        if (!source_frame.has_value()) {
            throw media::MediaError("The audio source frame is invalid.");
        }
        audio_session_->seek_to_source_frame(*source_frame, frame_rate_);
        audio_position_valid_ = true;
        audio_enabled_ = true;
    } catch (const media::MediaError& error) {
        reportAudioFailure(error, "open");
    } catch (const std::exception& error) {
        reportAudioFailure(error, "open");
    }
}

void PlaybackWorker::fillAudioOutput() {
    if (!audio_enabled_ || audio_session_ == nullptr || audio_output_ == nullptr) return;

    const auto effective_gain =
        (track_audio_muted_ || clip_audio_muted_)
            ? 0.0
            : track_audio_gain_ * clip_audio_gain_;
    const auto output = audio_session_->output_spec();
    const auto target_bytes = static_cast<std::size_t>(
        output.sample_rate * output.channel_count * 2 / 5);
    std::int64_t segment_end_sample = std::numeric_limits<std::int64_t>::max();
    if (segment_frame_count_ > 0 &&
        source_start_frame_ <= std::numeric_limits<std::int64_t>::max() - segment_frame_count_) {
        segment_end_sample = static_cast<std::int64_t>(std::ceil(
            static_cast<long double>(source_start_frame_ + segment_frame_count_) *
            output.sample_rate / static_cast<long double>(frame_rate_)));
    }

    while (pending_audio_bytes_.size() < static_cast<qsizetype>(target_bytes)) {
        auto chunk = audio_session_->decode_samples(4096);
        if (!chunk.has_value()) break;
        if (chunk->first_sample_index >= segment_end_sample) break;

        auto sample_count = static_cast<std::int64_t>(chunk->sampleCount());
        if (chunk->first_sample_index + sample_count > segment_end_sample) {
            sample_count = std::max<std::int64_t>(
                0,
                segment_end_sample - chunk->first_sample_index);
            chunk->samples.resize(static_cast<std::size_t>(sample_count) *
                                  static_cast<std::size_t>(chunk->channel_count));
        }
        if (sample_count <= 0) break;

        for (auto& sample : chunk->samples) {
            const auto scaled = static_cast<double>(sample) * effective_gain;
            sample = static_cast<std::int16_t>(std::clamp(
                scaled,
                static_cast<double>(std::numeric_limits<std::int16_t>::min()),
                static_cast<double>(std::numeric_limits<std::int16_t>::max())));
        }
        pending_audio_bytes_.append(
            reinterpret_cast<const char*>(chunk->samples.data()),
            static_cast<qsizetype>(chunk->samples.size() * sizeof(std::int16_t)));
    }

    while (!pending_audio_bytes_.isEmpty() && audio_output_->bytesFree() > 0) {
        const auto writable = std::min<qint64>(
            audio_output_->bytesFree(),
            pending_audio_bytes_.size());
        const auto written = audio_output_->write(
            pending_audio_bytes_.left(static_cast<qsizetype>(writable)));
        if (written <= 0) break;
        pending_audio_bytes_.remove(0, static_cast<qsizetype>(written));
    }
}

void PlaybackWorker::disableAudioOutput() noexcept {
    audio_enabled_ = false;
    audio_position_valid_ = false;
    pending_audio_bytes_.clear();
    if (audio_output_ != nullptr) audio_output_->stop();
}

void PlaybackWorker::reportAudioFailure(
    const media::MediaError& error,
    const char* operation) {
    reportAudioFailure(
        static_cast<const std::exception&>(error),
        operation,
        error.error_code().value_or(-1));
}

void PlaybackWorker::reportAudioFailure(
    const std::exception& error,
    const char* operation,
    qint64 error_code) {
    if (!audio_failure_reported_) {
        audio_failure_reported_ = true;
        try {
            logging::Context context{
                {"path", safePathForLog(source_path_)},
                {"frame_index", std::to_string(current_frame_index_)},
                {"source_start_frame", std::to_string(source_start_frame_)},
                {"segment_frame_count", std::to_string(segment_frame_count_)},
                {"track_index", std::to_string(track_index_)},
                {"clip_index", std::to_string(clip_index_)},
                {"track_audio_gain", std::to_string(track_audio_gain_)},
                {"clip_audio_gain", std::to_string(clip_audio_gain_)}};
            if (audio_session_ != nullptr) {
                context.emplace_back(
                    "sample_index",
                    std::to_string(audio_session_->current_sample_index()));
            }
            if (error_code >= 0) context.emplace_back("error_code", std::to_string(error_code));
            logging::Logger::instance().log(
                logging::Level::Error,
                "audio",
                operation,
                error.what(),
                context);
        } catch (...) {
        }
        emit audioWarning(
            QString::fromUtf8(error.what()),
            error_code,
            generation_);
    }
    disableAudioOutput();
    audio_session_.reset();
}

void PlaybackWorker::ensureTimer() {
    if (timer_ != nullptr) return;

    timer_ = new QTimer(this);
    connect(timer_, &QTimer::timeout, this, &PlaybackWorker::decodeTick);
}

void PlaybackWorker::finishPlayback() {
    if (timer_ != nullptr) timer_->stop();
    if (audio_output_ != nullptr) audio_output_->stop();
    audio_position_valid_ = false;
    pending_audio_bytes_.clear();
    const bool was_playing = playing_;
    playing_ = false;
    if (was_playing) emit playbackStateChanged(false, generation_);
    emit playbackFinished(generation_, was_playing);
}

void PlaybackWorker::emitFrame(std::optional<media::VideoFrame> frame) {
    if (!frame.has_value()) {
        finishPlayback();
        return;
    }

    const auto source_frame = session_->current_frame_index();
    if (!isSourceFrameInRange(source_frame)) {
        finishPlayback();
        return;
    }
    current_frame_index_ = source_frame - source_start_frame_;
    if (composition_enabled_) {
        emitComposedFrame();
        return;
    }
    auto payload = std::make_shared<const media::VideoFrame>(std::move(*frame));
    emit frameReady(std::move(payload), current_frame_index_, generation_);
}

void PlaybackWorker::emitComposedFrame() {
    const auto composed = decodeCompositionAt(
        primary_timeline_start_frame_ + current_frame_index_);
    if (!composed.has_value()) {
        throw media::MediaError("The timeline composition could not produce a frame.");
    }
    auto payload = std::make_shared<const media::VideoFrame>(*composed);
    emit frameReady(std::move(payload), current_frame_index_, generation_);
}

std::optional<media::VideoFrame> PlaybackWorker::decodeCompositionAt(
    std::int64_t global_frame) {
    std::vector<media::VideoFrame> decoded_frames;
    decoded_frames.reserve(composition_sessions_.size());
    std::vector<rendering::CompositionLayer> layers;
    layers.reserve(composition_sessions_.size());

    struct RenderRequest {
        CompositionSession* composition = nullptr;
        std::int64_t local_frame = 0;
        double opacity_multiplier = 1.0;
    };

    std::vector<CompositionSession*> ordered_sessions;
    ordered_sessions.reserve(composition_sessions_.size());
    for (auto& composition : composition_sessions_) {
        ordered_sessions.push_back(&composition);
    }
    std::sort(
        ordered_sessions.begin(),
        ordered_sessions.end(),
        [](const CompositionSession* left, const CompositionSession* right) {
            if (left->spec.track_index != right->spec.track_index) {
                return left->spec.track_index > right->spec.track_index;
            }
            if (left->spec.kind != right->spec.kind) {
                return left->spec.kind == timeline::ClipKind::Video;
            }
            return left->spec.clip_index < right->spec.clip_index;
        });

    std::vector<RenderRequest> requests;
    requests.reserve(ordered_sessions.size() + composition_transitions_.size());
    for (auto* composition : ordered_sessions) {
        const auto& spec = composition->spec;
        if (global_frame < spec.timeline_start_frame ||
            global_frame >= spec.timeline_start_frame + spec.segment_frame_count) {
            continue;
        }
        requests.push_back(RenderRequest{
            composition,
            global_frame - spec.timeline_start_frame,
            1.0});
    }

    const auto find_session = [this](qint64 track_index,
                                     qint64 clip_index) -> CompositionSession* {
        for (auto& composition : composition_sessions_) {
            if (composition.spec.track_index == track_index &&
                composition.spec.clip_index == clip_index) {
                return &composition;
            }
        }
        return nullptr;
    };
    const auto remove_requests_for = [&requests](CompositionSession* composition) {
        requests.erase(
            std::remove_if(
                requests.begin(),
                requests.end(),
                [composition](const RenderRequest& request) {
                    return request.composition == composition;
                }),
            requests.end());
    };
    const auto append_transition_request = [&requests](
        CompositionSession* composition,
        std::int64_t local_frame,
        double opacity_multiplier) {
        if (composition != nullptr && opacity_multiplier > 0.0) {
            requests.push_back(RenderRequest{
                composition,
                local_frame,
                std::clamp(opacity_multiplier, 0.0, 1.0)});
        }
    };

    for (const auto& transition : composition_transitions_) {
        if (transition.duration_frames <= 0) continue;
        auto* from = find_session(
            transition.track_index, transition.from_clip_index);
        auto* to = find_session(
            transition.track_index, transition.to_clip_index);
        if (from == nullptr || to == nullptr) continue;
        const auto boundary = transition.boundary_frame;
        const auto duration = transition.duration_frames;
        if (transition.kind == timeline::TransitionKind::CrossDissolve) {
            if (global_frame < boundary || global_frame >= boundary + duration) {
                continue;
            }
            const auto offset = global_frame - boundary;
            const auto from_local = std::max<std::int64_t>(
                0, from->spec.segment_frame_count - 1);
            const auto to_local = offset;
            const double blend = duration == 1
                ? 1.0
                : static_cast<double>(offset + 1) / static_cast<double>(duration);
            remove_requests_for(from);
            remove_requests_for(to);
            // FrameCompositor starts from an opaque black canvas. Keep the
            // outgoing layer opaque and use the incoming layer's alpha as the
            // blend factor; fading both layers would compound alpha against
            // the opaque background and darken the result.
            append_transition_request(
                from, from_local, 1.0);
            append_transition_request(
                to, to_local, blend);
        } else if (transition.kind == timeline::TransitionKind::FadeToBlack) {
            if (global_frame >= boundary - duration && global_frame < boundary) {
                const auto offset = global_frame - (boundary - duration);
                const double fade = static_cast<double>(offset + 1) /
                    static_cast<double>(duration);
                remove_requests_for(from);
                append_transition_request(
                    from,
                    global_frame - from->spec.timeline_start_frame,
                    1.0 - fade);
            } else if (global_frame >= boundary && global_frame < boundary + duration) {
                const auto offset = global_frame - boundary;
                const double fade = duration == 1
                    ? 0.0
                    : static_cast<double>(offset) /
                        static_cast<double>(duration - 1);
                remove_requests_for(to);
                append_transition_request(
                    to, offset, fade);
            }
        }
    }

    std::sort(
        requests.begin(),
        requests.end(),
        [](const RenderRequest& left, const RenderRequest& right) {
            if (left.composition->spec.track_index != right.composition->spec.track_index) {
                return left.composition->spec.track_index > right.composition->spec.track_index;
            }
            if (left.composition->spec.kind != right.composition->spec.kind) {
                return left.composition->spec.kind == timeline::ClipKind::Video;
            }
            return left.composition->spec.clip_index < right.composition->spec.clip_index;
        });

    for (const auto& request : requests) {
        const auto& spec = request.composition->spec;
        if (request.local_frame < 0 ||
            request.local_frame >= spec.segment_frame_count ||
            spec.source_start_frame >
                std::numeric_limits<std::int64_t>::max() - request.local_frame) {
            continue;
        }
        const auto source_frame = spec.source_start_frame + request.local_frame;
        std::optional<media::VideoFrame> frame;
        if (spec.kind == timeline::ClipKind::Text) {
            frame = rendering::renderText(spec.text);
            if (!frame.has_value()) {
                throw media::MediaError("The text layer could not be rasterized.");
            }
        } else if (request.composition->session != nullptr) {
            frame = request.composition->session->decode_frame_at(source_frame);
        }
        if (!frame.has_value()) continue;
        decoded_frames.push_back(std::move(*frame));
        auto transform = timeline::evaluateTransform(
            spec.transform,
            spec.keyframes,
            request.local_frame);
        transform.opacity *= request.opacity_multiplier;
        layers.push_back(rendering::CompositionLayer{
            &decoded_frames.back(),
            transform});
    }
    return rendering::FrameCompositor::compose(1920, 1080, layers);
}

void PlaybackWorker::reportFailure(
    const media::MediaError& error,
    const char* operation,
    std::optional<std::int64_t> requested_frame) {
    try {
        logging::Context context{
            {"path", safePathForLog(source_path_)},
            {"frame_index", std::to_string(current_frame_index_)},
            {"source_start_frame", std::to_string(source_start_frame_)},
            {"segment_frame_count", std::to_string(segment_frame_count_)}};
        if (requested_frame.has_value()) {
            context.emplace_back("requested_frame", std::to_string(*requested_frame));
        }
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
    emit playbackError(
        QString::fromUtf8(error.what()),
        error.error_code().value_or(-1),
        generation_);
}

void PlaybackWorker::reportFailure(
    const std::exception& error,
    const char* operation,
    std::optional<std::int64_t> requested_frame) {
    try {
        logging::Context context{
            {"path", safePathForLog(source_path_)},
            {"frame_index", std::to_string(current_frame_index_)},
            {"source_start_frame", std::to_string(source_start_frame_)},
            {"segment_frame_count", std::to_string(segment_frame_count_)}};
        if (requested_frame.has_value()) {
            context.emplace_back("requested_frame", std::to_string(*requested_frame));
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
    emit playbackError(QString::fromUtf8(error.what()), -1, generation_);
}

std::optional<std::int64_t> PlaybackWorker::sourceFrameForLocal(
    std::int64_t local_frame) const noexcept {
    if (local_frame < 0 ||
        local_frame > std::numeric_limits<std::int64_t>::max() -
            source_start_frame_) {
        return std::nullopt;
    }
    return source_start_frame_ + local_frame;
}

bool PlaybackWorker::isLocalFrameInRange(std::int64_t local_frame) const noexcept {
    return local_frame >= 0 &&
        (segment_frame_count_ <= 0 || local_frame < segment_frame_count_);
}

bool PlaybackWorker::isSourceFrameInRange(std::int64_t source_frame) const noexcept {
    if (source_frame < source_start_frame_) return false;
    if (segment_frame_count_ <= 0) return true;
    return source_frame - source_start_frame_ < segment_frame_count_;
}

int PlaybackWorker::frameIntervalMilliseconds() const noexcept {
    const double interval = 1000.0 / frame_rate_;
    const auto rounded = static_cast<int>(std::lround(interval));
    return std::clamp(rounded, 1, 1000);
}

} // namespace playback
