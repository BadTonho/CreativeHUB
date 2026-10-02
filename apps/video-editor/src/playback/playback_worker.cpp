#include "playback_worker.h"
#include "playback_transition_plan.h"

#include "../logging/logger.h"
#include "../rendering/preview_performance_metrics.h"
#include "../rendering/text_renderer.h"
#include "../timeline/timeline_time.h"

#include <QFileInfo>
#include <QByteArray>
#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>
#include <string>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace playback {
namespace {

constexpr double default_frame_rate = 30.0;
constexpr qint64 no_pending_seek = std::numeric_limits<qint64>::min();

bool hasAnimatedTextGeometry(
    const timeline::TransformKeyframes& keyframes) noexcept {
    return !keyframes.position_x.empty() ||
        !keyframes.position_y.empty() ||
        !keyframes.scale.empty() ||
        !keyframes.rotation.empty();
}

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

void consumeDecodeCacheHits(media::VideoPlaybackSession& session) noexcept {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.recordDecodedCacheHits(session.take_cache_hit_count());
    const auto cache = session.cache_snapshot();
    metrics.recordDecodedCacheState(cache.entries, cache.bytes);
}

void appendTimelinePositionContext(
    logging::Context& context,
    std::int64_t frame,
    timeline::FrameRate frame_rate) {
    context.emplace_back("timeline_frame", std::to_string(frame));
    context.emplace_back(
        "timeline_time_seconds",
        std::to_string(static_cast<double>(
            timeline::timelineTimeSeconds(frame, frame_rate))));
    context.emplace_back(
        "timeline_timecode",
        timeline::formatTimelineTimecode(frame, frame_rate));
}

std::unique_ptr<media::VideoPlaybackSession> openVideoPlaybackSession(
    const std::filesystem::path& source_path) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    rendering::PreviewPerformanceScope timing(
        metrics,
        rendering::PreviewTiming::MediaOpen);
    return media::VideoPlaybackSession::open(source_path);
}

void recordPacingCatchup(
    rendering::PreviewPerformanceMetrics& metrics,
    std::uint64_t count,
    bool audio_clock_ahead) noexcept {
    metrics.recordPacingSkippedFrames(count);
    if (audio_clock_ahead) {
        metrics.recordPacingAudioCatchupFrames(count);
    } else {
        metrics.recordPacingDeadlineCatchupFrames(count);
    }
}

void recordPacingCatchup(
    rendering::PreviewPerformanceMetrics& metrics,
    const detail::AudioPacingDecision& decision) noexcept {
    const auto total = decision.deadline_catchup_frames +
        decision.audio_catchup_frames;
    metrics.recordPacingSkippedFrames(total);
    metrics.recordPacingDeadlineCatchupFrames(
        decision.deadline_catchup_frames);
    metrics.recordPacingAudioCatchupFrames(
        decision.audio_catchup_frames);
}

} // namespace

PlaybackWorker::PlaybackWorker(QObject* parent, GpuCompose gpu_compose)
    : QObject(parent), gpu_compose_(std::move(gpu_compose)) {}

PlaybackWorker::~PlaybackWorker() {
    gpu_compositor_.reset();
    if (timer_ != nullptr) timer_->stop();
    cancelTransitionPreroll();
    if (transition_preroll_thread_.joinable()) {
        transition_preroll_thread_.join();
    }
    transition_preroll_state_.reset();
    disableAudioOutput();
    audio_session_.reset();
    session_.reset();
    composition_sessions_.clear();
    composition_specs_.clear();
    composition_transitions_.clear();
    composition_audio_mix_clips_.clear();
    composition_audio_mix_transitions_.clear();
    clearCompositionCache();
}

void PlaybackWorker::initializeDiagnostics() {
    if (diagnostics_logged_) return;
    diagnostics_logged_ = true;

    const auto thread_id = logging::current_thread_id();
    rendering::PreviewPerformanceMetrics::instance()
        .setPlaybackWorkerThreadId(thread_id);
    logging::Logger::instance().log(
        logging::Level::Info,
        "playback",
        "worker_ready",
        "Playback worker initialized.",
        {{"thread_role", "playback_worker"}});
}

void PlaybackWorker::clearCompositionCache() noexcept {
    cached_composition_generation_ = 0;
    cached_composition_global_frame_ = -1;
    cached_composition_frame_.reset();
}

void PlaybackWorker::requestSeek(qint64 frame_index, quint64 generation) {
    rendering::PreviewPerformanceMetrics::instance().recordSeekRequest();
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
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.recordActivationStarted();
    pending_seek_frame_.store(no_pending_seek, std::memory_order_relaxed);
    pending_seek_generation_.store(generation, std::memory_order_relaxed);
    pending_seek_sequence_.fetch_add(1, std::memory_order_release);

    if (timer_ != nullptr) timer_->stop();
    if (playing_) {
        rendering::PreviewPerformanceMetrics::instance().setPlaybackActive(false);
        playing_ = false;
        emit playbackStateChanged(false, generation_);
    }

    generation_ = generation;
    source_path_ = QFileInfo(source_path).filesystemFilePath();
    source_frame_rate_ = std::isfinite(frame_rate) && frame_rate > 0.0 &&
            frame_rate <= 1000.0
        ? frame_rate
        : default_frame_rate;
    if (composition_enabled_ && !composition_timeline_frame_rate_valid_) {
        setFallbackTimelineFrameRate(source_frame_rate_);
    }
    updateFrameRateMetrics();
    source_start_frame_ = source_start_frame;
    segment_frame_count_ = segment_frame_count;
    current_frame_index_ = 0;
    resetPlaybackClock();
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
    audio_pacing_policy_.reset();
    pending_audio_bytes_.clear();
    disableAudioOutput();
    audio_session_.reset();
    composition_audio_configured_ = false;
    composition_audio_cursor_valid_ = false;
    session_.reset();
    clearCompositionCache();

    try {
        if (source_start_frame_ < 0 || segment_frame_count_ < 0) {
            throw media::MediaError("The playback segment range is invalid.");
        }
        const bool has_prepared_composition_source = composition_enabled_ &&
            std::any_of(
                composition_sessions_.begin(), composition_sessions_.end(),
                [this, track_index, clip_index](const CompositionSession& entry) {
                    return entry.spec.kind == timeline::ClipKind::Video &&
                        entry.spec.track_index == track_index &&
                        entry.spec.clip_index == clip_index &&
                        !entry.spec.source_path.isEmpty() &&
                        QFileInfo(entry.spec.source_path).filesystemFilePath() ==
                            source_path_ && entry.session != nullptr;
                });
        if (!has_prepared_composition_source) {
            session_ = openVideoPlaybackSession(source_path_);
        }
        {
            rendering::PreviewPerformanceScope timing(
                metrics,
                rendering::PreviewTiming::AudioSetup);
            if (!composition_enabled_) configureAudio();
        }
        emit mediaReady(generation_);
    } catch (const media::MediaError& error) {
        reportFailure(error, "set_media");
    } catch (const std::exception& error) {
        reportFailure(error, "set_media");
    }
}

void PlaybackWorker::play() {
    if (!composition_enabled_ && source_path_.empty()) return;
    if (composition_enabled_ &&
        composition_end_frame_ <= composition_start_frame_) return;

    try {
        if (composition_enabled_) {
            rendering::PreviewPerformanceScope timing(
                rendering::PreviewPerformanceMetrics::instance(),
                rendering::PreviewTiming::AudioSetup);
            configureCompositionAudio();
        }
        if (!composition_enabled_) {
            if (!session_) session_ = openVideoPlaybackSession(source_path_);
            if (session_->at_end()) {
                session_->reset();
                current_frame_index_ = 0;
            }
            if (!ensureSessionAtCurrentFrame()) {
                finishPlayback();
                return;
            }
        }

        if (audio_enabled_ && audio_output_ != nullptr) {
            try {
                if (composition_enabled_) {
                    if (!composition_audio_cursor_valid_) {
                        const auto sample_position = static_cast<long double>(
                            current_timeline_frame_) * audio_output_->sampleRate() /
                            timeline_frame_rate_.asDouble();
                        if (current_timeline_frame_ < 0 ||
                            !std::isfinite(sample_position) ||
                            sample_position > static_cast<long double>(
                                std::numeric_limits<std::int64_t>::max())) {
                            throw media::MediaError(
                                "The Timeline audio position is invalid.");
                        }
                        next_composition_audio_sample_ = static_cast<std::int64_t>(
                            std::llround(sample_position));
                        composition_audio_cursor_valid_ = true;
                        pending_audio_bytes_.clear();
                    }
                } else if (audio_session_ != nullptr && !audio_position_valid_) {
                    const auto source_frame = sourceFrameForLocal(current_frame_index_);
                    if (!source_frame.has_value()) {
                        throw media::MediaError("The requested audio source frame is invalid.");
                    }
                    audio_session_->seek_to_source_frame(*source_frame, source_frame_rate_);
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
                    audio_clock_origin_frame_ = composition_enabled_
                        ? current_timeline_frame_
                        : current_frame_index_;
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
                    updateAudioBufferMetric();
                } catch (const media::MediaError& error) {
                    reportAudioFailure(error, "decode");
                } catch (const std::exception& error) {
                    reportAudioFailure(error, "decode");
                }
            }
        }

        ensureTimer();
        if (!playing_) {
            startPlaybackClock();
            playing_ = true;
            rendering::PreviewPerformanceMetrics::instance().setPlaybackActive(true);
            emit playbackStateChanged(true, generation_);
        }
        updateTransitionPreroll();
        scheduleNextPlaybackTick();
    } catch (const media::MediaError& error) {
        reportFailure(error, "play");
    } catch (const std::exception& error) {
        reportFailure(error, "play");
    }
}

void PlaybackWorker::pause() {
    if (timer_ != nullptr) timer_->stop();
    if (audio_output_ != nullptr) audio_output_->pause();
    resetPlaybackClock();
    audio_pacing_policy_.reset();
    rendering::PreviewPerformanceMetrics::instance().setPlaybackActive(false);
    if (!playing_) return;

    playing_ = false;
    emit playbackStateChanged(false, generation_);
}

void PlaybackWorker::stop() {
    pause();
    cancelTransitionPreroll();
    last_transition_preroll_session_index_ =
        std::numeric_limits<std::size_t>::max();
    last_transition_preroll_start_frame_ = -1;
    if (audio_output_ != nullptr) audio_output_->stop();
    audio_position_valid_ = false;
    composition_audio_cursor_valid_ = false;
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

void PlaybackWorker::setCompositionAudioParameters(
    qint64 track_index,
    qint64 clip_index,
    double track_audio_gain,
    bool track_audio_muted,
    double clip_audio_gain,
    bool clip_audio_muted) {
    const auto valid_gain = [](double gain) {
        return std::isfinite(gain) && gain >= 0.0 && gain <= 2.0 ? gain : 1.0;
    };
    for (auto& spec : composition_specs_) {
        if (spec.track_index == track_index) {
            spec.track_audio_gain = valid_gain(track_audio_gain);
            spec.track_audio_muted = track_audio_muted;
        }
        if (spec.track_index == track_index && spec.clip_index == clip_index) {
            spec.clip_audio_gain = valid_gain(clip_audio_gain);
            spec.clip_audio_muted = clip_audio_muted;
        }
    }
    for (auto& entry : composition_sessions_) {
        if (entry.spec.track_index == track_index) {
            entry.spec.track_audio_gain = valid_gain(track_audio_gain);
            entry.spec.track_audio_muted = track_audio_muted;
        }
        if (entry.spec.track_index == track_index &&
            entry.spec.clip_index == clip_index) {
            entry.spec.clip_audio_gain = valid_gain(clip_audio_gain);
            entry.spec.clip_audio_muted = clip_audio_muted;
        }
    }
    for (auto& clip : composition_audio_mix_clips_) {
        if (clip.track_index == track_index) {
            clip.track_gain = valid_gain(track_audio_gain);
            clip.track_muted = track_audio_muted;
        }
        if (clip.track_index == track_index && clip.clip_index == clip_index) {
            clip.clip_gain = valid_gain(clip_audio_gain);
            clip.clip_muted = clip_audio_muted;
        }
    }

    if (!composition_enabled_ || !audio_enabled_ || audio_output_ == nullptr) return;
    pending_audio_bytes_.clear();
    composition_audio_cursor_valid_ = false;
    audio_output_->stop();
    if (playing_) restartCompositionAudioOutput();
}

void PlaybackWorker::setMonitorVolume(double gain) {
    monitor_volume_gain_ = AudioOutput::normalizeVolume(gain);
    if (audio_output_ != nullptr) {
        audio_output_->setVolume(monitor_volume_gain_);
    }
}

void PlaybackWorker::setGpuCompositionEnabled(bool enabled, QOffscreenSurface* surface) {
    if (enabled == gpu_composition_enabled_ && surface == gpu_surface_) return;
    gpu_compositor_.reset();
    gpu_surface_ = surface;
    gpu_composition_enabled_ = enabled;
    gpu_composition_failed_ = false;
    gpu_warning_reported_ = false;
    clearCompositionCache();
}

void PlaybackWorker::setPreviewQuality(PreviewQuality quality) {
    if (quality != PreviewQuality::Full &&
        quality != PreviewQuality::Half &&
        quality != PreviewQuality::Quarter) {
        quality = PreviewQuality::Full;
    }
    if (preview_quality_ == quality) return;

    preview_quality_ = quality;
    clearCompositionCache();
}

void PlaybackWorker::setComposition(
    QVector<CompositionLayerSpec> layers,
    QVector<CompositionTransitionSpec> transitions,
    quint64 generation) {
    if (generation < generation_) return;
    const bool resume_audio_after_refresh = playing_;
    generation_ = generation;

    disableAudioOutput();
    audio_session_.reset();
    composition_audio_configured_ = false;
    composition_audio_cursor_valid_ = false;
    composition_audio_mix_clips_.clear();
    composition_audio_mix_transitions_.clear();

    cancelTransitionPreroll();
    if (composition_revision_ == std::numeric_limits<quint64>::max()) {
        composition_revision_ = 1;
    } else {
        ++composition_revision_;
    }
    last_transition_preroll_session_index_ =
        std::numeric_limits<std::size_t>::max();
    last_transition_preroll_start_frame_ = -1;
    collectTransitionPreroll();

    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    rendering::PreviewPerformanceScope setup_timing(
        metrics,
        rendering::PreviewTiming::CompositionSetup);

    clearCompositionCache();

    composition_specs_ = std::move(layers);
    composition_transitions_ = std::move(transitions);
    composition_sessions_.clear();
    composition_enabled_ = !composition_specs_.isEmpty();
    composition_start_frame_ = 0;
    composition_end_frame_ = 0;
    if (!composition_enabled_) {
        composition_position_initialized_ = false;
    }
    composition_timeline_frame_rate_valid_ = false;
    if (composition_enabled_) {
        const auto rate = std::find_if(
            composition_specs_.cbegin(),
            composition_specs_.cend(),
            [](const CompositionLayerSpec& spec) {
                return timeline::validFrameRate(spec.timeline_frame_rate);
            });
        if (rate != composition_specs_.cend()) {
            timeline_frame_rate_ = timeline::reducedFrameRate(
                rate->timeline_frame_rate);
            composition_timeline_frame_rate_valid_ = true;
        } else {
            setFallbackTimelineFrameRate(source_frame_rate_);
        }
    }
    const auto text_layer_count = static_cast<std::uint64_t>(std::count_if(
        composition_specs_.cbegin(),
        composition_specs_.cend(),
        [](const CompositionLayerSpec& spec) {
            return spec.kind == timeline::ClipKind::Text;
        }));
    metrics.setCompositionWorkload(
        static_cast<std::uint64_t>(composition_specs_.size()),
        text_layer_count,
        static_cast<std::uint64_t>(composition_transitions_.size()),
        composition_enabled_);
    primary_timeline_start_frame_ = 0;
    if (!composition_enabled_) {
        updateFrameRateMetrics();
        return;
    }

    std::int64_t composition_start = std::numeric_limits<std::int64_t>::max();
    std::int64_t composition_end = 0;
    bool has_primary_clip = false;
    bool primary_is_static = false;
    std::int64_t primary_clip_duration = 0;

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
                composition_session.session = openVideoPlaybackSession(
                    QFileInfo(spec.source_path).filesystemFilePath());
            } else if (spec.kind == timeline::ClipKind::Image) {
                if (spec.still_frame == nullptr) continue;
                composition_session.static_frame = spec.still_frame;
            }
            if (spec.track_index == track_index_ && spec.clip_index == clip_index_) {
                primary_timeline_start_frame_ = spec.timeline_start_frame;
                has_primary_clip = true;
                primary_is_static = spec.kind != timeline::ClipKind::Video;
                primary_clip_duration = spec.segment_frame_count;
                if (spec.kind == timeline::ClipKind::Video &&
                    std::isfinite(spec.frame_rate) && spec.frame_rate > 0.0 &&
                    spec.frame_rate <= 1000.0) {
                    source_frame_rate_ = spec.frame_rate;
                    if (!composition_timeline_frame_rate_valid_) {
                        setFallbackTimelineFrameRate(source_frame_rate_);
                    }
                } else if (!composition_timeline_frame_rate_valid_) {
                    setFallbackTimelineFrameRate(default_frame_rate);
                }
            }
            composition_sessions_.push_back(std::move(composition_session));
            const auto& prepared = composition_sessions_.back().spec;
            composition_audio_mix_clips_.push_back(media::TimelineAudioMixClip{
                composition_sessions_.size() - 1,
                prepared.track_index,
                prepared.clip_index,
                prepared.kind,
                prepared.has_audio_stream,
                prepared.timeline_start_frame,
                prepared.segment_frame_count,
                prepared.source_start_frame,
                prepared.frame_rate,
                prepared.track_audio_gain,
                prepared.clip_audio_gain,
                prepared.track_audio_muted,
                prepared.clip_audio_muted});
        }

        composition_audio_mix_transitions_.reserve(
            static_cast<std::size_t>(composition_transitions_.size()));
        for (const auto& transition : composition_transitions_) {
            composition_audio_mix_transitions_.push_back(
                media::TimelineAudioMixTransition{
                    transition.track_index,
                    transition.to_clip_index,
                    transition.boundary_frame,
                    transition.kind});
        }

        const bool has_composition_range =
            composition_start != std::numeric_limits<std::int64_t>::max() &&
            composition_end > composition_start;
        if (has_composition_range) {
            composition_start_frame_ = composition_start;
            composition_end_frame_ = composition_end;
            if (!composition_position_initialized_ ||
                current_timeline_frame_ < composition_start_frame_ ||
                current_timeline_frame_ >= composition_end_frame_) {
                current_timeline_frame_ = has_primary_clip
                    ? primary_timeline_start_frame_
                    : composition_start_frame_;
            }
            composition_position_initialized_ = true;
        } else {
            composition_start_frame_ = 0;
            composition_end_frame_ = 0;
            composition_position_initialized_ = false;
        }

        if (has_primary_clip) {
            if (primary_is_static) {
                source_path_.clear();
                session_.reset();
                audio_enabled_ = false;
                audio_session_.reset();
                disableAudioOutput();
            } else if (composition_enabled_) {
                // The active video is decoded from its prepared composition
                // session. Keep the separate session only as a fallback when
                // composition is unavailable.
                session_.reset();
            }
            segment_frame_count_ = primary_clip_duration;
            current_frame_index_ = std::clamp<std::int64_t>(
                current_timeline_frame_ - primary_timeline_start_frame_,
                0,
                std::max<std::int64_t>(0, segment_frame_count_ - 1));
        } else if (source_path_.empty() && composition_start !=
                       std::numeric_limits<std::int64_t>::max() &&
                   composition_end > composition_start) {
            primary_timeline_start_frame_ = composition_start;
            segment_frame_count_ = composition_end - composition_start;
            current_frame_index_ = std::clamp<std::int64_t>(
                current_timeline_frame_ - primary_timeline_start_frame_,
                0,
                std::max<std::int64_t>(0, segment_frame_count_ - 1));
            if (!composition_timeline_frame_rate_valid_) {
                setFallbackTimelineFrameRate(default_frame_rate);
            }
        }
        updateFrameRateMetrics();
        if (resume_audio_after_refresh && composition_enabled_) {
            configureCompositionAudio();
            restartCompositionAudioOutput();
        }
    } catch (const media::MediaError& error) {
        composition_sessions_.clear();
        composition_audio_mix_clips_.clear();
        composition_audio_mix_transitions_.clear();
        composition_transitions_.clear();
        composition_enabled_ = false;
        composition_timeline_frame_rate_valid_ = false;
        composition_start_frame_ = 0;
        composition_end_frame_ = 0;
        composition_position_initialized_ = false;
        metrics.setCompositionWorkload(0, 0, 0, false);
        updateFrameRateMetrics();
        reportFailure(error, "compose");
        if (resume_audio_after_refresh) configureCompositionAudio();
    } catch (const std::exception& error) {
        composition_sessions_.clear();
        composition_audio_mix_clips_.clear();
        composition_audio_mix_transitions_.clear();
        composition_transitions_.clear();
        composition_enabled_ = false;
        composition_timeline_frame_rate_valid_ = false;
        composition_start_frame_ = 0;
        composition_end_frame_ = 0;
        composition_position_initialized_ = false;
        metrics.setCompositionWorkload(0, 0, 0, false);
        updateFrameRateMetrics();
        reportFailure(error, "compose");
        if (resume_audio_after_refresh) configureCompositionAudio();
    }
}

void PlaybackWorker::setActiveCompositionClip(
    qint64 track_index,
    qint64 clip_index,
    qint64 global_timeline_frame) {
    track_index_ = track_index;
    clip_index_ = clip_index;
    const auto active = std::find_if(
        composition_specs_.cbegin(), composition_specs_.cend(),
        [track_index, clip_index](const CompositionLayerSpec& spec) {
            return spec.track_index == track_index &&
                spec.clip_index == clip_index;
        });
    if (active == composition_specs_.cend()) return;

    primary_timeline_start_frame_ = active->timeline_start_frame;
    segment_frame_count_ = active->segment_frame_count;
    if (global_timeline_frame != std::numeric_limits<qint64>::min()) {
        current_timeline_frame_ = global_timeline_frame;
    } else {
        current_timeline_frame_ = primary_timeline_start_frame_ +
            std::clamp<std::int64_t>(
                current_frame_index_, 0,
                std::max<std::int64_t>(0, segment_frame_count_ - 1));
    }
    composition_position_initialized_ = true;
    current_frame_index_ = std::clamp<std::int64_t>(
        current_timeline_frame_ - primary_timeline_start_frame_,
        0,
        std::max<std::int64_t>(0, segment_frame_count_ - 1));
    if (active->kind == timeline::ClipKind::Video &&
        std::isfinite(active->frame_rate) && active->frame_rate > 0.0 &&
        active->frame_rate <= 1000.0) {
        source_frame_rate_ = active->frame_rate;
        if (!composition_timeline_frame_rate_valid_) {
            setFallbackTimelineFrameRate(source_frame_rate_);
        }
    } else if (!composition_timeline_frame_rate_valid_) {
        setFallbackTimelineFrameRate(default_frame_rate);
    }
    updateFrameRateMetrics();

    if (active->kind == timeline::ClipKind::Text ||
        active->kind == timeline::ClipKind::Image) {
        source_path_.clear();
        session_.reset();
    }
}

void PlaybackWorker::cancelActivation(quint64 generation) {
    if (generation < generation_) return;
    generation_ = generation;
    pending_seek_frame_.store(no_pending_seek, std::memory_order_relaxed);
    pending_seek_generation_.store(generation, std::memory_order_relaxed);
    pending_seek_sequence_.fetch_add(1, std::memory_order_release);
    stop();
}

void PlaybackWorker::renderCompositionFrame(
    qint64 global_frame,
    qint64 frame_index,
    quint64 generation) {
    if (generation < generation_ || !composition_enabled_) return;
    generation_ = generation;
    try {
        current_timeline_frame_ = global_frame;
        if (!playing_) {
            composition_audio_cursor_valid_ = false;
            pending_audio_bytes_.clear();
            if (audio_output_ != nullptr) audio_output_->stop();
        }
        if (transition_preroll_state_ != nullptr &&
            global_frame >= transition_preroll_state_->transition_start_frame) {
            cancelTransitionPreroll();
        }
        composition_position_initialized_ = true;
        if (composition_enabled_ && segment_frame_count_ > 0) {
            current_frame_index_ = std::clamp<std::int64_t>(
                frame_index,
                0,
                segment_frame_count_ - 1);
        }
        auto& metrics = rendering::PreviewPerformanceMetrics::instance();
        if (cached_composition_frame_ != nullptr &&
            cached_composition_generation_ == generation &&
            cached_composition_global_frame_ == global_frame) {
            const auto trace_id = playing_
                ? metrics.createFrameDeliveryTrace(generation_, global_frame)
                : 0U;
            metrics.recordCompositionCacheHit();
            metrics.recordComposedFrame();
            metrics.recordEmittedFrame();
            emit frameReady(
                cached_composition_frame_, frame_index, generation_, trace_id);
            return;
        }

        const bool collect_slow_frame = playing_ && metrics.isEnabled();
        const auto frame_started = collect_slow_frame ? Clock::now() : Clock::time_point{};

        const auto seek_sequence = pending_seek_sequence_.load(std::memory_order_acquire);
        const auto should_cancel = [this, seek_sequence]() {
            return !isSeekCurrent(seek_sequence);
        };
        std::optional<std::vector<DecodedCompositionLayer>> decoded_layers;
        const auto decode_started = collect_slow_frame ? Clock::now() : Clock::time_point{};
        {
            rendering::PreviewPerformanceScope timing(
                metrics,
                rendering::PreviewTiming::Decode);
            decoded_layers = decodeCompositionLayers(global_frame, should_cancel);
        }
        const auto decode_elapsed = collect_slow_frame
            ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - decode_started)
            : std::chrono::nanoseconds::zero();
        if (should_cancel()) return;
        if (!decoded_layers.has_value()) {
            throw media::MediaError("The timeline composition could not produce a frame.");
        }

        std::optional<media::VideoFrame> composed;
        auto& composition_timings = composition_timings_scratch_;
        const auto composition_started = collect_slow_frame
            ? Clock::now()
            : Clock::time_point{};
        {
            rendering::PreviewPerformanceScope timing(
                metrics,
                rendering::PreviewTiming::Composition);
            composed = composeCompositionLayers(
                *decoded_layers,
                should_cancel,
                collect_slow_frame ? &composition_timings : nullptr);
        }
        const auto composition_elapsed = collect_slow_frame
            ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - composition_started)
            : std::chrono::nanoseconds::zero();
        if (should_cancel() || last_composition_cancelled_) return;
        if (!composed.has_value()) {
            throw media::MediaError("The timeline composition could not produce a frame.");
        }
        if (collect_slow_frame && !last_composition_gpu_) {
            metrics.recordBlendLookupComposition(composition_timings);
        }

        std::shared_ptr<const media::VideoFrame> payload;
        const auto payload_started = collect_slow_frame ? Clock::now() : Clock::time_point{};
        {
            rendering::PreviewPerformanceScope timing(
                metrics,
                rendering::PreviewTiming::Payload);
            payload = std::make_shared<const media::VideoFrame>(std::move(*composed));
        }
        const auto payload_elapsed = collect_slow_frame
            ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - payload_started)
            : std::chrono::nanoseconds::zero();

        if (collect_slow_frame) {
            rendering::SlowFrameSample sample;
            sample.gpu_composition = last_composition_gpu_;
            sample.playback_generation = generation_;
            sample.timeline_frame = global_frame;
            const auto clock_frame_rate = playbackFrameRate();
            sample.frame_rate_milli = std::isfinite(clock_frame_rate) &&
                    clock_frame_rate > 0.0
                ? static_cast<std::uint64_t>(std::llround(clock_frame_rate * 1000.0))
                : 0U;
            if (composition_enabled_ && timeline::validFrameRate(timeline_frame_rate_)) {
                sample.timeline_frame_rate_numerator = static_cast<std::uint64_t>(
                    timeline_frame_rate_.numerator);
                sample.timeline_frame_rate_denominator = static_cast<std::uint64_t>(
                    timeline_frame_rate_.denominator);
            }
            sample.frame_budget_nanoseconds = std::isfinite(clock_frame_rate) &&
                    clock_frame_rate > 0.0
                ? static_cast<std::uint64_t>(1'000'000'000.0 / clock_frame_rate)
                : 0U;
            sample.processing_nanoseconds = static_cast<std::uint64_t>(
                std::max<std::int64_t>(0,
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        Clock::now() - frame_started).count()));
            const bool sample_is_slow =
                sample.processing_nanoseconds > sample.frame_budget_nanoseconds;
            sample.decode_nanoseconds = static_cast<std::uint64_t>(
                std::max<std::int64_t>(0, decode_elapsed.count()));
            sample.composition_nanoseconds = static_cast<std::uint64_t>(
                std::max<std::int64_t>(0, composition_elapsed.count()));
            sample.payload_nanoseconds = static_cast<std::uint64_t>(
                std::max<std::int64_t>(0, payload_elapsed.count()));
            sample.composition_adapter_nanoseconds =
                composition_timings.layer_list_setup_nanoseconds;
            sample.output_buffer_create_nanoseconds =
                composition_timings.output_buffer_create_nanoseconds;
            sample.output_background_fill_nanoseconds =
                composition_timings.output_background_fill_nanoseconds;
            sample.active_layer_count = decoded_layers->size();
            if (sample_is_slow) {
                sample.composition_canvas_width = composition_timings.canvas_width;
                sample.composition_canvas_height = composition_timings.canvas_height;
            }

            for (std::size_t index = 0; index < decoded_layers->size(); ++index) {
                const auto& decoded = (*decoded_layers)[index];
                rendering::SlowFrameLayerSample layer;
                layer.track_id = decoded.track_id;
                layer.clip_id = decoded.clip_id;
                layer.track_index = decoded.track_index;
                layer.clip_index = decoded.clip_index;
                layer.source_frame = decoded.source_frame;
                if (sample_is_slow && decoded.frame != nullptr) {
                    layer.source_width = decoded.frame->width;
                    layer.source_height = decoded.frame->height;
                    layer.source_stride = decoded.frame->stride;
                    layer.transform_position_x = decoded.transform.position_x;
                    layer.transform_position_y = decoded.transform.position_y;
                    layer.transform_scale = decoded.transform.scale;
                    layer.transform_rotation_degrees =
                        decoded.transform.rotation_degrees;
                    layer.transform_opacity = decoded.transform.opacity;
                }
                switch (decoded.kind) {
                case timeline::ClipKind::Video:
                    layer.kind = rendering::SlowFrameLayerKind::Video;
                    break;
                case timeline::ClipKind::Image:
                    layer.kind = rendering::SlowFrameLayerKind::Image;
                    break;
                case timeline::ClipKind::Text:
                    layer.kind = rendering::SlowFrameLayerKind::Text;
                    break;
                }
                layer.decode_path = decoded.decode_path;
                layer.decode_nanoseconds = decoded.decode_nanoseconds;
                if (index < composition_timings.layers.size()) {
                    const auto& layer_timing = composition_timings.layers[index];
                    layer.composition_setup_nanoseconds =
                        layer_timing.setup_nanoseconds;
                    layer.raster_blend_nanoseconds =
                        layer_timing.raster_blend_nanoseconds;
                    layer.fast_path_copy_nanoseconds =
                        layer_timing.fast_path_copy_nanoseconds;
                    layer.blend_lookup_built = layer_timing.blend_lookup_built;
                    layer.blend_lookup_build_nanoseconds =
                        layer_timing.blend_lookup_build_nanoseconds;
                    layer.blend_lookup_active_block_nanoseconds =
                        layer_timing.blend_lookup_active_block_nanoseconds;
                    layer.blend_lookup_pixel_count =
                        layer_timing.blend_lookup_pixel_count;
                    layer.blend_lookup_active_block_count =
                        layer_timing.blend_lookup_active_block_count;
                    if (sample_is_slow) {
                        layer.composition_path = layer_timing.raster_path;
                        layer.full_frame_copy_eligibility =
                            layer_timing.full_frame_copy_eligibility;
                    }
                    layer.composition_nanoseconds =
                        layer.composition_setup_nanoseconds +
                        layer.raster_blend_nanoseconds +
                        layer.fast_path_copy_nanoseconds;
                    sample.composition_layer_setup_nanoseconds +=
                        layer.composition_setup_nanoseconds;
                    sample.composition_raster_blend_nanoseconds +=
                        layer.raster_blend_nanoseconds;
                    sample.composition_fast_path_copy_nanoseconds +=
                        layer.fast_path_copy_nanoseconds;
                }
                layer.forward_decode_collected =
                    decoded.forward_decode.collected;
                layer.forward_decode_completed =
                    decoded.forward_decode.completed;
                layer.forward_decode_cancelled =
                    decoded.forward_decode.cancelled;
                layer.forward_decode_start_frame =
                    decoded.forward_decode.starting_frame;
                layer.forward_decode_requested_frame =
                    decoded.forward_decode.requested_frame;
                layer.forward_decode_discarded_frames =
                    decoded.forward_decode.discarded_intermediate_frames;
                layer.forward_decode_elapsed_nanoseconds =
                    decoded.forward_decode.elapsed_nanoseconds;
                layer.forward_decode_packet_io_nanoseconds =
                    decoded.forward_decode.packet_io_nanoseconds;
                layer.forward_decode_receive_nanoseconds =
                    decoded.forward_decode.decoder_receive_nanoseconds;
                layer.forward_decode_pixel_conversion_nanoseconds =
                    decoded.forward_decode.target_pixel_conversion_nanoseconds;

                rendering::addSlowFrameLayer(sample, layer);
            }
            metrics.recordSlowFrame(sample);
        }

        cached_composition_generation_ = generation_;
        cached_composition_global_frame_ = global_frame;
        cached_composition_frame_ = payload;
        metrics.recordComposedFrame();
        metrics.recordEmittedFrame();
        const auto trace_id = playing_
            ? metrics.createFrameDeliveryTrace(generation_, global_frame)
            : 0U;
        emit frameReady(std::move(payload), frame_index, generation_, trace_id);
    } catch (const media::MediaError& error) {
        reportFailure(error, "compose", frame_index);
    } catch (const std::exception& error) {
        reportFailure(error, "compose", frame_index);
    }
}

void PlaybackWorker::stepForward() {
    pause();
    if (composition_enabled_) {
        if (composition_end_frame_ <= composition_start_frame_ ||
            current_timeline_frame_ >= composition_end_frame_ - 1) {
            finishPlayback();
            return;
        }
        ++current_timeline_frame_;
        current_frame_index_ = std::clamp<std::int64_t>(
            current_timeline_frame_ - primary_timeline_start_frame_,
            0,
            std::max<std::int64_t>(0, segment_frame_count_ - 1));
        emitComposedFrame();
        return;
    }
    if (source_path_.empty()) {
        return;
    }

    try {
        if (!session_) session_ = openVideoPlaybackSession(source_path_);
        if (segment_frame_count_ > 0 &&
            current_frame_index_ >= segment_frame_count_ - 1) {
            finishPlayback();
            return;
        }
        if (!ensureSessionAtCurrentFrame()) {
            finishPlayback();
            return;
        }
        auto& metrics = rendering::PreviewPerformanceMetrics::instance();
        std::optional<media::VideoFramePtr> frame;
        {
            rendering::PreviewPerformanceScope timing(
                metrics,
                rendering::PreviewTiming::Decode);
            frame = session_->decode_next_frame();
        }
        consumeDecodeCacheHits(*session_);
        if (frame.has_value()) metrics.recordDecodedFrame();
        if (!frame.has_value() ||
            !isSourceFrameInRange(session_->current_frame_index())) {
            finishPlayback();
            return;
        }
        emitFrame(std::move(frame));
        audio_position_valid_ = false;
    } catch (const media::MediaError& error) {
        reportFailure(error, "step_forward");
    } catch (const std::exception& error) {
        reportFailure(error, "step_forward");
    }
}

void PlaybackWorker::stepBackward() {
    pause();
    if (composition_enabled_) {
        current_timeline_frame_ = std::max<std::int64_t>(
            composition_start_frame_, current_timeline_frame_ - 1);
        current_frame_index_ = std::clamp<std::int64_t>(
            current_timeline_frame_ - primary_timeline_start_frame_,
            0,
            std::max<std::int64_t>(0, segment_frame_count_ - 1));
        emitComposedFrame();
        return;
    }
    if (source_path_.empty()) {
        return;
    }

    try {
        if (!session_) session_ = openVideoPlaybackSession(source_path_);
        const auto target_frame = std::max<std::int64_t>(0, current_frame_index_ - 1);
        const auto source_frame = sourceFrameForLocal(target_frame);
        if (!source_frame.has_value()) {
            throw media::MediaError("The requested previous frame is outside the playback segment.");
        }
        auto& metrics = rendering::PreviewPerformanceMetrics::instance();
        metrics.recordSeekOperation();
        std::optional<media::VideoFramePtr> frame;
        {
            rendering::PreviewPerformanceScope timing(
                metrics,
                rendering::PreviewTiming::Seek);
            frame = session_->decode_frame_at(*source_frame);
        }
        consumeDecodeCacheHits(*session_);
        if (frame.has_value()) metrics.recordDecodedFrame();
        emitFrame(std::move(frame));
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
        rendering::PreviewPerformanceMetrics::instance().recordSeekOperation();
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
            if (composition_enabled_) {
                current_frame_index_ = frame_index;
                current_timeline_frame_ = primary_timeline_start_frame_ + frame_index;
                composition_position_initialized_ = true;
                audio_position_valid_ = false;
                composition_audio_cursor_valid_ = false;
                pending_audio_bytes_.clear();
                if (audio_output_ != nullptr) audio_output_->stop();
                emitComposedFrame();
            } else if (!source_path_.empty()) {
                if (!session_) {
                    session_ = openVideoPlaybackSession(source_path_);
                }

                const auto source_frame = sourceFrameForLocal(frame_index);
                if (!source_frame.has_value()) {
                    throw media::MediaError("The requested source frame is outside the media range.");
                }
                auto& metrics = rendering::PreviewPerformanceMetrics::instance();
                std::optional<media::VideoFramePtr> frame;
                {
                    rendering::PreviewPerformanceScope timing(
                        metrics,
                        rendering::PreviewTiming::Seek);
                    frame = session_->decode_frame_at(
                        source_frame.value(),
                        seek_is_current);
                }
                consumeDecodeCacheHits(*session_);
                if (!isSeekCurrent(sequence)) continue;
                if (!frame.has_value()) {
                    throw media::MediaError("The requested frame is outside the media range.");
                }
                metrics.recordDecodedFrame();
                emitFrame(std::move(frame));
                audio_position_valid_ = false;
                pending_audio_bytes_.clear();
                if (audio_output_ != nullptr) audio_output_->stop();
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

    collectTransitionPreroll();

    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    const auto now = Clock::now();
    metrics.recordPlaybackTick();
    if (playback_scheduler_.active() &&
        now > playback_scheduler_.nextDeadline()) {
        metrics.recordTiming(
            rendering::PreviewTiming::PacingLag,
            now - playback_scheduler_.nextDeadline());
    }

    const auto deadline_target_frame = playback_scheduler_.targetFrame(now);

    try {
        if (composition_enabled_) {
            if (composition_end_frame_ <= composition_start_frame_ ||
                current_timeline_frame_ >= composition_end_frame_ - 1) {
                finishPlayback();
                return;
            }

            if (audio_enabled_) {
                try {
                    fillAudioOutput();
                    updateAudioBufferMetric();
                } catch (const media::MediaError& error) {
                    reportAudioFailure(error, "decode");
                } catch (const std::exception& error) {
                    reportAudioFailure(error, "decode");
                }
            }

            const auto scheduler_target_frame = std::min(
                deadline_target_frame, composition_end_frame_ - 1);
            detail::AudioPacingDecision pacing_decision;
            pacing_decision.target_frame = scheduler_target_frame;
            if (audio_enabled_ && audio_output_ != nullptr) {
                const auto elapsed_usecs = std::max<qint64>(
                    0,
                    audio_output_->processedUsecs() - audio_clock_origin_usecs_);
                const auto clock_frame_rate = playbackFrameRate();
                const auto audio_target_frame = detail::timelineFrameFromAudioElapsedUsecs(
                    audio_clock_origin_frame_, elapsed_usecs, clock_frame_rate);
                const auto bounded_audio_target = std::min(
                    audio_target_frame, composition_end_frame_ - 1);
                const auto drift_frames = bounded_audio_target - scheduler_target_frame;
                metrics.recordAudioClockDrift(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::duration<double>(
                            static_cast<double>(drift_frames) / clock_frame_rate)));
                pacing_decision = audio_pacing_policy_.selectTarget(
                    current_timeline_frame_,
                    scheduler_target_frame,
                    bounded_audio_target);
            } else {
                pacing_decision = audio_pacing_policy_.selectTarget(
                    current_timeline_frame_,
                    scheduler_target_frame,
                    scheduler_target_frame);
                metrics.setAudioBufferedUsecs(std::nullopt);
            }

            playback_scheduler_.advanceAfterTarget(scheduler_target_frame);
            const auto target_frame = pacing_decision.target_frame;
            if (target_frame <= current_timeline_frame_) {
                scheduleNextPlaybackTick();
                return;
            }
            recordPacingCatchup(metrics, pacing_decision);
            current_timeline_frame_ = target_frame;
            updateTransitionPreroll();
            current_frame_index_ = std::clamp<std::int64_t>(
                current_timeline_frame_ - primary_timeline_start_frame_,
                0,
                std::max<std::int64_t>(0, segment_frame_count_ - 1));
            emitComposedFrame();
            scheduleNextPlaybackTick();
            return;
        }

        if (!session_ && !composition_enabled_) {
            throw media::MediaError("Playback session is not available.");
        }
        if (audio_enabled_) {
            try {
                fillAudioOutput();
                updateAudioBufferMetric();
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

        const auto scheduler_target_frame = segment_frame_count_ > 0
            ? std::min(deadline_target_frame, segment_frame_count_ - 1)
            : deadline_target_frame;
        detail::AudioPacingDecision pacing_decision;
        pacing_decision.target_frame = scheduler_target_frame;
        if (audio_enabled_ && audio_output_ != nullptr) {
            const auto elapsed_usecs = std::max<qint64>(
                0,
                audio_output_->processedUsecs() - audio_clock_origin_usecs_);
            const auto clock_frame_rate = playbackFrameRate();
            const auto audio_target_frame = detail::timelineFrameFromAudioElapsedUsecs(
                audio_clock_origin_frame_, elapsed_usecs, clock_frame_rate);
            const auto bounded_audio_target = segment_frame_count_ > 0
                ? std::min(audio_target_frame, segment_frame_count_ - 1)
                : audio_target_frame;
            const auto drift_frames = bounded_audio_target -
                scheduler_target_frame;
            metrics.recordAudioClockDrift(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::duration<double>(
                        static_cast<double>(drift_frames) / clock_frame_rate)));
            pacing_decision = audio_pacing_policy_.selectTarget(
                current_frame_index_,
                scheduler_target_frame,
                bounded_audio_target);
        } else {
            pacing_decision = audio_pacing_policy_.selectTarget(
                current_frame_index_,
                scheduler_target_frame,
                scheduler_target_frame);
            metrics.setAudioBufferedUsecs(std::nullopt);
        }
        const auto target_frame = pacing_decision.target_frame;
        playback_scheduler_.advanceAfterTarget(scheduler_target_frame);
        if (target_frame <= current_frame_index_) {
            scheduleNextPlaybackTick();
            return;
        }

        if (composition_enabled_) {
            recordPacingCatchup(metrics, pacing_decision);
            current_frame_index_ = target_frame;
            emitComposedFrame();
            scheduleNextPlaybackTick();
            return;
        }

        const auto source_target_frame = sourceFrameForLocal(target_frame);
        if (!source_target_frame.has_value()) {
            throw media::MediaError("The playback target frame is outside the media range.");
        }
        const auto seek_sequence = pending_seek_sequence_.load(std::memory_order_acquire);
        const auto should_cancel = [this, seek_sequence]() {
            return !isSeekCurrent(seek_sequence);
        };

        std::optional<media::VideoFramePtr> frame;
        {
            rendering::PreviewPerformanceScope timing(
                metrics,
                rendering::PreviewTiming::Decode);
            frame = session_->decode_forward_to(*source_target_frame, should_cancel);
        }
        if (!frame.has_value() && !should_cancel() && !session_->at_end()) {
            rendering::PreviewPerformanceScope timing(
                metrics,
                rendering::PreviewTiming::Decode);
            frame = session_->decode_frame_at(*source_target_frame, should_cancel);
        }
        if (!should_cancel()) {
            consumeDecodeCacheHits(*session_);
            if (!frame.has_value() ||
                !isSourceFrameInRange(session_->current_frame_index())) {
                finishPlayback();
            } else {
                metrics.recordDecodedFrame();
                recordPacingCatchup(metrics, pacing_decision);
                emitFrame(std::move(frame));
            }
        }
    } catch (const media::MediaError& error) {
        reportFailure(error, "decode_tick");
    } catch (const std::exception& error) {
        reportFailure(error, "decode_tick");
    }

    if (playing_) scheduleNextPlaybackTick();
}

bool PlaybackWorker::isSeekCurrent(quint64 sequence) const noexcept {
    return pending_seek_sequence_.load(std::memory_order_acquire) == sequence;
}

bool PlaybackWorker::ensureSessionAtCurrentFrame() {
    const auto source_frame = sourceFrameForLocal(current_frame_index_);
    if (!source_frame.has_value()) return false;
    if (session_->current_frame_index() == *source_frame) return true;

    const auto frame = session_->decode_frame_at(*source_frame);
    consumeDecodeCacheHits(*session_);
    return frame.has_value() &&
        session_->current_frame_index() == *source_frame;
}

void PlaybackWorker::configureAudio() {
    composition_audio_configured_ = false;
    audio_enabled_ = false;
    audio_pacing_policy_.reset();
    rendering::PreviewPerformanceMetrics::instance().setAudioEnabled(false);
    audio_output_ = std::make_unique<AudioOutput>();
    audio_output_->setVolume(monitor_volume_gain_);

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
        audio_session_->seek_to_source_frame(*source_frame, source_frame_rate_);
        audio_position_valid_ = true;
        audio_enabled_ = true;
        rendering::PreviewPerformanceMetrics::instance().setAudioEnabled(true);
    } catch (const media::MediaError& error) {
        reportAudioFailure(error, "open");
    } catch (const std::exception& error) {
        reportAudioFailure(error, "open");
    }
}

void PlaybackWorker::fillAudioOutput() {
    if (!audio_enabled_ || audio_output_ == nullptr) return;
    if (composition_enabled_) {
        fillCompositionAudioOutput();
        return;
    }
    if (audio_session_ == nullptr) return;

    const auto effective_gain =
        (track_audio_muted_ || clip_audio_muted_)
            ? 0.0
            : track_audio_gain_ * clip_audio_gain_ *
                AudioOutput::sampleBoost(monitor_volume_gain_);
    const auto output = audio_session_->output_spec();
    const auto target_bytes = static_cast<std::size_t>(
        output.sample_rate * output.channel_count * 2 / 5);
    std::int64_t segment_end_sample = std::numeric_limits<std::int64_t>::max();
    if (segment_frame_count_ > 0 && source_frame_rate_ > 0.0) {
        const auto end_sample = detail::audioSegmentEndSample(
            source_start_frame_,
            source_frame_rate_,
            segment_frame_count_,
            playbackFrameRate(),
            output.sample_rate);
        if (end_sample.has_value()) segment_end_sample = *end_sample;
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

void PlaybackWorker::configureCompositionAudio() {
    if (composition_audio_configured_) return;
    composition_audio_configured_ = true;
    audio_enabled_ = false;
    audio_pacing_policy_.reset();
    audio_session_.reset();
    rendering::PreviewPerformanceMetrics::instance().setAudioEnabled(false);

    const bool has_timeline_audio = std::any_of(
        composition_audio_mix_clips_.begin(),
        composition_audio_mix_clips_.end(),
        [](const media::TimelineAudioMixClip& clip) {
            return clip.kind == timeline::ClipKind::Video && clip.has_audio;
        });
    if (!has_timeline_audio) return;

    audio_output_ = std::make_unique<AudioOutput>();
    audio_output_->setVolume(monitor_volume_gain_);
    QString error_message;
    qint64 error_code = 0;
    if (!audio_output_->initialize(&error_message, &error_code)) {
        if (!audio_output_->disabledByEnvironment()) {
            reportAudioFailure(
                std::runtime_error(error_message.isEmpty()
                    ? "The Timeline audio output could not be initialized."
                    : error_message.toUtf8().toStdString()),
                "composition_output",
                error_code);
        }
        return;
    }

    audio_enabled_ = true;
    composition_audio_cursor_valid_ = false;
    rendering::PreviewPerformanceMetrics::instance().setAudioEnabled(true);
}

void PlaybackWorker::restartCompositionAudioOutput() {
    if (!composition_enabled_ || !audio_enabled_ || audio_output_ == nullptr) return;
    const auto sample_position = static_cast<long double>(current_timeline_frame_) *
        audio_output_->sampleRate() / timeline_frame_rate_.asDouble();
    if (current_timeline_frame_ < 0 || !std::isfinite(sample_position) ||
        sample_position > static_cast<long double>(
            std::numeric_limits<std::int64_t>::max())) {
        reportAudioFailure(
            std::runtime_error("The Timeline audio position is invalid."),
            "composition_seek");
        return;
    }
    next_composition_audio_sample_ = static_cast<std::int64_t>(
        std::llround(sample_position));
    composition_audio_cursor_valid_ = true;
    audio_clock_origin_frame_ = current_timeline_frame_;

    QString error_message;
    qint64 error_code = 0;
    if (!audio_output_->start(&error_message, &error_code)) {
        reportAudioFailure(
            std::runtime_error(error_message.isEmpty()
                ? "The Timeline audio output could not be started."
                : error_message.toUtf8().toStdString()),
            "composition_output",
            error_code);
        return;
    }
    audio_clock_origin_usecs_ = audio_output_->processedUsecs();
    try {
        fillCompositionAudioOutput();
        updateAudioBufferMetric();
    } catch (const media::MediaError& error) {
        reportAudioFailure(error, "composition_decode");
    } catch (const std::exception& error) {
        reportAudioFailure(error, "composition_decode");
    }
}

void PlaybackWorker::fillCompositionAudioOutput() {
    if (!audio_enabled_ || audio_output_ == nullptr) return;
    const auto output = media::AudioPlaybackSession::OutputSpec{
        audio_output_->sampleRate(), audio_output_->channelCount()};
    if (output.sample_rate <= 0 || output.channel_count <= 0) return;

    const auto bytes_per_sample_frame = output.channel_count *
        audio_output_->bytesPerSample();
    const auto target_bytes = static_cast<std::size_t>(
        output.sample_rate * bytes_per_sample_frame * 2 / 5);
    constexpr std::int64_t mix_block_samples = 4096;
    const auto monitor_gain = AudioOutput::sampleBoost(monitor_volume_gain_);

    if (!composition_audio_cursor_valid_) {
        const auto sample_position = static_cast<long double>(current_timeline_frame_) *
            output.sample_rate / timeline_frame_rate_.asDouble();
        if (current_timeline_frame_ < 0 || !std::isfinite(sample_position) ||
            sample_position > static_cast<long double>(
                std::numeric_limits<std::int64_t>::max())) {
            throw media::MediaError("The Timeline audio position is invalid.");
        }
        next_composition_audio_sample_ = static_cast<std::int64_t>(
            std::llround(sample_position));
        composition_audio_cursor_valid_ = true;
    }

    while (pending_audio_bytes_.size() < static_cast<qsizetype>(target_bytes)) {
        const auto block_samples = mix_block_samples;
        // Audio sessions are per clip occurrence. Release decoders once the
        // generated Timeline buffer has passed that occurrence so long
        // projects do not keep every visited audio decoder open. A backward
        // seek can reopen the session on demand.
        for (const auto& clip : composition_audio_mix_clips_) {
            if (clip.source_index >= composition_sessions_.size() ||
                clip.kind != timeline::ClipKind::Video || !clip.has_audio ||
                clip.duration_frames <= 0 || clip.timeline_start_frame < 0 ||
                clip.timeline_start_frame >
                    std::numeric_limits<std::int64_t>::max() - clip.duration_frames) {
                continue;
            }
            const auto end_frame = clip.timeline_start_frame + clip.duration_frames;
            const auto end_sample = std::round(
                static_cast<long double>(end_frame) * output.sample_rate /
                timeline_frame_rate_.asDouble());
            if (!std::isfinite(end_sample) ||
                end_sample > static_cast<long double>(
                    std::numeric_limits<std::int64_t>::max()) ||
                end_sample > next_composition_audio_sample_) {
                continue;
            }
            auto& source = composition_sessions_[clip.source_index];
            source.audio_session.reset();
            if (!source.audio_open_failed) source.audio_open_attempted = false;
        }

        std::vector<float> mixed(
            static_cast<std::size_t>(block_samples) *
                static_cast<std::size_t>(output.channel_count),
            0.0F);
        const auto spans = media::planTimelineAudioMix(
            composition_audio_mix_clips_,
            composition_audio_mix_transitions_,
            timeline_frame_rate_.asDouble(),
            output.sample_rate,
            next_composition_audio_sample_,
            block_samples);

        for (const auto& span : spans) {
            if (span.source_index >= composition_sessions_.size()) continue;
            auto& source = composition_sessions_[span.source_index];
            if (source.audio_open_failed) continue;
            if (!source.audio_open_attempted) {
                source.audio_open_attempted = true;
                try {
                    source.audio_session = media::AudioPlaybackSession::open(
                        QFileInfo(source.spec.source_path).filesystemFilePath(), output);
                    if (!source.audio_session->has_audio()) {
                        source.audio_session.reset();
                    }
                } catch (const media::MediaError& error) {
                    source.audio_open_failed = true;
                    reportCompositionAudioFailure(
                        source.spec, error, "composition_source_open",
                        error.error_code().value_or(-1));
                    continue;
                } catch (const std::exception& error) {
                    source.audio_open_failed = true;
                    reportCompositionAudioFailure(
                        source.spec, error, "composition_source_open");
                    continue;
                }
            }
            if (source.audio_session == nullptr) continue;

            try {
                if (source.audio_session->current_sample_index() !=
                    span.source_start_sample) {
                    source.audio_session->seek_to_sample_index(
                        span.source_start_sample);
                }
                auto source_cursor = span.source_start_sample;
                const auto source_end = span.source_start_sample + span.sample_count;
                while (source_cursor < source_end) {
                    const auto remaining = static_cast<std::size_t>(
                        source_end - source_cursor);
                    auto chunk = source.audio_session->decode_samples(
                        std::min<std::size_t>(remaining, 8192U));
                    if (!chunk.has_value() || chunk->sampleCount() == 0) break;
                    media::accumulateTimelineAudioChunk(
                        span, *chunk, mixed, output.channel_count);
                    const auto chunk_end = chunk->first_sample_index +
                        static_cast<std::int64_t>(chunk->sampleCount());
                    const auto consumed_end = std::min(source_end, chunk_end);
                    if (consumed_end <= source_cursor) break;
                    source_cursor = consumed_end;
                }
            } catch (const media::MediaError& error) {
                source.audio_session.reset();
                source.audio_open_failed = true;
                reportCompositionAudioFailure(
                    source.spec, error, "composition_source_decode",
                    error.error_code().value_or(-1));
            } catch (const std::exception& error) {
                source.audio_session.reset();
                source.audio_open_failed = true;
                reportCompositionAudioFailure(
                    source.spec, error, "composition_source_decode");
            }
        }

        std::vector<std::int16_t> pcm(
            static_cast<std::size_t>(block_samples) *
                static_cast<std::size_t>(output.channel_count));
        for (std::size_t index = 0; index < mixed.size(); ++index) {
            const auto value = static_cast<double>(mixed[index]) * monitor_gain * 32768.0;
            pcm[index] = static_cast<std::int16_t>(std::clamp(
                value,
                static_cast<double>(std::numeric_limits<std::int16_t>::min()),
                static_cast<double>(std::numeric_limits<std::int16_t>::max())));
        }
        pending_audio_bytes_.append(
            reinterpret_cast<const char*>(pcm.data()),
            static_cast<qsizetype>(pcm.size() * sizeof(std::int16_t)));
        if (next_composition_audio_sample_ >
            std::numeric_limits<std::int64_t>::max() - block_samples) {
            throw media::MediaError("The Timeline audio position overflowed.");
        }
        next_composition_audio_sample_ += block_samples;
    }

    while (!pending_audio_bytes_.isEmpty() && audio_output_->bytesFree() > 0) {
        const auto writable = std::min<qint64>(
            audio_output_->bytesFree(), pending_audio_bytes_.size());
        const auto written = audio_output_->write(
            pending_audio_bytes_.left(static_cast<qsizetype>(writable)));
        if (written <= 0) break;
        pending_audio_bytes_.remove(0, static_cast<qsizetype>(written));
    }
}

void PlaybackWorker::reportCompositionAudioFailure(
    const CompositionLayerSpec& spec,
    const std::exception& error,
    const char* operation,
    qint64 error_code) {
    try {
        logging::Context context{
            {"path", safePathForLog(
                QFileInfo(spec.source_path).filesystemFilePath())},
            {"track_id", std::to_string(spec.track_id)},
            {"clip_id", std::to_string(spec.clip_id)},
            {"track_index", std::to_string(spec.track_index)},
            {"clip_index", std::to_string(spec.clip_index)},
            {"timeline_frame", std::to_string(current_timeline_frame_)}};
        if (error_code >= 0) {
            context.emplace_back("error_code", std::to_string(error_code));
        }
        logging::Logger::instance().log(
            logging::Level::Error,
            "audio",
            operation,
            error.what(),
            context);
    } catch (...) {
    }
}

void PlaybackWorker::updateAudioBufferMetric() noexcept {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    if (!audio_enabled_ || audio_output_ == nullptr) {
        metrics.setAudioBufferedUsecs(std::nullopt);
        return;
    }

    const auto buffered_usecs = audio_output_->bufferedUsecs();
    if (buffered_usecs.has_value() && *buffered_usecs >= 0) {
        metrics.setAudioBufferedUsecs(
            static_cast<std::uint64_t>(*buffered_usecs));
    } else {
        metrics.setAudioBufferedUsecs(std::nullopt);
    }
}

void PlaybackWorker::disableAudioOutput() noexcept {
    audio_enabled_ = false;
    audio_pacing_policy_.reset();
    rendering::PreviewPerformanceMetrics::instance().setAudioEnabled(false);
    audio_position_valid_ = false;
    composition_audio_cursor_valid_ = false;
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
                {"clip_local_frame", std::to_string(current_frame_index_)},
                {"source_start_frame", std::to_string(source_start_frame_)},
                {"segment_frame_count", std::to_string(segment_frame_count_)},
                {"track_index", std::to_string(track_index_)},
                {"clip_index", std::to_string(clip_index_)},
                {"track_audio_gain", std::to_string(track_audio_gain_)},
                {"clip_audio_gain", std::to_string(clip_audio_gain_)}};
            if (const auto timeline_frame = timelineFrameForDiagnostics();
                timeline_frame.has_value()) {
                appendTimelinePositionContext(
                    context, *timeline_frame, timeline_frame_rate_);
            }
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

std::optional<std::int64_t> PlaybackWorker::timelineFrameForDiagnostics(
    std::optional<std::int64_t> requested_clip_local_frame) const noexcept {
    if (track_index_ < 0 || clip_index_ < 0) return std::nullopt;
    if (composition_enabled_) {
        return current_timeline_frame_ >= 0
            ? std::optional<std::int64_t>(current_timeline_frame_)
            : std::nullopt;
    }
    const auto local_frame = requested_clip_local_frame.value_or(current_frame_index_);
    if (local_frame < 0 || primary_timeline_start_frame_ < 0 ||
        primary_timeline_start_frame_ >
            std::numeric_limits<std::int64_t>::max() - local_frame) {
        return std::nullopt;
    }
    return primary_timeline_start_frame_ + local_frame;
}

void PlaybackWorker::ensureTimer() {
    if (timer_ != nullptr) return;

    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setTimerType(Qt::PreciseTimer);
    connect(timer_, &QTimer::timeout, this, &PlaybackWorker::decodeTick);
}

void PlaybackWorker::scheduleNextPlaybackTick() {
    if (!playing_ || timer_ == nullptr || !playback_scheduler_.active()) return;

    const auto delay = playback_scheduler_.delayUntil(Clock::now());
    timer_->start(static_cast<int>(delay.count()));
}

void PlaybackWorker::cancelTransitionPreroll() noexcept {
    if (transition_preroll_state_ != nullptr) {
        transition_preroll_state_->cancel_requested.store(
            true, std::memory_order_release);
    }
}

void PlaybackWorker::collectTransitionPreroll() {
    const auto state = transition_preroll_state_;
    if (state == nullptr ||
        !state->finished.load(std::memory_order_acquire)) {
        return;
    }

    if (transition_preroll_thread_.joinable()) {
        transition_preroll_thread_.join();
    }

    std::optional<TransitionPrerollResult> result;
    {
        std::lock_guard lock(state->result_mutex);
        result = std::move(state->result);
    }
    transition_preroll_state_.reset();
    if (!result.has_value()) return;
    if (result->cancelled ||
        state->cancel_requested.load(std::memory_order_acquire)) {
        if (result->composition_revision == composition_revision_) {
            last_transition_preroll_session_index_ =
                std::numeric_limits<std::size_t>::max();
            last_transition_preroll_start_frame_ = -1;
        }
        return;
    }

    const bool composition_matches = composition_enabled_ &&
        result->composition_revision == composition_revision_ &&
        current_timeline_frame_ < result->transition_start_frame &&
        result->session_index < composition_sessions_.size();
    if (!composition_matches) {
        if (result->composition_revision == composition_revision_ &&
            current_timeline_frame_ >= result->transition_start_frame) {
            last_transition_preroll_session_index_ =
                std::numeric_limits<std::size_t>::max();
            last_transition_preroll_start_frame_ = -1;
        }
        return;
    }

    auto& composition = composition_sessions_[result->session_index];
    std::filesystem::path current_source_path;
    try {
        current_source_path = composition.spec.source_path.isEmpty()
            ? std::filesystem::path{}
            : QFileInfo(composition.spec.source_path).filesystemFilePath();
    } catch (const std::exception& error) {
        try {
            logging::Context context{
                {"path", safePathForLog(result->source_path)},
                {"track_index", std::to_string(result->track_index)},
                {"clip_index", std::to_string(result->clip_index)},
                {"transition_start_frame",
                 std::to_string(result->transition_start_frame)},
                {"source_frame", std::to_string(result->source_frame)}};
            logging::Logger::instance().log(
                logging::Level::Warning,
                "playback",
                "transition_preroll_endpoint_check",
                error.what(),
                context);
        } catch (...) {
        }
        return;
    }
    const bool endpoint_matches =
        composition.spec.kind == timeline::ClipKind::Video &&
        composition.spec.track_index == result->track_index &&
        composition.spec.clip_index == result->clip_index &&
        composition.spec.source_start_frame == result->source_frame &&
        current_source_path == result->source_path;
    if (!endpoint_matches) return;

    if (result->session == nullptr ||
        result->session->current_frame_index() != result->source_frame) {
        if (result->error_message.empty()) {
            result->error_message =
                "The incoming transition frame was not prepared by its decoder.";
        }
    } else {
        composition.session = std::move(result->session);
        return;
    }

    try {
        logging::Context context{
            {"path", safePathForLog(result->source_path)},
            {"track_index", std::to_string(result->track_index)},
            {"clip_index", std::to_string(result->clip_index)},
            {"transition_start_frame",
             std::to_string(result->transition_start_frame)},
            {"source_frame", std::to_string(result->source_frame)},
            {"composition_revision",
             std::to_string(result->composition_revision)}};
        if (current_timeline_frame_ >= 0) {
            appendTimelinePositionContext(
                context, current_timeline_frame_, timeline_frame_rate_);
        }
        if (result->error_code >= 0) {
            context.emplace_back("error_code", std::to_string(result->error_code));
        }
        logging::Logger::instance().log(
            logging::Level::Warning,
            "playback",
            "transition_preroll_fallback",
            result->error_message.empty()
                ? "The incoming Cross Dissolve decoder was not prepared in time."
                : result->error_message,
            context);
    } catch (...) {
    }
}

void PlaybackWorker::updateTransitionPreroll() {
    collectTransitionPreroll();
    if (!playing_ || !composition_enabled_ || current_timeline_frame_ < 0) {
        return;
    }

    if (transition_preroll_state_ != nullptr) {
        if (transition_preroll_state_->finished.load(std::memory_order_acquire)) {
            collectTransitionPreroll();
        } else {
            if (transition_preroll_state_->composition_revision !=
                    composition_revision_ ||
                current_timeline_frame_ >=
                    transition_preroll_state_->transition_start_frame) {
                // Once playback reaches the dissolve, avoid competing with the
                // normal decoder path. It remains the correctness-preserving fallback.
                transition_preroll_state_->cancel_requested.store(
                    true, std::memory_order_release);
            }
            return;
        }
    }
    if (transition_preroll_state_ != nullptr) return;
    if (transition_preroll_thread_.joinable()) {
        transition_preroll_thread_.join();
    }

    std::int64_t lookahead_frames = 1;
    if (timeline::validFrameRate(timeline_frame_rate_)) {
        const auto numerator = timeline_frame_rate_.numerator;
        const auto denominator = timeline_frame_rate_.denominator;
        lookahead_frames = std::max<std::int64_t>(
            1,
            numerator / denominator + (numerator % denominator == 0 ? 0 : 1));
    }

    std::vector<detail::CompositionSessionRef> sessions;
    sessions.reserve(composition_sessions_.size());
    for (std::size_t index = 0; index < composition_sessions_.size(); ++index) {
        sessions.push_back(detail::CompositionSessionRef{
            &composition_sessions_[index].spec, index});
    }
    const auto target = detail::nextTransitionPrerollTarget(
        sessions,
        std::span<const CompositionTransitionSpec>(
            composition_transitions_.constData(),
            static_cast<std::size_t>(composition_transitions_.size())),
        current_timeline_frame_,
        lookahead_frames);
    if (!target.has_value() || target->session_index >= composition_sessions_.size()) {
        return;
    }
    if (last_transition_preroll_session_index_ == target->session_index &&
        last_transition_preroll_start_frame_ == target->transition_start_frame) {
        return;
    }

    const auto& incoming = composition_sessions_[target->session_index];
    if (incoming.session != nullptr &&
        incoming.session->current_frame_index() == target->source_frame) {
        return;
    }

    TransitionPrerollResult request;
    request.composition_revision = composition_revision_;
    request.session_index = target->session_index;
    request.track_index = incoming.spec.track_index;
    request.clip_index = incoming.spec.clip_index;
    request.transition_start_frame = target->transition_start_frame;
    request.source_frame = target->source_frame;
    try {
        request.source_path = QFileInfo(incoming.spec.source_path).filesystemFilePath();
    } catch (const std::exception& error) {
        last_transition_preroll_session_index_ = target->session_index;
        last_transition_preroll_start_frame_ = target->transition_start_frame;
        try {
            logging::Logger::instance().log(
                logging::Level::Warning,
                "playback",
                "transition_preroll_path",
                error.what(),
                {{"track_index", std::to_string(incoming.spec.track_index)},
                 {"clip_index", std::to_string(incoming.spec.clip_index)},
                 {"transition_start_frame",
                  std::to_string(target->transition_start_frame)},
                 {"source_frame", std::to_string(target->source_frame)}});
        } catch (...) {
        }
        return;
    }

    auto state = std::make_shared<TransitionPrerollState>();
    state->composition_revision = composition_revision_;
    state->transition_start_frame = target->transition_start_frame;
    transition_preroll_state_ = state;
    last_transition_preroll_session_index_ = target->session_index;
    last_transition_preroll_start_frame_ = target->transition_start_frame;
    try {
        transition_preroll_thread_ = std::thread(
            [state, request = std::move(request)]() mutable {
                auto result = std::move(request);
                try {
                    result.session = openVideoPlaybackSession(result.source_path);
                    const auto should_cancel = [state]() {
                        return state->cancel_requested.load(
                            std::memory_order_acquire);
                    };
                    const auto prepared = result.session->decode_frame_at(
                        result.source_frame, should_cancel);
                    if (should_cancel()) {
                        result.cancelled = true;
                        result.session.reset();
                    } else if (!prepared.has_value() || *prepared == nullptr) {
                        result.error_message =
                            "The incoming Cross Dissolve source frame could not be decoded.";
                        result.session.reset();
                    }
                } catch (const media::MediaError& error) {
                    result.error_message = error.what();
                    result.error_code = error.error_code().value_or(-1);
                    result.session.reset();
                } catch (const std::exception& error) {
                    result.error_message = error.what();
                    result.session.reset();
                } catch (...) {
                    result.error_message =
                        "An unknown error occurred while preparing a Cross Dissolve.";
                    result.session.reset();
                }

                if (state->cancel_requested.load(std::memory_order_acquire)) {
                    result.cancelled = true;
                    result.session.reset();
                }

                {
                    std::lock_guard lock(state->result_mutex);
                    state->result = std::move(result);
                }
                state->finished.store(true, std::memory_order_release);
            });
    } catch (const std::system_error& error) {
        transition_preroll_state_.reset();
        try {
            logging::Logger::instance().log(
                logging::Level::Warning,
                "playback",
                "transition_preroll_start",
                error.what(),
                {{"track_index", std::to_string(incoming.spec.track_index)},
                 {"clip_index", std::to_string(incoming.spec.clip_index)},
                 {"transition_start_frame",
                  std::to_string(target->transition_start_frame)},
                 {"source_frame", std::to_string(target->source_frame)}});
        } catch (...) {
        }
    }
}

void PlaybackWorker::finishPlayback() {
    if (timer_ != nullptr) timer_->stop();
    cancelTransitionPreroll();
    last_transition_preroll_session_index_ =
        std::numeric_limits<std::size_t>::max();
    last_transition_preroll_start_frame_ = -1;
    if (audio_output_ != nullptr) audio_output_->stop();
    resetPlaybackClock();
    rendering::PreviewPerformanceMetrics::instance().setPlaybackActive(false);
    audio_position_valid_ = false;
    pending_audio_bytes_.clear();
    const bool was_playing = playing_;
    playing_ = false;
    if (was_playing) emit playbackStateChanged(false, generation_);
    emit playbackFinished(generation_, was_playing);
}

void PlaybackWorker::emitFrame(std::optional<media::VideoFramePtr> frame) {
    if (!frame.has_value() || *frame == nullptr) {
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
        current_timeline_frame_ = primary_timeline_start_frame_ + current_frame_index_;
        composition_position_initialized_ = true;
        emitComposedFrame();
        return;
    }
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.recordEmittedFrame();
    const auto timeline_frame = timelineFrameForDiagnostics(current_frame_index_);
    const auto trace_id = playing_
        ? metrics.createFrameDeliveryTrace(
            generation_, timeline_frame.value_or(-1))
        : 0U;
    emit frameReady(
        std::move(*frame), current_frame_index_, generation_, trace_id);
}

void PlaybackWorker::emitComposedFrame() {
    current_frame_index_ = std::clamp<std::int64_t>(
        current_timeline_frame_ - primary_timeline_start_frame_,
        0,
        std::max<std::int64_t>(0, segment_frame_count_ - 1));
    renderCompositionFrame(
        current_timeline_frame_,
        current_frame_index_,
        generation_);
}

std::optional<std::vector<PlaybackWorker::DecodedCompositionLayer>>
PlaybackWorker::decodeCompositionLayers(
    std::int64_t global_frame,
    const media::VideoPlaybackSession::CancellationPredicate& should_cancel) {
    std::vector<DecodedCompositionLayer> layers;
    layers.reserve(composition_sessions_.size());

    std::vector<detail::CompositionSessionRef> ordered_sessions;
    ordered_sessions.reserve(composition_sessions_.size());
    for (std::size_t index = 0; index < composition_sessions_.size(); ++index) {
        ordered_sessions.push_back(detail::CompositionSessionRef{
            &composition_sessions_[index].spec, index});
    }
    std::sort(
        ordered_sessions.begin(),
        ordered_sessions.end(),
        [](const detail::CompositionSessionRef& left,
           const detail::CompositionSessionRef& right) {
            if (left.spec->track_index != right.spec->track_index) {
                return left.spec->track_index > right.spec->track_index;
            }
            if (left.spec->kind != right.spec->kind) {
                return timeline::isMediaClipKind(left.spec->kind);
            }
            return left.spec->clip_index < right.spec->clip_index;
        });

    std::vector<detail::CompositionFrameRequest> requests;
    requests.reserve(ordered_sessions.size() + composition_transitions_.size());
    for (const auto& session : ordered_sessions) {
        const auto& spec = *session.spec;
        if (global_frame < spec.timeline_start_frame ||
            global_frame >= spec.timeline_start_frame + spec.segment_frame_count) {
            continue;
        }
        requests.push_back(detail::CompositionFrameRequest{
            session.session_index,
            global_frame - spec.timeline_start_frame,
            1.0});
    }

    detail::applyTransitionRequests(
        requests,
        ordered_sessions,
        std::span<const CompositionTransitionSpec>(
            composition_transitions_.constData(),
            static_cast<std::size_t>(composition_transitions_.size())),
        global_frame);

    std::sort(
        requests.begin(),
        requests.end(),
        [this](const detail::CompositionFrameRequest& left,
               const detail::CompositionFrameRequest& right) {
            const auto& left_spec = composition_sessions_[left.session_index].spec;
            const auto& right_spec = composition_sessions_[right.session_index].spec;
            if (left_spec.track_index != right_spec.track_index) {
                return left_spec.track_index > right_spec.track_index;
            }
            if (left_spec.kind != right_spec.kind) {
                return timeline::isMediaClipKind(left_spec.kind);
            }
            return left_spec.clip_index < right_spec.clip_index;
        });

    for (const auto& request : requests) {
        auto& composition = composition_sessions_[request.session_index];
        const auto& spec = composition.spec;
        if (request.local_frame < 0 ||
            request.local_frame >= spec.segment_frame_count) {
            continue;
        }
        const auto source_offset = timeline::sourceFrameOffsetForTimelineFrame(
            request.local_frame, spec.frame_rate, spec.timeline_frame_rate,
            spec.source_duration_frames);
        if (!source_offset.has_value() || spec.source_start_frame < 0 ||
            *source_offset > std::numeric_limits<std::int64_t>::max() -
                spec.source_start_frame) {
            continue;
        }
        const auto source_frame = spec.source_start_frame + *source_offset;
        std::shared_ptr<const media::VideoFrame> frame;
        auto& metrics = rendering::PreviewPerformanceMetrics::instance();
        const bool collect_layer_timing = playing_ && metrics.isEnabled();
        const auto layer_started = collect_layer_timing
            ? Clock::now()
            : Clock::time_point{};
        auto decode_path = rendering::SlowFrameDecodePath::None;
        media::ForwardDecodeDiagnostics forward_decode_diagnostics;
        if (should_cancel && should_cancel()) return std::nullopt;
        if (spec.kind == timeline::ClipKind::Text) {
            if (composition.cached_text_frame != nullptr) {
                frame = composition.cached_text_frame;
                decode_path = rendering::SlowFrameDecodePath::TextCache;
                metrics.recordTextCacheHit();
            } else {
                std::optional<media::VideoFrame> rendered;
                {
                    rendering::PreviewPerformanceScope timing(
                        metrics,
                        rendering::PreviewTiming::TextRasterization);
                    rendered = rendering::renderText(spec.text);
                }
                if (!rendered.has_value()) {
                    throw media::MediaError("The text layer could not be rasterized.");
                }
                composition.cached_text_frame =
                    std::make_shared<const media::VideoFrame>(std::move(*rendered));
                composition.cached_text_alpha_coverage =
                    rendering::FrameCompositor::buildAlphaCoverage(
                        *composition.cached_text_frame);
                frame = composition.cached_text_frame;
                decode_path = rendering::SlowFrameDecodePath::TextRasterization;
            }
        } else if (spec.kind == timeline::ClipKind::Image) {
            frame = composition.static_frame;
            decode_path = rendering::SlowFrameDecodePath::StaticFrame;
        } else if (composition.session != nullptr) {
            std::optional<media::VideoFramePtr> decoded;
            bool tried_forward_decode = false;
            bool tried_frame_at_decode = false;
            const auto current_source_frame =
                composition.session->current_frame_index();
            if (playing_ && request.allow_forward_decode &&
                detail::shouldUseSequentialDecode(current_source_frame, source_frame)) {
                tried_forward_decode = true;
                decoded = composition.session->decode_forward_to(
                    source_frame,
                    should_cancel,
                    collect_layer_timing ? &forward_decode_diagnostics : nullptr);
            }
            if (!decoded.has_value() && !composition.session->at_end() &&
                !(should_cancel && should_cancel())) {
                tried_frame_at_decode = true;
                decoded = composition.session->decode_frame_at(
                    source_frame,
                    should_cancel);
            }
            if (tried_forward_decode && tried_frame_at_decode) {
                decode_path = rendering::SlowFrameDecodePath::ForwardFallbackFrameAt;
            } else if (tried_forward_decode) {
                decode_path = rendering::SlowFrameDecodePath::Forward;
            } else if (tried_frame_at_decode) {
                decode_path = rendering::SlowFrameDecodePath::FrameAt;
            }
            if (should_cancel && should_cancel()) return std::nullopt;
            consumeDecodeCacheHits(*composition.session);
            if (decoded.has_value()) {
                metrics.recordDecodedFrame();
                frame = *decoded;
            }
        }
        if (frame == nullptr) continue;
        auto transform = timeline::evaluateTransform(
            spec.transform,
            spec.keyframes,
            request.local_frame);
        transform.opacity *= request.opacity_multiplier;
        const auto layer_decode_nanoseconds = collect_layer_timing
            ? std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - layer_started).count()
            : 0;
        layers.push_back(DecodedCompositionLayer{
            std::move(frame),
            transform,
            spec.kind == timeline::ClipKind::Text
                ? composition.cached_text_alpha_coverage
                : rendering::AlphaCoveragePtr{},
            spec.track_id,
            spec.clip_id,
            spec.track_index,
            spec.clip_index,
            source_frame,
            spec.kind,
            decode_path,
            layer_decode_nanoseconds <= 0
                ? 0U
                : static_cast<std::uint64_t>(layer_decode_nanoseconds),
            forward_decode_diagnostics,
            request.session_index});
    }
    return layers;
}

std::optional<media::VideoFrame> PlaybackWorker::composeCompositionLayers(
    const std::vector<DecodedCompositionLayer>& decoded_layers,
    const media::VideoPlaybackSession::CancellationPredicate& should_cancel,
    rendering::FrameCompositionTimings* timings) {
    using namespace creative_suite::composition;
    last_composition_gpu_ = false;
    last_composition_cancelled_ = false;
    if (timings) *timings = {};
    auto adapter_started = timings != nullptr ? Clock::now() : Clock::time_point{};
    int width = 1920;
    int height = 1080;
    switch (preview_quality_) {
    case PreviewQuality::Half:
        width /= 2;
        height /= 2;
        break;
    case PreviewQuality::Quarter:
        width /= 4;
        height /= 4;
        break;
    case PreviewQuality::Full:
        break;
    }

    std::vector<rendering::CompositionLayer> layers;
    layers.reserve(decoded_layers.size());
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    for (const auto& decoded : decoded_layers) {
        rendering::CompositionLayer layer{
            decoded.frame.get(),
            decoded.transform,
            decoded.alpha_coverage};
        layers.push_back(std::move(layer));
    }
    if (gpu_composition_enabled_ && !gpu_composition_failed_) {
        OpenGlCompositionTimings gpu_timings;
        OpenGlCompositionResult gpu_result;
        try {
            if (gpu_compose_) {
                gpu_result = gpu_compose_(width, height, layers, should_cancel, &gpu_timings);
            } else {
                if (!gpu_compositor_)
                    gpu_compositor_ = std::make_unique<OpenGlFrameCompositor>(gpu_surface_);
                gpu_result = gpu_compositor_->compose(width, height, layers, should_cancel, &gpu_timings);
            }
        } catch (const std::exception& error) {
            gpu_result = {OpenGlCompositionStatus::Failed, {}, "compose", error.what(), -1};
        }
        if (should_cancel() || gpu_result.status == OpenGlCompositionStatus::Cancelled) {
            metrics.recordGpuCompositionWork(gpu_timings);
            last_composition_cancelled_ = true;
            return {};
        }
        if (gpu_result.status == OpenGlCompositionStatus::Complete && gpu_result.frame) {
            last_composition_gpu_ = true;
            metrics.recordCompositionBackend(true, gpu_timings);
            if (timings) {
                timings->canvas_width = width;
                timings->canvas_height = height;
            }
            return std::move(gpu_result.frame);
        }
        metrics.recordGpuCompositionWork(gpu_timings);
        if (gpu_result.status == OpenGlCompositionStatus::Complete) {
            gpu_result = {OpenGlCompositionStatus::Failed, {}, "compose",
                "The GPU backend returned no completed frame.", -1};
        }
        const bool failed = gpu_result.status != OpenGlCompositionStatus::Unsupported;
        if (failed) {
            gpu_composition_failed_ = true;
            metrics.recordGpuCompositionFailure();
            gpu_compositor_.reset();
        }
        if (!gpu_warning_reported_ || failed) {
            gpu_warning_reported_ = true;
            logging::Logger::instance().log(
                failed ? logging::Level::Error : logging::Level::Warning,
                "gpu-composition", gpu_result.operation, gpu_result.cause,
                {{"error_code", std::to_string(gpu_result.error_code)},
                 {"canvas_width", std::to_string(width)}, {"canvas_height", std::to_string(height)},
                 {"timeline_frame", std::to_string(current_timeline_frame_)},
                 {"generation", std::to_string(generation_)},
                 {"track_index", std::to_string(track_index_)}, {"clip_index", std::to_string(clip_index_)},
                 {"layer_count", std::to_string(layers.size())}, {"fallback", "cpu"}});
            emit compositionWarning(
                "GPU preview is unavailable for this request. Using CPU.",
                gpu_result.error_code, generation_);
        }
    }
    if (should_cancel()) return {};
    if (gpu_composition_enabled_ && timings) adapter_started = Clock::now();
    // Only the CPU path prepares and reports CPU text raster fast paths.
    for (std::size_t index = 0; index < decoded_layers.size(); ++index) {
        const auto& decoded = decoded_layers[index];
        auto& layer = layers[index];
        if (decoded.kind == timeline::ClipKind::Text &&
            decoded.composition_session_index < composition_sessions_.size()) {
            auto& composition =
                composition_sessions_[decoded.composition_session_index];
            if (!hasAnimatedTextGeometry(composition.spec.keyframes)) {
                composition.cached_text_geometry =
                    rendering::FrameCompositor::prepareAlphaCoverageGeometry(
                        width,
                        height,
                        layer,
                        composition.cached_text_geometry);
                layer.prepared_alpha_geometry = composition.cached_text_geometry;
            }
        }
        if (decoded.frame != nullptr &&
            rendering::FrameCompositor::canUseAlphaCoverageFastPath(layer)) {
            metrics.recordTextCompositionFastPathHit();
        }
    }
    const auto adapter_elapsed = timings != nullptr
        ? std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - adapter_started).count()
        : 0;

    auto composed = rendering::FrameCompositor::compose(
        width,
        height,
        layers,
        timings);
    if (composed && !should_cancel())
        metrics.recordCompositionBackend(false, {}, gpu_composition_enabled_);
    if (timings != nullptr) {
        timings->layer_list_setup_nanoseconds = adapter_elapsed <= 0
            ? 0U
            : static_cast<std::uint64_t>(adapter_elapsed);
    }
    return composed;
}

void PlaybackWorker::reportFailure(
    const media::MediaError& error,
    const char* operation,
    std::optional<std::int64_t> requested_frame) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    if (std::string_view(operation) == "decode_tick" ||
        std::string_view(operation) == "set_media") {
        metrics.recordDecodeFailure();
    } else if (std::string_view(operation) == "seek") {
        metrics.recordSeekFailure();
    } else if (std::string_view(operation) == "compose") {
        metrics.recordCompositionFailure();
    }
    try {
        logging::Context context{
            {"path", safePathForLog(source_path_)},
            {"clip_local_frame", std::to_string(current_frame_index_)},
            {"source_start_frame", std::to_string(source_start_frame_)},
            {"segment_frame_count", std::to_string(segment_frame_count_)}};
        if (requested_frame.has_value()) {
            context.emplace_back(
                "requested_clip_local_frame", std::to_string(*requested_frame));
        }
        if (const auto timeline_frame =
                timelineFrameForDiagnostics(requested_frame);
            timeline_frame.has_value()) {
            appendTimelinePositionContext(
                context, *timeline_frame, timeline_frame_rate_);
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
    resetPlaybackClock();
    rendering::PreviewPerformanceMetrics::instance().setPlaybackActive(false);
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
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    if (std::string_view(operation) == "decode_tick" ||
        std::string_view(operation) == "set_media") {
        metrics.recordDecodeFailure();
    } else if (std::string_view(operation) == "seek") {
        metrics.recordSeekFailure();
    } else if (std::string_view(operation) == "compose") {
        metrics.recordCompositionFailure();
    }
    try {
        logging::Context context{
            {"path", safePathForLog(source_path_)},
            {"clip_local_frame", std::to_string(current_frame_index_)},
            {"source_start_frame", std::to_string(source_start_frame_)},
            {"segment_frame_count", std::to_string(segment_frame_count_)}};
        if (requested_frame.has_value()) {
            context.emplace_back(
                "requested_clip_local_frame", std::to_string(*requested_frame));
        }
        if (const auto timeline_frame =
                timelineFrameForDiagnostics(requested_frame);
            timeline_frame.has_value()) {
            appendTimelinePositionContext(
                context, *timeline_frame, timeline_frame_rate_);
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
    resetPlaybackClock();
    rendering::PreviewPerformanceMetrics::instance().setPlaybackActive(false);
    playing_ = false;
    session_.reset();
    emit playbackError(QString::fromUtf8(error.what()), -1, generation_);
}

std::optional<std::int64_t> PlaybackWorker::sourceFrameForLocal(
    std::int64_t local_frame) const noexcept {
    if (local_frame < 0) return std::nullopt;
    if (composition_enabled_) {
        const auto active = std::find_if(
            composition_sessions_.cbegin(), composition_sessions_.cend(),
            [this](const CompositionSession& item) {
                return item.spec.track_index == track_index_ &&
                    item.spec.clip_index == clip_index_;
            });
        if (active != composition_sessions_.cend()) {
            const auto& spec = active->spec;
            const auto offset = timeline::sourceFrameOffsetForTimelineFrame(
                local_frame, spec.frame_rate, spec.timeline_frame_rate,
                spec.source_duration_frames);
            if (!offset.has_value() || spec.source_start_frame < 0 ||
                *offset > std::numeric_limits<std::int64_t>::max() -
                    spec.source_start_frame) {
                return std::nullopt;
            }
            return spec.source_start_frame + *offset;
        }
    }
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

void PlaybackWorker::startPlaybackClock() noexcept {
    playback_scheduler_.start(
        Clock::now(),
        composition_enabled_ ? current_timeline_frame_ : current_frame_index_,
        playbackFrameRate());
}

double PlaybackWorker::playbackFrameRate() const noexcept {
    if (composition_enabled_ && timeline::validFrameRate(timeline_frame_rate_)) {
        return timeline_frame_rate_.asDouble();
    }
    return std::isfinite(source_frame_rate_) && source_frame_rate_ > 0.0 &&
            source_frame_rate_ <= 1000.0
        ? source_frame_rate_
        : default_frame_rate;
}

void PlaybackWorker::setFallbackTimelineFrameRate(
    double source_frame_rate) noexcept {
    const auto rate = timeline::frameRateFromDouble(source_frame_rate);
    timeline_frame_rate_ = rate.value_or(timeline::FrameRate{30, 1});
}

void PlaybackWorker::updateFrameRateMetrics() noexcept {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setTargetFrameRate(playbackFrameRate());
    if (composition_enabled_ && timeline::validFrameRate(timeline_frame_rate_)) {
        metrics.setTimelineFrameRate(
            static_cast<std::uint64_t>(timeline_frame_rate_.numerator),
            static_cast<std::uint64_t>(timeline_frame_rate_.denominator));
    } else {
        metrics.setTimelineFrameRate(0, 0);
    }
}

void PlaybackWorker::resetPlaybackClock() noexcept {
    playback_scheduler_.reset();
}

} // namespace playback
