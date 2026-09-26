#include "application/editor_session.h"
#include "application/media_controller.h"
#include "playback/playback_controller.h"
#include "rendering/preview_performance_metrics.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QMetaObject>
#include <QTimer>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

bool selectionMatchesClip(const application::EditorSession& session, timeline::ClipId clip_id) {
    const auto& selection = session.selection();
    if (selection.active_clip_id != clip_id || !selection.active_track_id.has_value()) {
        return false;
    }
    const auto clip = session.timeline().locateClip(clip_id);
    const auto track = session.timeline().locateTrack(*selection.active_track_id);
    return clip.has_value() && track.has_value() && clip->track_index == *track &&
        session.timeline().tracks()[*track].clips[clip->clip_index].track_id ==
            *selection.active_track_id;
}

struct FakeWorkerState {
    std::atomic<int> stop_calls{0};
    std::atomic<int> play_calls{0};
    std::atomic<int> pause_calls{0};
    std::atomic<quint64> media_request_generation{0};
    std::atomic<quint64> composition_request_generation{0};
    std::atomic<quint64> render_request_generation{0};
    std::atomic<int> render_request_count{0};
    std::atomic<int> preview_quality_value{-1};
    std::atomic<qint64> last_active_composition_global_frame{-1};
    std::atomic<quint64> seek_request_generation{0};
    std::atomic<qint64> last_seek_frame{-1};
    std::atomic<int> media_open_delay_ms{0};
    std::atomic_bool emit_traced_frame{false};
    std::mutex composition_mutex;
    std::vector<std::pair<timeline::TrackId, timeline::ClipId>> composition_clip_ids;
};

class FakePlaybackWorker final : public playback::PlaybackWorker {
public:
    explicit FakePlaybackWorker(std::shared_ptr<FakeWorkerState> state)
        : state_(std::move(state)) {}

    void setMedia(
        QString,
        double,
        qint64,
        qint64,
        double,
        bool,
        double,
        bool,
        qint64,
        qint64,
        quint64 generation) override {
        const auto delay = state_->media_open_delay_ms.exchange(
            0, std::memory_order_acq_rel);
        if (delay > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        state_->media_request_generation.store(generation, std::memory_order_release);
        generation_.store(generation, std::memory_order_release);
        if (fail_next_media_.exchange(false, std::memory_order_acq_rel)) {
            emit playbackError(QStringLiteral("Simulated media failure"), 42, generation);
            return;
        }
        emit mediaReady(generation);
        const auto trace_id = state_->emit_traced_frame.load(std::memory_order_acquire)
            ? rendering::PreviewPerformanceMetrics::instance().createFrameDeliveryTrace(
                generation, 0)
            : 0U;
        emit frameReady(makeFrame(0), 0, generation, trace_id);
    }

    void play() override {
        ++state_->play_calls;
        emit playbackStateChanged(true, generation_.load(std::memory_order_acquire));
    }

    void pause() override {
        ++state_->pause_calls;
        emit playbackStateChanged(false, generation_.load(std::memory_order_acquire));
    }

    void stop() override {
        ++state_->stop_calls;
        emit playbackStateChanged(false, generation_.load(std::memory_order_acquire));
    }

    void setAudioParameters(double, bool, double, bool) override {}
    void setMonitorVolume(double) override {}
    void setPreviewQuality(playback::PreviewQuality quality) override {
        state_->preview_quality_value.store(
            static_cast<int>(quality), std::memory_order_release);
    }
    void setComposition(
        QVector<playback::CompositionLayerSpec> layers,
        QVector<playback::CompositionTransitionSpec>,
        quint64 generation) override {
        {
            std::lock_guard lock(state_->composition_mutex);
            state_->composition_clip_ids.clear();
            for (const auto& layer : layers) {
                state_->composition_clip_ids.emplace_back(
                    layer.track_id,
                    layer.clip_id);
            }
        }
        state_->composition_request_generation.store(generation, std::memory_order_release);
        composition_generation_ = generation;
    }
    void setActiveCompositionClip(qint64, qint64, qint64 global_frame) override {
        state_->last_active_composition_global_frame.store(
            global_frame, std::memory_order_release);
    }

    void renderCompositionFrame(qint64, qint64 frame, quint64 generation) override {
        state_->render_request_generation.store(generation, std::memory_order_release);
        state_->render_request_count.fetch_add(1, std::memory_order_acq_rel);
        generation_.store(generation, std::memory_order_release);
        emit frameReady(makeFrame(static_cast<int>(frame)), frame, generation, 0);
    }

    void stepForward() override {
        emit frameReady(makeFrame(1), 1, generation_.load(std::memory_order_acquire), 0);
    }

    void stepBackward() override {
        emit frameReady(makeFrame(0), 0, generation_.load(std::memory_order_acquire), 0);
    }

    void requestSeek(qint64 frame, quint64 generation) override {
        state_->seek_request_generation.store(generation, std::memory_order_release);
        state_->last_seek_frame.store(frame, std::memory_order_release);
        QMetaObject::invokeMethod(
            this,
            [this, frame, generation]() {
                generation_.store(generation, std::memory_order_release);
                emit frameReady(makeFrame(static_cast<int>(frame)), frame, generation, 0);
            },
            Qt::QueuedConnection);
    }

    void emitFrameLater(qint64 frame, quint64 generation) {
        QMetaObject::invokeMethod(
            this,
            [this, frame, generation]() {
                emit frameReady(makeFrame(static_cast<int>(frame)), frame, generation, 0);
            },
            Qt::QueuedConnection);
    }

    void emitFrameBurstLater(
        qint64 first,
        qint64 last,
        quint64 generation,
        std::shared_ptr<std::atomic_bool> finished) {
        QMetaObject::invokeMethod(
            this,
            [this, first, last, generation, finished = std::move(finished)]() {
                for (auto frame = first; frame <= last; ++frame) {
                    emit frameReady(makeFrame(static_cast<int>(frame)), frame, generation, 0);
                }
                finished->store(true, std::memory_order_release);
            },
            Qt::QueuedConnection);
    }

    void emitFinishedLater(quint64 generation, bool during_playback) {
        QMetaObject::invokeMethod(
            this,
            [this, generation, during_playback]() {
                emit playbackFinished(generation, during_playback);
            },
            Qt::QueuedConnection);
    }

    void failNextMedia() noexcept {
        fail_next_media_.store(true, std::memory_order_release);
    }

    [[nodiscard]] quint64 currentGeneration() const noexcept {
        return generation_.load(std::memory_order_acquire);
    }

private:
    static playback::VideoFramePtr makeFrame(int value) {
        auto frame = std::make_shared<media::VideoFrame>();
        frame->width = 1;
        frame->height = 1;
        frame->stride = 4;
        frame->rgba_pixels = {
            static_cast<std::uint8_t>(value), 0, 0, 255};
        return frame;
    }

    std::shared_ptr<FakeWorkerState> state_;
    std::atomic_bool fail_next_media_{false};
    std::atomic<quint64> generation_{0};
    quint64 composition_generation_ = 0;
};

bool waitUntil(const std::function<bool()>& predicate, int timeout_ms = 3000) {
    if (predicate()) return true;
    QEventLoop loop;
    QTimer poll;
    QTimer timeout;
    poll.setInterval(5);
    timeout.setSingleShot(true);
    QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
        if (predicate()) loop.quit();
    });
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    timeout.start(timeout_ms);
    loop.exec();
    poll.stop();
    return predicate();
}

media::MediaItem makeMedia(const std::filesystem::path& path) {
    media::VideoMetadata metadata;
    metadata.kind = media::MediaKind::Video;
    metadata.source_path = path;
    metadata.display_name = path.filename().string();
    metadata.frame_rate = 30.0;
    metadata.frame_count = 3;
    metadata.duration_seconds = 0.1;
    media::VideoFrame first_frame;
    first_frame.width = 1;
    first_frame.height = 1;
    first_frame.stride = 4;
    first_frame.rgba_pixels = {0, 0, 0, 255};
    return {metadata, std::move(first_frame), metadata.display_name, "Unsorted", false};
}

media::MediaItem makeLongMedia(
    const std::filesystem::path& path,
    double frame_rate,
    std::int64_t frame_count) {
    auto item = makeMedia(path);
    item.metadata.frame_rate = frame_rate;
    item.metadata.frame_count = frame_count;
    item.metadata.duration_seconds = static_cast<double>(frame_count) / frame_rate;
    return item;
}

void runContinuousClockTests() {
    application::EditorSession session;
    application::MediaController media_controller(session);
    const auto first_source = std::filesystem::temp_directory_path() /
        "playback-clock-first.mkv";
    const auto trimmed_source = std::filesystem::temp_directory_path() /
        "playback-clock-trimmed.mkv";
    const auto first_media = makeLongMedia(first_source, 25.0, 600);
    const auto trimmed_media = makeLongMedia(trimmed_source, 60.0, 900);
    require(media_controller.commitImported(first_media).changed(),
            "Could not seed the first long playback item.");
    require(media_controller.commitImported(trimmed_media).changed(),
            "Could not seed the trimmed playback item.");

    auto& model = session.legacyTimelineForUi();
    require(model.addTrack("Video 1") == timeline::AddTrackResult::Added,
            "Could not seed the continuous playback track.");
    require(model.addClip(0, first_media.metadata, 0) == timeline::AddClipResult::Added,
            "Could not seed the first continuous playback clip.");
    require(model.addClip(0, trimmed_media.metadata, 720) == timeline::AddClipResult::Added,
            "Could not seed the second continuous playback clip.");
    require(model.trimClip(0, 1, 12, 300) == timeline::TrimClipResult::Trimmed,
            "Could not seed a clip with a nonzero source in-point.");

    auto fake_state = std::make_shared<FakeWorkerState>();
    playback::PlaybackController controller(
        session,
        nullptr,
        [fake_state]() { return new FakePlaybackWorker(fake_state); });
    std::vector<playback::PlaybackControllerEvent> events;
    controller.setEventHandler([&](const auto& event) { events.push_back(event); });
    require(controller.activateClip(1, 719, false) ==
                playback::PlaybackCommandResult::Pending,
            "Could not activate the first clip near its cut.");
    require(waitUntil([&]() {
        return std::any_of(events.begin(), events.end(), [](const auto& event) {
            const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
            return activation != nullptr && activation->clip_id == 1 &&
                activation->phase == playback::PlaybackActivationPhase::Committed;
        });
    }), "The first clip did not finish its initial activation.");
    require(controller.seekTimeline(719) == playback::PlaybackCommandResult::Applied,
            "Could not position the playhead immediately before the cut.");
    require(waitUntil([&]() {
        return fake_state->last_seek_frame.load(std::memory_order_acquire) == 719;
    }), "The first clip seek did not reach the playback worker.");
    require(waitUntil([&]() {
        return session.playheadFrame() == 719;
    }), "The first clip seek did not update the local playhead.");

    fake_state->media_open_delay_ms.store(180, std::memory_order_release);
    require(controller.play() == playback::PlaybackCommandResult::Applied,
            "Playback did not start before the clip boundary.");
    require(waitUntil([&]() {
        return session.selection().active_clip_id == 2 && controller.isPlaying();
    }), "The global playback clock did not remain active while opening the next clip.");
    require(waitUntil([&]() {
        const auto& preserved = session.preservedPlayheadFrameForUi();
        return preserved.has_value() && *preserved >= 723;
    }, 1000), "The timeline playhead stopped while the next clip was opening.");
    require(controller.isPlaying(),
            "Playback stopped while the next clip was opening.");

    require(waitUntil([&]() {
        return std::any_of(events.begin(), events.end(), [](const auto& event) {
            const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
            return activation != nullptr && activation->clip_id == 2 &&
                activation->phase == playback::PlaybackActivationPhase::Committed;
        });
    }), "The trimmed next clip did not commit after seeking to its current position.");
    require(waitUntil([&]() {
        return fake_state->last_seek_frame.load(std::memory_order_acquire) >= 3;
    }), "The activation seek did not account for the moving timeline clock.");
    require(fake_state->last_active_composition_global_frame.load(
                std::memory_order_acquire) >= 720,
            "The active composition clip was not synchronized to the global Timeline frame.");
    require(controller.isPlaying(),
            "Playback was not resumed after the trimmed clip's first current frame.");
    require(session.preservedPlayheadFrameForUi().has_value() &&
                *session.preservedPlayheadFrameForUi() <= 728,
            "The Timeline clock switched to the second clip's different frame rate.");
    require(!session.projectDirty(),
            "Continuous playback unexpectedly marked the project as changed.");

    require(controller.execute(playback::PlaybackCommand::Pause) ==
                playback::PlaybackCommandResult::Applied,
            "The continuous playback fixture did not pause cleanly.");
    require(!controller.isPlaying(), "Pause left the global playback clock running.");
    controller.shutdown();
}

void runPendingActivationCancellationTests() {
    application::EditorSession session;
    application::MediaController media_controller(session);
    const auto first_source = std::filesystem::temp_directory_path() /
        "playback-cancel-first.mkv";
    const auto second_source = std::filesystem::temp_directory_path() /
        "playback-cancel-second.mkv";
    const auto first_media = makeMedia(first_source);
    const auto second_media = makeMedia(second_source);
    require(media_controller.commitImported(first_media).changed() &&
                media_controller.commitImported(second_media).changed(),
            "Could not seed the cancellation fixture media.");
    auto& model = session.legacyTimelineForUi();
    require(model.addTrack("Video 1") == timeline::AddTrackResult::Added &&
                model.addClip(0, first_media.metadata, 0) == timeline::AddClipResult::Added &&
                model.addClip(0, second_media.metadata, 3) == timeline::AddClipResult::Added,
            "Could not seed the cancellation fixture timeline.");

    auto fake_state = std::make_shared<FakeWorkerState>();
    playback::PlaybackController controller(
        session,
        nullptr,
        [fake_state]() { return new FakePlaybackWorker(fake_state); });
    std::vector<playback::PlaybackControllerEvent> events;
    controller.setEventHandler([&](const auto& event) { events.push_back(event); });
    require(controller.activateClip(1, 2, false) ==
                playback::PlaybackCommandResult::Pending,
            "Could not activate the cancellation fixture's first clip.");
    require(waitUntil([&]() {
        return std::any_of(events.begin(), events.end(), [](const auto& event) {
            const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
            return activation != nullptr && activation->clip_id == 1 &&
                activation->phase == playback::PlaybackActivationPhase::Committed;
        });
    }), "The cancellation fixture's first clip did not activate.");

    fake_state->media_open_delay_ms.store(220, std::memory_order_release);
    require(controller.play() == playback::PlaybackCommandResult::Applied,
            "Playback did not start in the cancellation fixture.");
    require(waitUntil([&]() {
        return session.selection().active_clip_id == 2 && controller.isPlaying();
    }), "Playback did not reach the delayed second clip.");
    controller.stop();
    require(!controller.isPlaying(), "Stop left the Timeline clock running.");
    QEventLoop settle_loop;
    QTimer::singleShot(260, &settle_loop, &QEventLoop::quit);
    settle_loop.exec();
    require(!controller.isPlaying() &&
                std::none_of(events.begin(), events.end(), [](const auto& event) {
                    const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
                    return activation != nullptr && activation->clip_id == 2 &&
                        activation->phase == playback::PlaybackActivationPhase::Committed;
                }),
            "A canceled activation resumed playback or committed a stale frame.");

    require(controller.play() == playback::PlaybackCommandResult::Pending,
            "Play did not retry the canceled clip activation.");
    require(waitUntil([&]() {
        return std::any_of(events.begin(), events.end(), [](const auto& event) {
            const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
            return activation != nullptr && activation->clip_id == 2 &&
                activation->phase == playback::PlaybackActivationPhase::Committed;
        });
    }), "The canceled clip did not activate on retry.");
    require(controller.isPlaying(), "The retried clip did not resume playback.");
    controller.pause();

    const auto first_commits_before_return = std::count_if(
        events.begin(), events.end(), [](const auto& event) {
            const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
            return activation != nullptr && activation->clip_id == 1 &&
                activation->phase == playback::PlaybackActivationPhase::Committed;
        });
    const auto return_to_first = controller.seekTimeline(0);
    require(return_to_first == playback::PlaybackCommandResult::Pending,
            "Seeking back to the first clip did not activate its media.");
    require(waitUntil([&]() {
        return session.selection().active_clip_id == 1 &&
            std::count_if(events.begin(), events.end(), [](const auto& event) {
                const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
                return activation != nullptr && activation->clip_id == 1 &&
                    activation->phase == playback::PlaybackActivationPhase::Committed;
            }) > first_commits_before_return;
    }), "The first clip did not activate after the seek.");
    fake_state->media_open_delay_ms.store(220, std::memory_order_release);
    require(controller.play() == playback::PlaybackCommandResult::Applied,
            "Playback did not restart from the seeked first clip.");
    require(waitUntil([&]() {
        return session.selection().active_clip_id == 2 && controller.isPlaying();
    }), "Playback did not reach the delayed clip before the seek-cancellation check.");
    const auto clip_two_commits_before_seek = std::count_if(
        events.begin(), events.end(), [](const auto& event) {
            const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
            return activation != nullptr && activation->clip_id == 2 &&
                activation->phase == playback::PlaybackActivationPhase::Committed;
        });
    const auto first_commits_before_seek = std::count_if(
        events.begin(), events.end(), [](const auto& event) {
            const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
            return activation != nullptr && activation->clip_id == 1 &&
                activation->phase == playback::PlaybackActivationPhase::Committed;
        });
    require(controller.seekTimeline(0) == playback::PlaybackCommandResult::Pending,
            "Seeking during activation did not switch back to the requested clip.");
    require(waitUntil([&]() {
        return session.selection().active_clip_id == 1 &&
            std::count_if(events.begin(), events.end(), [](const auto& event) {
                const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
                return activation != nullptr && activation->clip_id == 1 &&
                    activation->phase == playback::PlaybackActivationPhase::Committed;
            }) > first_commits_before_seek;
    }), "The seek during activation did not complete on the requested clip.");
    require(!controller.isPlaying() &&
                std::count_if(events.begin(), events.end(), [](const auto& event) {
                    const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
                    return activation != nullptr && activation->clip_id == 2 &&
                        activation->phase == playback::PlaybackActivationPhase::Committed;
                }) == clip_two_commits_before_seek,
            "Seeking during activation allowed the canceled clip to resume or commit a stale frame.");
    controller.shutdown();
}

void runGapAndEndClockTests() {
    const auto verify_finish = [](std::int64_t second_clip_start, bool expect_gap) {
        application::EditorSession session;
        application::MediaController media_controller(session);
        const auto source = std::filesystem::temp_directory_path() /
            (expect_gap ? "playback-clock-gap.mkv" : "playback-clock-end.mkv");
        const auto media = makeMedia(source);
        require(media_controller.commitImported(media).changed(),
                "Could not seed a Timeline finish fixture.");
        auto& model = session.legacyTimelineForUi();
        require(model.addTrack("Video 1") == timeline::AddTrackResult::Added &&
                    model.addClip(0, media.metadata, 0) == timeline::AddClipResult::Added,
                "Could not seed the first Timeline finish clip.");
        if (expect_gap) {
            const auto later_source = std::filesystem::temp_directory_path() /
                "playback-clock-gap-later.mkv";
            const auto later_media = makeMedia(later_source);
            require(media_controller.commitImported(later_media).changed() &&
                        model.addClip(0, later_media.metadata, second_clip_start) ==
                            timeline::AddClipResult::Added,
                    "Could not seed the clip after the Timeline gap.");
        }

        auto fake_state = std::make_shared<FakeWorkerState>();
        playback::PlaybackController controller(
            session,
            nullptr,
            [fake_state]() { return new FakePlaybackWorker(fake_state); });
        std::vector<playback::PlaybackControllerEvent> events;
        controller.setEventHandler([&](const auto& event) { events.push_back(event); });
        require(controller.activateClip(1, 1, false) ==
                    playback::PlaybackCommandResult::Pending,
                "Could not activate a Timeline finish fixture.");
        require(waitUntil([&]() {
            return std::any_of(events.begin(), events.end(), [](const auto& event) {
                const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
                return activation != nullptr && activation->clip_id == 1 &&
                    activation->phase == playback::PlaybackActivationPhase::Committed;
            });
        }), "A Timeline finish fixture did not activate.");
        require(controller.play() == playback::PlaybackCommandResult::Applied,
                "The Timeline finish fixture did not start.");
        require(waitUntil([&]() {
            return std::any_of(events.begin(), events.end(), [expect_gap](const auto& event) {
                const auto* finished = std::get_if<playback::PlaybackFinishedEvent>(&event);
                return finished != nullptr && finished->during_playback &&
                    finished->gap == expect_gap;
            });
        }), expect_gap
            ? "Playback did not stop at the Timeline gap."
            : "Playback did not stop at the end of the Timeline.");
        require(!controller.isPlaying(),
                "The Timeline clock remained active after reaching a gap or the end.");
        controller.shutdown();
    };

    verify_finish(5, true);
    verify_finish(0, false);
}

void runControllerTests() {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();
    application::EditorSession session;
    application::MediaController media_controller(session);
    const auto source_one = std::filesystem::temp_directory_path() / "playback-one.mkv";
    const auto source_two = std::filesystem::temp_directory_path() / "playback-two.mkv";
    require(media_controller.commitImported(makeMedia(source_one)).changed(),
            "Could not seed the first media item.");
    require(media_controller.commitImported(makeMedia(source_two)).changed(),
            "Could not seed the second media item.");

    auto& model = session.legacyTimelineForUi();
    require(model.addTrack("Video 1") == timeline::AddTrackResult::Added,
            "Could not seed a timeline track.");
    require(model.addClip(0, makeMedia(source_one).metadata, 0) ==
                timeline::AddClipResult::Added,
            "Could not seed the first timeline clip.");
    require(model.addClip(0, makeMedia(source_two).metadata, 3) ==
                timeline::AddClipResult::Added,
            "Could not seed the second timeline clip.");
    const auto missing_media = std::filesystem::temp_directory_path() / "missing-media.mkv";
    require(model.addClip(0, makeMedia(missing_media).metadata, 6) ==
                timeline::AddClipResult::Added,
            "Could not seed a clip whose media is absent from the library.");

    auto fake_state = std::make_shared<FakeWorkerState>();
    fake_state->emit_traced_frame.store(true, std::memory_order_release);
    FakePlaybackWorker* fake_worker = nullptr;
    playback::PlaybackController controller(
        session,
        nullptr,
        [&]() {
            fake_worker = new FakePlaybackWorker(fake_state);
            return fake_worker;
        });
    std::vector<playback::PlaybackControllerEvent> events;
    controller.setEventHandler([&](const auto& event) { events.push_back(event); });
    require(controller.available(), "The controller did not start its worker thread.");

    const auto event_count_before_rejected_activation = events.size();
    require(controller.activateClip(3, 0, false) ==
                playback::PlaybackCommandResult::Rejected,
            "A clip without an imported media item was accepted.");
    require(events.size() == event_count_before_rejected_activation &&
                !session.selection().active_clip_id.has_value(),
            "Rejecting unavailable media emitted events or changed selection.");

    require(controller.activateClip(1, 0, false) ==
                playback::PlaybackCommandResult::Pending,
            "The controller rejected a valid first clip activation.");
    require(controller.activateClip(2, 0, false) ==
                playback::PlaybackCommandResult::Pending,
            "The controller rejected a rapid replacement activation.");
    require(waitUntil([&]() {
        return std::any_of(events.begin(), events.end(), [](const auto& event) {
                const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
                return activation != nullptr && activation->clip_id == 2 &&
                    activation->phase == playback::PlaybackActivationPhase::Committed;
            });
    }), "The second activation did not complete.");
    require(waitUntil([&]() {
        return std::any_of(events.begin(), events.end(), [](const auto& event) {
            const auto* frame = std::get_if<playback::PlaybackFrameEvent>(&event);
            return frame != nullptr && frame->clip_id == 2 &&
                frame->delivery_trace_id != 0;
        });
    }), "The controller did not preserve the worker frame trace ID.");

    require(session.selection().active_clip_id == 2,
            "A stale activation replaced the current stable clip identity.");
    require(selectionMatchesClip(session, 2),
            "Playback activation left the selected clip detached from its stable track ID.");
    const auto second_activation_generation = fake_worker->currentGeneration();
    require(fake_state->media_request_generation.load(std::memory_order_acquire) ==
                second_activation_generation,
            "The media worker request lost its activation generation.");
    require(fake_state->composition_request_generation.load(std::memory_order_acquire) != 0,
            "The reusable composition snapshot was not installed in the worker.");
    controller.renderCompositionFrame(5, 0);
    require(waitUntil([&]() {
        return fake_state->render_request_generation.load(std::memory_order_acquire) ==
            second_activation_generation;
    }), "A composition render request did not retain its captured generation.");
    const auto dirty_before_quality_change = session.projectDirty();
    const auto playhead_before_quality_change = session.playheadFrame();
    const auto render_requests_before_quality_change =
        fake_state->render_request_count.load(std::memory_order_acquire);
    controller.setPreviewQuality(playback::PreviewQuality::Half);
    require(waitUntil([&]() {
        return fake_state->preview_quality_value.load(std::memory_order_acquire) ==
                static_cast<int>(playback::PreviewQuality::Half) &&
            fake_state->render_request_count.load(std::memory_order_acquire) >
                render_requests_before_quality_change;
    }), "Changing preview quality while paused did not forward the setting and redraw the current frame.");
    require(session.projectDirty() == dirty_before_quality_change &&
                session.playheadFrame() == playhead_before_quality_change,
            "Changing preview quality modified the project or playhead.");
    require(std::none_of(events.begin(), events.end(), [](const auto& event) {
        const auto* frame = std::get_if<playback::PlaybackFrameEvent>(&event);
        return frame != nullptr && frame->clip_id == 1;
    }), "A stale frame reached the controller event boundary.");
    require(std::none_of(events.begin(), events.end(), [](const auto& event) {
        const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
        return activation != nullptr && activation->clip_id == 1 &&
            activation->phase == playback::PlaybackActivationPhase::Committed;
    }), "A stale media-ready event committed the first activation.");

    require(controller.execute(playback::PlaybackCommand::Play) ==
                playback::PlaybackCommandResult::Applied,
            "Play was not forwarded through the controller.");
    require(waitUntil([&]() { return controller.isPlaying(); }),
            "The controller did not apply the worker playing state.");
    const auto dirty_during_playback_quality_change = session.projectDirty();
    controller.setPreviewQuality(playback::PreviewQuality::Quarter);
    require(waitUntil([&]() {
        return fake_state->preview_quality_value.load(std::memory_order_acquire) ==
            static_cast<int>(playback::PreviewQuality::Quarter);
    }), "Changing preview quality during playback did not reach the worker.");
    require(controller.isPlaying() &&
                session.projectDirty() == dirty_during_playback_quality_change,
            "Changing preview quality interrupted playback or dirtied the project.");
    const auto play_generation = fake_worker->currentGeneration();
    const auto frames_before_invalidation = std::count_if(
        events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<playback::PlaybackFrameEvent>(event);
        });
    const auto playhead_before_invalidation = session.playheadFrame();
    controller.invalidate(true);
    fake_worker->emitFrameLater(99, play_generation);
    QEventLoop invalidation_loop;
    QTimer::singleShot(40, &invalidation_loop, &QEventLoop::quit);
    invalidation_loop.exec();
    require(!controller.isPlaying() &&
                session.playheadFrame() == playhead_before_invalidation &&
                std::count_if(events.begin(), events.end(), [](const auto& event) {
                    return std::holds_alternative<playback::PlaybackFrameEvent>(event);
                }) == frames_before_invalidation,
            "Invalidation did not stop playback and reject its stale frame without changing the playhead.");

    const auto frames_before_seek = std::count_if(
        events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<playback::PlaybackFrameEvent>(event);
        });
    const auto seek_result = controller.seekTimeline(3);
    require(seek_result == playback::PlaybackCommandResult::Applied ||
                seek_result == playback::PlaybackCommandResult::Pending,
            "Seeking within the active clip was rejected.");
    require(waitUntil([&]() {
        return std::count_if(events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<playback::PlaybackFrameEvent>(event);
        }) > frames_before_seek;
    }), "The seek result did not arrive through the controller.");
    require(fake_state->seek_request_generation.load(std::memory_order_acquire) ==
                fake_worker->currentGeneration(),
            "A seek worker request did not carry the active generation.");

    require(controller.step(playback::PlaybackStepDirection::Backward) ==
                playback::PlaybackCommandResult::Pending,
            "Stepping across the clip boundary did not activate the previous clip.");
    require(waitUntil([&]() {
        return session.selection().active_clip_id == 1 &&
            std::any_of(events.begin(), events.end(), [](const auto& event) {
                const auto* activation = std::get_if<playback::PlaybackActivationEvent>(&event);
                return activation != nullptr && activation->clip_id == 1 &&
                    activation->phase == playback::PlaybackActivationPhase::Committed;
            });
    }), "Crossing the clip boundary did not update selection by stable identity.");
    require(selectionMatchesClip(session, 1),
            "Stepping across clips left the selected clip detached from its track ID.");
    require(waitUntil([&]() {
        return std::any_of(events.begin(), events.end(), [](const auto& event) {
            const auto* frame = std::get_if<playback::PlaybackFrameEvent>(&event);
            return frame != nullptr && frame->clip_id == 1;
        });
    }), "The activated clip did not deliver its frame event.");

    const auto frame_count_before_burst = std::count_if(
        events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<playback::PlaybackFrameEvent>(event);
        });
    auto burst_finished = std::make_shared<std::atomic_bool>(false);
    fake_worker->emitFrameBurstLater(
        4, 6, fake_worker->currentGeneration(), burst_finished);
    while (!burst_finished->load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(waitUntil([&]() {
        return std::count_if(events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<playback::PlaybackFrameEvent>(event);
        }) > frame_count_before_burst;
    }), "The frame mailbox did not deliver the latest frame from a burst.");
    const auto frame_count_after_burst = std::count_if(
        events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<playback::PlaybackFrameEvent>(event);
        });
    require(frame_count_after_burst == frame_count_before_burst + 1,
            "The frame mailbox did not coalesce a burst to one UI delivery.");
    const auto last_frame = std::find_if(events.rbegin(), events.rend(), [](const auto& event) {
        return std::holds_alternative<playback::PlaybackFrameEvent>(event);
    });
    require(last_frame != events.rend() &&
                std::get<playback::PlaybackFrameEvent>(*last_frame).frame_index == 6,
            "The frame mailbox did not retain the latest frame in the burst.");

    const auto stale_generation = fake_worker->currentGeneration();
    const auto playhead_before_stale_events = session.playheadFrame();
    const auto finished_count = std::count_if(
        events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<playback::PlaybackFinishedEvent>(event);
        });
    controller.invalidate(true);
    require(model.moveClip({0, 1}, {0, 1}, 9) == timeline::MoveClipResult::Moved,
            "The editing-during-playback fixture could not alter the timeline.");
    controller.refreshComposition();
    require(waitUntil([&]() {
        return fake_state->composition_request_generation.load(std::memory_order_acquire) !=
            stale_generation;
    }), "A refreshed composition request did not receive the new playback generation.");
    {
        std::lock_guard lock(fake_state->composition_mutex);
        const auto expected_track_id = model.tracks()[0].track_id;
        const auto has_clip_id = [&](timeline::ClipId clip_id) {
            return std::find(
                fake_state->composition_clip_ids.begin(),
                fake_state->composition_clip_ids.end(),
                std::pair{expected_track_id, clip_id}) !=
                fake_state->composition_clip_ids.end();
        };
        require(has_clip_id(1) && has_clip_id(2),
                "Composition specs did not retain stable track and clip IDs.");
    }
    fake_worker->emitFrameLater(2, stale_generation);
    fake_worker->emitFinishedLater(stale_generation, true);
    const auto frames_before_stale = std::count_if(
        events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<playback::PlaybackFrameEvent>(event);
        });
    QEventLoop stale_loop;
    QTimer::singleShot(40, &stale_loop, &QEventLoop::quit);
    stale_loop.exec();
    const auto frames_after_stale = std::count_if(
        events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<playback::PlaybackFrameEvent>(event);
        });
    require(frames_after_stale == frames_before_stale,
            "An event from before the edit reached the preview boundary.");
    const auto finished_after_stale = std::count_if(
        events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<playback::PlaybackFinishedEvent>(event);
        });
    require(finished_after_stale == finished_count,
            "A stale playback-finished event reached the application boundary.");
    require(session.playheadFrame() == playhead_before_stale_events,
            "An event from before the edit changed the session playhead.");

    require(controller.activateClip(9999, 0, false) ==
                playback::PlaybackCommandResult::Rejected,
            "An absent stable clip identifier was accepted.");
    require(controller.activateClip(1, 99, false) ==
                playback::PlaybackCommandResult::Rejected,
            "An invalid local frame was accepted.");

    fake_worker->failNextMedia();
    require(controller.activateClip(2, 0, false) ==
                playback::PlaybackCommandResult::Pending,
            "The controller did not start the activation failure fixture.");
    require(waitUntil([&]() {
        return std::any_of(events.begin(), events.end(), [](const auto& event) {
            return std::holds_alternative<playback::PlaybackErrorEvent>(event);
        });
    }), "The controller did not deliver an activation error event.");
    require(!session.selection().active_clip_id.has_value() &&
                !session.selection().active_track_id.has_value(),
            "A failed activation left stale stable selection in the session.");

    std::uint64_t controller_trace_id = 0;
    for (const auto& event : events) {
        const auto* frame = std::get_if<playback::PlaybackFrameEvent>(&event);
        if (frame != nullptr && frame->clip_id == 2 && frame->delivery_trace_id != 0) {
            controller_trace_id = frame->delivery_trace_id;
            break;
        }
    }
    const auto delivery_snapshot = metrics.takeSnapshotAndReset().frame_delivery;
    bool controller_trace_found = false;
    for (std::size_t index = 0; index < delivery_snapshot.sample_count; ++index) {
        const auto& sample = delivery_snapshot.samples[index];
        controller_trace_found = controller_trace_found ||
            (sample.trace_id == controller_trace_id &&
             sample.last_stage ==
                 rendering::PreviewFrameDeliveryStage::ControllerDelivered);
    }
    require(controller_trace_id != 0 && controller_trace_found &&
                delivery_snapshot.stage_counts[static_cast<std::size_t>(
                    rendering::PreviewFrameDeliveryStage::MailboxPublished)] > 0 &&
                delivery_snapshot.stage_counts[static_cast<std::size_t>(
                    rendering::PreviewFrameDeliveryStage::ControllerDelivered)] > 0,
            "The worker trace ID did not correlate through the mailbox to controller delivery.");

    controller.shutdown();
    metrics.setEnabled(false);
    require(!controller.available(), "Shutdown left the playback worker available.");
    require(fake_state->stop_calls.load() > 0,
            "Shutdown did not stop the playback worker before joining its thread.");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        runControllerTests();
        runContinuousClockTests();
        runPendingActivationCancellationTests();
        runGapAndEndClockTests();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
