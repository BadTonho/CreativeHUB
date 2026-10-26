#include "voice_isolation_coordinator.h"
#include "logging/logger.h"
#include <QPointer>
#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioFormat>
#endif

namespace application {
VoiceIsolationCoordinator::VoiceIsolationCoordinator(QObject* parent) : QObject(parent) {
    worker_.setMaxThreadCount(1); debounce_.setSingleShot(true); debounce_.setInterval(300);
    connect(&debounce_, &QTimer::timeout, this, &VoiceIsolationCoordinator::start);
    connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] {
        cancel(); allStates("Processing"); emit contextChanged({}); debounce_.start();
    });
}
VoiceIsolationCoordinator::~VoiceIsolationCoordinator() { cancel(); worker_.waitForDone(); }
void VoiceIsolationCoordinator::allStates(const QString& state) {
    for (const auto& track : document_.timeline_tracks) {
        emit stateChanged(false, track.track_id, track.voice_isolation.enabled ? state : "Off", 0);
        for (const auto& clip : track.clips)
            emit stateChanged(true, clip.clip_id, clip.voice_isolation.enabled ? state : "Off", 0);
    }
}
void VoiceIsolationCoordinator::cancel() {
    debounce_.stop(); ++serial_;
    if (canceled_) canceled_->store(true);
    allStates("Cancelled");
}
void VoiceIsolationCoordinator::request(project::ProjectDocument document, std::uint64_t generation) {
#if defined(CREATIVE_SUITE_HAS_QT_MULTIMEDIA)
    if (qEnvironmentVariable("CREATIVE_SUITE_DISABLE_AUDIO_OUTPUT") != "1") {
        const auto preferred = QMediaDevices::defaultAudioOutput().preferredFormat().channelCount();
        if (preferred > 0 && preferred <= 64) channels_ = preferred;
    }
#endif
    const auto key = media::voiceIsolationRequestKey(document, channels_, false);
    if (key == recipe_ && generation == generation_) { document_ = std::move(document); return; }
    cancel(); recipe_ = key; generation_ = generation; document_ = std::move(document); force_.clear();
    const auto files = watcher_.files(); if (!files.empty()) watcher_.removePaths(files);
    QStringList sources;
    for (const auto& track : document_.timeline_tracks) for (const auto& clip : track.clips)
        if ((track.voice_isolation.enabled || clip.voice_isolation.enabled) && !clip.audio_extracted &&
            (clip.kind == timeline::ClipKind::Audio || clip.kind == timeline::ClipKind::Video))
            sources.push_back(QString::fromStdU16String(clip.source_path.u16string()));
    sources.removeDuplicates(); if (!sources.empty()) watcher_.addPaths(sources);
    emit contextChanged({}); allStates("Processing");
    if (media::hasVoiceIsolation(document_)) debounce_.start();
    else worker_.start([] {
        try { media::pruneVoiceIsolationCache(); }
        catch (const std::exception& error) { logging::Logger::instance().log(logging::Level::Error, "voice_isolation", "prune_cache", error.what()); }
    });
}
void VoiceIsolationCoordinator::regenerate(media::VoiceIsolationTarget target) {
    cancel(); force_.insert(target); allStates("Processing"); emit contextChanged({}); debounce_.start();
}
void VoiceIsolationCoordinator::start() {
    canceled_ = std::make_shared<std::atomic_bool>(false);
    const auto cancellation = canceled_; const auto serial = serial_;
    const auto channels = channels_;
    const auto snapshot = document_; const auto force = std::exchange(force_, {});
    QPointer<VoiceIsolationCoordinator> target(this);
    // The coordinator waits for the worker at destruction, so invoking onto
    // this receiver is safe; queued callbacks are removed by QObject teardown.
    worker_.start([this, target, cancellation, serial, snapshot, force, channels] {
        std::shared_ptr<const media::VoiceIsolationContext> context;
        QString state("Ready");
        try {
            context = media::prepareVoiceIsolation(snapshot, [cancellation] { return cancellation->load(); },
                [this, serial](auto item, int value) {
                    QMetaObject::invokeMethod(this, [this, serial, item, value] {
                        if (serial == serial_) emit stateChanged(item.first, item.second, "Processing", value);
                    }, Qt::QueuedConnection);
                }, force, channels);
        } catch (const media::VoiceIsolationCancelled&) { state = "Cancelled"; }
        catch (const std::exception& error) {
            state = cancellation->load() ? "Cancelled" : "Error";
            if (!cancellation->load()) logging::Logger::instance().log(logging::Level::Error, "voice_isolation", "background_generation", error.what());
        }
        QMetaObject::invokeMethod(this, [this, target, cancellation, serial, context = std::move(context), state] {
            if (!target || cancellation->load() || serial != serial_) return;
            allStates(state);
            if (context) {
                for (const auto& track : document_.timeline_tracks) {
                    if (track.voice_isolation.enabled && !context->tracks.contains(track.track_id)) emit stateChanged(false, track.track_id, "Unavailable", 0);
                    for (const auto& clip : track.clips) if (clip.voice_isolation.enabled && !context->clips.contains(clip.clip_id) &&
                        !(clip.audio_extracted && clip.linked_clip_id && context->clips.contains(*clip.linked_clip_id))) emit stateChanged(true, clip.clip_id, "Unavailable", 0);
                }
                emit contextChanged(context);
            }
        }, Qt::QueuedConnection);
    });
}
} // namespace application
