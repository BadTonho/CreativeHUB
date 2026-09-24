#include "application/editor_session.h"
#include "application/media_controller.h"
#include "playback/playback_controller.h"

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
#include <stdexcept>
#include <string>
#include <thread>
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
    std::atomic<quint64> seek_request_generation{0};
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
        state_->media_request_generation.store(generation, std::memory_order_release);
        generation_.store(generation, std::memory_order_release);
        if (fail_next_media_.exchange(false, std::memory_order_acq_rel)) {
            emit playbackError(QStringLiteral("Simulated media failure"), 42, generation);
            return;
        }
        emit mediaReady(generation);
        emit frameReady(makeFrame(0), 0, generation);
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
    void setComposition(
        QVector<playback::CompositionLayerSpec>,
        QVector<playback::CompositionTransitionSpec>,
        quint64 generation) override {
        state_->composition_request_generation.store(generation, std::memory_order_release);
        composition_generation_ = generation;
    }
    void setActiveCompositionClip(qint64, qint64) override {}

    void renderCompositionFrame(qint64, qint64 frame, quint64 generation) override {
        state_->render_request_generation.store(generation, std::memory_order_release);
        generation_.store(generation, std::memory_order_release);
        emit frameReady(makeFrame(static_cast<int>(frame)), frame, generation);
    }

    void stepForward() override {
        emit frameReady(makeFrame(1), 1, generation_.load(std::memory_order_acquire));
    }

    void stepBackward() override {
        emit frameReady(makeFrame(0), 0, generation_.load(std::memory_order_acquire));
    }

    void requestSeek(qint64 frame, quint64 generation) override {
        state_->seek_request_generation.store(generation, std::memory_order_release);
        QMetaObject::invokeMethod(
            this,
            [this, frame, generation]() {
                generation_.store(generation, std::memory_order_release);
                emit frameReady(makeFrame(static_cast<int>(frame)), frame, generation);
            },
            Qt::QueuedConnection);
    }

    void emitFrameLater(qint64 frame, quint64 generation) {
        QMetaObject::invokeMethod(
            this,
            [this, frame, generation]() {
                emit frameReady(makeFrame(static_cast<int>(frame)), frame, generation);
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
                    emit frameReady(makeFrame(static_cast<int>(frame)), frame, generation);
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

void runControllerTests() {
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

    require(session.selection().active_clip_id == 2,
            "A stale activation replaced the current stable clip identity.");
    require(selectionMatchesClip(session, 2),
            "Playback activation left the selected clip detached from its stable track ID.");
    const auto second_activation_generation = fake_worker->currentGeneration();
    require(fake_state->media_request_generation.load(std::memory_order_acquire) ==
                second_activation_generation &&
                fake_state->composition_request_generation.load(std::memory_order_acquire) ==
                    second_activation_generation,
            "A playback worker request lost its activation generation.");
    controller.renderCompositionFrame(5, 0);
    require(waitUntil([&]() {
        return fake_state->render_request_generation.load(std::memory_order_acquire) ==
            second_activation_generation;
    }), "A composition render request did not retain its captured generation.");
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
    require(controller.seekTimeline(3) == playback::PlaybackCommandResult::Applied,
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

    controller.shutdown();
    require(!controller.available(), "Shutdown left the playback worker available.");
    require(fake_state->stop_calls.load() > 0,
            "Shutdown did not stop the playback worker before joining its thread.");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        runControllerTests();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
