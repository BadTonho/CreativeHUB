#include "performance_metrics_panel.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <array>
#include <cmath>

namespace image_editor {
namespace {

QString milliseconds(std::uint64_t value) {
    return QString::number(static_cast<double>(value) / 1'000'000.0, 'f', 3);
}

QString megabytes(const std::optional<std::uint64_t>& value) {
    if (!value.has_value()) return QStringLiteral("N/A");
    return QString::number(static_cast<double>(*value) / (1024.0 * 1024.0), 'f', 1)
        + QStringLiteral(" MB");
}

QString cpuPercent(const std::optional<double>& value) {
    if (!value.has_value() || !std::isfinite(*value)) return QStringLiteral("N/A");
    return QString::number(*value, 'f', 1) + QStringLiteral("%");
}

constexpr std::array<ImageEditorPerformanceStage,
    static_cast<std::size_t>(ImageEditorPerformanceStage::Count)> stages = {
    ImageEditorPerformanceStage::OperationReplay,
    ImageEditorPerformanceStage::PaintStroke,
    ImageEditorPerformanceStage::EraseStroke,
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
    ImageEditorPerformanceStage::CanvasPaint,
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
        QStringLiteral("Stage"), QStringLiteral("Samples"),
        QStringLiteral("Average (ms)"), QStringLiteral("p95 (ms)"),
        QStringLiteral("Maximum (ms)")});
    table_->verticalHeader()->setVisible(false);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int column = 1; column < table_->columnCount(); ++column)
        table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);

    for (std::size_t row = 0; row < stages.size(); ++row) {
        const auto stage = stages[row];
        auto* stage_item = new QTableWidgetItem(QString::fromLatin1(
            imageEditorPerformanceStageName(stage)));
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
        ? QStringLiteral("Performance collection is enabled. Timing summaries cover up to the latest 2,048 samples per stage.")
        : QStringLiteral("Performance collection is off. Enable it in Settings to collect editing timings."));
}

void PerformanceMetricsPanel::setSnapshot(
    const ImageEditorPerformanceSnapshot& snapshot,
    const system_monitor::PerformanceSnapshot& resources,
    std::optional<std::uint64_t> peak_working_set_bytes,
    std::optional<std::uint64_t> peak_private_usage_bytes) {
    resources_label_->setText(QStringLiteral("CPU: %1   Working set: %2 (sampled peak %3)   Private memory: %4 (sampled peak %5)")
        .arg(cpuPercent(resources.process_cpu_percent),
             megabytes(resources.process_working_set_bytes),
             megabytes(peak_working_set_bytes),
             megabytes(resources.process_private_usage_bytes),
             megabytes(peak_private_usage_bytes)));

    for (std::size_t row = 0; row < stages.size(); ++row) {
        const auto& timing = snapshot.timing(stages[row]);
        const int row_index = static_cast<int>(row);
        table_->item(row_index, 1)->setText(timing.count == 0
            ? QStringLiteral("—") : QString::number(timing.count));
        table_->item(row_index, 2)->setText(timing.count == 0
            ? QStringLiteral("—") : QString::number(
                static_cast<double>(timing.total_nanoseconds) /
                    static_cast<double>(timing.count) / 1'000'000.0, 'f', 3));
        table_->item(row_index, 3)->setText(timing.count == 0
            ? QStringLiteral("—") : milliseconds(timing.p95_nanoseconds));
        table_->item(row_index, 4)->setText(timing.count == 0
            ? QStringLiteral("—") : milliseconds(timing.maximum_nanoseconds));
    }
}

} // namespace image_editor
