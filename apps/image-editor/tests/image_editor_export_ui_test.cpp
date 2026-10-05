#include "image_editor_window.h"
#include "image_export_dialog.h"
#include "image_export_controller.h"
#include "image_export_worker.h"

#include <QAction>
#include <QApplication>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileDialog>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTimer>
#include <QSignalSpy>

#include <cmath>
#include <atomic>
#include <functional>
#include <iostream>
#include <memory>

namespace {

bool driveExportAction(
    image_editor::ImageEditorWindow& window,
    const QString& output_path,
    const std::function<bool(image_editor::JpegExportOptionsDialog*)>& options_interaction,
    bool* options_were_seen,
    bool* progress_was_seen = nullptr,
    const QString& action_object_name = QStringLiteral("exportImageAction"),
    bool trigger_layer_panel_button = false) {
    bool file_dialog_was_seen = false;
    bool interaction_failed = false;
    QString last_modal_type;
    QTimer poll;
    poll.setInterval(5);
    QObject::connect(&poll, &QTimer::timeout, &window, [&]() {
        QWidget* modal = QApplication::activeModalWidget();
        if (modal != nullptr) last_modal_type = QString::fromLatin1(modal->metaObject()->className());
        if (auto* file_dialog = qobject_cast<QFileDialog*>(modal)) {
            file_dialog_was_seen = true;
            file_dialog->selectFile(output_path);
            auto* buttons = file_dialog->findChild<QDialogButtonBox*>();
            auto* save_button = buttons == nullptr
                ? nullptr : buttons->button(QDialogButtonBox::Save);
            if (save_button == nullptr) {
                interaction_failed = true;
                file_dialog->reject();
            } else {
                save_button->click();
            }
            return;
        }
        if (auto* options_dialog =
                qobject_cast<image_editor::JpegExportOptionsDialog*>(modal)) {
            if (options_were_seen != nullptr) *options_were_seen = true;
            if (!options_interaction(options_dialog)) interaction_failed = true;
            return;
        }
        if (qobject_cast<image_editor::ImageExportProgressDialog*>(modal) != nullptr &&
            progress_was_seen != nullptr) {
            *progress_was_seen = true;
        }
    });

    QTimer watchdog;
    watchdog.setSingleShot(true);
    QObject::connect(&watchdog, &QTimer::timeout, &window, [&]() {
        interaction_failed = true;
        std::cerr << "Export UI test timed out; last modal: "
                  << last_modal_type.toStdString() << ", file dialog seen: "
                  << file_dialog_was_seen << ".\n";
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            dialog->reject();
        }
    });

    poll.start();
    watchdog.start(10000);
    if (trigger_layer_panel_button) {
        if (auto* button = window.findChild<QPushButton*>(
                QStringLiteral("quickExportLayerButton"))) {
            button->click();
        } else {
            interaction_failed = true;
        }
    } else {
        if (auto* action = window.findChild<QAction*>(action_object_name)) {
            action->trigger();
        } else {
            interaction_failed = true;
        }
    }
    poll.stop();
    watchdog.stop();
    return file_dialog_was_seen && !interaction_failed;
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Creative Suite"));
    QCoreApplication::setApplicationName(QStringLiteral("Image Editor Export UI Tests"));
    QStandardPaths::setTestModeEnabled(true);

    QTemporaryDir temporary;
    if (!temporary.isValid()) return 1;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, temporary.path());

    const QString source_path = temporary.filePath(QStringLiteral("transparent.png"));
    QImage source(32, 32, QImage::Format_ARGB32);
    source.fill(Qt::transparent);
    source.setPixelColor(16, 16, QColor(220, 35, 25, 255));
    if (!source.save(source_path, "PNG")) {
        std::cerr << "Could not create the transparent export fixture.\n";
        return 1;
    }

    const QColor expected_background(28, 116, 205);
    const QString first_output = temporary.filePath(QStringLiteral("custom.jpg"));
    image_editor::ImageEditorWindow first_window;
    auto* quick_export_button = first_window.findChild<QPushButton*>(
        QStringLiteral("quickExportLayerButton"));
    auto* layer_list = first_window.findChild<QTreeWidget*>(
        QStringLiteral("imageLayerTree"));
    auto* layer_panel = first_window.findChild<QWidget*>(
        QStringLiteral("imageEditorLayerPanel"));
    if (quick_export_button == nullptr || layer_list == nullptr || layer_panel == nullptr ||
        layer_panel->layout() == nullptr ||
        layer_panel->layout()->indexOf(quick_export_button) < 0 ||
        layer_panel->layout()->indexOf(layer_list) < 0 ||
        layer_panel->layout()->indexOf(quick_export_button) >=
            layer_panel->layout()->indexOf(layer_list) ||
        quick_export_button->isEnabled()) {
        std::cerr << "The layer-panel Quick Export button is missing, misplaced, or initially enabled.\n";
        return 1;
    }
    if (!first_window.openImagePath(source_path)) {
        std::cerr << "Could not open the transparent export fixture.\n";
        return 1;
    }
    if (!quick_export_button->isEnabled()) {
        std::cerr << "The layer-panel Quick Export button stayed disabled for an open image.\n";
        return 1;
    }
    bool first_options_seen = false;
    const bool first_export_completed = driveExportAction(
        first_window, first_output,
        [expected_background](image_editor::JpegExportOptionsDialog* dialog) {
            if (dialog->findChild<QSlider*>(QStringLiteral("jpegQualitySlider")) == nullptr ||
                dialog->findChild<QSpinBox*>(QStringLiteral("jpegQualitySpinBox")) == nullptr ||
                dialog->findChild<QPushButton*>(QStringLiteral("jpegBackgroundButton")) == nullptr) {
                return false;
            }
            const auto defaults = dialog->options();
            if (defaults.jpeg_quality != 95 || defaults.jpeg_background != Qt::white) {
                return false;
            }
            auto* quality = dialog->findChild<QSpinBox*>(QStringLiteral("jpegQualitySpinBox"));
            quality->setValue(37);
            dialog->setBackgroundColor(expected_background);
            dialog->accept();
            return true;
        }, &first_options_seen);
    if (!first_export_completed || !first_options_seen || !QFile::exists(first_output)) {
        std::cerr << "The JPEG options flow did not complete an export.\n";
        return 1;
    }

    const QImage exported(first_output);
    const QColor matte_pixel = exported.pixelColor(2, 2);
    if (exported.isNull() || std::abs(matte_pixel.red() - expected_background.red()) >= 12 ||
        std::abs(matte_pixel.green() - expected_background.green()) >= 12 ||
        std::abs(matte_pixel.blue() - expected_background.blue()) >= 12) {
        std::cerr << "The UI export did not apply the selected JPEG background color.\n";
        return 1;
    }

    QSettings stored_settings;
    stored_settings.beginGroup(QStringLiteral("ImageEditor/Export"));
    const int stored_quality = stored_settings.value(QStringLiteral("jpegQuality")).toInt();
    const QColor stored_background(
        stored_settings.value(QStringLiteral("jpegBackground")).toString());
    stored_settings.endGroup();
    if (stored_quality != 37 || stored_background != expected_background) {
        std::cerr << "The accepted JPEG preferences were not persisted.\n";
        return 1;
    }

    QAction* quick_export_action = first_window.findChild<QAction*>(
        QStringLiteral("quickExportImageAction"));
    bool quick_export_is_in_file_menu = false;
    for (QAction* menu_entry : first_window.menuBar()->actions()) {
        if (menu_entry->text() == QStringLiteral("File") && menu_entry->menu() != nullptr) {
            quick_export_is_in_file_menu =
                menu_entry->menu()->actions().contains(quick_export_action);
            break;
        }
    }
    if (quick_export_action == nullptr || !quick_export_action->isEnabled() ||
        !quick_export_action->shortcut().isEmpty() || !quick_export_is_in_file_menu) {
        std::cerr << "Quick Export was not registered for an open image.\n";
        return 1;
    }

    const QString png_output = temporary.filePath(QStringLiteral("transparent-export.png"));
    bool png_options_seen = false;
    bool png_progress_seen = false;
    const bool png_export_completed = driveExportAction(
        first_window, png_output,
        [&png_options_seen](image_editor::JpegExportOptionsDialog*) {
            png_options_seen = true;
            return false;
        }, &png_options_seen, &png_progress_seen);
    const QImage exported_png(png_output);
    if (!png_export_completed || !png_progress_seen || !QFile::exists(png_output) || png_options_seen ||
        exported_png.isNull() || exported_png.pixelColor(2, 2).alpha() != 0) {
        std::cerr << "PNG export skipped progress, showed JPEG controls, or lost transparency.\n";
        return 1;
    }

    const QString quick_png_output = temporary.filePath(QStringLiteral("quick-layer.png"));
    bool quick_png_options_seen = false;
    bool quick_png_progress_seen = false;
    const bool quick_png_completed = driveExportAction(
        first_window, quick_png_output,
        [&quick_png_options_seen](image_editor::JpegExportOptionsDialog*) {
            quick_png_options_seen = true;
            return false;
        }, &quick_png_options_seen, &quick_png_progress_seen,
        QStringLiteral("quickExportImageAction"), true);
    const QImage quick_png(quick_png_output);
    if (!quick_png_completed || !quick_png_progress_seen || quick_png_options_seen ||
        quick_png.isNull() || quick_png.size() != source.size() ||
        quick_png.pixelColor(16, 16).alpha() != 0) {
        std::cerr << "Quick PNG export included the background or opened JPEG options.\n";
        return 1;
    }

    const QString quick_jpeg_output = temporary.filePath(QStringLiteral("quick-layer.jpg"));
    bool quick_jpeg_options_seen = false;
    bool quick_jpeg_progress_seen = false;
    const bool quick_jpeg_completed = driveExportAction(
        first_window, quick_jpeg_output,
        [&quick_jpeg_options_seen](image_editor::JpegExportOptionsDialog*) {
            quick_jpeg_options_seen = true;
            return false;
        }, &quick_jpeg_options_seen, &quick_jpeg_progress_seen,
        QStringLiteral("quickExportImageAction"));
    const QImage quick_jpeg(quick_jpeg_output);
    const QColor quick_jpeg_matte = quick_jpeg.pixelColor(16, 16);
    if (!quick_jpeg_completed || !quick_jpeg_progress_seen || quick_jpeg_options_seen ||
        quick_jpeg.isNull() ||
        std::abs(quick_jpeg_matte.red() - expected_background.red()) >= 12 ||
        std::abs(quick_jpeg_matte.green() - expected_background.green()) >= 12 ||
        std::abs(quick_jpeg_matte.blue() - expected_background.blue()) >= 12) {
        std::cerr << "Quick JPEG export did not reuse saved settings without an options dialog.\n";
        return 1;
    }

    const QString cancelled_output = temporary.filePath(QStringLiteral("cancelled.jpg"));
    image_editor::ImageEditorWindow second_window;
    if (!second_window.openImagePath(source_path)) return 1;
    bool persisted_options_seen = false;
    const bool second_flow_completed = driveExportAction(
        second_window, cancelled_output,
        [&persisted_options_seen, expected_background](
            image_editor::JpegExportOptionsDialog* dialog) {
            const auto options = dialog->options();
            persisted_options_seen = options.jpeg_quality == 37 &&
                options.jpeg_background == expected_background;
            dialog->reject();
            return persisted_options_seen;
        }, nullptr);
    if (!second_flow_completed || !persisted_options_seen ||
        QFile::exists(cancelled_output)) {
        std::cerr << "JPEG preferences were not restored or cancelling options wrote a file.\n";
        return 1;
    }

    image_editor::ImageExportProgressDialog progress;
    QSignalSpy cancel_spy(&progress,
                          &image_editor::ImageExportProgressDialog::cancelRequested);
    progress.show();
    progress.setPhaseText(QStringLiteral("Encoding image…"));
    auto* cancel_button = progress.findChild<QPushButton*>(
        QStringLiteral("imageExportCancelButton"));
    auto* progress_label = progress.findChild<QLabel*>(
        QStringLiteral("imageExportProgressLabel"));
    auto* progress_bar = progress.findChild<QProgressBar*>(
        QStringLiteral("imageExportProgressBar"));
    if (cancel_button == nullptr || progress_label == nullptr || progress_bar == nullptr) return 1;
    cancel_button->click();
    if (cancel_spy.size() != 1 || cancel_button->isEnabled() ||
        !progress_label->text().contains(QStringLiteral("discarded")) ||
        progress_bar->minimum() != 0 || progress_bar->maximum() != 0) {
        std::cerr << "The export progress dialog did not enter its cancellation state.\n";
        return 1;
    }
    progress.finish();

    image_editor::ImageExportProgressDialog render_progress;
    render_progress.setPhaseText(QStringLiteral("Rendering image…"));
    auto* render_cancel_button = render_progress.findChild<QPushButton*>(
        QStringLiteral("imageExportCancelButton"));
    auto* render_progress_label = render_progress.findChild<QLabel*>(
        QStringLiteral("imageExportProgressLabel"));
    if (render_cancel_button == nullptr || render_progress_label == nullptr) return 1;
    render_cancel_button->click();
    if (!render_progress_label->text().contains(QStringLiteral("Cancelling"))) {
        std::cerr << "Rendering cancellation showed an encoding-only message.\n";
        return 1;
    }
    render_progress.finish();

    image_editor::ImageExportSnapshot snapshot;
    snapshot.source_image = source;
    image_editor::ImageLayerData background;
    background.id = QStringLiteral("background");
    background.name = QStringLiteral("Background");
    background.background = true;
    snapshot.document.layers.push_back(background);

    const QString render_cancelled_path = temporary.filePath(
        QStringLiteral("worker-render-cancelled.png"));
    auto render_cancellation = std::make_shared<std::atomic_bool>(false);
    image_editor::ImageExportWorker render_worker(
        snapshot, render_cancelled_path, {}, render_cancellation);
    bool render_succeeded = false;
    bool render_was_cancelled = false;
    QObject::connect(&render_worker, &image_editor::ImageExportWorker::phaseChanged,
                     &render_worker, [render_cancellation](const QString& phase) {
        if (phase == QStringLiteral("Rendering image…")) {
            render_cancellation->store(true, std::memory_order_relaxed);
        }
    }, Qt::DirectConnection);
    QObject::connect(&render_worker, &image_editor::ImageExportWorker::finished,
                     &render_worker, [&render_succeeded, &render_was_cancelled](
                         bool succeeded, bool was_cancelled, const QString&) {
        render_succeeded = succeeded;
        render_was_cancelled = was_cancelled;
    }, Qt::DirectConnection);
    render_worker.run();
    if (render_succeeded || !render_was_cancelled ||
        QFile::exists(render_cancelled_path)) {
        std::cerr << "The export worker did not cancel cleanly during rendering.\n";
        return 1;
    }

    const QString commit_cancelled_path = temporary.filePath(
        QStringLiteral("worker-commit-cancelled.jpg"));
    const QByteArray previous_destination = QByteArrayLiteral("existing destination");
    QFile previous_destination_file(commit_cancelled_path);
    if (!previous_destination_file.open(QIODevice::WriteOnly) ||
        previous_destination_file.write(previous_destination) != previous_destination.size()) {
        std::cerr << "Could not seed the export worker destination.\n";
        return 1;
    }
    previous_destination_file.close();

    auto commit_cancellation = std::make_shared<std::atomic_bool>(false);
    image_editor::ImageExportWorker commit_worker(
        snapshot, commit_cancelled_path, {}, commit_cancellation);
    bool commit_succeeded = false;
    bool commit_was_cancelled = false;
    QObject::connect(&commit_worker, &image_editor::ImageExportWorker::phaseChanged,
                     &commit_worker, [commit_cancellation](const QString& phase) {
        if (phase == QStringLiteral("Finalizing export…")) {
            commit_cancellation->store(true, std::memory_order_relaxed);
        }
    }, Qt::DirectConnection);
    QObject::connect(&commit_worker, &image_editor::ImageExportWorker::finished,
                     &commit_worker, [&commit_succeeded, &commit_was_cancelled](
                         bool succeeded, bool was_cancelled, const QString&) {
        commit_succeeded = succeeded;
        commit_was_cancelled = was_cancelled;
    }, Qt::DirectConnection);
    commit_worker.run();
    QFile unchanged_destination(commit_cancelled_path);
    if (commit_succeeded || !commit_was_cancelled ||
        !unchanged_destination.open(QIODevice::ReadOnly) ||
        unchanged_destination.readAll() != previous_destination) {
        std::cerr << "The export worker replaced a destination after cancellation.\n";
        return 1;
    }

    image_editor::ImageDocumentSession controller_session;
    QString controller_error;
    if (!controller_session.openImage(source_path, &controller_error)) {
        std::cerr << "Could not create the export controller snapshot: "
                  << controller_error.toStdString() << ".\n";
        return 1;
    }
    const auto controller_snapshot = controller_session.exportSnapshot();
    const QString controller_output = temporary.filePath(
        QStringLiteral("controller-export.png"));
    const auto controller_result = image_editor::ImageExportController::run(
        nullptr, controller_snapshot, controller_output);
    if (controller_result.status != image_editor::ImageExportStatus::Succeeded ||
        !controller_result.error.isEmpty() || !QFile::exists(controller_output)) {
        std::cerr << "The export controller did not complete and report a successful export.\n";
        return 1;
    }

    const QString controller_failure_output = temporary.filePath(
        QStringLiteral("missing-controller-directory/export.png"));
    const auto controller_failure = image_editor::ImageExportController::run(
        nullptr, controller_snapshot, controller_failure_output);
    if (controller_failure.status != image_editor::ImageExportStatus::Failed ||
        controller_failure.error.isEmpty() || QFile::exists(controller_failure_output)) {
        std::cerr << "The export controller did not return the output failure.\n";
        return 1;
    }

    return 0;
}
