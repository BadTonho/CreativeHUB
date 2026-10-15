#include "performance_metrics_panel.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <array>
#include <cmath>

#include <QCoreApplication>
namespace image_editor {
namespace {

QString performanceStageDisplayName(ImageEditorPerformanceStage stage) {
    switch (stage) {
    case ImageEditorPerformanceStage::OperationReplay: return QCoreApplication::translate("ImageEditorUI", "Operation replay");
    case ImageEditorPerformanceStage::PaintStroke: return QCoreApplication::translate("ImageEditorUI", "Paint stroke");
    case ImageEditorPerformanceStage::EraseStroke: return QCoreApplication::translate("ImageEditorUI", "Erase stroke");
    case ImageEditorPerformanceStage::LinearGradient: return QCoreApplication::translate("ImageEditorUI", "Linear gradient");
    case ImageEditorPerformanceStage::Shape: return QCoreApplication::translate("ImageEditorUI", "Shape");
    case ImageEditorPerformanceStage::Text: return QCoreApplication::translate("ImageEditorUI", "Text");
    case ImageEditorPerformanceStage::RasterImage: return QCoreApplication::translate("ImageEditorUI", "Raster image");
    case ImageEditorPerformanceStage::Transform: return QCoreApplication::translate("ImageEditorUI", "Transform");
    case ImageEditorPerformanceStage::MaskApplication: return QCoreApplication::translate("ImageEditorUI", "Mask application");
    case ImageEditorPerformanceStage::LayerComposition: return QCoreApplication::translate("ImageEditorUI", "Layer composition");
    case ImageEditorPerformanceStage::GroupComposition: return QCoreApplication::translate("ImageEditorUI", "Group composition");
    case ImageEditorPerformanceStage::Composite: return QCoreApplication::translate("ImageEditorUI", "Composite");
    case ImageEditorPerformanceStage::SelectedLayerRender: return QCoreApplication::translate("ImageEditorUI", "Selected layer render");
    case ImageEditorPerformanceStage::SelectedGroupRender: return QCoreApplication::translate("ImageEditorUI", "Selected group render");
    case ImageEditorPerformanceStage::LayerThumbnail: return QCoreApplication::translate("ImageEditorUI", "Layer thumbnail");
    case ImageEditorPerformanceStage::GroupThumbnail: return QCoreApplication::translate("ImageEditorUI", "Group thumbnail");
    case ImageEditorPerformanceStage::MaskThumbnail: return QCoreApplication::translate("ImageEditorUI", "Mask thumbnail");
    case ImageEditorPerformanceStage::ThumbnailCacheHit: return QCoreApplication::translate("ImageEditorUI", "Thumbnail cache hit");
    case ImageEditorPerformanceStage::ThumbnailCacheMiss: return QCoreApplication::translate("ImageEditorUI", "Thumbnail cache miss");
    case ImageEditorPerformanceStage::LayerRasterCacheHit: return QCoreApplication::translate("ImageEditorUI", "Layer raster cache hit");
    case ImageEditorPerformanceStage::LayerRasterCacheMiss: return QCoreApplication::translate("ImageEditorUI", "Layer raster cache miss");
    case ImageEditorPerformanceStage::LayerRasterCacheBypass: return QCoreApplication::translate("ImageEditorUI", "Layer raster cache bypass");
    case ImageEditorPerformanceStage::CanvasPaint: return QCoreApplication::translate("ImageEditorUI", "Canvas paint");
    case ImageEditorPerformanceStage::InteractiveRender: return QCoreApplication::translate("ImageEditorUI", "Interactive render");
    case ImageEditorPerformanceStage::InteractivePresentation: return QCoreApplication::translate("ImageEditorUI", "Interactive presentation");
    case ImageEditorPerformanceStage::GpuUpload: return QCoreApplication::translate("ImageEditorUI", "GPU upload");
    case ImageEditorPerformanceStage::GpuDrawSubmission: return QCoreApplication::translate("ImageEditorUI", "GPU draw submission");
    case ImageEditorPerformanceStage::GpuFenceSubmission: return QCoreApplication::translate("ImageEditorUI", "GPU fence submission");
    case ImageEditorPerformanceStage::ExportRender: return QCoreApplication::translate("ImageEditorUI", "Export render");
    case ImageEditorPerformanceStage::JpegFlatten: return QCoreApplication::translate("ImageEditorUI", "JPEG flatten");
    case ImageEditorPerformanceStage::ExportEncode: return QCoreApplication::translate("ImageEditorUI", "Export encoding");
    case ImageEditorPerformanceStage::BenchmarkIteration: return QCoreApplication::translate("ImageEditorUI", "Benchmark iteration");
    case ImageEditorPerformanceStage::Count: break;
    }
    return QCoreApplication::translate("ImageEditorUI", "Unknown");
}

QString milliseconds(std::uint64_t value) {
    return QString::number(static_cast<double>(value) / 1'000'000.0, 'f', 3);
}

QString megabytes(const std::optional<std::uint64_t>& value) {
    if (!value.has_value()) return QCoreApplication::translate("ImageEditorUI", "N/A");
    return QString::number(static_cast<double>(*value) / (1024.0 * 1024.0), 'f', 1)
        + QStringLiteral(" MB");
}

QString cpuPercent(const std::optional<double>& value) {
    if (!value.has_value() || !std::isfinite(*value)) return QCoreApplication::translate("ImageEditorUI", "N/A");
    return QString::number(*value, 'f', 1) + QStringLiteral("%");
}

constexpr std::array<ImageEditorPerformanceStage,
    static_cast<std::size_t>(ImageEditorPerformanceStage::Count)> stages = {
    ImageEditorPerformanceStage::OperationReplay,
    ImageEditorPerformanceStage::PaintStroke,
    ImageEditorPerformanceStage::EraseStroke,
    ImageEditorPerformanceStage::LinearGradient,
    ImageEditorPerformanceStage::Shape,
    ImageEditorPerformanceStage::Text,
    ImageEditorPerformanceStage::RasterImage,
    ImageEditorPerformanceStage::Transform,
    ImageEditorPerformanceStage::MaskApplication,
    ImageEditorPerformanceStage::LayerComposition,
    ImageEditorPerformanceStage::GroupComposition,
    ImageEditorPerformanceStage::Composite,
    ImageEditorPerformanceStage::SelectedLayerRender,
    ImageEditorPerformanceStage::SelectedGroupRender,
    ImageEditorPerformanceStage::LayerThumbnail,
    ImageEditorPerformanceStage::GroupThumbnail,
    ImageEditorPerformanceStage::MaskThumbnail,
    ImageEditorPerformanceStage::ThumbnailCacheHit,
    ImageEditorPerformanceStage::ThumbnailCacheMiss,
    ImageEditorPerformanceStage::LayerRasterCacheHit,
    ImageEditorPerformanceStage::LayerRasterCacheMiss,
    ImageEditorPerformanceStage::LayerRasterCacheBypass,
    ImageEditorPerformanceStage::CanvasPaint,
    ImageEditorPerformanceStage::InteractiveRender,
    ImageEditorPerformanceStage::InteractivePresentation,
    ImageEditorPerformanceStage::GpuUpload,
    ImageEditorPerformanceStage::GpuDrawSubmission,
    ImageEditorPerformanceStage::GpuFenceSubmission,
    ImageEditorPerformanceStage::ExportRender,
    ImageEditorPerformanceStage::JpegFlatten,
    ImageEditorPerformanceStage::ExportEncode,
    ImageEditorPerformanceStage::BenchmarkIteration,
};

} // namespace

PerformanceMetricsPanel::PerformanceMetricsPanel(QWidget* parent)
    : QWidget(parent) {
    setObjectName(QStringLiteral("imageEditorPerformanceMetricsPanel"));
    auto* layout = new QVBoxLayout(this);
    status_label_ = new QLabel(this);
    status_label_->setObjectName(QStringLiteral("performanceMetricsStatusLabel"));
    resources_label_ = new QLabel(this);
    resources_label_->setObjectName(QStringLiteral("performanceMetricsResourcesLabel"));
    table_ = new QTableWidget(static_cast<int>(stages.size()), 5, this);
    table_->setObjectName(QStringLiteral("performanceMetricsTable"));
    table_->setHorizontalHeaderLabels({
        QCoreApplication::translate("ImageEditorUI", "Stage"), QCoreApplication::translate("ImageEditorUI", "Samples"),
        QCoreApplication::translate("ImageEditorUI", "Average (ms)"), QCoreApplication::translate("ImageEditorUI", "p95 (ms)"),
        QCoreApplication::translate("ImageEditorUI", "Maximum (ms)")});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < table_->columnCount(); ++column)
        table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);

    for (std::size_t row = 0; row < stages.size(); ++row) {
        const auto stage = stages[row];
        auto* stage_item = new QTableWidgetItem(performanceStageDisplayName(stage));
        table_->setItem(static_cast<int>(row), 0, stage_item);
        for (int column = 1; column < table_->columnCount(); ++column)
            table_->setItem(static_cast<int>(row), column,
                            new QTableWidgetItem(QStringLiteral("—")));
    }

    layout->addWidget(status_label_);
    layout->addWidget(resources_label_);
    layout->addWidget(table_, 1);
    setCollectionEnabled(false);
}

void PerformanceMetricsPanel::setCollectionEnabled(bool enabled) {
    status_label_->setText(enabled
        ? QCoreApplication::translate("ImageEditorUI", "Performance collection is enabled. Timing summaries cover up to the latest 2,048 samples per stage.")
        : QCoreApplication::translate("ImageEditorUI", "Performance collection is off. Enable it in Settings to collect editing timings."));
}

void PerformanceMetricsPanel::setSnapshot(
    const ImageEditorPerformanceSnapshot& snapshot,
    const system_monitor::PerformanceSnapshot& resources,
    std::optional<std::uint64_t> peak_working_set_bytes,
    std::optional<std::uint64_t> peak_private_usage_bytes) {
    resources_label_->setText(QCoreApplication::translate("ImageEditorUI", "CPU: %1   Working set: %2 (sampled peak %3)   Private memory: %4 (sampled peak %5)")
        .arg(cpuPercent(resources.process_cpu_percent),
             megabytes(resources.process_working_set_bytes),
             megabytes(peak_working_set_bytes),
             megabytes(resources.process_private_usage_bytes),
             megabytes(peak_private_usage_bytes)));

    for (std::size_t row = 0; row < stages.size(); ++row) {
        const auto& timing = snapshot.timing(stages[row]);
        const int row_index = static_cast<int>(row);
        table_->item(row_index, 1)->setText(timing.count == 0
            ? QCoreApplication::translate("ImageEditorUI", "—") : QString::number(timing.count));
        table_->item(row_index, 2)->setText(timing.count == 0
            ? QCoreApplication::translate("ImageEditorUI", "—") : QString::number(
                static_cast<double>(timing.total_nanoseconds) /
                    static_cast<double>(timing.count) / 1'000'000.0, 'f', 3));
        table_->item(row_index, 3)->setText(timing.count == 0
            ? QCoreApplication::translate("ImageEditorUI", "—") : milliseconds(timing.p95_nanoseconds));
        table_->item(row_index, 4)->setText(timing.count == 0
            ? QCoreApplication::translate("ImageEditorUI", "—") : milliseconds(timing.maximum_nanoseconds));
    }
}

} // namespace image_editor
