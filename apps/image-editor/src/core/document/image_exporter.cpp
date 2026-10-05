#include "image_exporter.h"
#include "image_document_renderer.h"
#include "image_document_utils.h"

#include <QDir>
#include <QFileInfo>
#include <QImageWriter>
#include <QPainter>
#include <QSaveFile>

#include <algorithm>

namespace image_editor {
ImageExportResult exportImageSnapshot(
    const ImageExportSnapshot& snapshot,
    const QString& output_path,
    const ImageExportOptions& options,
    const std::atomic_bool* cancellation_requested,
    const ImageExportProgressCallback& progress) {
    const auto failed = [](const QString& cause) {
        return ImageExportResult{ImageExportStatus::Failed, cause};
    };
    if (snapshot.source_image.isNull()) {
        return failed(QStringLiteral("Open or relink an image before exporting."));
    }
    const QString suffix = QFileInfo(output_path).suffix().toLower();
    const QString output_absolute = absoluteCleanPath(output_path);
    for (const auto& layer : snapshot.document.layers) for (const auto& op : layer.operations)
        if (op.kind == OperationKind::RasterImage &&
            absoluteCleanPath(op.raster.source_path).compare(output_absolute,
#ifdef Q_OS_WIN
                Qt::CaseInsensitive
#else
                Qt::CaseSensitive
#endif
            ) == 0) return failed(QStringLiteral("Choose an output path that preserves the imported source image."));
    QByteArray format;
    if (suffix == "png") format = "png";
    else if (suffix == "jpg" || suffix == "jpeg") format = "jpeg";
    else {
        return failed(QStringLiteral("Export supports PNG and JPEG files."));
    }

    if (format == "jpeg" && (options.jpeg_quality < 0 || options.jpeg_quality > 100)) {
        return failed(QStringLiteral("JPEG quality must be between 0 and 100."));
    }
    if (format == "jpeg" &&
        (!options.jpeg_background.isValid() || options.jpeg_background.alpha() != 255)) {
        return failed(QStringLiteral("Choose an opaque background color for JPEG export."));
    }
    if (options.scope != ImageExportScope::Composite &&
        options.scope != ImageExportScope::SelectedLayer &&
        options.scope != ImageExportScope::SelectedGroup) {
        return failed(QStringLiteral("The requested image export scope is invalid."));
    }
    if (options.scope == ImageExportScope::SelectedLayer &&
        std::none_of(snapshot.document.layers.cbegin(), snapshot.document.layers.cend(),
                     [&snapshot](const ImageLayerData& layer) {
                         return layer.id == snapshot.selected_layer_id;
                     })) {
        return failed(QStringLiteral("The selected layer is unavailable for export."));
    }
    if (options.scope == ImageExportScope::SelectedGroup &&
        findGroup(snapshot.document, snapshot.selected_group_id) == nullptr) {
        return failed(QStringLiteral("The selected group is unavailable for export."));
    }
    for (const auto& layer : snapshot.document.layers) {
        const auto* group = findGroup(snapshot.document, layer.parent_group_id);
        if (!layer.visible || layer.opacity == 0 ||
            (group && (!group->visible || group->opacity == 0)) ||
            (options.scope == ImageExportScope::SelectedLayer && layer.id != snapshot.selected_layer_id) ||
            (options.scope == ImageExportScope::SelectedGroup && layer.parent_group_id != snapshot.selected_group_id)) continue;
        for (const auto& op : layer.operations) {
            if (op.kind != OperationKind::RasterImage) continue;
            const auto source = snapshot.raster_images.value(op.raster.id,
                snapshot.raster_images.value(op.raster.source_path));
            if (source.isNull() || source.size() != op.raster.source_size)
                return failed(QStringLiteral("Relink the unavailable image before exporting: %1").arg(op.raster.source_path));
        }
    }
    if (exportWasCancelled(cancellation_requested)) {
        return {ImageExportStatus::Cancelled, {}};
    }
    if (progress) progress(ImageExportPhase::Rendering);

    QImage rendered;
    if (options.scope == ImageExportScope::SelectedLayer) {
        rendered = ImageDocumentRenderer::selectedLayer(snapshot.document, snapshot.source_image,
            snapshot.raster_images, snapshot.selected_layer_id, cancellation_requested);
    } else if (options.scope == ImageExportScope::SelectedGroup) {
        rendered = ImageDocumentRenderer::selectedGroup(snapshot.document, snapshot.source_image,
            snapshot.raster_images, snapshot.selected_group_id, cancellation_requested);
    } else {
        rendered = ImageDocumentRenderer::composite(snapshot.document, snapshot.source_image,
            snapshot.raster_images, {}, cancellation_requested);
    }
    if (exportWasCancelled(cancellation_requested)) {
        return {ImageExportStatus::Cancelled, {}};
    }
    if (rendered.isNull()) {
        return failed(QStringLiteral("The image could not be rendered for export."));
    }

    if (format == "jpeg") {
        QImage flattened(rendered.size(), QImage::Format_RGB32);
        if (flattened.isNull()) {
            return failed(QStringLiteral("Not enough memory to prepare the JPEG image."));
        }
        flattened.fill(options.jpeg_background);
        QPainter painter(&flattened);
        painter.drawImage(0, 0, rendered);
        painter.end();
        rendered = std::move(flattened);
    }

    if (exportWasCancelled(cancellation_requested)) {
        return {ImageExportStatus::Cancelled, {}};
    }
    if (progress) progress(ImageExportPhase::Encoding);
    if (exportWasCancelled(cancellation_requested)) {
        return {ImageExportStatus::Cancelled, {}};
    }

    QSaveFile output(output_path);
    if (!output.open(QIODevice::WriteOnly)) {
        return failed(output.errorString());
    }
    QImageWriter writer(&output, format);
    if (format == "jpeg") writer.setQuality(options.jpeg_quality);
    if (!writer.write(rendered)) {
        const QString cause = writer.errorString();
        output.cancelWriting();
        return failed(cause);
    }
    if (progress) progress(ImageExportPhase::Finalizing);
    if (exportWasCancelled(cancellation_requested)) {
        output.cancelWriting();
        return {ImageExportStatus::Cancelled, {}};
    }
    if (!output.commit()) {
        return failed(output.errorString());
    }
    return {ImageExportStatus::Succeeded, {}};
}


} // namespace image_editor
