#include "main_window.h"
#include "model/composition_interchange.h"
#include <QEventLoop>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QStatusBar>
#include <QTimer>
#include <future>
namespace motion::ui {
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
} // namespace
void MainWindow::exportCompositionInterchange() {
    if (!document_ || audio_keyframe_worker_ || open_cancel_requested_)
        return;
    finishPendingTransformEdit();
    QFileDialog dialog(this, "Export Composition Interchange");
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilter("Composition interchange (*.cscomp)");
    dialog.setDefaultSuffix("cscomp");
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    const auto file = dialog.selectedFiles().front();
    auto path = std::filesystem::path(file.toStdU16String());
    auto project = projectData();
    try {
        bool saved = worker(this, [path,
                                   project = std::move(project)](const std::atomic_bool &cancel) {
            return x::save(path, model::exportComposition(project), [&] { return cancel.load(); });
        });
        if (saved)
            statusBar()->showMessage("Composition interchange exported.", 5000);
    } catch (const x::Unsupported &e) {
        QMessageBox::information(this, "Composition Cannot Be Transferred",
                                 QString::fromUtf8(e.what()));
    } catch (const std::exception &e) {
        reportDocumentError("export_composition_interchange", path, e);
    }
}
void MainWindow::importCompositionInterchange() {
    if (audio_keyframe_worker_ || open_cancel_requested_)
        return;
    finishPendingTransformEdit();
    auto file = QFileDialog::getOpenFileName(this, "Import Composition Interchange", {},
                                             "Composition interchange (*.cscomp)");
    if (file.isEmpty())
        return;
    auto path = std::filesystem::path(file.toStdU16String());
    try {
        auto project = worker(
            this,
            [path](const std::atomic_bool &cancel) -> std::optional<model::MotionProjectData> {
                auto d = x::load(path, [&] { return cancel.load(); });
                if (!d)
                    return {};
                auto result = model::importComposition(*d);
                if (cancel.load())
                    return {};
                return result;
            });
        if (project)
            stageOpenProject({}, std::move(*project), true);
    } catch (const x::Unsupported &e) {
        QMessageBox::information(this, "Composition Cannot Be Transferred",
                                 QString::fromUtf8(e.what()));
    } catch (const std::exception &e) {
        reportDocumentError("import_composition_interchange", path, e);
    }
}
} // namespace motion::ui
