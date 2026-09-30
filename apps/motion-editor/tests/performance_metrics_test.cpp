#include "diagnostics/performance_metrics.h"
#include "settings/autosave_preferences.h"
#include "ui/general_settings_dialog.h"

#include <QApplication>
#include <QCheckBox>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QTemporaryDir temporary;
    require(temporary.isValid(), "temporary preferences directory is available");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    QCoreApplication::setOrganizationName(QStringLiteral("Creative Suite Motion Tests"));
    QCoreApplication::setApplicationName(QStringLiteral("Motion Performance Tests"));
    QSettings().clear();

    require(motion::settings::previewPerformanceMetricsEnabled(),
            "preview performance metrics are enabled by default");
    motion::settings::setPreviewPerformanceMetricsEnabled(false);
    require(!motion::settings::previewPerformanceMetricsEnabled(),
            "the performance preference can be disabled and persisted");

    motion::ui::GeneralSettingsDialog dialog(false);
    auto* checkbox = dialog.findChild<QCheckBox*>(
        QStringLiteral("motion-preview-performance-metrics-checkbox"));
    require(checkbox != nullptr && !checkbox->isChecked(),
            "General Settings reflects the saved preference");
    bool emitted_enabled = false;
    QObject::connect(&dialog,
        &motion::ui::GeneralSettingsDialog::previewMetricsEnabledChanged,
        [&](bool enabled) {
            emitted_enabled = enabled;
            motion::settings::setPreviewPerformanceMetricsEnabled(enabled);
        });
    checkbox->setChecked(true);
    require(emitted_enabled && motion::settings::previewPerformanceMetricsEnabled(),
            "changing the General Settings checkbox is applied immediately");

    auto& metrics = motion::diagnostics::PerformanceMetrics::instance();
    metrics.setEnabled(true);
    metrics.reset();
    metrics.recordRequest(10);
    metrics.recordCoalescedRequest(11);
    metrics.recordStaleResult(12);
    metrics.recordRenderedFrame();
    metrics.recordRenderedFrame();
    for (const std::uint64_t value : {1'000'000ULL, 2'000'000ULL, 3'000'000ULL,
                                      4'000'000ULL, 5'000'000ULL}) {
        metrics.recordTiming(motion::diagnostics::PreviewTimingStage::Decode, value);
    }
    metrics.recordEffectTiming(
        motion::diagnostics::PreviewEffectKind::GaussianBlur, 10'000'000);
    metrics.recordEffectTiming(
        motion::diagnostics::PreviewEffectKind::GaussianBlur, 30'000'000);
    metrics.recordEffectTiming(
        motion::diagnostics::PreviewEffectKind::ColorAdjustment, 2'000'000);
    metrics.recordViewerPaint(10);
    const auto snapshot = metrics.takeSnapshotAndReset();
    require(snapshot.has_value() && snapshot->requests == 1 &&
                snapshot->rendered_frames == 2 && snapshot->coalesced_requests == 1 &&
                snapshot->stale_results == 1,
            "preview counters aggregate requests, rendered frames, coalescing, and stale work");
    const auto& decode = snapshot->timings[
        static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::Decode)];
    require(decode.count == 5 && decode.total_nanoseconds == 15'000'000 &&
                decode.maximum_nanoseconds == 5'000'000 &&
                decode.p95_nanoseconds == 5'000'000 &&
                decode.p99_nanoseconds == 5'000'000,
            "timing summaries contain count, average inputs, maximum, p95, and p99");
    const auto& paint = snapshot->timings[
        static_cast<std::size_t>(motion::diagnostics::PreviewTimingStage::RequestToViewerPaint)];
    require(paint.count == 1 && paint.maximum_nanoseconds > 0,
            "request-to-viewer-paint latency is recorded for the matching generation");
    const auto& gaussian_blur = snapshot->effect_timings[
        static_cast<std::size_t>(motion::diagnostics::PreviewEffectKind::GaussianBlur)];
    const auto& color_adjustment = snapshot->effect_timings[
        static_cast<std::size_t>(motion::diagnostics::PreviewEffectKind::ColorAdjustment)];
    require(gaussian_blur.count == 2 && gaussian_blur.total_nanoseconds == 40'000'000 &&
                gaussian_blur.maximum_nanoseconds == 30'000'000 &&
                color_adjustment.count == 1 && color_adjustment.total_nanoseconds == 2'000'000,
            "effect timing summaries distinguish Gaussian Blur and Color Adjustment");
    system_monitor::PerformanceSnapshot resources;
    resources.process_cpu_percent = 27.5;
    resources.process_working_set_bytes = 123456;
    resources.system_total_bytes = 987654;
    motion::diagnostics::PreviewLogMetadata metadata{
        1920, 1080, 30000, 1001, 4, 3, 4};
    const auto log_context = motion::diagnostics::makePreviewPerformanceContext(
        *snapshot, resources, metadata);
    std::ostringstream serialized_context;
    for (const auto& [key, value] : log_context)
        serialized_context << key << '=' << value << '\n';
    const auto context_text = serialized_context.str();
    require(context_text.find("process_cpu_percent=27.500000") != std::string::npos &&
                context_text.find("schema_version=3") != std::string::npos &&
                context_text.find("decode_p95_ms=") != std::string::npos &&
                context_text.find("gaussian_blur_apply_count=2") != std::string::npos &&
                context_text.find("gaussian_blur_average_ms=20.000000") != std::string::npos &&
                context_text.find("color_adjustment_apply_count=1") != std::string::npos &&
                context_text.find("color_adjustment_average_ms=2.000000") != std::string::npos &&
                context_text.find("request_to_viewer_paint_count=1") != std::string::npos &&
                context_text.find("canvas_width=1920") != std::string::npos &&
                context_text.find("effect_count=3") != std::string::npos &&
                context_text.find("effect_worker_count=4") != std::string::npos,
            "preview log context includes resource, timing, latency, and workload fields");
    require(context_text.find("C:\\private-project") == std::string::npos &&
                context_text.find("private-media.mov") == std::string::npos &&
                context_text.find("Confidential layer") == std::string::npos &&
                context_text.find("private text") == std::string::npos,
            "preview metric records do not serialize project paths, layer names, or text");
    require(!metrics.takeSnapshotAndReset().has_value(),
            "an empty interval produces no preview sample");
    metrics.setEnabled(false);
    metrics.recordTiming(motion::diagnostics::PreviewTimingStage::Effects, 99);
    require(!metrics.takeSnapshotAndReset().has_value(),
            "disabled preview metrics collect no further records");

    std::cout << "Motion Studio performance metrics tests passed.\n";
    return EXIT_SUCCESS;
}
