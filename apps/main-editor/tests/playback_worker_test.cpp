#include "playback/playback_worker.h"
#include "playback/playback_frame_mailbox.h"
#include "logging/logger.h"
#include "rendering/preview_performance_metrics.h"

#include <QGuiApplication>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

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
        [&received_frames](playback::VideoFramePtr frame, qint64 frame_index, quint64) {
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
        [&received_frames](playback::VideoFramePtr frame, qint64 frame_index, quint64) {
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
            [&captured](playback::VideoFramePtr frame, qint64, quint64) {
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
        [&received_frame, &metrics](playback::VideoFramePtr frame, qint64, quint64) {
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
        [&frame_indices](playback::VideoFramePtr frame, qint64 frame_index, quint64) {
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
        [&frame_indices](playback::VideoFramePtr frame, qint64 frame_index, quint64) {
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
        [&frames](playback::VideoFramePtr frame, qint64, quint64) {
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

void validatePlaybackFrameMailbox() {
    playback::PlaybackFrameMailbox mailbox;
    auto first = std::make_shared<const media::VideoFrame>(media::VideoFrame{
        1, 1, 4, std::vector<std::uint8_t>{1, 2, 3, 4}});
    auto second = std::make_shared<const media::VideoFrame>(media::VideoFrame{
        1, 1, 4, std::vector<std::uint8_t>{5, 6, 7, 8}});

    require(
        !mailbox.publish(playback::PlaybackFramePacket{first, 1, 10}),
        "The first mailbox packet was incorrectly reported as replaced.");
    require(mailbox.acquireDispatch(),
            "The mailbox did not reserve its first UI dispatch.");
    require(
        mailbox.publish(playback::PlaybackFramePacket{second, 2, 11}),
        "The mailbox did not replace an older pending packet.");

    const auto packet = mailbox.take();
    require(packet.has_value() && packet->frame == second &&
                packet->frame_index == 2 && packet->generation == 11,
            "The mailbox did not retain the newest shared frame packet.");
    require(!mailbox.finishDispatch(),
            "The mailbox kept a dispatch scheduled after draining its packet.");
    require(mailbox.acquireDispatch(),
            "The mailbox could not reserve a subsequent dispatch.");
    mailbox.clearPending();
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
        [&frame_indices](playback::VideoFramePtr frame, qint64 frame_index, quint64) {
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
        [&frame_indices](playback::VideoFramePtr frame, qint64 frame_index, quint64) {
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
        validateWorkerDiagnostics(application);
        validateMissingMedia(application);
        validateSeekWithoutMedia(application);
        validatePlaybackFrameMailbox();
        validateCompositionPacing(application);
        validateNormalCompositionPacing(application);
        validateCompositionCaching();
        if (argc == 2) {
            validateReference(application, std::filesystem::path(argv[1]));
            validateSeekCoalescing(application, std::filesystem::path(argv[1]));
            validateSegmentRange(application, std::filesystem::path(argv[1]));
            validateCompositionTransitions(std::filesystem::path(argv[1]));
            validateCompositionPlayback(application, std::filesystem::path(argv[1]));
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
