#include "ui/main_window/main_window.h"
#include "export/motion_video_export.h"
#include "ui/dialogs/motion_video_export_dialog.h"
#include "settings/gpu_composition_preferences.h"

#include <creative_suite/composition/opengl_frame_compositor.h>
#include <creative_suite/media/video_encoder.h>
#include <creative_suite/media/video_playback.h>
#include <creative_suite/diagnostics/logger.h>

#include <QApplication>
#include <QAction>
#include <QDialogButtonBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QProgressDialog>
#include <QPushButton>
#include <QSize>
#include <QTemporaryDir>
#include <QTimer>
#include <QThread>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <QCoreApplication>
#include <QEventLoop>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using creative_suite::media::RgbaFrame;
using creative_suite::media::RgbaFramePtr;
using motion::model::CompositionLayer;
using motion::model::FrameRate;
using motion::model::LayerKind;
using motion::ui::MotionExportSettings;
using motion::ui::MotionExportSnapshot;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool softwareEncoder(const std::string& name)
{
    constexpr const char* hardware_names[]{
        "_mf", "nvenc", "_qsv", "_amf", "vaapi", "videotoolbox", "_v4l2m2m"};
    return std::none_of(std::begin(hardware_names), std::end(hardware_names),
        [&name](const char* marker) { return name.find(marker) != std::string::npos; });
}

std::pair<creative_suite::media::VideoContainerOption,
          creative_suite::media::VideoEncoderOption> chooseOutput()
{
    auto containers = creative_suite::media::availableVideoContainers();
    std::stable_sort(containers.begin(), containers.end(), [](const auto& left, const auto& right) {
        return left.name == "matroska" && right.name != "matroska";
    });
    for (const auto& container : containers) {
        for (const auto& encoder : container.video_encoders) {
            if (softwareEncoder(encoder.name) &&
                creative_suite::media::supportsVideoEncoder(container, encoder.name)) {
                return {container, encoder};
            }
        }
    }
    throw std::runtime_error("FFmpeg exposes no compatible software video encoder.");
}

std::filesystem::path pathFromQString(const QString& value)
{
    const auto bytes = value.toUtf8();
    const auto* begin = reinterpret_cast<const char8_t*>(bytes.constData());
    return std::filesystem::path(std::u8string(begin, begin + bytes.size()));
}

RgbaFramePtr solidFrame(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    auto frame = std::make_shared<RgbaFrame>();
    frame->width = 64;
    frame->height = 48;
    frame->stride = frame->width * 4;
    frame->rgba_pixels.resize(static_cast<std::size_t>(frame->stride) * frame->height);
    for (int y = 0; y < frame->height; ++y) {
        for (int x = 0; x < frame->width; ++x) {
            const auto offset = static_cast<std::size_t>(y) * frame->stride +
                static_cast<std::size_t>(x) * 4U;
            frame->rgba_pixels[offset] = red;
            frame->rgba_pixels[offset + 1] = green;
            frame->rgba_pixels[offset + 2] = blue;
            frame->rgba_pixels[offset + 3] = 255;
        }
    }
    return frame;
}

std::filesystem::path outputPath(const QTemporaryDir& directory,
                                 const creative_suite::media::VideoContainerOption& container,
                                 const char* stem)
{
    auto extension = container.extensions.substr(0, container.extensions.find(','));
    if (extension.empty()) extension = "video";
    return std::filesystem::path(directory.path().toStdString()) /
        (std::string(stem) + "." + extension);
}

MotionExportSettings settingsFor(
    const std::filesystem::path& path,
    const creative_suite::media::VideoContainerOption& container,
    const creative_suite::media::VideoEncoderOption& encoder,
    FrameRate output_rate = {24, 1})
{
    return MotionExportSettings{
        path, container.name, encoder.name, 64, 48, output_rate, 1.0};
}

CompositionLayer imageLayer(std::int64_t start, std::int64_t duration)
{
    CompositionLayer layer{};
    layer.id = 1;
    layer.kind = LayerKind::Image;
    layer.name = "Green background";
    layer.source_path = std::filesystem::path("virtual-image.png");
    layer.timeline_start_frame = start;
    layer.duration_frames = duration;
    return layer;
}

MotionExportSnapshot makeSnapshot(const std::filesystem::path& source_video)
{
    const motion::model::CanvasSize canvas{64, 48};
    MotionExportSnapshot snapshot;
    snapshot.canvas_size = canvas;
    snapshot.frame_rate = {30000, 1001};

    auto image = imageLayer(3, 4);
    image.effects.emplace_back(motion::model::ColorAdjustmentEffect{
        true, 20.0, 100.0, 100.0});
    image.effects.emplace_back(motion::model::GaussianBlurEffect{true, 10.0});
    snapshot.still_frames.emplace(image.source_path, solidFrame(20, 180, 30));
    snapshot.layers.push_back(std::move(image));

    CompositionLayer shape{};
    shape.id = 2;
    shape.kind = LayerKind::Shape;
    shape.name = "Red ellipse";
    shape.timeline_start_frame = 3;
    shape.duration_frames = 4;
    auto shape_content = motion::model::defaultShapeLayerContent(canvas,
        motion::model::ShapeKind::Ellipse);
    shape_content.width = 18;
    shape_content.height = 18;
    shape_content.fill_color = {240, 30, 20, 255};
    shape.content = shape_content;
    require(creative_suite::animation::setKeyframe(shape.keyframes,
                creative_suite::animation::TransformProperty::Opacity, 0, 0.0),
            "shape opacity start key is valid");
    require(creative_suite::animation::setKeyframe(shape.keyframes,
                creative_suite::animation::TransformProperty::Opacity, 3, 1.0),
            "shape opacity end key is valid");
    require(creative_suite::animation::setKeyframeInterpolation(
                shape.keyframes,
                creative_suite::animation::TransformProperty::Opacity, 0,
                creative_suite::animation::InterpolationMode::CubicBezier,
                {0.42, 0.0, 1.0, 1.0}),
            "shape opacity supports an eased export segment");
    snapshot.layers.push_back(std::move(shape));

    CompositionLayer text{};
    text.id = 3;
    text.kind = LayerKind::Text;
    text.name = "Title";
    text.timeline_start_frame = 3;
    text.duration_frames = 4;
    auto text_content = motion::model::defaultTextLayerContent(canvas);
    text_content.text = "Motion Studio";
    text_content.font_size_pixels = 11;
    text.content = std::move(text_content);
    snapshot.layers.push_back(std::move(text));

    CompositionLayer video{};
    video.id = 4;
    video.kind = LayerKind::Video;
    video.name = "Video inset";
    video.source_path = source_video;
    video.timeline_start_frame = 3;
    video.duration_frames = 4;
    video.source_frame_count = 60;
    video.source_frame_rate = 30.0;
    video.transform.position_x = 0.12;
    video.transform.position_y = 0.12;
    video.transform.scale = 0.12;
    snapshot.layers.push_back(std::move(video));

    CompositionLayer hidden{};
    hidden.id = 5;
    hidden.kind = LayerKind::Shape;
    hidden.name = "Hidden tail";
    hidden.timeline_start_frame = 7;
    hidden.duration_frames = 4;
    hidden.visible = false;
    hidden.content = motion::model::defaultShapeLayerContent(canvas);
    snapshot.layers.push_back(std::move(hidden));
    return snapshot;
}

std::vector<std::uint8_t> fileBytes(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

std::vector<creative_suite::media::VideoFramePtr> decodeVideo(
    const std::filesystem::path& path)
{
    auto decoder = creative_suite::media::VideoPlaybackSession::open(path);
    std::vector<creative_suite::media::VideoFramePtr> frames;
    while (const auto frame = decoder->decode_next_frame()) frames.push_back(*frame);
    return frames;
}

bool videoFramesMatch(
    const std::vector<creative_suite::media::VideoFramePtr>& left,
    const std::vector<creative_suite::media::VideoFramePtr>& right,
    int maximum_channel_delta)
{
    if (left.size() != right.size()) return false;
    for (std::size_t frame_index = 0; frame_index < left.size(); ++frame_index) {
        if (left[frame_index] == nullptr || right[frame_index] == nullptr ||
            left[frame_index]->width != right[frame_index]->width ||
            left[frame_index]->height != right[frame_index]->height ||
            left[frame_index]->rgba_pixels.size() != right[frame_index]->rgba_pixels.size()) {
            return false;
        }
        for (std::size_t byte_index = 0;
             byte_index < left[frame_index]->rgba_pixels.size(); ++byte_index) {
            if (std::abs(static_cast<int>(left[frame_index]->rgba_pixels[byte_index]) -
                         static_cast<int>(right[frame_index]->rgba_pixels[byte_index])) >
                maximum_channel_delta) {
                return false;
            }
        }
    }
    return true;
}

template<typename Widget>
Widget* findWidget(QObject* parent, const char* object_name)
{
    auto* widget = parent->findChild<Widget*>(QString::fromLatin1(object_name));
    require(widget != nullptr, object_name);
    return widget;
}

QAction* findAction(motion::ui::MainWindow& window, const char* object_name)
{
    return findWidget<QAction>(&window, object_name);
}

bool waitFor(const std::function<bool()>& condition, int timeout_ms = 15000)
{
    QElapsedTimer timer;
    timer.start();
    while (!condition() && timer.elapsed() < timeout_ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    return condition();
}

void testMainWindowExportAction(const QTemporaryDir& directory)
{
    QTemporaryDir recovery_directory;
    require(recovery_directory.isValid(), "the export UI test recovery folder is available");
    const auto recovery = pathFromQString(recovery_directory.path());
    motion::ui::MainWindow window(nullptr, recovery, "export-ui-test");
    QTimer::singleShot(0, [] {
        auto* dialog = QApplication::activeModalWidget();
        require(dialog != nullptr, "New Composition is active");
        findWidget<QLineEdit>(dialog, "motion-canvas-width")->setText(QStringLiteral("64"));
        findWidget<QLineEdit>(dialog, "motion-canvas-height")->setText(QStringLiteral("48"));
        findWidget<QComboBox>(dialog, "motion-frame-rate")->setCurrentIndex(2);
        findWidget<QDialogButtonBox>(dialog, "motion-new-composition-buttons")
            ->button(QDialogButtonBox::Ok)->click();
    });
    findAction(window, "motion-new-composition-action")->trigger();
    require(window.compositionDocument() != nullptr,
            "New Composition creates a document for the export action");
    findAction(window, "motion-new-text-layer-action")->trigger();
    const bool dirty_before_export = window.isWindowModified();
    require(dirty_before_export, "the text layer makes the current document dirty");
    auto* export_action = findAction(window, "motion-export-video-action");
    require(export_action->isEnabled(), "Export Video enables when a layer exists");

    QTimer::singleShot(0, [&directory] {
        auto* dialog = QApplication::activeModalWidget();
        require(dialog != nullptr, "Export Video opens the settings dialog");
        auto* path = findWidget<QLineEdit>(dialog, "motion-export-output-path");
        auto* container_combo = findWidget<QComboBox>(dialog, "motion-export-container");
        const auto container_name = container_combo->currentData().toString().toStdString();
        const auto containers = creative_suite::media::availableVideoContainers();
        const auto found = std::find_if(containers.begin(), containers.end(),
            [&container_name](const auto& container) { return container.name == container_name; });
        require(found != containers.end(), "the selected output container is available");
        auto extension = found->extensions.substr(0, found->extensions.find(','));
        if (extension.empty()) extension = "video";
        const auto output = std::filesystem::path(directory.path().toStdString()) /
            ("window-export." + extension);
        const auto encoded = output.u8string();
        path->setText(QString::fromUtf8(reinterpret_cast<const char*>(encoded.data()),
                                        static_cast<qsizetype>(encoded.size())));
        findWidget<QPushButton>(dialog, "motion-export-accept")->click();
    });
    export_action->trigger();
    require(waitFor([&window] {
                return window.findChild<QProgressDialog*>(QStringLiteral("motion-export-progress")) == nullptr;
            }, 30000),
            "the progress dialog closes when the export worker completes");
    require(window.isWindowModified() == dirty_before_export,
            "export does not change the composition dirty state");

    QTimer::singleShot(0, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt != nullptr) prompt->button(QMessageBox::Discard)->click();
    });
    window.close();
}

void testSettingsDialog()
{
    motion::ui::MotionVideoExportDialog dialog({640, 360}, {30000, 1001});
    auto* output = findWidget<QLineEdit>(&dialog, "motion-export-output-path");
    auto* container = findWidget<QComboBox>(&dialog, "motion-export-container");
    auto* encoder = findWidget<QComboBox>(&dialog, "motion-export-video-encoder");
    auto* resolution = findWidget<QComboBox>(&dialog, "motion-export-resolution");
    auto* rate = findWidget<QDoubleSpinBox>(&dialog, "motion-export-frame-rate");
    auto* quality = findWidget<QComboBox>(&dialog, "motion-export-quality");
    require(!container->currentData().toString().isEmpty(),
            "the export settings choose an available container");
    require(resolution->currentIndex() == 0 && resolution->currentData().toSize() == QSize(640, 360),
            "the composition dimensions are the initial output resolution");
    require(std::abs(rate->value() - 30000.0 / 1001.0) < 0.001,
            "the output frame-rate field starts at the exact composition rate");
    require(quality->currentIndex() == 1,
            "the export dialog starts with Standard quality");
    const auto available = creative_suite::media::availableVideoContainers();
    const auto mp4 = std::find_if(available.begin(), available.end(), [](const auto& option) {
        return option.name == "mp4";
    });
    if (mp4 != available.end()) {
        require(container->currentData().toString() == QStringLiteral("mp4"),
                "MP4 is the preferred container when available");
        const auto h264 = std::find_if(mp4->video_encoders.begin(), mp4->video_encoders.end(),
            [](const auto& option) { return option.name == "libx264"; });
        if (h264 != mp4->video_encoders.end()) {
            require(encoder->currentData().toString() == QStringLiteral("libx264"),
                    "H.264 software encoding is preferred when available");
        }
    }
    require(!dialog.exportSettings().has_value(), "an output path is required");
    output->setText(QStringLiteral("export-test.mkv"));
    const auto settings = dialog.exportSettings();
    require(settings.has_value() && settings->frame_rate == FrameRate{30000, 1001} &&
                settings->width == 640 && settings->height == 360,
            "untouched export fields preserve exact composition settings");
    require(dialog.findChild<QWidget*>(QStringLiteral("motion-export-audio")) == nullptr,
            "Motion export settings do not expose audio controls");
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    try {
        qputenv("CREATIVE_SUITE_MOTION_GPU_COMPOSITION", "1");
        require(motion::settings::gpuCompositionEnabled(),
                "the shared GPU composition opt-in can be enabled for preview and export");
        const motion::ui::MotionExportCancelled cancellation_exception;
        require(std::string(cancellation_exception.what()) ==
                    "Motion Studio video export was canceled.",
                "the export cancellation exception preserves its user-facing cause");

        testSettingsDialog();

        const auto [container, encoder] = chooseOutput();
        QTemporaryDir temporary_directory;
        require(temporary_directory.isValid(), "a temporary export folder is available");
        const auto log_directory = pathFromQString(temporary_directory.path()) / "diagnostics";
        require(creative_suite::diagnostics::Logger::instance().initialize(log_directory),
                "a temporary diagnostics log can be initialized for export metrics checks");
        const std::filesystem::path source_video =
            std::filesystem::path(MOTION_EDITOR_EXPORT_TEST_MEDIA_DIR) / "reference.mkv";
        require(std::filesystem::is_regular_file(source_video),
                "the existing video reference fixture is available");

        const auto target = outputPath(temporary_directory, container, "motion-export");
        auto snapshot = makeSnapshot(source_video);
        std::vector<int> progress;
        std::atomic_bool cancel_requested{false};
        motion::ui::MotionExportPerformanceSummary export_performance;
        motion::ui::MotionVideoExporter::exportVideo(
            snapshot, settingsFor(target, container, encoder), cancel_requested,
            [&progress](int value) { progress.push_back(value); }, &export_performance);
        require(export_performance.elapsed_nanoseconds > 0 &&
                    export_performance.frames_rendered == 9 &&
                    export_performance.render_count == 9 &&
                    export_performance.write_count == 9 &&
                    export_performance.render_total_nanoseconds > 0 &&
                    export_performance.write_total_nanoseconds > 0,
                "successful export collects elapsed, render, and encode/write timings");
        require(std::filesystem::is_regular_file(target), "Motion export publishes a video file");
        require(!progress.empty() && progress.back() == 100,
                "Motion export reports completion progress");
        require(progress.size() <= 101,
                "large jobs report bounded percentage updates instead of per-frame events");

        const auto decoded = decodeVideo(target);
        require(decoded.size() == 9,
                "fractional composition rate converts to the expected nine 24-fps output frames");
        require(decoded.front() != nullptr && decoded.back() != nullptr,
                "export output decodes at the beginning and end");
        const auto has_non_black = [](const creative_suite::media::VideoFrame& frame) {
            for (int y = 0; y < frame.height; ++y) {
                for (int x = 0; x < frame.width; ++x) {
                    const auto offset = static_cast<std::size_t>(y) * frame.stride +
                        static_cast<std::size_t>(x) * 4U;
                    if (frame.rgba_pixels[offset] > 35 || frame.rgba_pixels[offset + 1] > 35 ||
                        frame.rgba_pixels[offset + 2] > 35) return true;
                }
            }
            return false;
        };
        require(!has_non_black(*decoded.front()), "blank lead-in exports as opaque black");
        require(has_non_black(*decoded[3]),
                "image, video, text, shape, transforms, and keyframes contribute to preview output");
        const auto background_offset = static_cast<std::size_t>(1) * decoded[3]->stride +
            static_cast<std::size_t>(1) * 4U;
        require(decoded[3]->rgba_pixels[background_offset] > 50 &&
                    decoded[3]->rgba_pixels[background_offset + 1] > 210 &&
                    decoded[3]->rgba_pixels[background_offset + 2] > 60,
                "Color Adjustment is included in exported still-layer pixels");
        require(!has_non_black(*decoded.back()),
                "hidden layers extend the export duration without rendering pixels");

        QObject gpu_worker_receiver;
        std::optional<motion::ui::MotionExportResult> gpu_fallback_result;
        const auto gpu_fallback_target = outputPath(
            temporary_directory, container, "gpu-export-no-surface-fallback");
        motion::ui::MotionVideoExportWorker gpu_fallback_worker(
            &gpu_worker_receiver, snapshot,
            settingsFor(gpu_fallback_target, container, encoder), {},
            [&gpu_fallback_result](motion::ui::MotionExportResult result) {
                gpu_fallback_result = std::move(result);
            },
            motion::ui::MotionExportRenderOptions{true, nullptr});
        gpu_fallback_worker.start();
        require(waitFor([&] { return gpu_fallback_result.has_value(); }),
                "an export with no OpenGL surface reports completion through its worker");
        gpu_fallback_worker.wait();
        require(gpu_fallback_result->succeeded,
                "an unavailable export surface falls back to a successful CPU export");
        const auto gpu_fallback_decoded = decodeVideo(gpu_fallback_target);
        require(videoFramesMatch(decoded, gpu_fallback_decoded, 2),
                "a GPU-requested CPU fallback preserves the exported frames");

        const auto gpu_cancel_target = outputPath(
            temporary_directory, container, "gpu-export-cancel-preserves-output");
        {
            std::ofstream output(gpu_cancel_target, std::ios::binary);
            output << "existing-gpu-export-destination";
        }
        const auto gpu_cancel_previous_bytes = fileBytes(gpu_cancel_target);
        std::atomic_bool gpu_cancel_requested{false};
        motion::ui::MotionExportPerformanceSummary gpu_cancel_performance;
        bool gpu_cancel_reported = false;
        try {
            motion::ui::MotionVideoExporter::exportVideo(
                snapshot,
                settingsFor(gpu_cancel_target, container, encoder),
                gpu_cancel_requested,
                [&gpu_cancel_requested](int progress) {
                    if (progress > 0) gpu_cancel_requested.store(true, std::memory_order_release);
                },
                &gpu_cancel_performance,
                motion::ui::MotionExportRenderOptions{true, nullptr});
        } catch (const motion::ui::MotionExportCancelled&) {
            gpu_cancel_reported = true;
        }
        require(gpu_cancel_reported && gpu_cancel_performance.gpu_composition_requested &&
                    gpu_cancel_performance.gpu.failures == 1 &&
                    gpu_cancel_performance.gpu.fallback_frames > 0,
                "canceling a GPU-requested fallback export reports its backend and cancellation");
        require(fileBytes(gpu_cancel_target) == gpu_cancel_previous_bytes,
                "canceling a GPU-requested export preserves an existing destination");

        auto gpu_surface = creative_suite::composition::OpenGlFrameCompositor::createSurface();
        std::optional<motion::ui::MotionExportResult> gpu_surface_result;
        std::filesystem::path gpu_surface_target;
        if (gpu_surface) {
            gpu_surface_target = outputPath(
                temporary_directory, container, "gpu-export-surface");
            motion::ui::MotionVideoExportWorker gpu_surface_worker(
                &gpu_worker_receiver, snapshot,
                settingsFor(gpu_surface_target, container, encoder), {},
                [&gpu_surface_result](motion::ui::MotionExportResult result) {
                    gpu_surface_result = std::move(result);
                },
                motion::ui::MotionExportRenderOptions{true, gpu_surface.get()});
            gpu_surface_worker.start();
            require(waitFor([&] { return gpu_surface_result.has_value(); }),
                    "an export with a GUI-created surface reports completion through its worker");
            gpu_surface_worker.wait();
            require(gpu_surface_result->succeeded,
                    "GPU export or its CPU fallback completes without losing the output");
            const auto gpu_surface_decoded = decodeVideo(gpu_surface_target);
            require(videoFramesMatch(decoded, gpu_surface_decoded, 4),
                    "the GPU export path preserves CPU-reference output within encoder tolerance");
        }

        const auto preserved_target = outputPath(temporary_directory, container, "preserve-on-cancel");
        {
            std::ofstream output(preserved_target, std::ios::binary);
            output << "existing-destination";
        }
        const auto previous_bytes = fileBytes(preserved_target);
        std::atomic_bool canceled{true};
        motion::ui::MotionExportPerformanceSummary cancellation_performance;
        bool cancellation_reported = false;
        try {
            motion::ui::MotionVideoExporter::exportVideo(
                snapshot, settingsFor(preserved_target, container, encoder), canceled,
                {}, &cancellation_performance);
        } catch (const motion::ui::MotionExportCancelled&) {
            cancellation_reported = true;
        }
        require(cancellation_reported, "a requested cancellation has a distinct outcome");
        require(fileBytes(preserved_target) == previous_bytes,
                "cancellation leaves an existing destination unchanged");
        require(cancellation_performance.elapsed_nanoseconds > 0 &&
                    cancellation_performance.frames_rendered == 0,
                "cancelled export retains a timing summary without rendering frames");

        const auto failure_target = outputPath(temporary_directory, container, "preserve-on-failure");
        {
            std::ofstream output(failure_target, std::ios::binary);
            output << "existing-destination";
        }
        const auto failure_bytes = fileBytes(failure_target);
        MotionExportSnapshot unavailable;
        unavailable.canvas_size = {64, 48};
        unavailable.frame_rate = {24, 1};
        CompositionLayer missing_video{};
        missing_video.id = 99;
        missing_video.kind = LayerKind::Video;
        missing_video.source_path = temporary_directory.path().toStdString() + "/missing-video.mkv";
        missing_video.timeline_start_frame = 0;
        missing_video.duration_frames = 2;
        missing_video.source_frame_count = 10;
        missing_video.source_frame_rate = 24.0;
        unavailable.layers.push_back(missing_video);
        std::atomic_bool not_canceled{false};
        motion::ui::MotionExportPerformanceSummary failure_performance;
        bool failed = false;
        try {
            motion::ui::MotionVideoExporter::exportVideo(
                unavailable, settingsFor(failure_target, container, encoder), not_canceled,
                {}, &failure_performance);
        } catch (const std::exception&) {
            failed = true;
        }
        require(failed, "an unavailable visible source fails export with an error");
        require(fileBytes(failure_target) == failure_bytes,
                "failed export preserves an existing destination");
        require(failure_performance.elapsed_nanoseconds > 0,
                "failed export retains elapsed-time diagnostics");
        for (const auto& entry : std::filesystem::directory_iterator(temporary_directory.path().toStdString())) {
            require(entry.path().filename().string().find(".rendering-motion-") == std::string::npos,
                    "failed and canceled export temporary files are removed");
        }

        testMainWindowExportAction(temporary_directory);

        QObject worker_receiver;
        std::optional<motion::ui::MotionExportResult> cancelled_result;
        motion::ui::MotionVideoExportWorker cancelled_worker(
            &worker_receiver, snapshot,
            settingsFor(outputPath(temporary_directory, container, "worker-cancel"),
                        container, encoder),
            {}, [&cancelled_result](motion::ui::MotionExportResult result) {
                cancelled_result = std::move(result);
            });
        cancelled_worker.cancel();
        cancelled_worker.start();
        require(waitFor([&] { return cancelled_result.has_value(); }),
                "a canceled export worker reports its completed job");
        cancelled_worker.wait();
        require(cancelled_result->cancelled && !cancelled_result->succeeded,
                "a worker cancellation has a distinct result");

        std::optional<motion::ui::MotionExportResult> failed_result;
        motion::ui::MotionVideoExportWorker failed_worker(
            &worker_receiver, unavailable,
            settingsFor(failure_target, container, encoder),
            {}, [&failed_result](motion::ui::MotionExportResult result) {
                failed_result = std::move(result);
            });
        failed_worker.start();
        require(waitFor([&] { return failed_result.has_value(); }),
                "a failed export worker reports its completed job");
        failed_worker.wait();
        require(!failed_result->cancelled && !failed_result->succeeded &&
                    !failed_result->error_message.empty(),
                "a failed worker preserves the technical failure result");

        const auto log_contents = fileBytes(
            creative_suite::diagnostics::Logger::instance().log_path());
        const std::string log_text(log_contents.begin(), log_contents.end());
        std::istringstream log_lines(log_text);
        std::string log_line;
        std::vector<std::string> export_summaries;
        while (std::getline(log_lines, log_line)) {
            if (log_line.find("operation=\"export_summary\"") != std::string::npos)
                export_summaries.push_back(log_line);
        }
        require(export_summaries.size() >= 3,
                "completed, canceled, and failed jobs each write an export summary");
        bool saw_completed = false;
        bool saw_cancelled = false;
        bool saw_failed = false;
        bool saw_gpu_fallback = false;
        bool saw_gpu_surface_export = false;
        std::size_t completed_gpu_requested_summaries = 0;
        for (const auto& summary : export_summaries) {
            saw_completed = saw_completed || summary.find("outcome=\"completed\"") != std::string::npos;
            saw_cancelled = saw_cancelled || summary.find("outcome=\"cancelled\"") != std::string::npos;
            saw_failed = saw_failed || summary.find("outcome=\"failed\"") != std::string::npos;
            if (summary.find("outcome=\"completed\"") != std::string::npos &&
                summary.find("gpu_composition_requested=\"true\"") != std::string::npos) {
                ++completed_gpu_requested_summaries;
            }
            require(summary.find("schema_version=\"2\"") != std::string::npos &&
                        summary.find("gpu_composition_requested=") != std::string::npos &&
                        summary.find("gpu_backend_used=") != std::string::npos &&
                        summary.find("gpu_composition_attempts=") != std::string::npos &&
                        summary.find("gpu_composition_frames=") != std::string::npos &&
                        summary.find("gpu_composition_fallback_frames=") != std::string::npos &&
                        summary.find("gpu_composition_failures=") != std::string::npos &&
                        summary.find("gpu_composition_uploaded_bytes=") != std::string::npos &&
                        summary.find("gpu_composition_readback_bytes=") != std::string::npos &&
                        summary.find("gpu_upload_average_ms=") != std::string::npos &&
                        summary.find("gpu_draw_submission_average_ms=") != std::string::npos &&
                        summary.find("gpu_readback_average_ms=") != std::string::npos &&
                        summary.find("render_average_ms=") != std::string::npos &&
                        summary.find("write_average_ms=") != std::string::npos &&
                        summary.find("achieved_frames_per_second=") != std::string::npos &&
                        summary.find("output_path=") == std::string::npos &&
                        summary.find("window-export.") == std::string::npos,
                    "export performance summaries include render/output metrics without paths");
            if (summary.find("gpu_composition_requested=\"true\"") != std::string::npos &&
                summary.find("gpu_surface_available=\"false\"") != std::string::npos) {
                saw_gpu_fallback = summary.find("gpu_backend_used=\"cpu_fallback\"") !=
                        std::string::npos &&
                    summary.find("gpu_composition_attempts=\"1\"") != std::string::npos &&
                    summary.find("gpu_composition_fallback_frames=\"9\"") != std::string::npos &&
                    summary.find("gpu_composition_failures=\"1\"") != std::string::npos;
            }
            if (summary.find("gpu_composition_requested=\"true\"") != std::string::npos &&
                summary.find("gpu_surface_available=\"true\"") != std::string::npos) {
                const bool expected_backend =
                    summary.find("gpu_backend_used=\"gpu\"") != std::string::npos ||
                    summary.find("gpu_backend_used=\"mixed\"") != std::string::npos ||
                    summary.find("gpu_backend_used=\"cpu_fallback\"") != std::string::npos;
                saw_gpu_surface_export = saw_gpu_surface_export ||
                    (expected_backend &&
                     summary.find("frames_rendered=\"9\"") != std::string::npos);
            }
        }
        require(saw_completed && saw_cancelled && saw_failed,
                "export summaries identify each terminal job outcome");
        require(saw_gpu_fallback,
                "export summaries record the no-surface fallback, failure, and per-frame backend");
        require(completed_gpu_requested_summaries >= 2U + (gpu_surface ? 1U : 0U),
                "the shared environment opt-in reaches the MainWindow export worker");
        if (gpu_surface) {
            require(saw_gpu_surface_export,
                    "export summaries identify GPU use or a full CPU fallback with a surface");
        }

        std::cout << "Motion Studio video export tests passed.\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "Motion Studio video export test failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
