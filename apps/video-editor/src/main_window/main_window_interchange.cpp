#include "application/composition_interchange.h"
#include "main_window.h"
#include "main_window_support.h"
#include "workspaces/edit/ui/edit_workspace.h"
#include "workspaces/edit/controllers/edit_workspace_controller.h"
#include <QEventLoop>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStatusBar>
#include <QTimer>
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/media_importer.h>
#include <future>
#include <set>
#include <QCoreApplication>
namespace x = creative_suite::composition_exchange;
namespace {
template <class F> auto worker(QWidget *parent, F work) {
    auto cancel = std::make_shared<std::atomic_bool>(false);
    QProgressDialog progress("Preparing composition interchange...", "Cancel", 0, 0, parent);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    auto task =
        std::async(std::launch::async, [work = std::move(work), cancel] { return work(*cancel); });
    QEventLoop loop;
    QTimer timer;
    QObject::connect(&progress, &QProgressDialog::canceled, [cancel] { cancel->store(true); });
    QObject::connect(&timer, &QTimer::timeout, [&] {
        if (task.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
            loop.quit();
    });
    timer.start(20);
    progress.show();
    loop.exec();
    progress.close();
    return task.get();
}
void failure(QWidget *owner, const std::filesystem::path &path, const std::exception &e) {
    creative_suite::diagnostics::Logger::instance().log(
        creative_suite::diagnostics::Level::Error, "composition_exchange", "transfer", e.what(),
        {{"path", main_window_detail::pathToUtf8(path)}});
    QMessageBox::warning(owner, QCoreApplication::translate("MainWindow", "Composition Interchange"), QString::fromUtf8(e.what()));
}
} // namespace
void MainWindow::exportCompositionInterchange() {
    if (project_load_pending_)
        return;
    if (!active_timeline_clip_id_) {
        QMessageBox::information(this, QCoreApplication::translate("MainWindow", "Composition Interchange"),
                                 QCoreApplication::translate("MainWindow", "Select a Fusion Composition to export."));
        return;
    }
    auto location = timeline_model_.locateClip(*active_timeline_clip_id_);
    if (!location)
        return;
    const auto clip = timeline_model_.tracks()[location->track_index].clips[location->clip_index];
    QFileDialog dialog(this, "Export Composition Interchange");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter("Composition interchange (*.cscomp)");
    dialog.setDefaultSuffix("cscomp");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    const auto name = dialog.selectedFiles().front();
    const auto path = std::filesystem::path(name.toStdU16String());
    const auto fps = timeline_model_.frameRate();
    const int w = editor_session_.canvasWidth(), h = editor_session_.canvasHeight();
    try {
        const bool saved = worker(this, [=](const std::atomic_bool &canceled) {
            auto d = application::exportComposition(clip, w, h, fps);
            return x::save(path, d, [&] { return canceled.load(); });
        });
        if (saved)
            statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Composition interchange exported."), 5000);
    } catch (const x::Unsupported &e) {
        QMessageBox::information(this, QCoreApplication::translate("MainWindow", "Composition Cannot Be Transferred"),
                                 QString::fromUtf8(e.what()));
    } catch (const std::exception &e) {
        failure(this, path, e);
    }
}
void MainWindow::importCompositionInterchange() {
    if (project_load_pending_)
        return;
    const auto project_generation = project_generation_;
    if (!active_timeline_track_id_) {
        QMessageBox::information(this, QCoreApplication::translate("MainWindow", "Composition Interchange"),
                                 QCoreApplication::translate("MainWindow", "Select a video track before importing."));
        return;
    }
    const auto track = *active_timeline_track_id_;
    const auto position = timelinePlayheadFrame();
    const auto name = QFileDialog::getOpenFileName(this, QCoreApplication::translate("MainWindow", "Import Composition Interchange"), {},
                                                   QCoreApplication::translate("MainWindow", "Composition interchange (*.cscomp)"));
    if (name.isEmpty())
        return;
    const auto path = std::filesystem::path(name.toStdU16String());
    const int w = editor_session_.canvasWidth(), h = editor_session_.canvasHeight();
    const auto fps = timeline_model_.frameRate();
    try {
        auto command = worker(
            this,
            [=](const std::atomic_bool &canceled)
                -> std::optional<application::AddFusionCompositionCommand> {
                auto d = x::load(path, [&] { return canceled.load(); });
                if (!d)
                    return {};
                if (d->width != w || d->height != h ||
                    timeline::reducedFrameRate({d->fps_numerator, d->fps_denominator}) !=
                        timeline::reducedFrameRate(fps))
                    throw x::Unsupported(
                        "The composition canvas and frame rate must match the current project.");
                for (auto& layer : d->layers)
                    if (!layer.source.empty()) layer.source = creative_suite::media::MediaLibrary::canonicalPath(layer.source);
                application::AddFusionCompositionCommand c{track, position, d->duration};
                c.graph = application::importComposition(*d);
                c.display_name = d->name;
                std::vector<std::filesystem::path> paths;
                std::set<std::filesystem::path> unique;
                for (const auto &l : d->layers)
                    if (!l.source.empty() && unique.insert(l.source).second)
                        paths.push_back(l.source);
                auto media = creative_suite::media::MediaImporter{}.process(paths, canceled);
                if (media.cancelled || canceled.load())
                    return {};
                for (auto &item : media.files) {
                    if (!item.item)
                        throw x::Error("Cannot prepare media: " + item.cause);
                    for (const auto &layer : d->layers) {
                        if (!layer.source.empty() &&
                            creative_suite::media::MediaLibrary::canonicalPath(layer.source) ==
                                item.item->metadata.source_path &&
                            ((layer.kind == x::Kind::Image && item.item->metadata.kind != creative_suite::media::MediaKind::Image) ||
                             (layer.kind == x::Kind::Video && item.item->metadata.kind != creative_suite::media::MediaKind::Video)))
                            throw x::Error("Referenced media has changed kind: " +
                                           main_window_detail::pathToUtf8(layer.source));
                    }
                    c.media.push_back(std::move(*item.item));
                }
                return c;
            });
        if (command && project_generation == project_generation_ && edit_workspace_ && edit_workspace_->controller()) {
            const bool has_media = !command->media.empty();
            const auto result = edit_workspace_->controller()->insertPreparedComposition(std::move(*command));
            if (result.changed() && has_media) populateMediaBrowser();
        }
    } catch (const x::Unsupported &e) {
        QMessageBox::information(this, QCoreApplication::translate("MainWindow", "Composition Cannot Be Transferred"),
                                 QString::fromUtf8(e.what()));
    } catch (const std::exception &e) {
        failure(this, path, e);
    }
}
