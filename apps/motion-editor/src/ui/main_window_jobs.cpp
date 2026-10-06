#include "main_window.h"
#include "main_window_support.h"

#include "../persistence/motion_document_store.h"
#include "audio_keyframe_dialog.h"
#include "audio_keyframe_generation.h"
#include "motion_video_export.h"
#include "media_pool_widget.h"
#include "motion_video_export_dialog.h"
#include "timeline_navigator.h"

#include <creative_suite/animation/animation.h>
#include <creative_suite/diagnostics/logger.h>

#include <QFileDialog>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProgressDialog>
#include <QStatusBar>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace motion::ui {
using creative_suite::animation::TransformProperty;
using detail::pathFromQString;
using detail::pathForLog;

void MainWindow::startVideoExport()
{
    finishPendingTransformEdit();
    finishPendingContentEdit();
    if (!document_ || !media_pool_ || export_worker_ || audio_keyframe_worker_) return;

    MotionVideoExportDialog dialog(document_->canvasSize(), document_->frameRate(), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto settings = dialog.exportSettings();
    if (!settings.has_value()) return;

    MotionExportSnapshot snapshot;
    snapshot.canvas_size = document_->canvasSize();
    snapshot.frame_rate = document_->frameRate();
    snapshot.layers = document_->layers();
    for (const auto& layer : snapshot.layers) {
        if (layer.kind == model::LayerKind::Image) {
            snapshot.still_frames.emplace(
                layer.source_path, media_pool_->sharedFirstFrameForPath(layer.source_path));
        }
    }

    export_progress_ = new QProgressDialog(
        QStringLiteral("Preparing video export..."), QStringLiteral("Cancel"), 0, 100, this);
    export_progress_->setObjectName(QStringLiteral("motion-export-progress"));
    export_progress_->setWindowTitle(QStringLiteral("Export Video"));
    export_progress_->setWindowModality(Qt::NonModal);
    export_progress_->setAutoClose(false);
    export_progress_->setAutoReset(false);
    export_progress_->setValue(0);
    connect(export_progress_, &QProgressDialog::canceled, this, [this] {
        if (!export_worker_) return;
        export_worker_->cancel();
        export_progress_->setLabelText(QStringLiteral("Canceling video export..."));
        export_progress_->setCancelButton(nullptr);
    });

    QPointer<MainWindow> owner(this);
    export_worker_ = std::make_unique<MotionVideoExportWorker>(
        this, std::move(snapshot), *settings,
        [owner](int progress) {
            if (!owner.isNull() && owner->export_progress_ != nullptr) {
                owner->export_progress_->setLabelText(
                    QStringLiteral("Rendering video... %1%").arg(progress));
                owner->export_progress_->setValue(progress);
            }
        },
        [owner](MotionExportResult result) mutable {
            if (!owner.isNull()) owner->finishVideoExport(std::move(result));
        });
    updateDocumentState();
    export_progress_->show();
    export_worker_->start();
}
void MainWindow::finishVideoExport(MotionExportResult result)
{
    if (export_worker_) {
        export_worker_->wait();
        export_worker_.reset();
    }
    if (export_progress_ != nullptr) {
        export_progress_->close();
        export_progress_->deleteLater();
        export_progress_ = nullptr;
    }
    updateDocumentState();
    if (result.succeeded) {
        statusBar()->showMessage(
            QStringLiteral("Video exported to %1")
                .arg(QString::fromUtf8(pathForLog(result.output_path))), 8000);
    } else if (result.cancelled) {
        statusBar()->showMessage(QStringLiteral("Video export canceled."), 5000);
    } else {
        QMessageBox::warning(this, QStringLiteral("Video Export Failed"),
            QStringLiteral("The video could not be exported. See the Motion Studio log for details."));
    }
}
void MainWindow::generateKeyframesFromAudio()
{
    finishPendingTransformEdit();
    if (!document_ || audio_keyframe_worker_ || export_worker_) return;
    const auto selected = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (selected == document_->layers().end() || selected->duration_frames <= 0) return;

    AudioKeyframeDialog dialog(QString::fromUtf8(
        selected->name.data(), static_cast<qsizetype>(selected->name.size())), this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto settings = dialog.settings();

    const auto maximum_keyframes = persistence::MotionDocumentStore::maximum_keyframe_count;
    std::size_t other_keyframes = 0;
    const auto add_other_keyframes = [&other_keyframes, maximum_keyframes](std::size_t count) {
        other_keyframes = count > maximum_keyframes - other_keyframes
            ? maximum_keyframes : other_keyframes + count;
    };
    for (const auto& layer : document_->layers()) {
        if (layer.id != selected->id) {
            add_other_keyframes(layer.keyframes.position_x.size());
            add_other_keyframes(layer.keyframes.position_y.size());
            add_other_keyframes(layer.keyframes.scale.size());
            add_other_keyframes(layer.keyframes.rotation.size());
            add_other_keyframes(layer.keyframes.opacity.size());
            continue;
        }
        if (settings.property != TransformProperty::PositionX)
            add_other_keyframes(layer.keyframes.position_x.size());
        if (settings.property != TransformProperty::PositionY)
            add_other_keyframes(layer.keyframes.position_y.size());
        if (settings.property != TransformProperty::Scale)
            add_other_keyframes(layer.keyframes.scale.size());
        if (settings.property != TransformProperty::Rotation)
            add_other_keyframes(layer.keyframes.rotation.size());
        if (settings.property != TransformProperty::Opacity)
            add_other_keyframes(layer.keyframes.opacity.size());
    }
    const auto available_keyframes = other_keyframes < maximum_keyframes
        ? maximum_keyframes - other_keyframes : 0;
    const auto analysis_limit = std::min<std::uint64_t>(
        static_cast<std::uint64_t>(selected->duration_frames),
        static_cast<std::uint64_t>(available_keyframes) + 1U);
    if (analysis_limit == 0) {
        QMessageBox::warning(this, QStringLiteral("Audio Keyframes"),
            QStringLiteral("This composition has reached its supported keyframe limit."));
        return;
    }

    AudioKeyframeGenerationRequest request;
    request.audio_path = pathFromQString(settings.audio_path);
    request.layer_id = selected->id;
    request.frame_rate = document_->frameRate();
    request.layer_duration_frames = selected->duration_frames;
    request.analysis_frame_limit = static_cast<std::int64_t>(analysis_limit);
    request.property = settings.property;
    request.minimum_value = settings.minimum_value;
    request.maximum_value = settings.maximum_value;
    request.maximum_keyframe_count = available_keyframes;

    audio_keyframe_progress_ = new QProgressDialog(
        QStringLiteral("Analyzing audio..."), QStringLiteral("Cancel"), 0, 100, this);
    audio_keyframe_progress_->setObjectName(QStringLiteral("motion-audio-keyframe-progress"));
    audio_keyframe_progress_->setWindowTitle(QStringLiteral("Generate Audio Keyframes"));
    audio_keyframe_progress_->setWindowModality(Qt::WindowModal);
    audio_keyframe_progress_->setAutoClose(false);
    audio_keyframe_progress_->setAutoReset(false);
    audio_keyframe_progress_->setMinimumDuration(250);
    audio_keyframe_progress_->setValue(0);
    connect(audio_keyframe_progress_, &QProgressDialog::canceled, this, [this] {
        if (!audio_keyframe_worker_) return;
        audio_keyframe_worker_->cancel();
        audio_keyframe_progress_->setLabelText(QStringLiteral("Canceling audio analysis..."));
        audio_keyframe_progress_->setCancelButton(nullptr);
    });

    QPointer<MainWindow> owner(this);
    audio_keyframe_worker_ = std::make_unique<AudioKeyframeGenerationWorker>(
        this, std::move(request),
        [owner](int progress) {
            if (owner.isNull() || owner->audio_keyframe_progress_ == nullptr) return;
            owner->audio_keyframe_progress_->setLabelText(
                QStringLiteral("Analyzing audio... %1%").arg(progress));
            owner->audio_keyframe_progress_->setValue(progress);
        },
        [owner](AudioKeyframeGenerationResult result) mutable {
            if (!owner.isNull()) owner->finishAudioKeyframeGeneration(std::move(result));
        });
    updateDocumentState();
    audio_keyframe_progress_->show();
    audio_keyframe_worker_->start();
}
void MainWindow::finishAudioKeyframeGeneration(AudioKeyframeGenerationResult result)
{
    if (audio_keyframe_worker_) {
        audio_keyframe_worker_->wait();
        audio_keyframe_worker_.reset();
    }
    if (audio_keyframe_progress_ != nullptr) {
        audio_keyframe_progress_->close();
        audio_keyframe_progress_->deleteLater();
        audio_keyframe_progress_ = nullptr;
    }
    updateDocumentState();

    if (result.cancelled) {
        statusBar()->showMessage(QStringLiteral("Audio keyframe generation canceled."), 5000);
        return;
    }
    if (!result.succeeded) {
        QMessageBox::warning(this, QStringLiteral("Audio Keyframes Failed"),
            QStringLiteral("Keyframes could not be generated. See the Motion Studio log for details."));
        return;
    }
    if (result.silent) {
        statusBar()->showMessage(
            QStringLiteral("No audio samples or signal were found; the layer was not changed."),
            6000);
        return;
    }
    if (!document_) return;
    const auto selected = std::find_if(document_->layers().begin(), document_->layers().end(),
        [&result](const auto& layer) { return layer.id == result.layer_id; });
    if (selected == document_->layers().end()) return;

    auto before = captureEditState();
    try {
        if (!document_->replaceLayerKeyframes(
                result.layer_id, result.property, result.keyframes)) {
            throw std::runtime_error("The generated keyframes failed document validation.");
        }
    } catch (const std::exception& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_audio", "apply_keyframes", error.what(),
            {{"path", pathForLog(result.audio_path)},
             {"layer_id", std::to_string(result.layer_id)}});
        QMessageBox::warning(this, QStringLiteral("Audio Keyframes Failed"),
            QStringLiteral("Keyframes could not be applied. See the Motion Studio log for details."));
        return;
    }

    (void)recordCompositionEdit(std::move(before));
    selected_layer_id_ = result.layer_id;
    timeline_->setLayerExpanded(result.layer_id, true);
    timeline_->setTransformGroupExpanded(result.layer_id, true);
    selectCurveSegment(result.layer_id, result.property, 0);
    refreshTimeline();
    syncTransformInspector();
    updateDocumentState();
    requestPreview();
    statusBar()->showMessage(
        QStringLiteral("Generated %1 audio keyframes.").arg(result.keyframes.size()), 6000);
}

} // namespace motion::ui
