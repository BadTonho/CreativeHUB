#include "playback/playback_worker.h"
#include "playback/playback_frame_mailbox.h"
#include "playback/playback_transition_plan.h"
#include "logging/logger.h"
#include "media/video_playback.h"
#include "rendering/frame_compositor.h"
#include "rendering/preview_performance_metrics.h"

#include <QGuiApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <optional>
#include <span>

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

QString toQString(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char*>(value.data()),
        static_cast<int>(value.size()));
}

void validateWorkerDiagnostics(QCoreApplication&) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    const auto ui_thread_id = logging::current_thread_id();
    QThread playback_thread;
    auto* worker = new playback::PlaybackWorker;
    worker->moveToThread(&playback_thread);

    QObject::connect(
        &playback_thread,
        &QThread::finished,
        worker,
        &QObject::deleteLater);
    QObject::connect(
        &playback_thread,
        &QThread::started,
        worker,
        &playback::PlaybackWorker::initializeDiagnostics,
        Qt::QueuedConnection);

    playback_thread.start();
    QEventLoop wait_loop;
    QTimer::singleShot(500, &wait_loop, &QEventLoop::quit);
    wait_loop.exec();

    const auto snapshot = metrics.takeSnapshotAndReset();
    playback_thread.quit();
    playback_thread.wait();
    metrics.setEnabled(false);

    require(snapshot.playback_worker_thread_id != 0,
            "Playback worker thread identifier was not captured.");
    require(snapshot.playback_worker_thread_id != ui_thread_id,
            "Playback worker thread identifier matches the UI thread.");
}

void validateMissingMedia(QCoreApplication& application) {
    playback::PlaybackWorker worker;
    bool received_error = false;
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&received_error](const QString&, qint64, quint64) {
            received_error = true;
        });

    worker.setMedia(
        toQString(std::filesystem::temp_directory_path() / "missing-creative-suite.mkv"),
        30.0,
        0,
        0,
        1.0,
        false,
        1.0,
        false,
        -1,
        -1,
        1);
    QCoreApplication::processEvents();
    require(received_error, "Missing media did not produce a worker error.");
    Q_UNUSED(application);
}

void validateSeekWithoutMedia(QCoreApplication& application) {
    playback::PlaybackWorker worker;
    bool received_error = false;
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&received_error](const QString&, qint64, quint64) {
            received_error = true;
        });

    worker.requestSeek(0, 2);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &application,
        &QCoreApplication::quit);
    timeout.start(100);
    application.exec();

    require(!received_error,
            "Seeking without a selected media source produced an error.");
}

void validateReference(QCoreApplication& application, const std::filesystem::path& path) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    playback::PlaybackWorker worker;
    bool media_ready = false;
    bool playback_finished = false;
    bool finished_during_playback = false;
    bool playback_error = false;
    std::size_t frame_count = 0;
    qint64 last_frame_index = -1;
    int ready_count = 0;

    QObject::connect(
        &worker,
        &playback::PlaybackWorker::mediaReady,
        [&media_ready, &ready_count](quint64) {
            media_ready = true;
            ++ready_count;
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&frame_count, &last_frame_index](
            playback::VideoFramePtr frame,
            qint64 frame_index,
            quint64,
            quint64) {
            require(frame != nullptr, "Worker emitted an empty frame payload.");
            ++frame_count;
            last_frame_index = frame_index;
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackFinished,
        [&application, &playback_finished, &finished_during_playback](
            quint64,
            bool during_playback) {
            playback_finished = true;
            finished_during_playback = during_playback;
            application.quit();
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&application, &playback_error](const QString&, qint64, quint64) {
            playback_error = true;
            application.quit();
        });

    worker.setMedia(toQString(path), 30.0, 0, 0, 1.0, false, 1.0, false, 0, 0, 7);
    require(media_ready, "Opening valid media did not emit mediaReady.");

    worker.play();
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &application, &QCoreApplication::quit);
    timeout.start(10000);
    application.exec();

    require(!playback_error, "Valid media playback emitted an error.");
    require(playback_finished, "Valid media playback did not finish.");
    require(finished_during_playback,
            "Playback completion was not marked as active playback.");
    require(frame_count >= 2, "Worker playback emitted too few frames.");
    require(last_frame_index >= 0, "Worker playback did not expose the final frame index.");

    const auto snapshot = metrics.takeSnapshotAndReset();
    require(snapshot.payload.count == 0,
            "Source playback created a copied payload instead of sharing the decoded frame.");
    require(snapshot.activation_events == 1 &&
                snapshot.media_open.count == 1 &&
                snapshot.audio_setup.count == 1,
            "Media activation did not measure opening and audio setup.");
    require(snapshot.playback_start_events == 1,
            "Media playback did not record its start transition.");

    worker.setMedia(toQString(path), 30.0, 0, 0, 1.0, false, 1.0, false, 0, 0, 8);
    require(ready_count == 2, "Reactivating media did not emit mediaReady again.");
    metrics.setEnabled(false);
}

void validateSeekCoalescing(QCoreApplication& application, const std::filesystem::path& path) {
    playback::PlaybackWorker worker;
    std::vector<qint64> received_frames;
    bool received_error = false;

    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&received_frames](playback::VideoFramePtr frame, qint64 frame_index, quint64, quint64) {
            require(frame != nullptr, "Coalesced seek emitted an empty frame payload.");
            received_frames.push_back(frame_index);
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&received_error](const QString&, qint64, quint64) {
            received_error = true;
        });

    worker.setMedia(toQString(path), 30.0, 0, 0, 1.0, false, 1.0, false, 0, 0, 20);
    worker.requestSeek(10, 21);
    worker.requestSeek(40, 22);
    worker.requestSeek(90, 23);

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &application, &QCoreApplication::quit);
    timeout.start(1000);
    application.exec();

    require(!received_error, "Coalesced seeks produced a playback error.");
    require(received_frames.size() == 1,
            "Obsolete seek requests were not coalesced.");
    require(received_frames.front() == 90,
            "The worker did not emit the newest seek request.");
}

void validateSegmentRange(
    QCoreApplication& application,
    const std::filesystem::path& path) {
    playback::PlaybackWorker worker;
    std::vector<qint64> received_frames;
    bool received_error = false;
    bool received_ready = false;

    QObject::connect(
        &worker,
        &playback::PlaybackWorker::mediaReady,
        [&received_ready](quint64) {
            received_ready = true;
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&received_frames](playback::VideoFramePtr frame, qint64 frame_index, quint64, quint64) {
            require(frame != nullptr, "A segment seek emitted an empty frame payload.");
            received_frames.push_back(frame_index);
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&received_error](const QString&, qint64, quint64) {
            received_error = true;
        });

    worker.setMedia(toQString(path), 30.0, 30, 3, 1.0, false, 1.0, false, 0, 0, 24);
    require(received_ready, "Opening a ranged media session did not emit mediaReady.");

    worker.requestSeek(0, 25);
    QTimer seek_timeout;
    seek_timeout.setSingleShot(true);
    QObject::connect(
        &seek_timeout,
        &QTimer::timeout,
        &application,
        &QCoreApplication::quit);
    seek_timeout.start(1000);
    application.exec();

    require(!received_error, "Seeking to the first segment frame failed.");
    require(received_frames.size() == 1 && received_frames.front() == 0,
            "The worker did not expose a local segment frame index.");

    worker.stepForward();
    worker.stepForward();
    require(received_frames.size() == 3 && received_frames[1] == 1 &&
                received_frames[2] == 2,
            "Segment frame stepping did not stay within the local range.");

    bool finished = false;
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackFinished,
        [&finished](quint64, bool) {
            finished = true;
        });
    worker.stepForward();
    require(finished, "The worker did not finish at the segment boundary.");
    require(received_frames.size() == 3,
            "The worker emitted a frame beyond the segment boundary.");

    worker.requestSeek(3, 26);
    QTimer invalid_timeout;
    invalid_timeout.setSingleShot(true);
    QObject::connect(
        &invalid_timeout,
        &QTimer::timeout,
        &application,
        &QCoreApplication::quit);
    invalid_timeout.start(1000);
    application.exec();
    require(received_error, "An out-of-range segment seek was accepted.");
}

void validateTransitionPlan() {
    using playback::detail::CompositionFrameRequest;
    using playback::detail::CompositionSessionRef;

    std::array<playback::CompositionLayerSpec, 3> layers;
    layers[0].track_index = 0;
    layers[0].clip_index = 0;
    layers[0].timeline_start_frame = 0;
    layers[0].segment_frame_count = 60;
    layers[1].track_index = 0;
    layers[1].clip_index = 1;
    layers[1].timeline_start_frame = 60;
    layers[1].segment_frame_count = 60;
    layers[2].track_index = 1;
    layers[2].clip_index = 0;
    layers[2].timeline_start_frame = 0;
    layers[2].segment_frame_count = 120;
    const std::array<CompositionSessionRef, 3> sessions{{
        {&layers[0], 0}, {&layers[1], 1}, {&layers[2], 2}}};

    const auto request_for = [](const std::vector<CompositionFrameRequest>& requests,
                                std::size_t index) -> const CompositionFrameRequest* {
        const auto found = std::find_if(
            requests.begin(), requests.end(),
            [index](const CompositionFrameRequest& request) {
                return request.session_index == index;
            });
        return found == requests.end() ? nullptr : &*found;
    };
    const auto at = [&](std::int64_t frame,
                        playback::CompositionTransitionSpec transition,
                        bool include_other = true) {
        std::vector<CompositionFrameRequest> requests;
        for (std::size_t index = 0; index < layers.size(); ++index) {
            if (index == 2 && !include_other) continue;
            const auto& layer = layers[index];
            if (frame >= layer.timeline_start_frame &&
                frame < layer.timeline_start_frame + layer.segment_frame_count) {
                requests.push_back({index, frame - layer.timeline_start_frame, 1.0, true});
            }
        }
        playback::detail::applyTransitionRequests(
            requests, sessions, std::span(&transition, 1), frame);
        return requests;
    };
    const auto close = [](double actual, double expected) {
        return std::abs(actual - expected) < 1e-12;
    };

    const playback::CompositionTransitionSpec dissolve{
        0, 0, 1, 60, 10, timeline::TransitionKind::CrossDissolve};
    const auto before_dissolve = at(59, dissolve);
    require(before_dissolve.size() == 2 &&
                request_for(before_dissolve, 0)->local_frame == 59 &&
                request_for(before_dissolve, 1) == nullptr,
            "Cross dissolve changed requests before its boundary.");
    for (const auto [frame, expected_blend] :
         {std::pair{60, 0.1}, std::pair{64, 0.5}, std::pair{69, 1.0}}) {
        const auto requests = at(frame, dissolve);
        const auto* from = request_for(requests, 0);
        const auto* to = request_for(requests, 1);
        const auto* other = request_for(requests, 2);
        require(requests.size() == 3 && from != nullptr && to != nullptr &&
                    other != nullptr && from->local_frame == 59 &&
                    !from->allow_forward_decode && close(from->opacity_multiplier, 1.0) &&
                    to->local_frame == frame - 60 &&
                    close(to->opacity_multiplier, expected_blend) &&
                    other->local_frame == frame &&
                    close(other->opacity_multiplier, 1.0),
                "Cross dissolve changed its held frame, blend, or unrelated layer.");
    }
    const auto after_dissolve = at(70, dissolve);
    require(after_dissolve.size() == 2 && request_for(after_dissolve, 0) == nullptr &&
                request_for(after_dissolve, 1)->local_frame == 10,
            "Cross dissolve remained active beyond its last frame.");
    auto one_frame_dissolve = dissolve;
    one_frame_dissolve.duration_frames = 1;
    const auto single_dissolve = at(60, one_frame_dissolve);
    require(single_dissolve.size() == 3 &&
                close(request_for(single_dissolve, 1)->opacity_multiplier, 1.0),
            "A one-frame Cross Dissolve did not fully show the incoming clip.");

    const playback::CompositionTransitionSpec fade{
        0, 0, 1, 60, 10, timeline::TransitionKind::FadeToBlack};
    const auto fade_start = at(50, fade);
    require(fade_start.size() == 2 &&
                close(request_for(fade_start, 0)->opacity_multiplier, 0.9),
            "Fade to Black changed the first outgoing opacity.");
    const auto fade_end = at(59, fade, false);
    require(fade_end.empty(),
            "Fade to Black left an outgoing layer at the last pre-cut frame.");
    const auto black = at(60, fade, false);
    require(black.empty(),
            "Fade to Black did not leave the junction frame black.");
    const auto fade_in = at(61, fade);
    require(fade_in.size() == 2 &&
                request_for(fade_in, 1)->local_frame == 1 &&
                close(request_for(fade_in, 1)->opacity_multiplier, 1.0 / 9.0) &&
                request_for(fade_in, 2) != nullptr,
            "Fade to Black changed its incoming frame or another track.");
    const auto fade_last = at(69, fade);
    require(fade_last.size() == 2 &&
                close(request_for(fade_last, 1)->opacity_multiplier, 1.0),
            "Fade to Black did not fully show the incoming clip at its end.");
    const auto fade_after = at(70, fade);
    require(fade_after.size() == 2 &&
                close(request_for(fade_after, 1)->opacity_multiplier, 1.0),
            "Fade to Black remained active after its duration.");
    auto one_frame_fade = fade;
    one_frame_fade.duration_frames = 1;
    require(at(59, one_frame_fade, false).empty() &&
                at(60, one_frame_fade, false).empty(),
            "A one-frame Fade to Black did not suppress both junction sides.");

    auto invalid = dissolve;
    invalid.duration_frames = 0;
    require(at(60, invalid).size() == 2,
            "A zero-duration transition changed visible requests.");
    invalid.duration_frames = -1;
    require(at(60, invalid).size() == 2,
            "A negative-duration transition changed visible requests.");
    invalid = dissolve;
    invalid.to_clip_index = 99;
    require(at(60, invalid).size() == 2,
            "A transition with a missing endpoint changed visible requests.");
    invalid = dissolve;
    invalid.track_index = 99;
    require(at(60, invalid).size() == 2,
            "A transition on another track changed visible requests.");

    const std::array<CompositionSessionRef, 4> reordered{{
        {&layers[1], 3}, {&layers[0], 0},
        {&layers[1], 1}, {&layers[2], 2}}};
    std::vector<CompositionFrameRequest> duplicate_requests{
        {1, 0, 1.0, true}, {2, 60, 1.0, true}};
    playback::detail::applyTransitionRequests(
        duplicate_requests, reordered, std::span(&dissolve, 1), 60);
    require(duplicate_requests.size() == 3 &&
                request_for(duplicate_requests, 0) != nullptr &&
                request_for(duplicate_requests, 1) != nullptr &&
                request_for(duplicate_requests, 3) == nullptr,
            "Transition endpoints no longer use the first worker session.");
}

void validatePlaybackDecodeGapPolicy() {
    using playback::detail::shouldUseSequentialDecode;
    require(shouldUseSequentialDecode(10, 11),
            "A one-frame source gap did not select sequential decoding.");
    require(shouldUseSequentialDecode(10, 18),
            "The eight-frame source-gap boundary did not select sequential decoding.");
    require(!shouldUseSequentialDecode(10, 19),
            "A nine-frame source gap did not select direct seeking.");
    require(!shouldUseSequentialDecode(-1, 7),
            "An uninitialized decoder position selected sequential decoding.");
    require(!shouldUseSequentialDecode(10, 10) &&
                !shouldUseSequentialDecode(10, 9),
            "A same-frame or backward request selected sequential decoding.");
}

void validateCompositionTransitions(
    const std::filesystem::path& path) {
    const auto make_layer = [&path](qint64 timeline_start,
                                    qint64 source_start,
                                    qint64 duration,
                                    qint64 clip_index) {
        playback::CompositionLayerSpec layer;
        layer.source_path = toQString(path);
        layer.frame_rate = 30.0;
        layer.timeline_start_frame = timeline_start;
        layer.source_start_frame = source_start;
        layer.segment_frame_count = duration;
        layer.track_index = 0;
        layer.clip_index = clip_index;
        return layer;
    };
    const auto capture = [](QVector<playback::CompositionLayerSpec> layers,
                            QVector<playback::CompositionTransitionSpec> transitions,
                            qint64 global_frame,
                            qint64 frame_index,
                            quint64 generation) {
        playback::PlaybackWorker worker;
        std::optional<media::VideoFrame> captured;
        bool failed = false;
        QObject::connect(
            &worker,
            &playback::PlaybackWorker::frameReady,
            [&captured](playback::VideoFramePtr frame, qint64, quint64, quint64) {
                if (frame != nullptr) captured = *frame;
            });
        QObject::connect(
            &worker,
            &playback::PlaybackWorker::playbackError,
            [&failed](const QString&, qint64, quint64) {
                failed = true;
            });
        worker.setComposition(std::move(layers), std::move(transitions), generation);
        worker.renderCompositionFrame(global_frame, frame_index, generation);
        require(!failed && captured.has_value(),
                "The worker did not produce a composed transition frame.");
        return *captured;
    };

    const auto outgoing = capture(
        QVector<playback::CompositionLayerSpec>{make_layer(0, 0, 60, 0)},
        {},
        59,
        59,
        100);
    const auto incoming = capture(
        QVector<playback::CompositionLayerSpec>{make_layer(0, 60, 59, 1)},
        {},
        0,
        0,
        101);
    const auto mixed = capture(
        QVector<playback::CompositionLayerSpec>{
            make_layer(0, 0, 60, 0),
            make_layer(60, 60, 59, 1)},
        QVector<playback::CompositionTransitionSpec>{
            playback::CompositionTransitionSpec{
                0, 0, 1, 60, 10, timeline::TransitionKind::CrossDissolve}},
        60,
        60,
        102);

    require(outgoing.width == 1920 && outgoing.height == 1080 &&
                incoming.width == 1920 && mixed.width == 1920,
            "Transition composition did not use the project canvas.");
    const auto pixel = [](const media::VideoFrame& frame) {
        const auto offset = static_cast<std::size_t>(frame.height / 2) *
                static_cast<std::size_t>(frame.stride) +
            static_cast<std::size_t>(frame.width / 2) * 4;
        return std::array<int, 4>{
            frame.rgba_pixels[offset], frame.rgba_pixels[offset + 1],
            frame.rgba_pixels[offset + 2], frame.rgba_pixels[offset + 3]};
    };
    const auto outgoing_pixel = pixel(outgoing);
    const auto incoming_pixel = pixel(incoming);
    const auto mixed_pixel = pixel(mixed);
    for (std::size_t channel = 0; channel < 3; ++channel) {
        const auto expected = static_cast<int>(std::lround(
            outgoing_pixel[channel] * 0.9 + incoming_pixel[channel] * 0.1));
        require(std::abs(mixed_pixel[channel] - expected) <= 2,
                "Cross dissolve did not produce the expected intermediate pixel: " +
                    std::to_string(mixed_pixel[channel]) + " vs " +
                    std::to_string(expected) + " (out=" +
                    std::to_string(outgoing_pixel[channel]) + ", in=" +
                    std::to_string(incoming_pixel[channel]) + ")");
    }

    const auto black = capture(
        QVector<playback::CompositionLayerSpec>{
            make_layer(0, 0, 60, 0),
            make_layer(60, 60, 59, 1)},
        QVector<playback::CompositionTransitionSpec>{
            playback::CompositionTransitionSpec{
                0, 0, 1, 60, 10, timeline::TransitionKind::FadeToBlack}},
        60,
        60,
        103);
    const auto black_pixel = pixel(black);
    require(black_pixel[0] == 0 && black_pixel[1] == 0 &&
                black_pixel[2] == 0 && black_pixel[3] == 255,
            "Fade to black did not produce an opaque black junction frame.");
}

void validateCompositionSourceRateMapping(
    const std::filesystem::path& path) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();
    auto reference = media::VideoPlaybackSession::open(path);
    playback::PlaybackWorker worker;
    bool media_ready = false;
    bool playback_error = false;
    media::VideoFramePtr actual;
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&actual](playback::VideoFramePtr frame, qint64, quint64, quint64) {
            actual = std::move(frame);
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::mediaReady,
        [&media_ready](quint64) { media_ready = true; });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&playback_error](const QString&, qint64, quint64) {
            playback_error = true;
        });

    // Standalone playback uses the source rate until a valid composition
    // snapshot supplies its project rate.
    worker.setMedia(toQString(path), 60.0, 10, 60,
        1.0, false, 1.0, false, 0, 0, 402);
    require(media_ready && !playback_error,
            "Preparing standalone playback before composition failed.");
    auto rate_snapshot = metrics.takeSnapshotAndReset();
    require(rate_snapshot.target_frame_rate_milli == 60'000 &&
                rate_snapshot.timeline_frame_rate_numerator == 0 &&
                rate_snapshot.timeline_frame_rate_denominator == 0,
            "Standalone playback did not report its source rate without a Timeline rate.");

    constexpr std::array<double, 3> source_rates{24.0, 30.0, 60.0};
    constexpr std::int64_t timeline_frame_rate = 30;
    constexpr std::int64_t local_frame = 15;
    QVector<playback::CompositionLayerSpec> layers;
    for (std::size_t index = 0; index < source_rates.size(); ++index) {
        playback::CompositionLayerSpec layer;
        layer.source_path = toQString(path);
        layer.frame_rate = source_rates[index];
        layer.timeline_frame_rate = {timeline_frame_rate, 1};
        layer.timeline_start_frame = static_cast<std::int64_t>(index) * 60;
        layer.source_start_frame = 10;
        layer.source_duration_frames = 1'000;
        layer.segment_frame_count = 60;
        layer.track_index = 0;
        layer.clip_index = static_cast<std::int64_t>(index);
        layers.push_back(std::move(layer));
    }
    worker.setComposition(std::move(layers), {}, 403);
    rate_snapshot = metrics.takeSnapshotAndReset();
    require(rate_snapshot.target_frame_rate_milli == 30'000 &&
                rate_snapshot.timeline_frame_rate_numerator == 30 &&
                rate_snapshot.timeline_frame_rate_denominator == 1,
            "Preparing a composition did not replace the standalone clock with the project Timeline rate.");

    constexpr std::array<std::int64_t, 3> expected_source_offsets{12, 15, 30};
    for (std::size_t index = 0; index < source_rates.size(); ++index) {
        const auto clip_index = static_cast<std::int64_t>(index);
        const auto generation = static_cast<quint64>(404 + index);
        media_ready = false;
        worker.setMedia(
            toQString(path), source_rates[index], 10, 60,
            1.0, false, 1.0, false, 0, clip_index, generation);
        worker.setActiveCompositionClip(0, clip_index);
        require(media_ready && !playback_error,
                "Activating a source with a different FPS failed after composition preparation.");
        rate_snapshot = metrics.takeSnapshotAndReset();
        require(rate_snapshot.target_frame_rate_milli == 30'000 &&
                    rate_snapshot.timeline_frame_rate_numerator == 30 &&
                    rate_snapshot.timeline_frame_rate_denominator == 1,
                "Changing the active source replaced the composed Timeline clock rate.");

        const auto expected_source_frame = 10 + expected_source_offsets[index];
        const auto source_frame = reference->decode_frame_at(expected_source_frame);
        require(source_frame.has_value() && *source_frame != nullptr,
                "The source-rate mapping fixture did not contain its expected source frame.");
        const std::vector<rendering::CompositionLayer> reference_layers{{
            source_frame->get(), timeline::Transform2D{}, {}}};
        const auto expected = rendering::FrameCompositor::compose(
            1920, 1080, reference_layers);
        require(expected.has_value(),
                "The source-rate mapping reference frame could not be composed.");

        actual.reset();
        const auto global_frame = clip_index * 60 + local_frame;
        worker.renderCompositionFrame(global_frame, local_frame, generation);
        require(actual != nullptr && actual->rgba_pixels == expected->rgba_pixels,
                "Composition preview did not map the Timeline frame through the active source rate and in-point.");
    }

    metrics.setEnabled(false);
    metrics.reset();
}

void validateCompositionPlayback(
    QCoreApplication& application,
    const std::filesystem::path& path) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    playback::PlaybackWorker worker;
    bool playback_finished = false;
    bool playback_error = false;
    bool empty_frame = false;
    int frame_count = 0;

    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&frame_count, &empty_frame, &metrics](
            playback::VideoFramePtr frame,
            qint64,
            quint64,
            quint64) {
            if (frame == nullptr) {
                empty_frame = true;
                return;
            }
            ++frame_count;
            metrics.recordCpuPresentedFrame();
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackFinished,
        [&application, &playback_finished](quint64, bool during_playback) {
            playback_finished = during_playback;
            application.quit();
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&application, &playback_error](const QString&, qint64, quint64) {
            playback_error = true;
            application.quit();
        });

    playback::CompositionLayerSpec layer;
    layer.source_path = toQString(path);
    layer.frame_rate = 30.0;
    layer.timeline_start_frame = 0;
    layer.source_start_frame = 0;
    layer.segment_frame_count = 3;
    layer.track_index = 0;
    layer.clip_index = 0;
    layer.transform.opacity = 0.5;

    worker.setActiveCompositionClip(0, 0);
    worker.setComposition(
        QVector<playback::CompositionLayerSpec>{layer},
        {},
        200);
    worker.play();

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &application,
        &QCoreApplication::quit);
    timeout.start(2000);
    application.exec();

    const auto snapshot = metrics.takeSnapshotAndReset();
    metrics.setEnabled(false);
    require(!playback_error,
            "Composition playback without a selected media source emitted an error.");
    require(!empty_frame,
            "Composition playback emitted an empty frame.");
    require(playback_finished,
            "Composition playback without a selected media source did not finish.");
    require(frame_count >= 1,
            "Composition playback without a selected media source emitted no frames.");
    require(snapshot.media_open.count == 1 &&
                snapshot.composition_setup.count == 1,
            "Composition activation did not measure media opening and setup.");
    require(snapshot.playback_start_events == 1 &&
                snapshot.playback_start_to_presentation.count == 1,
            "Composition playback start-to-presentation timing was not recorded.");
    require(snapshot.blend_lookup_composition_frames > 0 &&
                snapshot.blend_lookup_layer_observations > 0 &&
                snapshot.blend_lookup_active_layer_observations > 0 &&
                snapshot.blend_lookup_table_builds > 0 &&
                snapshot.blend_lookup_pixel_count > 0 &&
                snapshot.blend_lookup_active_block_count > 0,
            "Partial-opacity Timeline playback did not contribute blend lookup interval metrics.");
}

void validateCompositionReuseAcrossMediaActivation(
    QCoreApplication& application,
    const std::filesystem::path& path) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    playback::PlaybackWorker worker;
    bool received_frame = false;
    bool playback_finished = false;
    bool playback_error = false;
    std::size_t received_frame_count = 0;
    bool start_playback_after_seek = false;
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&worker, &received_frame, &received_frame_count,
         &start_playback_after_seek](playback::VideoFramePtr frame, qint64, quint64, quint64) {
            if (frame != nullptr) {
                received_frame = true;
                ++received_frame_count;
                if (start_playback_after_seek) {
                    start_playback_after_seek = false;
                    worker.play();
                }
            }
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackFinished,
        [&application, &playback_finished](quint64, bool during_playback) {
            playback_finished = during_playback;
            if (during_playback) application.quit();
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&application, &playback_error](const QString&, qint64, quint64) {
            playback_error = true;
            application.quit();
        });

    QVector<playback::CompositionLayerSpec> layers;
    for (qint64 clip_index = 0; clip_index < 2; ++clip_index) {
        playback::CompositionLayerSpec layer;
        layer.source_path = toQString(path);
        layer.frame_rate = 30.0;
        layer.timeline_start_frame = clip_index * 30;
        layer.source_start_frame = 0;
        layer.segment_frame_count = 30;
        layer.track_index = 0;
        layer.clip_index = clip_index;
        layers.push_back(std::move(layer));
    }
    worker.setActiveCompositionClip(0, 0);
    worker.setComposition(
        std::move(layers),
        {},
        810);

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &application,
        &QCoreApplication::quit);
    const auto playActivatedClip = [&](qint64 clip_index, quint64 generation) {
        received_frame = false;
        received_frame_count = 0;
        playback_finished = false;
        playback_error = false;
        worker.setActiveCompositionClip(0, clip_index);
        worker.setMedia(
            toQString(path), 30.0, 0, 30,
            1.0, false, 1.0, false,
            0, clip_index, generation);
        start_playback_after_seek = true;
        worker.requestSeek(0, generation);
        timeout.start(5000);
        application.exec();
        timeout.stop();

        require(!playback_error,
                "Playback after activating a prepared composition clip reported an error.");
        require(playback_finished,
                "Playback after activating a prepared composition clip did not finish cleanly.");
        require(received_frame && received_frame_count >= 2,
                "Playback after activating a prepared composition clip emitted too few frames.");
    };

    playActivatedClip(0, 811);
    playActivatedClip(1, 812);

    const auto snapshot = metrics.takeSnapshotAndReset();
    metrics.setEnabled(false);
    require(snapshot.composition_setup.count == 1 && snapshot.media_open.count == 2 &&
                snapshot.composed_frames > 0,
            "Media activation reopened or disabled the prepared composition sessions.");
}

void validateCompositionSeekMetrics(
    QCoreApplication& application,
    const std::filesystem::path& path) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    playback::PlaybackWorker worker;
    bool received_frame = false;
    bool playback_error = false;
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&received_frame, &metrics](playback::VideoFramePtr frame, qint64, quint64, quint64) {
            require(frame != nullptr, "Composition seek emitted an empty frame.");
            received_frame = true;
            metrics.recordCpuPresentedFrame();
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&application, &playback_error](const QString&, qint64, quint64) {
            playback_error = true;
            application.quit();
        });

    playback::CompositionLayerSpec layer;
    layer.source_path = toQString(path);
    layer.frame_rate = 30.0;
    layer.timeline_start_frame = 0;
    layer.source_start_frame = 0;
    layer.segment_frame_count = 3;
    layer.track_index = 0;
    layer.clip_index = 0;

    worker.setActiveCompositionClip(0, 0);
    worker.setComposition(
        QVector<playback::CompositionLayerSpec>{layer},
        {},
        210);
    worker.requestSeek(1, 211);

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &application,
        &QCoreApplication::quit);
    timeout.start(2000);
    application.exec();

    const auto snapshot = metrics.takeSnapshotAndReset();
    metrics.setEnabled(false);
    require(!playback_error && received_frame,
            "Composition seek did not produce a valid frame.");
    require(snapshot.seek_requests == 1 && snapshot.seek_operations == 1,
            "Composition seek request and operation counts are incorrect.");
    require(snapshot.seek_to_presentation.count == 1,
            "Composition seek-to-presentation timing was not recorded.");
}

void validateDirectDecodeCatchup(
    QCoreApplication& application,
    const std::filesystem::path& path) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    playback::PlaybackWorker worker;
    std::vector<qint64> frame_indices;
    bool playback_finished = false;
    bool playback_error = false;
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&frame_indices](playback::VideoFramePtr frame, qint64 frame_index, quint64, quint64) {
            require(frame != nullptr, "Direct catch-up emitted an empty frame.");
            frame_indices.push_back(frame_index);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackFinished,
        [&application, &playback_finished](quint64, bool during_playback) {
            playback_finished = during_playback;
            application.quit();
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&application, &playback_error](const QString&, qint64, quint64) {
            playback_error = true;
            application.quit();
        });

    worker.setMedia(toQString(path), 30.0, 0, 30, 1.0, false, 1.0, false, 0, 0, 501);
    worker.play();

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &application,
        &QCoreApplication::quit);
    timeout.start(6000);
    application.exec();

    const auto snapshot = metrics.takeSnapshotAndReset();
    metrics.setEnabled(false);
    require(!playback_error && playback_finished,
            "Delayed direct playback did not finish cleanly.");
    require(frame_indices.size() >= 2,
            "Delayed direct playback emitted too few frames.");
    require(snapshot.pacing_skipped_frames > 0,
            "Delayed direct playback did not record skipped frames.");
    require(snapshot.decode_discarded_frames > 0,
            "Delayed direct playback did not discard intermediate decoder frames.");
    require(snapshot.pixel_conversion.count <= frame_indices.size() + 1,
            "Direct catch-up converted frames that were not published.");
    for (std::size_t index = 1; index < frame_indices.size(); ++index) {
        require(frame_indices[index] > frame_indices[index - 1],
                "Direct catch-up emitted non-monotonic frame indices.");
    }
}

void validateCompositionDecodeCatchup(
    QCoreApplication& application,
    const std::filesystem::path& path) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    playback::PlaybackWorker worker;
    std::vector<qint64> frame_indices;
    bool playback_finished = false;
    bool playback_error = false;
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&frame_indices](playback::VideoFramePtr frame, qint64 frame_index, quint64, quint64) {
            require(frame != nullptr, "Composition catch-up emitted an empty frame.");
            frame_indices.push_back(frame_index);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackFinished,
        [&application, &playback_finished](quint64, bool during_playback) {
            playback_finished = during_playback;
            application.quit();
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&application, &playback_error](const QString&, qint64, quint64) {
            playback_error = true;
            application.quit();
        });

    playback::CompositionLayerSpec layer;
    layer.source_path = toQString(path);
    layer.frame_rate = 30.0;
    layer.timeline_start_frame = 0;
    layer.source_start_frame = 0;
    layer.segment_frame_count = 30;
    layer.track_index = 0;
    layer.clip_index = 0;
    worker.setActiveCompositionClip(0, 0);
    worker.setComposition(
        QVector<playback::CompositionLayerSpec>{layer},
        {},
        502);
    worker.play();

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &application,
        &QCoreApplication::quit);
    timeout.start(6000);
    application.exec();

    const auto snapshot = metrics.takeSnapshotAndReset();
    metrics.setEnabled(false);
    require(!playback_error && playback_finished,
            "Delayed video composition playback did not finish cleanly; frames=" +
                std::to_string(frame_indices.size()) +
                ", ticks=" + std::to_string(snapshot.playback_ticks) +
                ", skipped=" + std::to_string(snapshot.pacing_skipped_frames) +
                ", discarded=" + std::to_string(snapshot.decode_discarded_frames));
    require(frame_indices.size() >= 2,
            "Delayed composition playback emitted too few frames.");
    require(snapshot.pacing_skipped_frames > 0,
            "Delayed composition playback did not record skipped frames.");
    require(snapshot.decode_discarded_frames > 0,
            "Delayed composition playback did not discard intermediate decoder frames.");
    require(snapshot.pixel_conversion.count <= frame_indices.size() + 4,
            "Composition catch-up converted frames that were not published.");
    for (std::size_t index = 1; index < frame_indices.size(); ++index) {
        require(frame_indices[index] > frame_indices[index - 1],
                "Composition catch-up emitted non-monotonic frame indices.");
    }
}

void validateCompositionCaching() {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    playback::PlaybackWorker worker;
    std::vector<playback::VideoFramePtr> frames;
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&frames](playback::VideoFramePtr frame, qint64, quint64, quint64) {
            frames.push_back(std::move(frame));
        });

    playback::CompositionLayerSpec text_layer;
    text_layer.frame_rate = 30.0;
    text_layer.timeline_start_frame = 0;
    text_layer.source_start_frame = 0;
    text_layer.segment_frame_count = 3;
    text_layer.track_index = 0;
    text_layer.clip_index = 0;
    text_layer.kind = timeline::ClipKind::Text;
    text_layer.transform.scale = 0.1;
    text_layer.text.content = "Cached title";
    text_layer.text.font_size_pixels = 32.0;

    worker.setActiveCompositionClip(0, 0);
    worker.setComposition(
        QVector<playback::CompositionLayerSpec>{text_layer},
        {},
        300);
    worker.renderCompositionFrame(0, 0, 300);
    worker.renderCompositionFrame(1, 1, 300);
    worker.renderCompositionFrame(1, 1, 300);

    const auto snapshot = metrics.takeSnapshotAndReset();
    require(frames.size() == 3,
            "Composition caching did not emit all requested frames.");
    require(frames[0] != nullptr && frames[1] != nullptr && frames[2] != nullptr,
            "Composition caching emitted an empty frame.");
    require(frames[1] == frames[2],
            "The repeated composition frame did not reuse its payload.");
    require(snapshot.text_cache_hits == 1,
            "The text layer was rasterized again instead of using its cache.");
    require(snapshot.text_composition_fast_path_hits == 2,
            "The text composition fast path was not used for both composed frames.");
    require(snapshot.composition_cache_hits == 1,
            "The repeated composition frame did not hit the composition cache.");
    require(snapshot.text_rasterization.count == 1,
            "Text rasterization was not measured exactly once.");
    require(snapshot.text_rasterization.maximum_nanoseconds > 0,
            "Text rasterization timing did not record an elapsed duration.");

    worker.setComposition(
        QVector<playback::CompositionLayerSpec>{text_layer},
        {},
        301);
    worker.renderCompositionFrame(1, 1, 301);
    const auto invalidation = metrics.takeSnapshotAndReset();
    metrics.setEnabled(false);
    require(frames.size() == 4 && frames[2] != frames[3],
            "Changing the composition did not invalidate the composed frame cache.");
    require(invalidation.composition_cache_hits == 0,
            "A new composition unexpectedly reused the previous composition cache.");
    require(invalidation.text_composition_fast_path_hits == 1,
            "The new composition did not rebuild the text composition fast path state.");
}

void validateCompositionPreviewQuality() {
    playback::PlaybackWorker worker;
    std::vector<std::pair<int, int>> emitted_dimensions;
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&emitted_dimensions](playback::VideoFramePtr frame, qint64, quint64, quint64) {
            if (frame != nullptr) {
                emitted_dimensions.emplace_back(frame->width, frame->height);
            }
        });

    auto still = std::make_shared<media::VideoFrame>();
    still->width = 1;
    still->height = 1;
    still->stride = 4;
    still->rgba_pixels = {40, 80, 120, 255};
    playback::CompositionLayerSpec layer;
    layer.kind = timeline::ClipKind::Image;
    layer.segment_frame_count = 1;
    layer.track_index = 0;
    layer.clip_index = 0;
    layer.still_frame = still;
    layer.transform.scale = 0.0001;

    worker.setActiveCompositionClip(0, 0);
    worker.setComposition({layer}, {}, 71);
    worker.renderCompositionFrame(0, 0, 71);
    worker.setPreviewQuality(playback::PreviewQuality::Half);
    worker.renderCompositionFrame(0, 0, 71);
    worker.setPreviewQuality(playback::PreviewQuality::Quarter);
    worker.renderCompositionFrame(0, 0, 71);
    worker.setPreviewQuality(playback::PreviewQuality::Full);
    worker.renderCompositionFrame(0, 0, 71);

    require(emitted_dimensions == std::vector<std::pair<int, int>>{
                {1920, 1080}, {960, 540}, {480, 270}, {1920, 1080}},
            "Changing Playback Preview Quality did not recompute the current frame at the selected dimensions.");
}

void validateStaticImageComposition() {
    playback::PlaybackWorker worker;
    std::vector<playback::VideoFramePtr> frames;
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&frames](playback::VideoFramePtr frame, qint64, quint64, quint64) {
            frames.push_back(std::move(frame));
        });

    auto still = std::make_shared<const media::VideoFrame>(media::VideoFrame{
        2, 1, 8, std::vector<std::uint8_t>{10, 20, 30, 255, 40, 50, 60, 128}});
    playback::CompositionLayerSpec image_layer;
    image_layer.frame_rate = 30.0;
    image_layer.timeline_start_frame = 0;
    image_layer.segment_frame_count = 150;
    image_layer.track_index = 0;
    image_layer.clip_index = 0;
    image_layer.kind = timeline::ClipKind::Image;
    image_layer.still_frame = still;

    worker.setActiveCompositionClip(0, 0);
    worker.setComposition(
        QVector<playback::CompositionLayerSpec>{image_layer},
        {},
        500);
    worker.renderCompositionFrame(0, 0, 500);
    worker.renderCompositionFrame(1, 1, 500);

    require(frames.size() == 2 && frames[0] != nullptr && frames[1] != nullptr,
            "Static image composition did not emit both frames.");
    require(frames[0]->width == 1920 && frames[0]->height == 1080 &&
                frames[0]->rgba_pixels == frames[1]->rgba_pixels,
            "Static image composition did not reuse the same image pixels.");
}

void validatePlaybackFrameMailbox() {
    playback::PlaybackFrameMailbox mailbox;
    auto first = std::make_shared<const media::VideoFrame>(media::VideoFrame{
        1, 1, 4, std::vector<std::uint8_t>{1, 2, 3, 4}});
    auto second = std::make_shared<const media::VideoFrame>(media::VideoFrame{
        1, 1, 4, std::vector<std::uint8_t>{5, 6, 7, 8}});

    require(
        !mailbox.publish(playback::PlaybackFramePacket{first, 1, 10, 101}).has_value(),
        "The first mailbox packet was incorrectly reported as replaced.");
    require(mailbox.acquireDispatch(),
            "The mailbox did not reserve its first UI dispatch.");
    require(
        mailbox.publish(playback::PlaybackFramePacket{second, 2, 11, 102}).has_value(),
        "The mailbox did not replace an older pending packet.");

    const auto packet = mailbox.take();
    require(packet.has_value() && packet->frame == second &&
                packet->frame_index == 2 && packet->generation == 11 &&
                packet->delivery_trace_id == 102,
            "The mailbox did not retain the newest shared frame packet.");
    require(!mailbox.finishDispatch(),
            "The mailbox kept a dispatch scheduled after draining its packet.");
    require(mailbox.acquireDispatch(),
            "The mailbox could not reserve a subsequent dispatch.");
    require(!mailbox.publish(playback::PlaybackFramePacket{second, 3, 12, 103})
                 .has_value(),
            "The mailbox unexpectedly replaced a packet before clear.");
    const auto cleared = mailbox.clearPending();
    require(cleared.has_value() && cleared->delivery_trace_id == 103,
            "Clearing the mailbox did not return the discarded trace packet.");
    require(!mailbox.finishDispatch(),
            "Clearing the mailbox did not remove the pending packet.");
}

void validateCompositionPacing(QCoreApplication& application) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    playback::PlaybackWorker worker;
    std::vector<qint64> frame_indices;
    bool playback_finished = false;
    bool playback_error = false;

    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&frame_indices](playback::VideoFramePtr frame, qint64 frame_index, quint64, quint64) {
            require(frame != nullptr, "Pacing emitted an empty composition frame.");
            frame_indices.push_back(frame_index);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackFinished,
        [&application, &playback_finished](quint64, bool during_playback) {
            playback_finished = during_playback;
            application.quit();
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&application, &playback_error](const QString&, qint64, quint64) {
            playback_error = true;
            application.quit();
        });

    playback::CompositionLayerSpec layer;
    layer.kind = timeline::ClipKind::Text;
    layer.frame_rate = 30.0;
    layer.timeline_start_frame = 0;
    layer.segment_frame_count = 6;
    layer.track_index = 0;
    layer.clip_index = 0;
    layer.text.content = "Pacing";

    worker.setActiveCompositionClip(0, 0);
    worker.setComposition(
        QVector<playback::CompositionLayerSpec>{layer},
        {},
        401);
    worker.play();

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &application,
        &QCoreApplication::quit);
    timeout.start(2000);
    application.exec();

    const auto snapshot = metrics.takeSnapshotAndReset();
    metrics.setEnabled(false);
    require(!playback_error && playback_finished,
            "Text-only delayed composition playback did not finish cleanly.");
    require(frame_indices.size() >= 2,
            "Delayed composition playback emitted too few frames.");
    require(snapshot.playback_ticks >= frame_indices.size(),
            "Playback tick metrics did not cover composition playback.");
    require(snapshot.pacing_skipped_frames > 0,
            "Delayed composition playback did not record skipped frames.");
    for (std::size_t index = 1; index < frame_indices.size(); ++index) {
        require(frame_indices[index] > frame_indices[index - 1],
                "Pacing emitted non-monotonic frame indices.");
    }
}

void validateNormalCompositionPacing(QCoreApplication& application) {
    auto& metrics = rendering::PreviewPerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();

    playback::PlaybackWorker worker;
    std::vector<qint64> frame_indices;
    bool playback_finished = false;
    bool playback_error = false;

    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&frame_indices](playback::VideoFramePtr frame, qint64 frame_index, quint64, quint64) {
            require(frame != nullptr, "Normal composition emitted an empty frame.");
            frame_indices.push_back(frame_index);
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackFinished,
        [&application, &playback_finished](quint64, bool during_playback) {
            playback_finished = during_playback;
            application.quit();
        });
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::playbackError,
        [&application, &playback_error](const QString&, qint64, quint64) {
            playback_error = true;
            application.quit();
        });

    playback::CompositionLayerSpec layer;
    layer.kind = timeline::ClipKind::Text;
    layer.frame_rate = 24.0;
    layer.timeline_start_frame = 0;
    layer.segment_frame_count = 48;
    layer.track_index = 0;
    layer.clip_index = 0;
    layer.text.content = "Normal pacing";

    worker.setActiveCompositionClip(0, 0);
    worker.setComposition(
        QVector<playback::CompositionLayerSpec>{layer},
        {},
        402);
    worker.play();

    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(
        &timeout,
        &QTimer::timeout,
        &application,
        &QCoreApplication::quit);
    timeout.start(5000);
    application.exec();

    const auto snapshot = metrics.takeSnapshotAndReset();
    metrics.setEnabled(false);
    require(!playback_error && playback_finished,
            "Normal 24 fps composition playback did not finish cleanly.");
    require(frame_indices.size() >= 42,
            "Normal 24 fps composition playback lost too many frames.");
    require(snapshot.pacing_skipped_frames <= 4,
            "Normal 24 fps composition playback accumulated systematic skips.");
    require(snapshot.playback_ticks >= frame_indices.size(),
            "Normal playback tick metrics did not cover emitted frames.");
    for (std::size_t index = 1; index < frame_indices.size(); ++index) {
        require(frame_indices[index] > frame_indices[index - 1],
                "Normal pacing emitted non-monotonic frame indices.");
    }
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);

    try {
        validateTransitionPlan();
        validatePlaybackDecodeGapPolicy();
        validateWorkerDiagnostics(application);
        validateMissingMedia(application);
        validateSeekWithoutMedia(application);
        validatePlaybackFrameMailbox();
        validateCompositionPreviewQuality();
        validateCompositionPacing(application);
        validateNormalCompositionPacing(application);
        validateCompositionCaching();
        validateStaticImageComposition();
        if (argc == 2) {
            validateReference(application, std::filesystem::path(argv[1]));
            validateSeekCoalescing(application, std::filesystem::path(argv[1]));
            validateSegmentRange(application, std::filesystem::path(argv[1]));
            validateCompositionTransitions(std::filesystem::path(argv[1]));
            validateCompositionSourceRateMapping(std::filesystem::path(argv[1]));
            validateCompositionPlayback(application, std::filesystem::path(argv[1]));
            validateCompositionReuseAcrossMediaActivation(
                application, std::filesystem::path(argv[1]));
            validateCompositionSeekMetrics(application, std::filesystem::path(argv[1]));
            validateCompositionDecodeCatchup(application, std::filesystem::path(argv[1]));
            validateDirectDecodeCatchup(application, std::filesystem::path(argv[1]));
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
