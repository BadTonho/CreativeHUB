#include "main_window.h"
#include "logging/logger.h"
#include "media/audio_ducking_analysis.h"
#include "media/video_metadata.h"
#include "workspaces/audio/ui/audio_workspace.h"
#include "workspaces/edit/ui/edit_workspace.h"
#include <QPointer>
#include <QCoreApplication>
#include <QProgressDialog>
#include <QStatusBar>
#include <QThreadPool>
#include <atomic>

void MainWindow::refreshAudioSelection() {
    if (!audio_workspace_) return;
    const auto location = active_timeline_clip_id_ ? timeline_model_.locateClip(*active_timeline_clip_id_) : std::nullopt;
    audio_workspace_->setSelectedClip(location ? &timeline_model_.tracks()[location->track_index].clips[location->clip_index] : nullptr);
}
void MainWindow::applyAudioClipEffects(timeline::ClipId id, std::vector<timeline::AudioEffectInstance> effects) {
    try {
        const auto result = timeline_command_service_.execute(application::SetClipAudioEffectsCommand{id, std::move(effects)});
        if (!result.changed()) return;
        applyTimelineEditResult(result, false); updateTimelineState(); refreshPlaybackComposition();
    } catch (const std::exception& error) {
        logging::Logger::instance().log(logging::Level::Error, "audio_workspace", "set_clip_effects", error.what(), {{"clip_id", std::to_string(id)}});
        statusBar()->showMessage(QCoreApplication::translate("AudioWorkspace", "Could not change the clip audio effects."));
    }
}
void MainWindow::applyAudioTrackDucking(timeline::TrackId id, timeline::AudioDuckingSettings settings) {
    try {
        const auto result = timeline_command_service_.execute(application::SetTrackAudioDuckingCommand{id, std::move(settings)});
        if (!result.changed()) return;
        applyTimelineEditResult(result, false); updateTimelineState(); refreshPlaybackComposition();
    } catch (const std::exception& error) {
        logging::Logger::instance().log(logging::Level::Error, "audio_workspace", "set_ducking", error.what(), {{"track_id", std::to_string(id)}});
        statusBar()->showMessage(QCoreApplication::translate("AudioWorkspace", "Could not change ducking settings."));
    }
}
void MainWindow::generateAudioDuckingCurve(timeline::TrackId id, timeline::AudioDuckingSettings settings) {
    settings.mode = timeline::AudioDuckingMode::Automatic;
    if (!timeline::validAudioDucking(settings) || settings.source_track_id == id) return;
    const auto snapshot = currentProjectDocument();
    const auto generation = project_generation_;
    auto canceled = std::make_shared<std::atomic_bool>(false);
    auto* progress = new QProgressDialog(QCoreApplication::translate("AudioWorkspace", "Generating ducking curve…"),
        QCoreApplication::translate("AudioWorkspace", "Cancel"), 0, 100, this);
    progress->setObjectName("audioDuckingProgress"); progress->setWindowModality(Qt::NonModal);
    progress->setAutoClose(false); progress->setAutoReset(false); progress->show();
    connect(progress, &QProgressDialog::canceled, this, [canceled] { canceled->store(true); });
    connect(progress, &QObject::destroyed, [canceled] { canceled->store(true); });
    QPointer<MainWindow> target(this); QPointer<QProgressDialog> dialog(progress);
    // This UI-thread receiver outlives the worker and its queued completion.
    // Guarded window/dialog pointers are inspected only on the UI thread.
    auto dispatcher = std::shared_ptr<QObject>(new QObject, [](QObject* object) { object->deleteLater(); });
    QThreadPool::globalInstance()->start([dispatcher, target, dialog, snapshot, generation, id, settings = std::move(settings), canceled]() mutable {
        std::vector<timeline::AudioDuckingKeyframe> curve;
        QString error;
        try {
            curve = media::generateAudioDuckingCurve(snapshot, settings,
                [canceled] { return canceled->load(); }, [dispatcher, dialog](int value) {
                    QMetaObject::invokeMethod(dispatcher.get(), [dialog, value] { if (dialog) dialog->setValue(value); }, Qt::QueuedConnection);
                });
        } catch (const std::exception& exception) {
            if (!canceled->load()) {
                logging::Context context{{"track_id", std::to_string(id)},
                    {"source_track_id", std::to_string(settings.source_track_id.value_or(0))}};
                if (const auto* media_error = dynamic_cast<const media::MediaError*>(&exception);
                    media_error && media_error->error_code())
                    context.emplace_back("error_code", std::to_string(*media_error->error_code()));
                logging::Logger::instance().log(logging::Level::Error, "audio_workspace", "generate_ducking_curve",
                    exception.what(), context);
                error = QString::fromUtf8(exception.what());
            }
        }
        QMetaObject::invokeMethod(dispatcher.get(), [dispatcher, target, dialog, snapshot, generation, id, settings = std::move(settings), canceled,
                                         curve = std::move(curve), error]() mutable {
            if (dialog) dialog->deleteLater();
            if (!target || canceled->load()) return;
            if (generation != target->project_generation_ || snapshot != target->currentProjectDocument()) {
                target->statusBar()->showMessage(QCoreApplication::translate("AudioWorkspace", "The project changed. Generate the ducking curve again."));
                return;
            }
            if (!error.isEmpty()) {
                target->statusBar()->showMessage(QCoreApplication::translate("AudioWorkspace", "Could not generate the ducking curve. See the error log."));
                return;
            }
            settings.curve = std::move(curve); settings.mode = timeline::AudioDuckingMode::Curve;
            target->applyAudioTrackDucking(id, std::move(settings));
        }, Qt::QueuedConnection);
    });
}
