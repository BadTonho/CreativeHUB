#include "main_window.h"
#include "main_window_support.h"

#include "ui/media_pool/media_pool_widget.h"

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/media_library.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMenu>
#include <QMessageBox>
#include <QMetaObject>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QThreadPool>
#include <QUuid>
#include <QRunnable>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <memory>
#include <system_error>
#include <utility>

namespace motion::ui {
namespace {

using creative_suite::media::RgbaFrame;
using creative_suite::media::RgbaFramePtr;
using creative_suite::media::MediaLibrary;
using detail::pathForDisplay;
using detail::pathForLog;
using detail::pathFromQString;

QString linkedImageExecutable()
{
#if defined(Q_OS_WIN)
    const QString binary = QStringLiteral("creative-suite-image-editor.exe");
#else
    const QString binary = QStringLiteral("creative-suite-image-editor");
#endif
    QSettings settings;
    const auto configured = settings.value(
        QStringLiteral("applications/image_editor_executable")).toString();
    if (!configured.isEmpty() && QFileInfo(configured).isFile()) return configured;

    const QDir app_dir(QCoreApplication::applicationDirPath());
    const QStringList candidates{
        app_dir.filePath(binary),
        app_dir.filePath(QStringLiteral("../image-editor/Release/") + binary),
        app_dir.filePath(QStringLiteral("../image-editor/Debug/") + binary),
        app_dir.filePath(QStringLiteral("../../image-editor/Release/") + binary),
        app_dir.filePath(QStringLiteral("../../image-editor/Debug/") + binary)};
    for (const auto& candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.isFile() && info.isExecutable()) return info.absoluteFilePath();
    }
    return QStandardPaths::findExecutable(QStringLiteral("creative-suite-image-editor"));
}

RgbaFramePtr frameFromImage(QImage image)
{
    if (image.isNull()) return {};
    image = image.convertToFormat(QImage::Format_RGBA8888);
    if (image.isNull()) return {};
    auto frame = std::make_shared<RgbaFrame>();
    frame->width = image.width();
    frame->height = image.height();
    frame->stride = frame->width * 4;
    frame->rgba_pixels.resize(static_cast<std::size_t>(frame->stride) *
                               static_cast<std::size_t>(frame->height));
    for (int y = 0; y < frame->height; ++y) {
        std::memcpy(frame->rgba_pixels.data() +
                        static_cast<std::size_t>(y) * frame->stride,
                    image.constScanLine(y), static_cast<std::size_t>(frame->stride));
    }
    return frame;
}

QImage imageFromFrame(const RgbaFramePtr& frame)
{
    if (frame == nullptr || frame->width <= 0 || frame->height <= 0 ||
        frame->stride < frame->width * 4 ||
        frame->rgba_pixels.size() < static_cast<std::size_t>(frame->stride) *
            static_cast<std::size_t>(frame->height)) return {};
    QImage image(frame->width, frame->height, QImage::Format_RGBA8888);
    if (image.isNull()) return {};
    for (int y = 0; y < frame->height; ++y) {
        std::memcpy(image.scanLine(y), frame->rgba_pixels.data() +
                        static_cast<std::size_t>(y) * frame->stride,
                    static_cast<std::size_t>(frame->width) * 4);
    }
    return image;
}

std::filesystem::path linkedImageDirectory(
    const std::filesystem::path& motion_path,
    model::LayerId layer_id,
    const QString& identity)
{
    auto root = motion_path;
    root += ".motion-studio";
    return root / "image-editor" /
        (std::to_string(layer_id) + "-" + identity.toUtf8().toStdString());
}

std::pair<std::int64_t, std::int64_t> fileSignature(
    const std::filesystem::path& path)
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) return {-1, -1};
    const auto size = std::filesystem::file_size(path, error);
    if (error) return {-1, -1};
    const auto modified = std::filesystem::last_write_time(path, error);
    if (error) return {-1, -1};
    return {static_cast<std::int64_t>(size),
            static_cast<std::int64_t>(modified.time_since_epoch().count())};
}

} // namespace

void MainWindow::showLayerContextMenu(model::LayerId id,
                                      const QPoint& global_position)
{
    selectLayer(id);
    if (!document_) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [id](const auto& layer) { return layer.id == id; });
    if (found == document_->layers().end()) return;
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("motion-layer-context-menu"));
    if (found->kind == model::LayerKind::Image) {
        auto* edit = menu.addAction(QStringLiteral("Edit Image in Image Editor"));
        edit->setObjectName(QStringLiteral("motion-context-edit-image-action"));
        connect(edit, &QAction::triggered, this, &MainWindow::editSelectedLayerImage);
    }
    auto* remove = menu.addAction(QStringLiteral("Remove Layer"));
    connect(remove, &QAction::triggered, this, [this, id] {
        if (!document_) return;
        finishPendingTransformEdit();
        auto before = captureEditState();
        if (!document_->removeLayer(id)) return;
        (void)recordCompositionEdit(std::move(before));
        if (selected_layer_id_ == id) selected_layer_id_ = 0;
        updateDocumentState();
        refreshTimeline();
        syncTransformInspector();
        requestPreview();
    });
    menu.exec(global_position);
}

void MainWindow::editSelectedLayerImage()
{
    if (!document_ || selected_layer_id_ == 0 || audio_keyframe_worker_ || export_worker_) return;
    finishPendingTransformEdit();
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end() || found->kind != model::LayerKind::Image) return;

    if (!document_path_.has_value() && !saveCompositionAs()) return;
    if (!document_path_.has_value()) return;

    auto link = found->linked_image;
    bool new_link = !link.has_value();
    if (!link.has_value()) {
        const auto identity = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto directory = linkedImageDirectory(*document_path_, found->id, identity);
        link = model::LinkedImageDocument{
            MediaLibrary::canonicalPath(directory / "composition.cimg"),
            MediaLibrary::canonicalPath(directory / "published.png"),
            MediaLibrary::canonicalPath(directory / "source.png")};
    }

    std::error_code directory_error;
    std::filesystem::create_directories(link->document_path.parent_path(), directory_error);
    if (directory_error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "image_editor_compatibility", "create_link_directory",
            directory_error.message(), {{"path", pathForLog(link->document_path.parent_path())}});
        QMessageBox::warning(this, QStringLiteral("Image Editor Link Error"),
            QStringLiteral("The linked image folder could not be created."));
        return;
    }

    QFileInfo snapshot_info(pathForDisplay(link->source_snapshot_path));
    if (!snapshot_info.isFile()) {
        QImage source(pathForDisplay(found->source_path));
        if (source.isNull() && media_pool_ != nullptr)
            source = imageFromFrame(media_pool_->sharedFirstFrameForPath(found->source_path));
        if (source.isNull() || !source.save(pathForDisplay(link->source_snapshot_path), "PNG")) {
            const QString cause = QStringLiteral(
                "The original image could not be copied to the linked-image recovery file.");
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Error,
                "image_editor_compatibility", "create_source_snapshot",
                cause.toUtf8().toStdString(),
                {{"source", pathForLog(found->source_path)},
                 {"snapshot", pathForLog(link->source_snapshot_path)}});
            QMessageBox::warning(this, QStringLiteral("Image Editor Link Error"), cause);
            return;
        }
    }

    if (new_link) {
        const auto previous = found->linked_image;
        if (!document_->setLayerLinkedImage(found->id, link) || !saveToPath(*document_path_)) {
            (void)document_->setLayerLinkedImage(found->id, previous);
            updateDocumentState();
            return;
        }
        updateDocumentState();
        initializeLinkedImageFrames();
    }

    auto executable = linkedImageExecutable();
    if (executable.isEmpty()) {
        executable = QFileDialog::getOpenFileName(
            this, QStringLiteral("Locate Image Editor"),
            QCoreApplication::applicationDirPath(),
            QStringLiteral("Image Editor executable (*)"));
        if (executable.isEmpty()) return;
        QSettings settings;
        settings.setValue(QStringLiteral("applications/image_editor_executable"), executable);
        settings.sync();
    }

    const QStringList arguments{
        QStringLiteral("--linked-source"), pathForDisplay(link->source_snapshot_path),
        QStringLiteral("--linked-document"), pathForDisplay(link->document_path),
        QStringLiteral("--publish-output"), pathForDisplay(link->published_output_path)};
    if (!QProcess::startDetached(executable, arguments,
                                 QFileInfo(executable).absolutePath())) {
        const QString cause = QStringLiteral("The Image Editor process could not be started.");
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "image_editor_compatibility", "launch_image_editor",
            cause.toUtf8().toStdString(),
            {{"executable", executable.toUtf8().toStdString()},
             {"document_path", pathForLog(link->document_path)},
             {"layer_id", std::to_string(found->id)}});
        QMessageBox::warning(this, QStringLiteral("Could Not Start Image Editor"), cause);
        return;
    }
    statusBar()->showMessage(
        QStringLiteral("Image Editor opened for this Motion layer. Save there to refresh this layer."),
        6000);
}

void MainWindow::initializeLinkedImageFrames()
{
    ++linked_image_generation_;
    linked_image_frames_.clear();
    linked_image_signatures_.clear();
    linked_image_decode_requests_.clear();
    if (!document_) return;
    for (const auto& layer : document_->layers()) {
        if (layer.kind != model::LayerKind::Image || !layer.linked_image.has_value()) continue;
        const auto& link = *layer.linked_image;
        linked_image_signatures_[layer.id] = fileSignature(link.published_output_path);
        const auto output = QFileInfo(pathForDisplay(link.published_output_path));
        const auto snapshot = QFileInfo(pathForDisplay(link.source_snapshot_path));
        const auto path = output.isFile() ? link.published_output_path
            : snapshot.isFile() ? link.source_snapshot_path : layer.source_path;
        if (!path.empty()) decodeLinkedImageFrame(layer.id, path, output.isFile());
        else if (statusBar() != nullptr)
            statusBar()->showMessage(
                QStringLiteral("A linked image is missing; use Edit Image in Image Editor to repair it."),
                9000);
    }
}

void MainWindow::refreshLinkedImagePublications()
{
    if (!document_) return;
    for (const auto& layer : document_->layers()) {
        if (layer.kind != model::LayerKind::Image || !layer.linked_image.has_value()) continue;
        const auto& link = *layer.linked_image;
        const auto signature = fileSignature(link.published_output_path);
        const auto existing = linked_image_signatures_.find(layer.id);
        if (existing == linked_image_signatures_.end()) {
            linked_image_signatures_[layer.id] = signature;
            if (signature.first >= 0)
                decodeLinkedImageFrame(layer.id, link.published_output_path, true);
            continue;
        }
        if (existing->second == signature) continue;
        existing->second = signature;
        if (signature.first >= 0) {
            decodeLinkedImageFrame(layer.id, link.published_output_path, true);
        } else if (!linked_image_frames_.contains(layer.id)) {
            const QFileInfo snapshot(pathForDisplay(link.source_snapshot_path));
            const auto fallback = snapshot.isFile()
                ? link.source_snapshot_path : layer.source_path;
            if (!fallback.empty()) decodeLinkedImageFrame(layer.id, fallback, false);
            else if (statusBar() != nullptr)
                statusBar()->showMessage(
                    QStringLiteral("A linked image is missing; use Edit Image in Image Editor to repair it."),
                    9000);
        }
    }
}

void MainWindow::decodeLinkedImageFrame(model::LayerId id,
                                        const std::filesystem::path& path,
                                        bool published_output)
{
    const auto generation = linked_image_generation_;
    const auto request = ++linked_image_decode_requests_[id];
    const QPointer<MainWindow> owner(this);
    const auto encoded_path = pathForDisplay(path);
    QThreadPool::globalInstance()->start(QRunnable::create(
        [owner, id, path, encoded_path, published_output, generation, request] {
            QImageReader reader(encoded_path);
            reader.setAutoTransform(true);
            reader.setDecideFormatFromContent(true);
            const auto decoded = reader.read();
            const auto frame = frameFromImage(decoded);
            const QString error = reader.errorString();
            if (owner.isNull()) return;
            QMetaObject::invokeMethod(owner, [owner, id, path, published_output,
                                               generation, request, frame, error] {
                if (owner.isNull() || generation != owner->linked_image_generation_ ||
                    owner->linked_image_decode_requests_[id] != request) return;
                if (frame == nullptr) {
                    creative_suite::diagnostics::Logger::instance().log(
                        creative_suite::diagnostics::Level::Error,
                        "image_editor_compatibility",
                        published_output ? "decode_published_image"
                                         : "decode_link_recovery_image",
                        error.toUtf8().toStdString(),
                        {{"path", pathForLog(path)}, {"layer_id", std::to_string(id)}});
                    if (published_output && !owner->linked_image_frames_.contains(id) &&
                        owner->document_.has_value()) {
                        const auto layer = std::find_if(
                            owner->document_->layers().begin(), owner->document_->layers().end(),
                            [id](const auto& candidate) { return candidate.id == id; });
                        if (layer != owner->document_->layers().end() &&
                            layer->linked_image.has_value()) {
                            const auto& link = *layer->linked_image;
                            const QFileInfo snapshot(pathForDisplay(link.source_snapshot_path));
                            const auto fallback = snapshot.isFile()
                                ? link.source_snapshot_path : layer->source_path;
                            if (!fallback.empty() && fallback != path &&
                                QFileInfo::exists(pathForDisplay(fallback))) {
                                owner->decodeLinkedImageFrame(id, fallback, false);
                            } else if (owner->statusBar() != nullptr) {
                                owner->statusBar()->showMessage(
                                    QStringLiteral("The linked image needs repair; no valid publication or recovery image is available."),
                                    9000);
                            }
                        }
                    } else if (!published_output &&
                               !owner->linked_image_frames_.contains(id) &&
                               owner->statusBar() != nullptr) {
                        owner->statusBar()->showMessage(
                            QStringLiteral("The linked image needs repair; its recovery image could not be opened."),
                            9000);
                    } else if (published_output && owner->statusBar() != nullptr) {
                        owner->statusBar()->showMessage(
                            QStringLiteral("Image publication could not be decoded; keeping the last valid frame."),
                            6000);
                    }
                    return;
                }
                owner->linked_image_frames_[id] = frame;
                owner->requestPreview();
            }, Qt::QueuedConnection);
        }));
}

} // namespace motion::ui
