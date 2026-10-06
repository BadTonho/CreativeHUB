#pragma once

#include "../../core/diagnostics/image_editor_performance_metrics.h"

#include <creative_suite/system_monitor/performance_usage.h>

#include <QWidget>

#include <optional>
#include <cstdint>

class QLabel;
class QTableWidget;

namespace image_editor {

class PerformanceMetricsPanel final : public QWidget {
public:
    explicit PerformanceMetricsPanel(QWidget* parent = nullptr);

    void setCollectionEnabled(bool enabled);
    void setSnapshot(const ImageEditorPerformanceSnapshot& snapshot,
                     const system_monitor::PerformanceSnapshot& resources,
                     std::optional<std::uint64_t> peak_working_set_bytes,
                     std::optional<std::uint64_t> peak_private_usage_bytes);

private:
    QLabel* status_label_ = nullptr;
    QLabel* resources_label_ = nullptr;
    QTableWidget* table_ = nullptr;
};

} // namespace image_editor
