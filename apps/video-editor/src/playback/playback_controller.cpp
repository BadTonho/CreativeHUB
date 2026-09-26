#include "playback_controller.h"

#include "frame_step_navigation.h"
#include "rendering/preview_performance_metrics.h"

#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace playback {

namespace {

std::filesystem::path canonicalPath(const std::filesystem::path& path) {
    return media::MediaLibrary::canonicalPath(path);
}

QString pathToQString(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char*>(value.data()),
        static_cast<qsizetype>(value.size()));
}

} // namespace

PlaybackController::PlaybackController(
    application::EditorSession& session,
    QObject* parent,
    WorkerFactory worker_factory)
    : QObject(parent),
      session_(session),
      worker_factory_(std::move(worker_factory)),
      timeline_clock_timer_(this) {
    qRegisterMetaType<VideoFramePtr>();
    qRegisterMetaType<CompositionLayerSpec>();
    qRegisterMetaType<QVector<CompositionLayerSpec>>();
    qRegisterMetaType<CompositionTransitionSpec>();
    qRegisterMetaType<QVector<CompositionTransitionSpec>>();

    worker_ = worker_factory_ ? worker_factory_() : new PlaybackWorker;
    if (worker_ == nullptr) return;
    timeline_clock_timer_.setTimerType(Qt::PreciseTimer);
    timeline_clock_timer_.setInterval(16);
    QObject::connect(
        &timeline_clock_timer_,
        &QTimer::timeout,
        this,
        &PlaybackController::updateTimelineClock);
    worker_->moveToThread(&worker_thread_);

    QObject::connect(
        &worker_thread_,
        &QThread::started,
        worker_,
        &PlaybackWorker::initializeDiagnostics,
        Qt::QueuedConnection);
    QObject::connect(
        &worker_thread_,
        &QThread::finished,
        worker_,
        &QObject::deleteLater);
    QObject::connect(
        worker_,
        &PlaybackWorker::frameReady,
        this,
        [this](VideoFramePtr frame, qint64 index, quint64 generation) {
            queueFrame(std::move(frame), index, generation);
        },
        Qt::DirectConnection);
    QObject::connect(
        worker_,
        &PlaybackWorker::mediaReady,
        this,
        [this](quint64 generation) { handleWorkerMediaReady(generation); },
        Qt::QueuedConnection);
    QObject::connect(
        worker_,
        &PlaybackWorker::playbackStateChanged,
        this,
        [this](bool playing, quint64 generation) {
            handleWorkerStateChanged(playing, generation);
        },
        Qt::QueuedConnection);
    QObject::connect(
        worker_,
        &PlaybackWorker::playbackFinished,
        this,
        [this](quint64 generation, bool during_playback) {
            handleWorkerFinished(generation, during_playback);
        },
        Qt::QueuedConnection);
    QObject::connect(
        worker_,
        &PlaybackWorker::playbackError,
        this,
        [this](const QString& message, qint64 code, quint64 generation) {
            handleWorkerError(message, code, generation);
        },
        Qt::QueuedConnection);
    QObject::connect(
        worker_,
        &PlaybackWorker::audioWarning,
        this,
        [this](const QString& message, qint64 code, quint64 generation) {
            handleWorkerAudioWarning(message, code, generation);
        },
        Qt::QueuedConnection);

    worker_thread_.start();
}

PlaybackController::~PlaybackController() {
    shutdown();
}

void PlaybackController::setEventHandler(EventHandler handler) {
    event_handler_ = std::move(handler);
}

void PlaybackController::shutdown() noexcept {
    if (shutting_down_.exchange(true, std::memory_order_acq_rel)) return;
    stopTimelineClock();
    pending_activation_.reset();
    playing_ = false;
    frame_mailbox_.clearPending();
    if (worker_ == nullptr) return;

    if (worker_thread_.isRunning()) {
        QMetaObject::invokeMethod(
            worker_,
            [worker = worker_]() { worker->stop(); },
            Qt::BlockingQueuedConnection);
        worker_thread_.quit();
        worker_thread_.wait();
    }
    worker_ = nullptr;
}

bool PlaybackController::available() const noexcept {
    return worker_ != nullptr && worker_thread_.isRunning() &&
        !shutting_down_.load(std::memory_order_acquire);
}

bool PlaybackController::isPlaying() const noexcept {
    return playing_;
}

void PlaybackController::queueWorker(
    std::function<void(PlaybackWorker&)> operation,
    std::optional<quint64> request_generation) {
    if (!available() || !operation) return;
#ifndef NDEBUG
    if (request_generation.has_value()) {
        Q_ASSERT(*request_generation == generation_);
    }
#else
    static_cast<void>(request_generation);
#endif
    auto* worker = worker_;
    QMetaObject::invokeMethod(
        worker,
        [this, worker, request_generation,
         operation = std::move(operation)]() mutable {
            if (request_generation.has_value() &&
                published_generation_.load(std::memory_order_acquire) !=
                    *request_generation) {
                return;
            }
            if (worker != nullptr) operation(*worker);
        },
        Qt::QueuedConnection);
}

void PlaybackController::requestSeekForGeneration(
    qint64 frame,
    quint64 request_generation) {
#ifndef NDEBUG
    Q_ASSERT(request_generation == generation_);
#endif
    if (worker_ != nullptr) worker_->requestSeek(frame, request_generation);
}

void PlaybackController::refreshComposition() {
    if (!available()) return;
    composition_ready_ = true;
    auto revision = composition_revision_.fetch_add(1, std::memory_order_acq_rel) + 1;
    if (revision == 0) {
        composition_revision_.store(1, std::memory_order_release);
        revision = 1;
    }

    QVector<CompositionLayerSpec> layers;
    QVector<CompositionTransitionSpec> transitions;
    const auto& model = session_.timeline();
    const auto& media_library = session_.mediaLibrary();

    for (std::size_t track_index = 0; track_index < model.trackCount(); ++track_index) {
        const auto& track = model.tracks()[track_index];
        for (std::size_t clip_index = 0; clip_index < track.clips.size(); ++clip_index) {
            const auto& clip = track.clips[clip_index];
            const media::MediaItem* imported = nullptr;
            if (timeline::isMediaClipKind(clip.kind)) {
                if (!media_library.contains(clip.source_path)) continue;
                const auto media_index = media_library.indexForPath(clip.source_path);
                if (media_index >= media_library.size()) continue;
                imported = &media_library.items()[media_index];
                if (imported->offline) continue;
            }

            std::shared_ptr<const media::VideoFrame> still_image_frame;
            if (clip.kind == timeline::ClipKind::Image) {
                still_image_frame = clip.still_image_override != nullptr
                    ? clip.still_image_override
                    : imported != nullptr
                        ? std::make_shared<const media::VideoFrame>(imported->first_frame)
                        : VideoFramePtr{};
            }

            layers.push_back(CompositionLayerSpec{
                timeline::isMediaClipKind(clip.kind)
                    ? pathToQString(clip.source_path)
                    : QString(),
                clip.frame_rate.value_or(
                    imported != nullptr && imported->metadata.frame_rate.has_value()
                        ? *imported->metadata.frame_rate
                        : 30.0),
                clip.timeline_start_frame,
                clip.source_start_frame,
                clip.timeline_duration_frames,
                static_cast<qint64>(track_index),
                static_cast<qint64>(clip_index),
                clip.transform,
                clip.keyframes,
                clip.kind,
                clip.text,
                std::move(still_image_frame)});
        }

        for (const auto& transition : track.transitions) {
            const auto from = std::find_if(
                track.clips.begin(), track.clips.end(),
                [&transition](const timeline::TimelineClip& clip) {
                    return clip.clip_id == transition.from_clip_id;
                });
            const auto to = std::find_if(
                track.clips.begin(), track.clips.end(),
                [&transition](const timeline::TimelineClip& clip) {
                    return clip.clip_id == transition.to_clip_id;
                });
            if (from == track.clips.end() || to == track.clips.end() || from + 1 != to) {
                continue;
            }
            transitions.push_back(CompositionTransitionSpec{
                static_cast<qint64>(track_index),
                static_cast<qint64>(std::distance(track.clips.begin(), from)),
                static_cast<qint64>(std::distance(track.clips.begin(), to)),
                from->timeline_start_frame + from->timeline_duration_frames,
                transition.duration_frames,
                transition.kind});
        }
    }

    qint64 active_track = -1;
    qint64 active_clip = -1;
    if (session_.selection().active_clip_id.has_value()) {
        const auto location = model.locateClip(*session_.selection().active_clip_id);
        if (location.has_value()) {
            active_track = static_cast<qint64>(location->track_index);
            active_clip = static_cast<qint64>(location->clip_index);
        }
    }
    queueWorker([this, revision, active_track, active_clip,
                 layers = std::move(layers),
                 transitions = std::move(transitions)]
                (PlaybackWorker& worker) mutable {
        if (composition_revision_.load(std::memory_order_acquire) != revision) return;
        const auto generation = published_generation_.load(std::memory_order_acquire);
        worker.setActiveCompositionClip(active_track, active_clip);
        worker.setComposition(std::move(layers), std::move(transitions), generation);
    });
}

void PlaybackController::setMonitorVolume(double gain) {
    queueWorker([gain](PlaybackWorker& worker) { worker.setMonitorVolume(gain); });
}

void PlaybackController::setAudioParametersForActiveClip() {
    const auto location = activeClipLocation();
    if (!location.has_value()) return;
    const auto& track = session_.timeline().tracks()[location->track_index];
    const auto& clip = track.clips[location->clip_index];
    queueWorker([track_gain = track.audio_gain,
                 track_muted = track.audio_muted,
                 clip_gain = clip.audio_gain,
                 clip_muted = clip.audio_muted](PlaybackWorker& worker) {
        worker.setAudioParameters(track_gain, track_muted, clip_gain, clip_muted);
    });
}

void PlaybackController::setGeneration(quint64 generation) noexcept {
    generation_ = generation;
    published_generation_.store(generation, std::memory_order_release);
}

void PlaybackController::discardPendingActivation(bool clear_selection) {
    if (!pending_activation_.has_value()) return;
    const auto clip_id = pending_activation_->clip_id;
    const bool preserve = pending_activation_->preserve_timeline_playhead;
    pending_activation_.reset();
    if (clear_selection && session_.selection().active_clip_id == clip_id) {
        auto& selection = session_.selectionForUi();
        selection.active_clip_id.reset();
        selection.active_track_id.reset();
        session_.assertInvariants();
    }
    playing_ = timeline_clock_active_;
    emitEvent(PlaybackActivationEvent{
        clip_id, PlaybackActivationPhase::Discarded, 0, false, preserve,
        clear_selection});
}

void PlaybackController::cancelPendingActivation() {
    if (!pending_activation_.has_value()) return;
    if (generation_ == std::numeric_limits<quint64>::max()) setGeneration(1);
    else setGeneration(generation_ + 1);
    discardPendingActivation(false);
    const auto generation = generation_;
    queueWorker([generation](PlaybackWorker& worker) {
        worker.cancelActivation(generation);
    });
}

bool PlaybackController::validatePendingActivation() const {
    if (!pending_activation_.has_value()) return false;
    const auto& pending = *pending_activation_;
    const auto location = session_.timeline().locateClip(pending.clip_id);
    if (!location.has_value()) return false;
    const auto& clip = session_.timeline().tracks()[location->track_index]
        .clips[location->clip_index];
    if (canonicalPath(clip.source_path) != pending.canonical_source_path) return false;
    const auto& library = session_.mediaLibrary();
    return library.contains(pending.canonical_source_path) &&
        !library.items()[library.indexForPath(pending.canonical_source_path)].offline;
}

void PlaybackController::invalidate(bool stop_worker) {
    stopTimelineClock();
    composition_revision_.fetch_add(1, std::memory_order_acq_rel);
    if (generation_ == std::numeric_limits<quint64>::max()) {
        setGeneration(1);
    } else {
        setGeneration(generation_ + 1);
    }
    discardPendingActivation(false);
    playing_ = false;
    composition_ready_ = false;
    ready_clip_id_.reset();
    if (stop_worker) stop();
    else pause();
}

void PlaybackController::pause() {
    if (!available()) return;
    if (timeline_clock_active_) updateTimelineClock();
    cancelPendingActivation();
    stopTimelineClock();
    playing_ = false;
    queueWorker([](PlaybackWorker& worker) { worker.pause(); });
}

void PlaybackController::stop() {
    if (!available()) return;
    if (timeline_clock_active_) updateTimelineClock();
    cancelPendingActivation();
    stopTimelineClock();
    playing_ = false;
    queueWorker([](PlaybackWorker& worker) { worker.stop(); });
}

void PlaybackController::seekActiveClip(std::int64_t local_frame) {
    if (!available()) return;
    if (timeline_clock_active_) updateTimelineClock();
    stopTimelineClock();
    const auto location = activeClipLocation();
    if (!location.has_value()) return;
    const auto& clip = session_.timeline().tracks()[location->track_index]
        .clips[location->clip_index];
    const auto clamped = std::clamp<std::int64_t>(
        local_frame, 0, std::max<std::int64_t>(0, clip.timeline_duration_frames - 1));
    if (pending_activation_.has_value()) {
        const auto clip_id = clip.clip_id;
        cancelPendingActivation();
        (void)activateClip(clip_id, clamped, false);
        return;
    }
    cancelPendingActivation();
    session_.setPlayheadFrame(clamped);
    const auto global_frame = clip.timeline_start_frame + clamped;
    session_.preservedPlayheadFrameForUi() = global_frame;
    emitEvent(PlaybackPositionEvent{global_frame, clamped, clip.clip_id});
    requestSeekForGeneration(static_cast<qint64>(clamped), generation_);
}

void PlaybackController::renderCompositionFrame(
    std::int64_t global_frame,
    std::int64_t local_frame) {
    if (!available()) return;
    if (!composition_ready_) refreshComposition();
    const auto generation = generation_;
    queueWorker([global_frame, local_frame, generation](PlaybackWorker& worker) {
        worker.renderCompositionFrame(
            static_cast<qint64>(global_frame),
            static_cast<qint64>(local_frame),
            generation);
    }, generation);
}

PlaybackCommandResult PlaybackController::activateClip(
    timeline::ClipId clip_id,
    std::int64_t local_frame,
    bool resume_playback,
    bool preserve_timeline_playhead) {
    if (!available()) return PlaybackCommandResult::Unavailable;
    const auto location = session_.timeline().locateClip(clip_id);
    if (!location.has_value()) return PlaybackCommandResult::Rejected;
    const auto& track = session_.timeline().tracks()[location->track_index];
    const auto& clip = track.clips[location->clip_index];
    if (local_frame < 0 || local_frame >= clip.timeline_duration_frames) {
        return PlaybackCommandResult::Rejected;
    }

    const media::MediaItem* media_item = nullptr;
    if (timeline::isMediaClipKind(clip.kind)) {
        const auto& library = session_.mediaLibrary();
        if (!library.contains(clip.source_path)) return PlaybackCommandResult::Rejected;
        const auto media_index = library.indexForPath(clip.source_path);
        if (media_index >= library.size() || library.items()[media_index].offline) {
            return PlaybackCommandResult::Rejected;
        }
        media_item = &library.items()[media_index];
    }

    if (generation_ == std::numeric_limits<quint64>::max()) setGeneration(1);
    else setGeneration(generation_ + 1);
    discardPendingActivation(false);
    auto& selection = session_.selectionForUi();
    selection.active_track_id = track.track_id;
    selection.active_clip_id = clip.clip_id;
    session_.assertInvariants();
    ready_clip_id_.reset();
    if (!preserve_timeline_playhead) {
        session_.preservedPlayheadFrameForUi().reset();
    }
    session_.setPlayheadFrame(local_frame);
    if (resume_playback && !timeline_clock_active_) {
        startTimelineClock(clip.timeline_start_frame + local_frame);
    }
    if (timeline_clock_active_) {
        publishTimelinePosition(
            clip.timeline_start_frame + local_frame, *location);
    }
    playing_ = timeline_clock_active_ || resume_playback;

    if (clip.kind == timeline::ClipKind::Text || clip.kind == timeline::ClipKind::Image) {
        if (composition_ready_) {
            const auto worker_track_index = static_cast<qint64>(location->track_index);
            const auto worker_clip_index = static_cast<qint64>(location->clip_index);
            const auto generation = generation_;
            queueWorker([worker_track_index, worker_clip_index](PlaybackWorker& worker) {
                worker.setActiveCompositionClip(worker_track_index, worker_clip_index);
            }, generation);
        } else {
            refreshComposition();
        }
        emitEvent(PlaybackActivationEvent{
            clip_id, PlaybackActivationPhase::Committed, local_frame, false,
            preserve_timeline_playhead});
        ready_clip_id_ = clip_id;
        renderCompositionFrame(timelineFrame(), local_frame);
        if (resume_playback) queueWorker([](PlaybackWorker& worker) { worker.play(); });
        return PlaybackCommandResult::Applied;
    }

    if (media_item == nullptr) return PlaybackCommandResult::Rejected;
    pending_activation_ = PendingActivation{
        clip_id,
        canonicalPath(clip.source_path),
        local_frame,
        clip.source_start_frame,
        clip.timeline_duration_frames,
        resume_playback,
        preserve_timeline_playhead,
        generation_};
    emitEvent(PlaybackActivationEvent{
        clip_id, PlaybackActivationPhase::Pending, local_frame, false,
        preserve_timeline_playhead});

    queueWorker([](PlaybackWorker& worker) { worker.stop(); });
    const auto source_path = pathToQString(media_item->metadata.source_path);
    const auto frame_rate = media_item->metadata.frame_rate.value_or(30.0);
    const auto source_start = clip.source_start_frame;
    const auto segment_duration = clip.timeline_duration_frames;
    const auto track_gain = track.audio_gain;
    const auto track_muted = track.audio_muted;
    const auto clip_gain = clip.audio_gain;
    const auto clip_muted = clip.audio_muted;
    const auto track_index = static_cast<qint64>(location->track_index);
    const auto clip_index = static_cast<qint64>(location->clip_index);
    const auto generation = generation_;
    queueWorker([source_path, frame_rate, source_start, segment_duration,
                 track_gain, track_muted, clip_gain, clip_muted, track_index,
                 clip_index, generation](PlaybackWorker& worker) {
        worker.setMedia(
            source_path, frame_rate, source_start, segment_duration,
            track_gain, track_muted, clip_gain, clip_muted,
            track_index, clip_index, generation);
    }, generation);
    if (composition_ready_) {
        queueWorker([track_index, clip_index](PlaybackWorker& worker) {
            worker.setActiveCompositionClip(track_index, clip_index);
        }, generation);
    } else {
        refreshComposition();
    }
    return PlaybackCommandResult::Pending;
}

std::optional<timeline::ClipLocation> PlaybackController::activeClipLocation() const noexcept {
    if (!session_.selection().active_clip_id.has_value()) return std::nullopt;
    return session_.timeline().locateClip(*session_.selection().active_clip_id);
}

std::int64_t PlaybackController::timelineFrame() const noexcept {
    if (session_.preservedPlayheadFrameForUi().has_value()) {
        return std::max<std::int64_t>(0, *session_.preservedPlayheadFrameForUi());
    }
    const auto location = activeClipLocation();
    if (!location.has_value()) return session_.playheadFrame();
    const auto& clip = session_.timeline().tracks()[location->track_index]
        .clips[location->clip_index];
    const auto local = std::clamp<std::int64_t>(
        session_.playheadFrame(), 0, std::max<std::int64_t>(0, clip.timeline_duration_frames - 1));
    if (clip.timeline_start_frame > std::numeric_limits<std::int64_t>::max() - local) {
        return clip.timeline_start_frame;
    }
    return clip.timeline_start_frame + local;
}

bool PlaybackController::clipCanPlay(const timeline::TimelineClip& clip) const {
    if (clip.kind == timeline::ClipKind::Text) return true;
    const auto& library = session_.mediaLibrary();
    if (!library.contains(clip.source_path)) return false;
    const auto index = library.indexForPath(clip.source_path);
    return index < library.size() && !library.items()[index].offline;
}

bool PlaybackController::hasFutureClip(std::int64_t frame) const {
    const auto& model = session_.timeline();
    return std::any_of(model.tracks().begin(), model.tracks().end(),
        [frame](const timeline::TimelineTrack& track) {
            return std::any_of(track.clips.begin(), track.clips.end(),
                [frame](const timeline::TimelineClip& clip) {
                    return clip.timeline_start_frame > frame;
                });
        });
}

PlaybackCommandResult PlaybackController::play() {
    if (!available()) return PlaybackCommandResult::Unavailable;
    if (pending_activation_.has_value()) return PlaybackCommandResult::Pending;
    const auto target_frame = timelineFrame();
    const auto destination = session_.timeline().topClipAt(target_frame);
    if (!destination.has_value()) return PlaybackCommandResult::Gap;
    const auto& clip = session_.timeline().tracks()[destination->track_index]
        .clips[destination->clip_index];
    if (!clipCanPlay(clip)) return PlaybackCommandResult::Rejected;
    const auto active = activeClipLocation();
    const bool same_clip = active.has_value() &&
        session_.timeline().tracks()[active->track_index].clips[active->clip_index].clip_id ==
            clip.clip_id;
    const bool ready = same_clip &&
        (ready_clip_id_ == clip.clip_id ||
         clip.kind == timeline::ClipKind::Text ||
         clip.kind == timeline::ClipKind::Image);
    if (!ready) {
        const auto local = target_frame - clip.timeline_start_frame;
        return activateClip(clip.clip_id, local, true, true);
    }
    startTimelineClock(target_frame);
    publishTimelinePosition(target_frame, *destination);
    playing_ = true;
    emitEvent(PlaybackStateEvent{true});
    queueWorker([](PlaybackWorker& worker) { worker.play(); });
    return PlaybackCommandResult::Applied;
}

PlaybackCommandResult PlaybackController::execute(PlaybackCommand command) {
    switch (command) {
    case PlaybackCommand::Play:
        return play();
    case PlaybackCommand::Pause:
        if (!available()) return PlaybackCommandResult::Unavailable;
        pause();
        return PlaybackCommandResult::Applied;
    case PlaybackCommand::StepForward:
        return step(PlaybackStepDirection::Forward);
    case PlaybackCommand::StepBackward:
        return step(PlaybackStepDirection::Backward);
    }
    return PlaybackCommandResult::Rejected;
}

PlaybackCommandResult PlaybackController::step(PlaybackStepDirection direction) {
    if (!available()) return PlaybackCommandResult::Unavailable;
    if (timeline_clock_active_) updateTimelineClock();
    stopTimelineClock();
    cancelPendingActivation();
    playing_ = false;
    const auto active = activeClipLocation();
    if (!active.has_value()) return PlaybackCommandResult::NoClip;
    const auto& clip = session_.timeline().tracks()[active->track_index]
        .clips[active->clip_index];
    if (!clipCanPlay(clip)) return PlaybackCommandResult::Rejected;
    const auto decision = detail::decideFrameStep(
        session_.timeline(),
        active,
        session_.playheadFrame(),
        direction == PlaybackStepDirection::Forward
            ? detail::FrameStepDirection::Forward
            : detail::FrameStepDirection::Backward);
    switch (decision.action) {
    case detail::FrameStepAction::StepWorker:
        if (ready_clip_id_ != clip.clip_id) {
            return activateClip(clip.clip_id, decision.local_frame, false);
        }
        queueWorker([direction](PlaybackWorker& worker) {
            if (direction == PlaybackStepDirection::Forward) worker.stepForward();
            else worker.stepBackward();
        });
        return PlaybackCommandResult::Applied;
    case detail::FrameStepAction::ActivateClip:
        if (!decision.destination.has_value()) return PlaybackCommandResult::Rejected;
        return activateClip(
            session_.timeline().tracks()[decision.destination->track_index]
                .clips[decision.destination->clip_index].clip_id,
            decision.local_frame,
            false);
    case detail::FrameStepAction::Gap:
        return PlaybackCommandResult::Gap;
    case detail::FrameStepAction::Beginning:
        return PlaybackCommandResult::Beginning;
    case detail::FrameStepAction::End:
        return PlaybackCommandResult::End;
    }
    return PlaybackCommandResult::Rejected;
}

PlaybackCommandResult PlaybackController::seekTimeline(std::int64_t global_frame) {
    if (!available()) return PlaybackCommandResult::Unavailable;
    if (timeline_clock_active_) updateTimelineClock();
    stopTimelineClock();
    cancelPendingActivation();
    const auto total = session_.timeline().totalDurationFrames();
    if (total <= 0) return PlaybackCommandResult::NoClip;
    const auto target = std::clamp<std::int64_t>(global_frame, 0, total - 1);
    session_.preservedPlayheadFrameForUi().reset();
    if (generation_ == std::numeric_limits<quint64>::max()) setGeneration(1);
    else setGeneration(generation_ + 1);
    playing_ = false;
    const auto destination = session_.timeline().topClipAt(target);
    if (!destination.has_value()) {
        session_.preservedPlayheadFrameForUi() = target;
        pause();
        return PlaybackCommandResult::Gap;
    }
    const auto& clip = session_.timeline().tracks()[destination->track_index]
        .clips[destination->clip_index];
    const auto local = target - clip.timeline_start_frame;
    const auto active = activeClipLocation();
    const bool same_clip = active.has_value() &&
        session_.timeline().tracks()[active->track_index].clips[active->clip_index].clip_id ==
            clip.clip_id;
    if (!same_clip || ready_clip_id_ != clip.clip_id ||
        clip.kind == timeline::ClipKind::Text ||
        clip.kind == timeline::ClipKind::Image) {
        return activateClip(clip.clip_id, local, false);
    }
    if (!clipCanPlay(clip)) return PlaybackCommandResult::Rejected;
    session_.setPlayheadFrame(local);
    requestSeekForGeneration(static_cast<qint64>(local), generation_);
    return PlaybackCommandResult::Applied;
}

void PlaybackController::handleWorkerMediaReady(quint64 generation) {
    if (generation != generation_ || !pending_activation_.has_value() ||
        pending_activation_->generation != generation) return;
    if (!validatePendingActivation()) {
        discardPendingActivation(true);
        return;
    }
    requestSeekForGeneration(
        static_cast<qint64>(pending_activation_->target_frame), generation);
}

void PlaybackController::handleWorkerStateChanged(bool playing, quint64 generation) {
    if (generation != generation_) return;
    if (timeline_clock_active_ && !playing) return;
    playing_ = playing;
    emitEvent(PlaybackStateEvent{playing});
}

void PlaybackController::handleWorkerFinished(
    quint64 generation,
    bool during_playback) {
    if (generation != generation_) return;
    if (during_playback && timeline_clock_active_) {
        updateTimelineClock();
        return;
    }
    if (during_playback && !playing_) return;
    playing_ = false;
    bool gap = false;
    if (during_playback) {
        const auto active = activeClipLocation();
        if (active.has_value()) {
            const auto& clip = session_.timeline().tracks()[active->track_index]
                .clips[active->clip_index];
            const auto boundary = clip.timeline_start_frame + clip.timeline_duration_frames;
            if (const auto next = session_.timeline().topClipAt(boundary); next.has_value()) {
                const auto& next_clip = session_.timeline().tracks()[next->track_index]
                    .clips[next->clip_index];
                const auto result = activateClip(
                    next_clip.clip_id,
                    std::max<std::int64_t>(0, boundary - next_clip.timeline_start_frame),
                    true);
                if (result == PlaybackCommandResult::Pending ||
                    result == PlaybackCommandResult::Applied) return;
            }
            gap = hasFutureClip(boundary);
        }
    }
    emitEvent(PlaybackFinishedEvent{during_playback, gap});
}

void PlaybackController::handleWorkerError(
    const QString& message,
    qint64 error_code,
    quint64 generation) {
    if (generation != generation_) return;
    stopTimelineClock();
    ready_clip_id_.reset();
    composition_ready_ = false;
    PlaybackErrorEvent event;
    event.message = message;
    event.error_code = error_code;
    event.generation = generation;
    if (pending_activation_.has_value() &&
        pending_activation_->generation == generation) {
        event.activation_clip_id = pending_activation_->clip_id;
        event.source_path = pending_activation_->canonical_source_path;
        event.target_frame = pending_activation_->target_frame;
        event.source_start_frame = pending_activation_->source_start_frame;
        event.segment_frame_count = pending_activation_->segment_frame_count;
        pending_activation_.reset();
        auto& selection = session_.selectionForUi();
        if (selection.active_clip_id == event.activation_clip_id) {
            selection.active_clip_id.reset();
            selection.active_track_id.reset();
        }
        session_.assertInvariants();
    }
    playing_ = false;
    emitEvent(std::move(event));
}

void PlaybackController::handleWorkerAudioWarning(
    const QString& message,
    qint64 error_code,
    quint64 generation) {
    if (generation != generation_) return;
    emitEvent(PlaybackAudioWarningEvent{message, error_code});
}

void PlaybackController::queueFrame(
    VideoFramePtr frame,
    qint64 frame_index,
    quint64 generation) {
    if (frame == nullptr || shutting_down_.load(std::memory_order_acquire)) return;
    if (generation != published_generation_.load(std::memory_order_acquire)) {
        rendering::PreviewPerformanceMetrics::instance().recordStaleFrameDiscarded();
        return;
    }
    if (frame_mailbox_.publish(PlaybackFramePacket{
            std::move(frame), frame_index, generation})) {
        rendering::PreviewPerformanceMetrics::instance().recordPacingCoalescedFrame();
    }
    if (!frame_mailbox_.acquireDispatch()) return;
    QMetaObject::invokeMethod(
        this,
        [this]() { drainFrameMailbox(); },
        Qt::QueuedConnection);
}

void PlaybackController::drainFrameMailbox() {
    const auto packet = frame_mailbox_.take();
    if (packet.has_value() && packet->generation == generation_ &&
        packet->frame != nullptr) {
        bool present_packet = true;
        if (pending_activation_.has_value() &&
            pending_activation_->generation == packet->generation) {
            if (!validatePendingActivation()) {
                discardPendingActivation(true);
            } else {
                if (timeline_clock_active_ &&
                    pending_activation_->resume_playback &&
                    ((packet->frame_index < pending_activation_->target_frame &&
                      pending_activation_->target_frame - packet->frame_index > 1) ||
                     (packet->frame_index > pending_activation_->target_frame &&
                      packet->frame_index - pending_activation_->target_frame > 1))) {
                    requestSeekForGeneration(
                        static_cast<qint64>(pending_activation_->target_frame),
                        packet->generation);
                    present_packet = false;
                }
                if (present_packet) {
                    const auto pending = *pending_activation_;
                    pending_activation_.reset();
                    ready_clip_id_ = pending.clip_id;
                    session_.setPlayheadFrame(packet->frame_index);
                    emitEvent(PlaybackActivationEvent{
                        pending.clip_id, PlaybackActivationPhase::Committed,
                        packet->frame_index, false,
                        pending.preserve_timeline_playhead});
                    if (pending.resume_playback && timeline_clock_active_) {
                        playing_ = true;
                        queueWorker([](PlaybackWorker& worker) { worker.play(); },
                                    packet->generation);
                    }
                }
            }
        } else {
            const auto active = activeClipLocation();
            if (timeline_clock_active_ && active.has_value()) {
                const auto& clip = session_.timeline().tracks()[active->track_index]
                    .clips[active->clip_index];
                const auto expected_global = timelineClockFrame();
                const auto expected_local = expected_global - clip.timeline_start_frame;
                if (packet->frame_index < expected_local &&
                    expected_local - packet->frame_index > 1) {
                    present_packet = false;
                }
            } else {
                session_.preservedPlayheadFrameForUi().reset();
                session_.setPlayheadFrame(packet->frame_index);
                if (const auto location = activeClipLocation(); location.has_value()) {
                    const auto& clip = session_.timeline().tracks()[location->track_index]
                        .clips[location->clip_index];
                    const auto local_frame = std::clamp<std::int64_t>(
                        packet->frame_index,
                        0,
                        std::max<std::int64_t>(0, clip.timeline_duration_frames - 1));
                    emitEvent(PlaybackPositionEvent{
                        clip.timeline_start_frame + local_frame,
                        local_frame,
                        clip.clip_id});
                }
            }
        }

        const auto active_after = activeClipLocation();
        if (present_packet && active_after.has_value()) {
            const auto& clip = session_.timeline().tracks()[active_after->track_index]
                .clips[active_after->clip_index];
            emitEvent(PlaybackFrameEvent{
                packet->frame,
                packet->frame_index,
                clip.clip_id});

        }
    }
    if (frame_mailbox_.finishDispatch()) {
        QMetaObject::invokeMethod(
            this,
            [this]() { drainFrameMailbox(); },
            Qt::QueuedConnection);
    }
}

void PlaybackController::updateTimelineClock() {
    if (!timeline_clock_active_) return;
    const auto total = session_.timeline().totalDurationFrames();
    if (total <= 0) {
        stopTimelineClock();
        playing_ = false;
        queueWorker([](PlaybackWorker& worker) { worker.pause(); });
        emitEvent(PlaybackFinishedEvent{true, false});
        emitEvent(PlaybackStateEvent{false});
        return;
    }

    const auto raw_frame = timelineClockFrame();
    const bool reached_end = raw_frame >= total;
    const auto frame = std::clamp<std::int64_t>(raw_frame, 0, total - 1);
    const auto destination = session_.timeline().topClipAt(frame);
    if (!destination.has_value()) {
        session_.preservedPlayheadFrameForUi() = frame;
        if (frame != last_timeline_clock_frame_) {
            last_timeline_clock_frame_ = frame;
            emitEvent(PlaybackPositionEvent{frame, session_.playheadFrame(), 0});
        }
        const bool gap = hasFutureClip(frame);
        stopTimelineClock();
        playing_ = false;
        queueWorker([](PlaybackWorker& worker) { worker.pause(); });
        emitEvent(PlaybackFinishedEvent{true, gap});
        emitEvent(PlaybackStateEvent{false});
        return;
    }

    const auto& clip = session_.timeline().tracks()[destination->track_index]
        .clips[destination->clip_index];
    const auto local_frame = std::clamp<std::int64_t>(
        frame - clip.timeline_start_frame,
        0,
        std::max<std::int64_t>(0, clip.timeline_duration_frames - 1));
    publishTimelinePosition(frame, *destination);

    const bool destination_is_pending = pending_activation_.has_value() &&
        pending_activation_->clip_id == clip.clip_id;
    if (destination_is_pending) {
        pending_activation_->target_frame = local_frame;
    } else if (!activeClipLocation().has_value() ||
        session_.selection().active_clip_id != clip.clip_id) {
        const auto result = activateClip(
            clip.clip_id, local_frame, !reached_end, true);
        if (result == PlaybackCommandResult::Rejected ||
            result == PlaybackCommandResult::Unavailable) {
            stopTimelineClock();
            playing_ = false;
            queueWorker([](PlaybackWorker& worker) { worker.pause(); });
            emitEvent(PlaybackFinishedEvent{true, hasFutureClip(frame)});
            emitEvent(PlaybackStateEvent{false});
            return;
        }
    } else {
        session_.setPlayheadFrame(local_frame);
    }

    if (reached_end) {
        if (pending_activation_.has_value()) {
            pending_activation_->resume_playback = false;
        }
        stopTimelineClock();
        playing_ = false;
        queueWorker([](PlaybackWorker& worker) { worker.pause(); });
        emitEvent(PlaybackFinishedEvent{true, false});
        emitEvent(PlaybackStateEvent{false});
    }
}

void PlaybackController::startTimelineClock(std::int64_t timeline_frame) {
    if (timeline_clock_active_) return;
    timeline_clock_origin_frame_ = std::max<std::int64_t>(0, timeline_frame);
    last_timeline_clock_frame_ = timeline_clock_origin_frame_ - 1;
    timeline_clock_frame_rate_ = timelineFrameRate();
    timeline_clock_started_at_ = Clock::now();
    timeline_clock_active_ = true;
    playing_ = true;
    timeline_clock_timer_.start();
    emitEvent(PlaybackStateEvent{true});
}

void PlaybackController::stopTimelineClock() {
    timeline_clock_active_ = false;
    timeline_clock_timer_.stop();
}

void PlaybackController::publishTimelinePosition(
    std::int64_t timeline_frame,
    const timeline::ClipLocation& location) {
    if (location.track_index >= session_.timeline().trackCount() ||
        location.clip_index >= session_.timeline().clipCount(location.track_index)) {
        return;
    }
    const auto& clip = session_.timeline().tracks()[location.track_index]
        .clips[location.clip_index];
    const auto local_frame = std::clamp<std::int64_t>(
        timeline_frame - clip.timeline_start_frame,
        0,
        std::max<std::int64_t>(0, clip.timeline_duration_frames - 1));
    session_.preservedPlayheadFrameForUi() = timeline_frame;
    session_.setPlayheadFrame(local_frame);
    if (timeline_frame == last_timeline_clock_frame_) return;
    last_timeline_clock_frame_ = timeline_frame;
    emitEvent(PlaybackPositionEvent{
        timeline_frame, local_frame, clip.clip_id});
}

double PlaybackController::timelineFrameRate() const noexcept {
    for (const auto& track : session_.timeline().tracks()) {
        for (const auto& clip : track.clips) {
            if (clip.frame_rate.has_value() &&
                std::isfinite(*clip.frame_rate) && *clip.frame_rate > 0.0) {
                return *clip.frame_rate;
            }
        }
    }
    return 30.0;
}

std::int64_t PlaybackController::timelineClockFrame() const noexcept {
    if (!timeline_clock_active_ || timeline_clock_frame_rate_ <= 0.0) {
        return last_timeline_clock_frame_;
    }
    const auto elapsed = std::chrono::duration<double>(
        Clock::now() - timeline_clock_started_at_).count();
    const auto frame_offset = static_cast<long double>(elapsed) *
        static_cast<long double>(timeline_clock_frame_rate_);
    if (!std::isfinite(frame_offset) ||
        frame_offset >= static_cast<long double>(
            std::numeric_limits<std::int64_t>::max())) {
        return std::numeric_limits<std::int64_t>::max();
    }
    const auto offset = static_cast<std::int64_t>(
        std::max<long double>(0.0L, std::floor(frame_offset)));
    if (offset > std::numeric_limits<std::int64_t>::max() -
            timeline_clock_origin_frame_) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return std::max(
        last_timeline_clock_frame_, timeline_clock_origin_frame_ + offset);
}

void PlaybackController::emitEvent(PlaybackControllerEvent event) {
    if (event_handler_) event_handler_(event);
}

} // namespace playback
