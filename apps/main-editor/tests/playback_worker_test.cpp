#include "playback/playback_worker.h"

#include <QCoreApplication>
#include <QTimer>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

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

    worker.setMedia(toQString(path), 30.0, 7);
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

    worker.setMedia(toQString(path), 30.0, 8);
    require(ready_count == 2, "Reactivating media did not emit mediaReady again.");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);

    try {
        validateMissingMedia(application);
        if (argc == 2) {
            validateReference(application, std::filesystem::path(argv[1]));
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }

    return 0;
}
