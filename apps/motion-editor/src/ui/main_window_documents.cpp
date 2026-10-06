#include "main_window.h"
#include "main_window_support.h"

#include "audio_keyframe_generation.h"
#include "composition_viewer.h"
#include "export/motion_video_export.h"
#include "rendering/preview_renderer.h"

#include "../persistence/motion_document_store.h"
#include "inspector/inspector_widget.h"
#include "media_pool_widget.h"
#include "dialogs/new_composition_dialog.h"
#include "timeline_navigator.h"
#include "workspace/motion_workspace.h"

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/media_importer.h>
#include <creative_suite/media/media_library.h>

#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProgressDialog>
#include <QPushButton>
#include <QRunnable>
#include <QStatusBar>
#include <QThreadPool>
#include <QTimer>

#include <algorithm>
#include <filesystem>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace motion::ui {
using detail::pathFromQString;
using detail::pathForLog;
using detail::pathForDisplay;

namespace {

class OpenMediaStageTask final : public QRunnable {
public:
    using ProgressHandler = std::function<void(
        std::uint64_t, std::size_t, std::size_t, const std::filesystem::path&)>;
    using FinishedHandler = std::function<void(
        std::uint64_t, std::filesystem::path, model::MotionProjectData,
        creative_suite::media::MediaImportBatchResult, bool,
        std::filesystem::path)>;

    OpenMediaStageTask(QObject* receiver,
                       std::uint64_t generation,
                       std::filesystem::path document_path,
                       model::MotionProjectData project,
                       std::shared_ptr<std::atomic_bool> cancel,
                       bool recovered,
                       std::filesystem::path recovery_snapshot_path,
                       ProgressHandler progress_handler,
                       FinishedHandler finished_handler)
        : receiver_(receiver), generation_(generation),
          document_path_(std::move(document_path)), project_(std::move(project)),
          cancel_(std::move(cancel)), recovered_(recovered),
          recovery_snapshot_path_(std::move(recovery_snapshot_path)),
          progress_handler_(std::move(progress_handler)),
          finished_handler_(std::move(finished_handler))
    {
        setAutoDelete(true);
    }

    void run() override
    {
        std::vector<std::filesystem::path> paths;
        paths.reserve(project_.media.size());
        for (const auto& media : project_.media) paths.push_back(media.source_path);
        QPointer<QObject> receiver = receiver_;
        const auto generation = generation_;
        const auto progress_handler = progress_handler_;
        auto result = creative_suite::media::MediaImporter{}.process(
            paths, *cancel_, [receiver, generation, progress_handler](
                std::size_t completed, std::size_t total,
                const std::filesystem::path& path) {
                if (receiver.isNull()) return;
                QMetaObject::invokeMethod(receiver.data(),
                    [receiver, generation, completed, total, path, progress_handler] {
                        if (!receiver.isNull())
                            progress_handler(generation, completed, total, path);
                    }, Qt::QueuedConnection);
            });
        if (receiver.isNull()) return;
        const auto finished_handler = finished_handler_;
        auto document_path = document_path_;
        auto recovery_snapshot_path = recovery_snapshot_path_;
        const auto recovered = recovered_;
        auto project = std::move(project_);
        QMetaObject::invokeMethod(receiver.data(),
            [receiver, generation, document_path = std::move(document_path),
             project = std::move(project), result = std::move(result),
             recovered, recovery_snapshot_path = std::move(recovery_snapshot_path),
             finished_handler]() mutable {
                if (!receiver.isNull())
                    finished_handler(generation, std::move(document_path),
                                     std::move(project), std::move(result), recovered,
                                     std::move(recovery_snapshot_path));
            }, Qt::QueuedConnection);
    }

private:
    QPointer<QObject> receiver_;
    std::uint64_t generation_ = 0;
    std::filesystem::path document_path_;
    model::MotionProjectData project_;
    std::shared_ptr<std::atomic_bool> cancel_;
    bool recovered_ = false;
    std::filesystem::path recovery_snapshot_path_;
    ProgressHandler progress_handler_;
    FinishedHandler finished_handler_;
};

} // namespace

void MainWindow::createNewComposition()
{
    if (audio_keyframe_worker_) return;
    finishPendingTransformEdit();
    NewCompositionDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) return;
    const auto settings = dialog.compositionSettings();
    if (!settings.has_value()) return;
    if (!confirmReplaceDocument()) return;

    cleanupCurrentUnsavedSnapshots("new_composition_cleanup");
    cleanupRecoveredUnsavedSnapshot("new_recovered_snapshot_cleanup");
    last_autosaved_data_.reset();
    composition_history_.clear();
    active_transform_edit_.reset();
    document_.emplace(settings->canvas_size.width,
                      settings->canvas_size.height,
                      settings->frame_rate);
    document_path_.reset();
    saved_data_.reset();
    selected_layer_id_ = 0;
    if (viewer_ == nullptr) createWorkspace();
    else {
        media_pool_->clear();
        preview_renderer_->resetSessions();
    }
    resetCurveEditor();
    import_media_action_->setEnabled(true);
    timeline_->setCompositionTiming(settings->frame_rate);
    timeline_->setLayers(document_->layers());
    timeline_->setSelectedLayerId(0);
    viewer_->setComposition(document_->canvasSize(), std::nullopt);
    inspector_->setMedia(nullptr);
    inspector_->selectMediaTab();
    syncTransformInspector();
    updateDocumentState();
    requestPreview();
}
void MainWindow::openComposition()
{
    if (audio_keyframe_worker_) return;
    finishPendingTransformEdit();
    if (open_cancel_requested_) return;
    QFileDialog dialog(this, QStringLiteral("Open Composition"));
    dialog.setObjectName(QStringLiteral("motion-open-composition-dialog"));
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setNameFilters({QStringLiteral("Motion Studio documents (*.motion)"),
                           QStringLiteral("All files (*)")});
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;

    const auto path = pathFromQString(dialog.selectedFiles().front());
    try {
        const auto recoverable = recovery_store_.recoverableSnapshotsForProject(path);
        if (!recoverable.empty()) {
            const auto selected = chooseRecoverySnapshot(
                recoverable, QFileInfo(pathForDisplay(path)).fileName());
            if (selected.has_value()) {
                restoreRecoverySnapshot(*selected);
                return;
            }
        }
        auto project = persistence::MotionDocumentStore::load(path);
        stageOpenProject(path, std::move(project));
    } catch (const persistence::MotionDocumentError& error) {
        reportDocumentError("open_document", path, error,
                            static_cast<int>(error.code()),
                            error.systemError().value_or(-1));
        return;
    } catch (const std::exception& error) {
        reportDocumentError("open_document", path, error);
    }
}
void MainWindow::stageOpenProject(
    std::filesystem::path target_path,
    model::MotionProjectData project,
    bool recovered,
    std::filesystem::path recovery_snapshot_path)
{
    const auto generation = ++open_generation_;
    open_cancel_requested_ = std::make_shared<std::atomic_bool>(false);
    if (project.media.empty()) {
        finishOpen(generation, std::move(target_path), std::move(project), {},
                   recovered, std::move(recovery_snapshot_path));
        return;
    }

    open_progress_ = new QProgressDialog(
        QStringLiteral("Preparing linked media..."), QStringLiteral("Cancel"),
        0, static_cast<int>(project.media.size()), this);
    open_progress_->setObjectName(QStringLiteral("motion-open-media-progress"));
    open_progress_->setWindowTitle(QStringLiteral("Open Composition"));
    open_progress_->setWindowModality(Qt::WindowModal);
    open_progress_->setMinimumDuration(250);
    open_progress_->setValue(0);
    connect(open_progress_, &QProgressDialog::canceled, this, [this] {
        if (open_cancel_requested_)
            open_cancel_requested_->store(true, std::memory_order_relaxed);
    });
    new_composition_action_->setEnabled(false);
    open_composition_action_->setEnabled(false);
    open_progress_->show();

    QPointer<MainWindow> owner(this);
    auto progress_handler = [owner](std::uint64_t task_generation,
                                    std::size_t completed,
                                    std::size_t total,
                                    const std::filesystem::path& media_path) {
        if (owner.isNull() || task_generation != owner->open_generation_ ||
            owner->open_progress_ == nullptr) return;
        owner->open_progress_->setRange(0, static_cast<int>(total));
        owner->open_progress_->setValue(static_cast<int>(completed));
        owner->open_progress_->setLabelText(QStringLiteral("Loading %1")
            .arg(QFileInfo(pathForDisplay(media_path)).fileName()));
    };
    auto finished_handler = [owner](std::uint64_t task_generation,
                                    std::filesystem::path document_path,
                                    model::MotionProjectData staged_project,
                                    creative_suite::media::MediaImportBatchResult result,
                                    bool was_recovered,
                                    std::filesystem::path snapshot_path) {
        if (!owner.isNull())
            owner->finishOpen(task_generation, std::move(document_path),
                              std::move(staged_project), std::move(result),
                              was_recovered, std::move(snapshot_path));
    };
    QThreadPool::globalInstance()->start(new OpenMediaStageTask(
        this, generation, std::move(target_path), std::move(project),
        open_cancel_requested_, recovered, std::move(recovery_snapshot_path),
        std::move(progress_handler), std::move(finished_handler)));
}
bool MainWindow::saveComposition()
{
    finishPendingTransformEdit();
    if (!document_) return false;
    return document_path_.has_value()
        ? saveToPath(*document_path_)
        : saveCompositionAs();
}
bool MainWindow::saveCompositionAs()
{
    finishPendingTransformEdit();
    if (!document_) return false;
    QFileDialog dialog(this, QStringLiteral("Save Composition As"));
    dialog.setObjectName(QStringLiteral("motion-save-composition-dialog"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setNameFilters({QStringLiteral("Motion Studio documents (*.motion)")});
    dialog.setDefaultSuffix(QStringLiteral("motion"));
    if (document_path_.has_value())
        dialog.selectFile(QString::fromUtf8(pathForLog(*document_path_)));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return false;
    return saveToPath(pathFromQString(dialog.selectedFiles().front()));
}
bool MainWindow::saveToPath(const std::filesystem::path& path)
{
    finishPendingTransformEdit();
    if (!document_) return false;
    const bool was_untitled = !document_path_.has_value();
    try {
        const auto project_snapshot = projectData();
        persistence::MotionDocumentStore::save(path, project_snapshot);
        document_path_ = creative_suite::media::MediaLibrary::canonicalPath(path);
        saved_data_ = project_snapshot;
        last_autosaved_data_.reset();
        if (was_untitled) {
            cleanupCurrentUnsavedSnapshots("save_as_unsaved_recovery_cleanup");
            cleanupRecoveredUnsavedSnapshot("save_as_recovered_snapshot_cleanup");
        }
        updateDocumentState();
        statusBar()->showMessage(QStringLiteral("Composition saved."), 4000);
        return true;
    } catch (const persistence::MotionDocumentError& error) {
        reportDocumentError("save_document", path, error,
                            static_cast<int>(error.code()),
                            error.systemError().value_or(-1));
    } catch (const std::exception& error) {
        reportDocumentError("save_document", path, error);
    }
    return false;
}
bool MainWindow::confirmReplaceDocument()
{
    if (!documentIsDirty()) return true;
    QMessageBox prompt(QMessageBox::Warning,
                       QStringLiteral("Unsaved Changes"),
                       QStringLiteral("Save changes to the current composition before continuing?"),
                       QMessageBox::NoButton, this);
    prompt.setObjectName(QStringLiteral("motion-unsaved-changes-prompt"));
    auto* save = prompt.addButton(QMessageBox::Save);
    auto* discard = prompt.addButton(QMessageBox::Discard);
    auto* cancel = prompt.addButton(QMessageBox::Cancel);
    save->setObjectName(QStringLiteral("motion-unsaved-save-button"));
    discard->setObjectName(QStringLiteral("motion-unsaved-discard-button"));
    cancel->setObjectName(QStringLiteral("motion-unsaved-cancel-button"));
    prompt.setDefaultButton(save);
    prompt.exec();
    if (prompt.clickedButton() == save) return saveComposition();
    return prompt.clickedButton() == discard;
}
model::MotionProjectData MainWindow::projectData() const
{
    if (!document_) throw std::logic_error("There is no open Motion Studio composition");
    model::MotionProjectData snapshot;
    snapshot.composition = {document_->canvasSize(), document_->frameRate()};
    snapshot.layers = document_->layers();
    if (media_pool_ != nullptr) {
        snapshot.bins = media_pool_->library().bins();
        snapshot.media.reserve(media_pool_->library().items().size());
        for (const auto& item : media_pool_->library().items()) {
            snapshot.media.push_back({item.metadata.source_path,
                                      item.metadata.kind,
                                      item.display_name,
                                      item.bin_path});
        }
    }
    return snapshot;
}
bool MainWindow::documentIsDirty() const
{
    if (!document_) return false;
    if (!saved_data_.has_value()) return true;
    try {
        return projectData() != *saved_data_;
    } catch (...) {
        return true;
    }
}
void MainWindow::updateDocumentState()
{
    const bool has_document = document_.has_value();
    const bool dirty = documentIsDirty();
    bool selected_layer_exists = false;
    if (has_document) {
        selected_layer_exists = std::any_of(
            document_->layers().begin(), document_->layers().end(), [this](const auto& layer) {
                return layer.id == selected_layer_id_ && layer.duration_frames > 0;
            });
    }
    if (import_media_action_ != nullptr) import_media_action_->setEnabled(has_document);
    if (new_text_layer_action_ != nullptr) new_text_layer_action_->setEnabled(has_document);
    if (new_rectangle_layer_action_ != nullptr)
        new_rectangle_layer_action_->setEnabled(has_document);
    if (new_ellipse_layer_action_ != nullptr)
        new_ellipse_layer_action_->setEnabled(has_document);
    if (generate_audio_keyframes_action_ != nullptr) {
        generate_audio_keyframes_action_->setEnabled(
            selected_layer_exists && !audio_keyframe_worker_ && !export_worker_);
    }
    if (save_composition_action_ != nullptr) save_composition_action_->setEnabled(has_document);
    if (save_composition_as_action_ != nullptr)
        save_composition_as_action_->setEnabled(has_document);
    if (export_video_action_ != nullptr) {
        const bool has_layers = has_document && !document_->layers().empty();
        export_video_action_->setEnabled(
            has_layers && !export_worker_ && !audio_keyframe_worker_);
    }
    updateHistoryActions();
    if (!has_document) {
        setWindowModified(false);
        setWindowTitle(QStringLiteral("Motion Studio"));
        return;
    }
    const QString document_name = document_path_.has_value()
        ? QFileInfo(pathForDisplay(*document_path_)).fileName()
        : QStringLiteral("Untitled");
    setWindowTitle(QStringLiteral("%1 — Motion Studio[*]").arg(document_name));
    setWindowModified(dirty);
}
void MainWindow::reportDocumentError(const char* operation,
                                     const std::filesystem::path& path,
                                     const std::exception& error,
                                     int error_code,
                                     int system_error)
{
    creative_suite::diagnostics::Context context{
        {"path", pathForLog(path)}, {"error_code", std::to_string(error_code)}};
    if (system_error >= 0)
        context.emplace_back("system_error", std::to_string(system_error));
    creative_suite::diagnostics::Logger::instance().log(
        creative_suite::diagnostics::Level::Error,
        "motion_document", operation, error.what(), context);
    QMessageBox::warning(
        this, QStringLiteral("Document Error"),
        QStringLiteral("The composition could not be %1. Check the Motion Studio log for details.")
            .arg(QString::fromUtf8(operation).contains(QStringLiteral("save"))
                     ? QStringLiteral("saved") : QStringLiteral("opened")));
}
void MainWindow::finishOpen(std::uint64_t generation,
                            std::filesystem::path path,
                            model::MotionProjectData project,
                            creative_suite::media::MediaImportBatchResult result,
                            bool recovered,
                            std::filesystem::path recovery_snapshot_path)
{
    if (generation != open_generation_) return;
    if (open_progress_ != nullptr) open_progress_->hide();
    open_cancel_requested_.reset();
    new_composition_action_->setEnabled(true);
    open_composition_action_->setEnabled(true);
    if (result.cancelled) return;

    try {
        auto staged_document = model::CompositionDocument(
            project.composition.canvas_size.width,
            project.composition.canvas_size.height,
            project.composition.frame_rate,
            project.layers);
        creative_suite::media::MediaLibrary staged_library;
        for (const auto& bin : project.bins) {
            if (bin == creative_suite::media::default_bin) continue;
            (void)staged_library.createBin(bin);
        }

        std::unordered_map<std::filesystem::path, std::size_t> imported_by_path;
        imported_by_path.reserve(result.files.size());
        for (std::size_t i = 0; i < result.files.size(); ++i) {
            imported_by_path.emplace(
                creative_suite::media::MediaLibrary::canonicalPath(result.files[i].path), i);
        }
        std::size_t offline_count = 0;
        auto& logger = creative_suite::diagnostics::Logger::instance();
        for (const auto& media : project.media) {
            const auto found = imported_by_path.find(
                creative_suite::media::MediaLibrary::canonicalPath(media.source_path));
            bool restored = false;
            if (found != imported_by_path.end()) {
                auto& file = result.files[found->second];
                if (file.status == creative_suite::media::MediaImportFileStatus::Imported &&
                    file.item && file.item->metadata.kind == media.kind) {
                    auto imported = std::move(*file.item);
                    const auto mutation = staged_library.addOnline(
                        std::move(imported.metadata), std::move(imported.first_frame),
                        media.display_name, media.bin_path);
                    restored = mutation == creative_suite::media::MediaMutationResult::Changed;
                    if (!restored) {
                        logger.log(creative_suite::diagnostics::Level::Warning,
                                   "motion_document", "restore_media",
                                   "The imported source could not be added to the staged Media Pool",
                                   {{"path", pathForLog(media.source_path)},
                                    {"media_mutation", std::to_string(static_cast<int>(mutation))}});
                    }
                } else if (file.status == creative_suite::media::MediaImportFileStatus::Failed) {
                    logger.log(creative_suite::diagnostics::Level::Warning,
                               "motion_document", "restore_media", file.cause,
                               {{"path", pathForLog(media.source_path)},
                                {"error_code", file.error_code.has_value()
                                    ? std::to_string(*file.error_code) : std::string{}}});
                } else if (file.status == creative_suite::media::MediaImportFileStatus::Imported) {
                    logger.log(creative_suite::diagnostics::Level::Warning,
                               "motion_document", "restore_media",
                               "The source media kind does not match the saved Media Pool entry",
                               {{"path", pathForLog(media.source_path)},
                                {"saved_kind", std::to_string(static_cast<int>(media.kind))},
                                {"imported_kind", file.item.has_value()
                                    ? std::to_string(static_cast<int>(file.item->metadata.kind))
                                    : std::string{}}});
                }
            }
            if (!restored) {
                (void)staged_library.addOffline(media.source_path, media.display_name,
                                                media.bin_path, media.kind);
                ++offline_count;
            }
        }

        if (!confirmReplaceDocument()) return;
        const auto current_unsaved_directory = recovery_store_.recoveryRoot() /
            "unsaved" / recovery_store_.sessionId();
        const bool recovered_from_current_session = recovered && path.empty() &&
            recovery_snapshot_path.parent_path() == current_unsaved_directory;
        const auto previous_recovered_snapshot = recovered_untitled_snapshot_path_;
        composition_history_.clear();
        active_transform_edit_.reset();
        document_.emplace(std::move(staged_document));
        document_path_ = path.empty()
            ? std::nullopt
            : std::optional<std::filesystem::path>(
                  creative_suite::media::MediaLibrary::canonicalPath(path));
        last_autosaved_data_.reset();
        selected_layer_id_ = 0;
        if (viewer_ == nullptr) createWorkspace();
        else if (preview_renderer_) preview_renderer_->resetSessions();
        resetCurveEditor();
        media_pool_->replaceLibrary(std::move(staged_library));
        timeline_->setCompositionTiming(document_->frameRate());
        timeline_->setLayers(document_->layers());
        timeline_->setSelectedLayerId(0);
        viewer_->setComposition(document_->canvasSize(), std::nullopt);
        inspector_->setMedia(nullptr);
        inspector_->selectMediaTab();
        syncTransformInspector();
        if (recovered) saved_data_.reset();
        else saved_data_ = projectData();
        updateDocumentState();
        requestPreview();
        if (!recovered_from_current_session)
            cleanupCurrentUnsavedSnapshots("open_unsaved_recovery_cleanup");
        if (previous_recovered_snapshot.has_value() &&
            (!recovered || recovery_snapshot_path != *previous_recovered_snapshot)) {
            cleanupRecoveredUnsavedSnapshot("open_recovered_snapshot_cleanup");
        } else {
            recovered_untitled_snapshot_path_.reset();
        }
        if (recovered && !document_path_.has_value())
            recovered_untitled_snapshot_path_ = recovery_snapshot_path;
        if (recovered) {
            last_autosaved_data_ = projectData();
        }
        statusBar()->showMessage(offline_count == 0
            ? QStringLiteral("Composition opened.")
            : QStringLiteral("Composition opened; %1 media item(s) are offline.")
                  .arg(offline_count), 7000);
    } catch (const std::exception& error) {
        reportDocumentError("open_document", path, error);
    }
}
void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!confirmReplaceDocument()) {
        event->ignore();
        return;
    }
    if (audio_keyframe_worker_) {
        audio_keyframe_worker_->cancelAndWait();
        audio_keyframe_worker_.reset();
    }
    if (audio_keyframe_progress_ != nullptr) {
        audio_keyframe_progress_->close();
        audio_keyframe_progress_->deleteLater();
        audio_keyframe_progress_ = nullptr;
    }
    if (export_worker_) {
        export_worker_->cancelAndWait();
        export_worker_.reset();
    }
    if (export_progress_ != nullptr) {
        export_progress_->close();
        export_progress_->deleteLater();
        export_progress_ = nullptr;
    }
    if (autosave_timer_ != nullptr) autosave_timer_->stop();
    cleanupCurrentUnsavedSnapshots("close_unsaved_recovery_cleanup");
    cleanupRecoveredUnsavedSnapshot("close_recovered_snapshot_cleanup");
    saveWorkspaceLayout();
    event->accept();
}

} // namespace motion::ui
