#include "playback/playback_worker.h"

#include <QCoreApplication>
#include <QTimer>

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
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

void validateReference(QCoreApplication& application, const std::filesystem::path& path) {
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

    worker.setMedia(toQString(path), 30.0, 0, 0, 1.0, false, 1.0, false, 0, 0, 8);
    require(ready_count == 2, "Reactivating media did not emit mediaReady again.");
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

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);

    try {
        validateMissingMedia(application);
        if (argc == 2) {
            validateReference(application, std::filesystem::path(argv[1]));
            validateSeekCoalescing(application, std::filesystem::path(argv[1]));
            validateSegmentRange(application, std::filesystem::path(argv[1]));
            validateCompositionTransitions(std::filesystem::path(argv[1]));
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
