#include "main_window.h"
#include "main_window_support.h"

#include "ui/workers/audio_keyframe_generation.h"
#include "ui/viewer/composition_viewer.h"
#include "export/motion_video_export.h"
#include "rendering/preview_renderer.h"

#include "persistence/motion_document_store.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/media_pool/media_pool_widget.h"
#include "ui/dialogs/new_composition_dialog.h"
#include "ui/timeline/timeline_navigator.h"
#include "ui/workspace/motion_workspace.h"

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/media_importer.h>
#include <creative_suite/media/media_library.h>

#include <QCloseEvent>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
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
#include <cmath>
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
    linked_handoff_.reset();
    handoff_needs_initial_layer_ = false;
    pending_linked_handoff_.reset();
    pending_handoff_needs_initial_layer_ = false;
    linked_publication_pending_ = false;
    document_revision_ = 0;
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

void MainWindow::openHandoffRequest(const std::filesystem::path& request_path)
{
    startup_handoff_requested_ = true;
    QFile file(detail::pathForDisplay(request_path));
    if (!file.open(QIODevice::ReadOnly)) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_handoff", "read_request", file.errorString().toUtf8().toStdString(),
            {{"path", pathForLog(request_path)}});
        QMessageBox::warning(this, QStringLiteral("Motion Handoff"),
            QStringLiteral("The Video Editor handoff request could not be read."));
        startup_handoff_requested_ = false;
        return;
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse_error);
    creative_suite::motion_handoff::Request request;
    QString validation_error;
    if (parse_error.error != QJsonParseError::NoError || !document.isObject() ||
        !creative_suite::motion_handoff::Request::parse(
            document.object(), &request, &validation_error) ||
        !model::isSupportedFrameRate({request.frame_rate_numerator,
                                      request.frame_rate_denominator})) {
        const auto cause = parse_error.error == QJsonParseError::NoError
            ? validation_error : parse_error.errorString();
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_handoff", "validate_request", cause.toUtf8().toStdString(),
            {{"path", pathForLog(request_path)}});
        QMessageBox::warning(this, QStringLiteral("Motion Handoff"),
            QStringLiteral("The Video Editor handoff request is invalid."));
        startup_handoff_requested_ = false;
        return;
    }

    const auto motion_path = detail::pathFromQString(request.document_path);
    std::error_code exists_error;
    const bool document_exists = std::filesystem::is_regular_file(motion_path, exists_error) &&
        !exists_error;
    try {
        if (document_exists) {
            auto project = persistence::MotionDocumentStore::load(motion_path);
            pending_linked_handoff_ = request;
            pending_handoff_needs_initial_layer_ = false;
            stageOpenProject(motion_path, std::move(project));
            return;
        }
        model::MotionProjectData project;
        project.composition.canvas_size = {request.canvas_width, request.canvas_height};
        project.composition.frame_rate = {
            request.frame_rate_numerator, request.frame_rate_denominator};
        model::MotionMediaEntryData source;
        source.source_path = detail::pathFromQString(request.source_path);
        source.kind = request.source_kind == QStringLiteral("image")
            ? creative_suite::media::MediaKind::Image
            : creative_suite::media::MediaKind::Video;
        project.media.push_back(std::move(source));
        pending_linked_handoff_ = request;
        pending_handoff_needs_initial_layer_ = true;
        stageOpenProject(motion_path, std::move(project));
    } catch (const persistence::MotionDocumentError& error) {
        pending_linked_handoff_.reset();
        pending_handoff_needs_initial_layer_ = false;
        startup_handoff_requested_ = false;
        reportDocumentError("open_handoff_document", motion_path, error,
                            static_cast<int>(error.code()),
                            error.systemError().value_or(-1));
    } catch (const std::exception& error) {
        pending_linked_handoff_.reset();
        pending_handoff_needs_initial_layer_ = false;
        startup_handoff_requested_ = false;
        reportDocumentError("open_handoff_document", motion_path, error);
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
    if (!document_path_.has_value()) return saveCompositionAs();
    if (!saveToPath(*document_path_)) return false;
    if (linked_handoff_.has_value()) startLinkedPublication();
    return true;
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
    const auto path = pathFromQString(dialog.selectedFiles().front());
    const bool saved = saveToPath(path);
    if (saved && linked_handoff_.has_value() &&
        creative_suite::media::MediaLibrary::canonicalPath(path) !=
            creative_suite::media::MediaLibrary::canonicalPath(
                detail::pathFromQString(linked_handoff_->document_path))) {
        linked_handoff_.reset();
        handoff_needs_initial_layer_ = false;
        statusBar()->showMessage(QStringLiteral("Independent composition copy saved."), 5000);
    }
    return saved;
}
bool MainWindow::saveToPath(const std::filesystem::path& path)
{
    finishPendingTransformEdit();
    if (!document_) return false;
    const bool was_untitled = !document_path_.has_value();
    try {
        auto project_snapshot = projectData();
        const auto target_path = creative_suite::media::MediaLibrary::canonicalPath(path);
        auto lock_path = path;
        lock_path += ".lock";
        QLockFile lock(detail::pathForDisplay(lock_path));
        lock.setStaleLockTime(30000);
        if (!lock.tryLock(0)) {
            QMessageBox::warning(this, QStringLiteral("Document Is Being Edited"),
                QStringLiteral("Another Motion Studio session is saving this composition. Close that session or reopen the composition before saving."));
            return false;
        }
        std::error_code file_error;
        if (std::filesystem::is_regular_file(target_path, file_error) && !file_error) {
            const auto disk_document = persistence::MotionDocumentStore::load(target_path);
            const bool same_document = document_path_.has_value() &&
                creative_suite::media::MediaLibrary::canonicalPath(*document_path_) == target_path;
            if (same_document && disk_document.document_revision != document_revision_) {
                const auto message = QStringLiteral("The composition changed in another Motion Studio session. This copy was kept open; reopen the latest file before saving again.");
                creative_suite::diagnostics::Logger::instance().log(
                    creative_suite::diagnostics::Level::Error,
                    "motion_document", "save_revision_conflict",
                    message.toUtf8().toStdString(),
                    {{"path", pathForLog(target_path)},
                     {"expected_revision", std::to_string(document_revision_)},
                     {"actual_revision", std::to_string(disk_document.document_revision)}});
                QMessageBox::warning(this, QStringLiteral("Concurrent Composition Change"), message);
                return false;
            }
            project_snapshot.document_revision = disk_document.document_revision + 1;
        } else {
            project_snapshot.document_revision = 1;
        }
        persistence::MotionDocumentStore::save(path, project_snapshot);
        document_revision_ = project_snapshot.document_revision;
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
    snapshot.document_revision = document_revision_;
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
    const auto pending_handoff_matches_path = [this, &path] {
        return pending_linked_handoff_.has_value() && !path.empty() &&
            creative_suite::media::MediaLibrary::canonicalPath(
                detail::pathFromQString(pending_linked_handoff_->document_path)) ==
            creative_suite::media::MediaLibrary::canonicalPath(path);
    };
    const auto clear_matching_pending_handoff = [this, &pending_handoff_matches_path] {
        if (!pending_handoff_matches_path()) return;
        pending_linked_handoff_.reset();
        pending_handoff_needs_initial_layer_ = false;
    };
    if (result.cancelled) {
        clear_matching_pending_handoff();
        startup_handoff_requested_ = false;
        return;
    }

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

        if (!confirmReplaceDocument()) {
            clear_matching_pending_handoff();
            startup_handoff_requested_ = false;
            return;
        }
        const bool opening_linked_handoff = pending_handoff_matches_path();
        if (opening_linked_handoff) {
            linked_handoff_ = std::move(pending_linked_handoff_);
            pending_linked_handoff_.reset();
            handoff_needs_initial_layer_ = pending_handoff_needs_initial_layer_;
            pending_handoff_needs_initial_layer_ = false;
        } else {
            linked_handoff_.reset();
            handoff_needs_initial_layer_ = false;
            linked_publication_pending_ = false;
        }
        document_revision_ = project.document_revision;
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
        startup_handoff_requested_ = false;
        if (recovered) {
            last_autosaved_data_ = projectData();
        }
        statusBar()->showMessage(offline_count == 0
            ? QStringLiteral("Composition opened.")
            : QStringLiteral("Composition opened; %1 media item(s) are offline.")
                  .arg(offline_count), 7000);
        if (handoff_needs_initial_layer_) finishLinkedHandoffInitialization();
    } catch (const std::exception& error) {
        clear_matching_pending_handoff();
        startup_handoff_requested_ = false;
        reportDocumentError("open_document", path, error);
    }
}

void MainWindow::finishLinkedHandoffInitialization()
{
    if (!handoff_needs_initial_layer_ || !linked_handoff_.has_value() ||
        !document_ || media_pool_ == nullptr) return;
    const auto request = *linked_handoff_;
    const auto source_path = detail::pathFromQString(request.source_path);
    const auto index = media_pool_->library().indexForPath(source_path);
    if (index >= media_pool_->library().size() ||
        media_pool_->library().items()[index].offline) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_handoff", "create_initial_layer",
            "The source media could not be loaded for the linked composition.",
            {{"path", pathForLog(source_path)}});
        statusBar()->showMessage(
            QStringLiteral("The linked source is offline. Restore it, then reopen the handoff from the Video Editor."),
            8000);
        return;
    }
    const auto metadata = media_pool_->library().items()[index].metadata;
    auto before = captureEditState();
    model::LayerId layer_id = 0;
    try {
        const auto result = document_->addMediaLayer(metadata, 0, &layer_id);
        if (result != model::AddMediaLayerResult::Added) {
            statusBar()->showMessage(
                QStringLiteral("The source could not be added to the linked Motion composition."), 7000);
            return;
        }
        if (request.origin_kind == QStringLiteral("timeline_clip")) {
            const auto layer_it = std::find_if(document_->layers().begin(), document_->layers().end(),
                [layer_id](const auto& candidate) { return candidate.id == layer_id; });
            if (layer_it == document_->layers().end()) {
                *document_ = std::move(before.document);
                return;
            }
            const auto* layer = &*layer_it;
            const auto source_start = request.source_start_frame;
            const auto frame_count = layer->source_frame_count;
            if (source_start < 0 || source_start >= frame_count ||
                !std::isfinite(layer->source_frame_rate) || layer->source_frame_rate <= 0.0) {
                throw std::runtime_error("The selected clip source range is outside the source video.");
            }
            const auto remaining_source_frames = frame_count - source_start;
            const long double maximum_timeline = std::ceil(
                static_cast<long double>(remaining_source_frames) *
                static_cast<long double>(document_->frameRate().numerator) /
                (static_cast<long double>(document_->frameRate().denominator) *
                 static_cast<long double>(layer->source_frame_rate)));
            if (!std::isfinite(maximum_timeline) || maximum_timeline < 1.0L ||
                maximum_timeline >= std::ldexp(1.0L, 63)) {
                throw std::runtime_error("The selected clip source range has invalid duration metadata.");
            }
            const auto maximum_frames = static_cast<std::int64_t>(maximum_timeline);
            const auto duration = std::min(request.timeline_duration_frames, maximum_frames);
            if (!document_->setVideoSourceRange(
                    layer_id, source_start, duration, maximum_frames)) {
                throw std::runtime_error("The selected clip source range could not be applied.");
            }
        }
        if (!recordCompositionEdit(before)) {
            statusBar()->showMessage(
                QStringLiteral("The linked source could not be added to the composition."), 7000);
            return;
        }
        selected_layer_id_ = layer_id;
        handoff_needs_initial_layer_ = false;
        updateDocumentState();
        refreshTimeline();
        syncTransformInspector();
        requestPreview();
        statusBar()->showMessage(
            request.origin_kind == QStringLiteral("timeline_clip")
                ? QStringLiteral("The selected video range is ready in Motion Studio.")
                : QStringLiteral("The source is ready in Motion Studio; images start with the standard five-second duration."),
            6000);
    } catch (const std::exception& error) {
        if (document_) *document_ = std::move(before.document);
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_handoff", "create_initial_layer", error.what(),
            {{"path", pathForLog(source_path)}, {"document_path", pathForLog(
                detail::pathFromQString(request.document_path))}});
        QMessageBox::warning(this, QStringLiteral("Motion Handoff"),
            QStringLiteral("The source range could not be added. Check the Motion Studio log for details."));
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
