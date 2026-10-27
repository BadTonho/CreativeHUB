#include "ui/main_window/main_window.h"
#include "export/motion_video_export.h"
#include "ui/dialogs/motion_video_export_dialog.h"
#include "ui/dialogs/motion_png_sequence_export_dialog.h"
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
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QLabel>
#include <QMessageBox>
#include <QOffscreenSurface>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardItemModel>
#include <QSpinBox>
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
using motion::model::GlowEffect;
using motion::model::LayerKind;
using motion::ui::MotionExportSettings;
using motion::ui::MotionExportSnapshot;
using motion::ui::MotionPngSequenceSettings;

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
          creative_suite::media::VideoEncoderOption> chooseOutput(bool lossless_reference = false)
{
    auto containers = creative_suite::media::availableVideoContainers();
    std::stable_sort(containers.begin(), containers.end(), [](const auto& left, const auto& right) {
        return left.name == "matroska" && right.name != "matroska";
    });
    // Native pixel parity uses lossless FFV1: lossy encoders can amplify a
    // tolerated compositor rounding difference beyond the encoded tolerance.
    if (lossless_reference) {
        for (const auto& container : containers) if (container.name == "matroska") {
            for (const auto& encoder : container.video_encoders) if (encoder.name == "ffv1" &&
                    creative_suite::media::supportsVideoEncoder(container, encoder.name))
                return {container, encoder};
        }
        throw std::runtime_error("Native GPU export parity requires Matroska/FFV1.");
    }
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

struct AlphaOutputChoice {
    creative_suite::media::VideoContainerOption container;
    creative_suite::media::VideoEncoderOption encoder;
    creative_suite::media::VideoAlphaProfileOption profile;
};

std::optional<AlphaOutputChoice> chooseAlphaOutput()
{
    auto containers = creative_suite::media::availableVideoContainers();
    for (const auto profile : {
            creative_suite::media::VideoOutputProfile::Ffv1LosslessAlpha,
            creative_suite::media::VideoOutputProfile::ProRes4444Alpha,
            creative_suite::media::VideoOutputProfile::ProRes4444XqAlpha,
            creative_suite::media::VideoOutputProfile::Vp9Alpha}) {
        for (const auto& container : containers) {
            for (const auto& encoder : container.video_encoders) {
                const auto option = std::find_if(encoder.alpha_profiles.begin(),
                    encoder.alpha_profiles.end(), [&](const auto& value) {
                        return value.profile == profile;
                    });
                if (option != encoder.alpha_profiles.end())
                    return AlphaOutputChoice{container, encoder, *option};
            }
        }
    }
    return std::nullopt;
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
        true, 20.0, 100.0, 100.0, 0, {{0, 20.0}, {3, 50.0}}});
    image.effects.emplace_back(motion::model::GaussianBlurEffect{true, 10.0});
    snapshot.still_frames.emplace(image.id, solidFrame(20, 180, 30));
    snapshot.layers.push_back(std::move(image));

    auto linked_image = imageLayer(3, 4);
    linked_image.id = 6;
    linked_image.name = "Linked image revision";
    linked_image.transform.position_x = 0.85;
    linked_image.transform.position_y = 0.85;
    linked_image.transform.scale = 0.18;
    snapshot.still_frames.emplace(linked_image.id, solidFrame(190, 20, 180));
    snapshot.layers.push_back(std::move(linked_image));

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

    CompositionLayer glow_shape{};
    glow_shape.id = 7;
    glow_shape.kind = LayerKind::Shape;
    glow_shape.name = "Glow export fixture";
    glow_shape.timeline_start_frame = 3;
    glow_shape.duration_frames = 4;
    glow_shape.transform.position_x = 0.82;
    glow_shape.transform.position_y = 0.82;
    auto glow_content = motion::model::defaultShapeLayerContent(canvas,
        motion::model::ShapeKind::Rectangle);
    glow_content.width = 8;
    glow_content.height = 8;
    glow_content.fill_color = {240, 30, 20, 255};
    glow_shape.content = glow_content;
    GlowEffect glow;
    glow.parameters.color = {10, 240, 255};
    glow.parameters.opacity_percent = 100;
    glow.parameters.radius_pixels = 8;
    glow_shape.effects.push_back(glow);
    snapshot.layers.push_back(std::move(glow_shape));

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

std::optional<std::uint64_t> summaryNumber(
    const std::string& summary, const std::string& field)
{
    const auto prefix = field + "=\"";
    const auto start = summary.find(prefix);
    if (start == std::string::npos) return std::nullopt;
    const auto value_start = start + prefix.size();
    const auto end = summary.find('"', value_start);
    if (end == std::string::npos) return std::nullopt;
    try {
        return std::stoull(summary.substr(value_start, end - value_start));
    } catch (...) {
        return std::nullopt;
    }
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
    auto* sequence_action = findAction(window, "motion-export-png-sequence-action");
    require(sequence_action->isEnabled(), "PNG sequence export enables when a visual layer exists");

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

    QTimer::singleShot(0, [&directory] {
        auto* dialog = QApplication::activeModalWidget();
        require(dialog != nullptr, "Export PNG Sequence opens its destination dialog");
        auto* name = findWidget<QLineEdit>(dialog, "motion-png-sequence-directory-name");
        require(name->text() == QStringLiteral("Motion_Sequence"),
                "untitled compositions suggest the Motion_Sequence folder name");
        findWidget<QLineEdit>(dialog, "motion-png-sequence-parent")
            ->setText(directory.path());
        name->setText(QStringLiteral("window-sequence"));
        findWidget<QPushButton>(dialog, "motion-png-sequence-accept")->click();
    });
    sequence_action->trigger();
    require(waitFor([&window] {
                return window.findChild<QProgressDialog*>(QStringLiteral("motion-export-progress")) == nullptr;
            }, 30000),
            "the progress dialog closes when PNG sequence export completes");
    const auto ui_sequence = std::filesystem::path(directory.path().toStdString()) /
        "window-sequence";
    require(std::filesystem::is_regular_file(ui_sequence / "frame_000000.png"),
            "the File menu sequence action publishes a numbered PNG frame");
    require(window.isWindowModified() == dirty_before_export,
            "PNG sequence export does not change the composition dirty state");

    QTimer::singleShot(0, [] {
        auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
        if (prompt != nullptr) prompt->button(QMessageBox::Discard)->click();
    });
    window.close();
}

void testPngSequenceDialog(const QTemporaryDir& parent_directory)
{
    motion::ui::MotionPngSequenceExportDialog dialog(
        parent_directory.path(), QStringLiteral("composition_frames"));
    auto* parent = findWidget<QLineEdit>(&dialog, "motion-png-sequence-parent");
    auto* name = findWidget<QLineEdit>(&dialog, "motion-png-sequence-directory-name");
    auto* accept = findWidget<QPushButton>(&dialog, "motion-png-sequence-accept");
    require(accept->isEnabled(), "a valid new sequence folder can be exported");
    const auto settings = dialog.exportSettings();
    require(settings.has_value() && settings->directory_name == "composition_frames",
            "the destination dialog returns the selected parent and new folder name");

    std::filesystem::create_directory(
        std::filesystem::path(parent_directory.path().toStdString()) / "existing");
    name->setText(QStringLiteral("existing"));
    require(!accept->isEnabled() && !dialog.exportSettings().has_value(),
            "the dialog refuses an existing destination folder");
    name->setText(QStringLiteral("invalid/name"));
    require(!accept->isEnabled(), "the dialog rejects path separators in the folder name");
    require(findWidget<QLabel>(&dialog, "motion-png-sequence-note")->text().contains(
                QStringLiteral("valid folder name")),
            "the dialog explains why an invalid folder name cannot be exported");
    parent->setText(QString{});
    require(!accept->isEnabled(), "the dialog requires an existing parent folder");
}

MotionExportSnapshot makePngSequenceSnapshot()
{
    MotionExportSnapshot snapshot;
    snapshot.canvas_size = {64, 48};
    snapshot.frame_rate = {24, 1};

    auto image = imageLayer(1, 2);
    auto pixels = std::make_shared<RgbaFrame>(*solidFrame(220, 40, 30));
    for (std::size_t offset = 3; offset < pixels->rgba_pixels.size(); offset += 4)
        pixels->rgba_pixels[offset] = 0;
    for (int y = 16; y < 32; ++y) {
        for (int x = 24; x < 40; ++x) {
            const auto offset = static_cast<std::size_t>(y) * pixels->stride +
                static_cast<std::size_t>(x) * 4U;
            pixels->rgba_pixels[offset + 3] = 128;
        }
    }
    snapshot.still_frames.emplace(image.id, std::move(pixels));
    snapshot.layers.push_back(std::move(image));

    CompositionLayer hidden{};
    hidden.id = 2;
    hidden.kind = LayerKind::Shape;
    hidden.name = "Hidden duration tail";
    hidden.timeline_start_frame = 4;
    hidden.duration_frames = 1;
    hidden.visible = false;
    hidden.content = motion::model::defaultShapeLayerContent(snapshot.canvas_size);
    snapshot.layers.push_back(std::move(hidden));

    CompositionLayer null_layer{};
    null_layer.id = 3;
    null_layer.kind = LayerKind::Null;
    null_layer.name = "Non-rendering Null";
    null_layer.timeline_start_frame = 7;
    null_layer.duration_frames = 9;
    snapshot.layers.push_back(std::move(null_layer));
    return snapshot;
}

void testPngSequenceExporter(const QTemporaryDir& parent_directory)
{
    const auto parent = pathFromQString(parent_directory.path());
    const MotionPngSequenceSettings settings{parent, "transparent-frames"};
    const auto destination = parent / "transparent-frames";
    const auto snapshot = makePngSequenceSnapshot();

    std::vector<int> progress;
    std::atomic_bool not_canceled{false};
    motion::ui::MotionPngSequenceExporter::exportSequence(snapshot, settings,
        not_canceled, [&progress](int value) { progress.push_back(value); });
    require(std::filesystem::is_directory(destination),
            "a completed PNG sequence publishes one new folder");
    require(!progress.empty() && progress.back() == 100,
            "PNG sequence export reports completion progress");
    for (int index = 0; index < 5; ++index) {
        const auto number = std::string(6 - std::to_string(index).size(), '0') +
            std::to_string(index);
        const auto path = destination / ("frame_" + number + ".png");
        require(std::filesystem::is_regular_file(path),
                "the exported sequence uses zero-based six-digit frame names");
        const auto encoded = path.u8string();
        QImageReader reader(QString::fromUtf8(reinterpret_cast<const char*>(encoded.data()),
            static_cast<qsizetype>(encoded.size())));
        const auto image = reader.read();
        require(!image.isNull() && image.width() == 64 && image.height() == 48,
                "every sequence image preserves the composition dimensions");
        if (index == 0 || index == 4) {
            require(image.pixelColor(1, 1).alpha() == 0,
                    "empty lead-in and hidden-only duration frames remain transparent");
        }
        if (index == 1) {
            require(image.pixelColor(32, 24).alpha() == 128 &&
                        image.pixelColor(1, 1).alpha() == 0,
                    "partially transparent layer pixels and transparent canvas pixels survive PNG export");
        }
    }
    std::size_t exported_file_count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(destination)) {
        if (entry.is_regular_file()) ++exported_file_count;
    }
    require(exported_file_count == 5,
            "the sequence contains one PNG for each exported frame and no manifest");
    require(!std::filesystem::exists(destination / "frame_000005.png"),
            "Null layers do not extend the exported frame range");

    const auto gpu_fallback_destination = parent / "gpu-fallback-frames";
    motion::ui::MotionExportRenderOptions gpu_fallback_options;
    gpu_fallback_options.gpu_composition_enabled = true;
    std::atomic_bool gpu_fallback_not_canceled{false};
    motion::ui::MotionPngSequenceExporter::exportSequence(snapshot,
        MotionPngSequenceSettings{parent, "gpu-fallback-frames"},
        gpu_fallback_not_canceled, {}, gpu_fallback_options);
    for (int index = 0; index < 5; ++index) {
        const auto number = std::string(6 - std::to_string(index).size(), '0') +
            std::to_string(index);
        const auto reference = QImage(QString::fromStdString(
            (destination / ("frame_" + number + ".png")).string()));
        const auto fallback = QImage(QString::fromStdString(
            (gpu_fallback_destination / ("frame_" + number + ".png")).string()));
        require(reference == fallback,
                "an unavailable OpenGL surface falls back to identical CPU sequence pixels");
    }

    const auto collision = parent / "existing-sequence";
    std::filesystem::create_directory(collision);
    {
        std::ofstream sentinel(collision / "keep.txt", std::ios::binary);
        sentinel << "preserve";
    }
    const auto collision_sentinel = fileBytes(collision / "keep.txt");
    bool collision_rejected = false;
    try {
        motion::ui::MotionPngSequenceExporter::exportSequence(snapshot,
            MotionPngSequenceSettings{parent, "existing-sequence"}, not_canceled);
    } catch (const std::exception&) {
        collision_rejected = true;
    }
    require(collision_rejected &&
                fileBytes(collision / "keep.txt") == collision_sentinel,
            "an existing destination is rejected without changing its files");

    const auto late_collision = parent / "late-sequence-collision";
    std::atomic_bool late_collision_not_canceled{false};
    bool late_collision_rejected = false;
    std::vector<std::uint8_t> late_collision_sentinel;
    try {
        motion::ui::MotionPngSequenceExporter::exportSequence(snapshot,
            MotionPngSequenceSettings{parent, "late-sequence-collision"},
            late_collision_not_canceled, [&](int progress) {
                if (progress == 0 || std::filesystem::exists(late_collision)) return;
                std::filesystem::create_directory(late_collision);
                std::ofstream sentinel(late_collision / "keep.txt", std::ios::binary);
                sentinel << "preserve";
                sentinel.close();
                late_collision_sentinel = fileBytes(late_collision / "keep.txt");
            });
    } catch (const std::exception& error) {
        late_collision_rejected =
            std::string(error.what()).find("appeared during rendering") != std::string::npos;
    }
    require(late_collision_rejected &&
                fileBytes(late_collision / "keep.txt") == late_collision_sentinel,
            "a destination created while rendering is preserved and prevents publication");

    const auto canceled_destination = parent / "canceled-sequence";
    std::atomic_bool cancel_requested{false};
    bool cancellation_reported = false;
    try {
        motion::ui::MotionPngSequenceExporter::exportSequence(snapshot,
            MotionPngSequenceSettings{parent, "canceled-sequence"}, cancel_requested,
            [&cancel_requested](int) { cancel_requested.store(true, std::memory_order_release); });
    } catch (const motion::ui::MotionExportCancelled&) {
        cancellation_reported = true;
    }
    require(cancellation_reported && !std::filesystem::exists(canceled_destination),
            "canceling a sequence removes its staging folder and never publishes a partial sequence");

    const auto failed_destination = parent / "failed-sequence";
    motion::ui::MotionExportRenderOptions fail_write;
    fail_write.fail_png_write_number_for_testing = 2;
    bool write_failure_reported = false;
    try {
        motion::ui::MotionPngSequenceExporter::exportSequence(snapshot,
            MotionPngSequenceSettings{parent, "failed-sequence"}, not_canceled, {}, fail_write);
    } catch (const std::exception& error) {
        const std::string message = error.what();
        write_failure_reported = message.find("frame 1") != std::string::npos &&
            message.find("frame_000001.png") != std::string::npos;
    }
    require(write_failure_reported && !std::filesystem::exists(failed_destination),
            "a PNG write failure reports its frame and path and removes staging");

    QObject worker_receiver;
    std::optional<motion::ui::MotionExportResult> worker_result;
    motion::ui::MotionExportWorker failed_sequence_worker(
        &worker_receiver, snapshot,
        MotionPngSequenceSettings{parent, "worker-failed-sequence"}, {},
        [&worker_result](motion::ui::MotionExportResult result) {
            worker_result = std::move(result);
        }, fail_write);
    failed_sequence_worker.start();
    require(waitFor([&] { return worker_result.has_value(); }),
            "a PNG sequence worker reports a failed frame write");
    failed_sequence_worker.wait();
    require(worker_result->kind == motion::ui::MotionExportKind::PngSequence &&
                !worker_result->succeeded &&
                worker_result->error_message.find("frame 1") != std::string::npos &&
                worker_result->error_message.find("frame_000001.png") != std::string::npos,
            "the failed sequence worker preserves the frame and path diagnostic");
    const auto failure_log_bytes = fileBytes(
        creative_suite::diagnostics::Logger::instance().log_path());
    const std::string failure_log(failure_log_bytes.begin(), failure_log_bytes.end());
    require(failure_log.find("operation=\"render_png_sequence\"") != std::string::npos &&
                failure_log.find("worker-failed-sequence") != std::string::npos &&
                failure_log.find("frame_000001.png") != std::string::npos,
            "PNG sequence failures log their operation, destination and failing frame path");

    for (const auto& entry : std::filesystem::directory_iterator(parent)) {
        require(entry.path().filename().string().find(".motion-sequence-rendering-") != 0,
                "canceled and failed sequence staging folders are removed");
    }
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
    auto* output_profile = findWidget<QComboBox>(&dialog, "motion-export-output-profile");
    auto* bitrate = findWidget<QDoubleSpinBox>(&dialog, "motion-export-video-bitrate");
    auto* vp9_crf = findWidget<QSpinBox>(&dialog, "motion-export-vp9-crf");
    require(!container->currentData().toString().isEmpty(),
            "the export settings choose an available container");
    require(!encoder->currentData(Qt::UserRole + 1).toBool(),
            "experimental hardware encoders require an explicit selection");
    require(resolution->currentIndex() == 0 && resolution->currentData().toSize() == QSize(640, 360),
            "the composition dimensions are the initial output resolution");
    require(std::abs(rate->value() - 30000.0 / 1001.0) < 0.001,
            "the output frame-rate field starts at the exact composition rate");
    require(quality->currentIndex() == 1,
            "the export dialog starts with Standard quality");
    require(output_profile->currentData().toInt() ==
                static_cast<int>(creative_suite::media::VideoOutputProfile::Bitrate),
            "the export dialog defaults to opaque output");
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
    const auto alpha = chooseAlphaOutput();
    if (alpha) {
        output->setText(QStringLiteral("alpha-profile-test.mp4"));
        output_profile->setCurrentIndex(1);
        const auto transparent_settings = dialog.exportSettings();
        require(transparent_settings && transparent_settings->profile !=
                    creative_suite::media::VideoOutputProfile::Bitrate &&
                    transparent_settings->container_name ==
                        container->currentData().toString().toStdString() &&
                    transparent_settings->alpha_pixel_format_name.size() > 0,
                "the transparent selector chooses a discovered alpha adapter");
        require(QFileInfo(output->text()).suffix() != QStringLiteral("mp4"),
                "switching profiles updates the incompatible MP4 extension");
        require(bitrate->isHidden(),
                "transparent profiles hide the opaque bitrate control");

        const auto selectAlphaEncoder = [&](const char* container_name,
                                            const char* encoder_name) {
            const auto container_index = container->findData(QString::fromLatin1(container_name));
            if (container_index < 0) return false;
            container->setCurrentIndex(container_index);
            const auto encoder_index = encoder->findData(QString::fromLatin1(encoder_name));
            if (encoder_index < 0) return false;
            encoder->setCurrentIndex(encoder_index);
            return true;
        };
        const auto mov_alpha = std::find_if(available.begin(), available.end(), [](const auto& option) {
            if (option.name != "mov") return false;
            return std::any_of(option.video_encoders.begin(), option.video_encoders.end(),
                [](const auto& value) {
                    return value.name == "prores_ks" && std::any_of(
                        value.alpha_profiles.begin(), value.alpha_profiles.end(),
                        [](const auto& alpha_profile) {
                            return alpha_profile.profile ==
                                    creative_suite::media::VideoOutputProfile::ProRes4444Alpha ||
                                alpha_profile.profile ==
                                    creative_suite::media::VideoOutputProfile::ProRes4444XqAlpha;
                        });
                });
        });
        if (mov_alpha != available.end() && selectAlphaEncoder("mov", "prores_ks")) {
            const auto prores_encoder = std::find_if(mov_alpha->video_encoders.begin(),
                mov_alpha->video_encoders.end(), [](const auto& value) {
                    return value.name == "prores_ks";
                });
            require(prores_encoder != mov_alpha->video_encoders.end() &&
                    quality->count() == static_cast<int>(prores_encoder->alpha_profiles.size()) &&
                    quality->count() > 0 &&
                    quality->isEnabled() == (quality->count() > 1),
                    "ProRes exposes every runtime-supported alpha quality profile");
            for (int index = 0; index < quality->count(); ++index)
                require(quality->itemText(index).contains(QStringLiteral("16-bit alpha")),
                    "ProRes quality controls identify 16-bit alpha");
            const auto preferred_prores = quality->findData(static_cast<int>(
                creative_suite::media::VideoOutputProfile::ProRes4444XqAlpha));
            const auto selected_prores = preferred_prores >= 0 ? preferred_prores
                : quality->findData(static_cast<int>(
                    creative_suite::media::VideoOutputProfile::ProRes4444Alpha));
            require(selected_prores >= 0, "a discovered ProRes profile is selectable");
            quality->setCurrentIndex(selected_prores);
            const auto prores_settings = dialog.exportSettings();
            require(prores_settings && prores_settings->profile == static_cast<
                        creative_suite::media::VideoOutputProfile>(
                            quality->currentData().toInt()),
                    "the selected ProRes quality profile reaches the export settings");
        }
        const auto webm_alpha = std::find_if(available.begin(), available.end(), [](const auto& option) {
            if (option.name != "webm") return false;
            return std::any_of(option.video_encoders.begin(), option.video_encoders.end(),
                [](const auto& value) {
                    return value.name == "libvpx-vp9" && std::any_of(
                        value.alpha_profiles.begin(), value.alpha_profiles.end(),
                        [](const auto& alpha_profile) {
                            return alpha_profile.profile ==
                                creative_suite::media::VideoOutputProfile::Vp9Alpha;
                        });
                });
        });
        if (webm_alpha != available.end() && selectAlphaEncoder("webm", "libvpx-vp9")) {
            require(vp9_crf->minimum() == 0 && vp9_crf->maximum() == 63 &&
                    vp9_crf->value() == 32 && !vp9_crf->isHidden() && quality->isHidden(),
                    "VP9 exposes CRF 0-63 at its default value of 32");
            vp9_crf->setValue(19);
            const auto vp9_settings = dialog.exportSettings();
            require(vp9_settings && vp9_settings->profile ==
                        creative_suite::media::VideoOutputProfile::Vp9Alpha &&
                    vp9_settings->quality_value == 19,
                    "the VP9 CRF value reaches the export settings");
        }

        output_profile->setCurrentIndex(0);
        require(dialog.exportSettings() && dialog.exportSettings()->profile ==
                    creative_suite::media::VideoOutputProfile::Bitrate,
                "returning to Opaque restores the standard export profile");
        if (mov_alpha != available.end()) {
            require(selectAlphaEncoder("mov", "prores_ks"),
                    "the existing ProRes alpha adapter can be selected for opaque output");
            output_profile->setCurrentIndex(1);
            require(container->currentData().toString() == QStringLiteral("mov") &&
                    encoder->currentData().toString() == QStringLiteral("prores_ks"),
                    "enabling transparency preserves a compatible container and encoder");
            output_profile->setCurrentIndex(0);
        }
    } else {
        auto* profile_model = qobject_cast<QStandardItemModel*>(output_profile->model());
        auto* transparent_item = profile_model != nullptr
            ? profile_model->item(1) : nullptr;
        require(transparent_item != nullptr && !transparent_item->isEnabled() &&
                    !transparent_item->toolTip().isEmpty(),
                "the unsupported transparent profile is disabled with a reason");
    }
    require(dialog.findChild<QWidget*>(QStringLiteral("motion-export-audio")) == nullptr,
            "Motion export settings do not expose audio controls");
}

} // namespace

int main(int argc, char* argv[])
{
    const bool require_gpu = argc > 1 && std::string(argv[1]) == "--require-gpu";
    QApplication application(argc, argv);
    try {
        qputenv("CREATIVE_SUITE_MOTION_GPU_COMPOSITION", "1");
        require(motion::settings::gpuCompositionEnabled(),
                "the shared GPU composition opt-in can be enabled for preview and export");
        const motion::ui::MotionExportCancelled cancellation_exception;
        require(std::string(cancellation_exception.what()) ==
                    "Motion Studio export was canceled.",
                "the export cancellation exception preserves its user-facing cause");

        testSettingsDialog();

        const auto [container, encoder] = chooseOutput(require_gpu);
        QTemporaryDir temporary_directory;
        require(temporary_directory.isValid(), "a temporary export folder is available");
        const auto log_directory = pathFromQString(temporary_directory.path()) / "diagnostics";
        require(creative_suite::diagnostics::Logger::instance().initialize(log_directory),
                "a temporary diagnostics log can be initialized for export metrics checks");
        testPngSequenceDialog(temporary_directory);
        testPngSequenceExporter(temporary_directory);
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
                    decoded[3]->rgba_pixels[background_offset + 2] > 60 &&
                    decoded[5]->rgba_pixels[background_offset] >
                        decoded[3]->rgba_pixels[background_offset] + 30,
                "animated Color Adjustment keyframes are evaluated per exported frame");
        const auto linked_image_offset = static_cast<std::size_t>(41) * decoded[3]->stride +
            static_cast<std::size_t>(54) * 4U;
        require(decoded[3]->rgba_pixels[linked_image_offset] > 130 &&
                    decoded[3]->rgba_pixels[linked_image_offset + 1] < 70 &&
                    decoded[3]->rgba_pixels[linked_image_offset + 2] > 120,
                "export resolves each same-source Image layer from its own linked frame");
        const auto glow_halo_offset = static_cast<std::size_t>(39) * decoded[3]->stride +
            static_cast<std::size_t>(44) * 4U;
        require(decoded[3]->rgba_pixels[glow_halo_offset + 2] >
                    decoded[3]->rgba_pixels[glow_halo_offset] + 25 &&
                    decoded[3]->rgba_pixels[glow_halo_offset + 1] >
                    decoded[3]->rgba_pixels[glow_halo_offset] + 25,
                "CPU preview and video export include the colored Outer Glow halo");
        require(!has_non_black(*decoded.back()),
                "hidden layers extend the export duration without rendering pixels");

        if (const auto alpha = chooseAlphaOutput()) {
            const auto alpha_target = outputPath(temporary_directory, alpha->container,
                "motion-transparent-export");
            auto alpha_settings = settingsFor(alpha_target, alpha->container, alpha->encoder);
            alpha_settings.profile = alpha->profile.profile;
            alpha_settings.alpha_pixel_format_name = alpha->profile.pixel_format_name;
            alpha_settings.quality_value = 32;
            auto surface = creative_suite::composition::OpenGlFrameCompositor::createSurface();
            motion::ui::MotionExportRenderOptions alpha_render_options;
            alpha_render_options.gpu_composition_enabled = surface != nullptr;
            alpha_render_options.gpu_surface = surface.get();
            std::atomic_bool alpha_not_canceled{false};
            motion::ui::MotionVideoExporter::exportVideo(snapshot, alpha_settings,
                alpha_not_canceled, {}, nullptr, alpha_render_options);
            const auto alpha_decoded = decodeVideo(alpha_target);
            require(alpha_decoded.size() == 9 && alpha_decoded.front() && alpha_decoded[3],
                "transparent Motion export decodes the full frame range");
            require(alpha_decoded.front()->rgba_pixels[3] <= 2,
                "blank lead-in remains transparent in the alpha export profile");
            require(alpha_decoded[3]->rgba_pixels[3] >= 250,
                "visible image content remains opaque in the alpha export profile");
        }

        auto baseline_video_snapshot = snapshot;
        auto source_in_video_snapshot = snapshot;
        const auto video_layer = [](MotionExportSnapshot& value) -> CompositionLayer& {
            const auto found = std::find_if(value.layers.begin(), value.layers.end(),
                [](const CompositionLayer& layer) { return layer.id == 4; });
            if (found == value.layers.end())
                throw std::runtime_error("The export fixture video layer is missing.");
            found->transform.scale = 0.65;
            return *found;
        };
        (void)video_layer(baseline_video_snapshot);
        video_layer(source_in_video_snapshot).source_start_frame = 12;
        const auto baseline_video_target = outputPath(
            temporary_directory, container, "video-source-in-baseline");
        const auto source_in_video_target = outputPath(
            temporary_directory, container, "video-source-in-shifted");
        std::atomic_bool baseline_not_canceled{false};
        std::atomic_bool source_in_not_canceled{false};
        motion::ui::MotionVideoExporter::exportVideo(
            baseline_video_snapshot,
            settingsFor(baseline_video_target, container, encoder),
            baseline_not_canceled);
        motion::ui::MotionVideoExporter::exportVideo(
            source_in_video_snapshot,
            settingsFor(source_in_video_target, container, encoder),
            source_in_not_canceled);
        const auto baseline_video_frames = decodeVideo(baseline_video_target);
        const auto source_in_video_frames = decodeVideo(source_in_video_target);
        require(baseline_video_frames.size() == source_in_video_frames.size() &&
                    !videoFramesMatch(baseline_video_frames, source_in_video_frames, 0),
                "Export uses a video layer's source-in frame instead of rendering from frame zero");

        QObject gpu_worker_receiver;
        std::optional<motion::ui::MotionExportResult> gpu_fallback_result;
        const auto gpu_fallback_target = outputPath(
            temporary_directory, container, "gpu-export-no-surface-fallback");
        motion::ui::MotionExportWorker gpu_fallback_worker(
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
        require(!require_gpu || bool(gpu_surface), "native GPU export requires an OpenGL surface");
        std::optional<motion::ui::MotionExportResult> gpu_surface_result;
        std::filesystem::path gpu_surface_target;
        motion::ui::MotionExportPerformanceSummary gpu_surface_performance;
        if (gpu_surface) {
            gpu_surface_target = outputPath(
                temporary_directory, container, "gpu-export-surface");
            motion::ui::MotionExportWorker gpu_surface_worker(
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

            const auto gpu_metrics_target = outputPath(
                temporary_directory, container, "gpu-export-async-metrics");
            std::atomic_bool metrics_not_canceled{false};
            motion::ui::MotionVideoExporter::exportVideo(
                snapshot, settingsFor(gpu_metrics_target, container, encoder),
                metrics_not_canceled, {}, &gpu_surface_performance,
                motion::ui::MotionExportRenderOptions{true, gpu_surface.get()});
            require(gpu_surface_performance.frames_rendered == 9 &&
                        (gpu_surface_performance.readback_mode == "pbo_async" ||
                         gpu_surface_performance.readback_mode == "gpu_sync" ||
                         gpu_surface_performance.readback_mode == "cpu_fallback"),
                    "GPU export metrics identify async, synchronous, or CPU fallback mode");
            require(!require_gpu || (gpu_surface_performance.readback_mode == "pbo_async" &&
                        gpu_surface_performance.gpu.composition_frames == 9 &&
                        gpu_surface_performance.gpu.fallback_frames == 0),
                    "native GPU export must encode every GPU frame through asynchronous RGBA readback");
            if (require_gpu) std::cout << "native_gpu_export_frames="
                << gpu_surface_performance.gpu.composition_frames << " readback_mode="
                << gpu_surface_performance.readback_mode << '\n';
            if (gpu_surface_performance.readback_mode == "pbo_async") {
                require(gpu_surface_performance.gpu.readback_frames_submitted == 9 &&
                            gpu_surface_performance.gpu.readback_frames_collected == 9 &&
                            gpu_surface_performance.gpu.readback_slots >= 1 &&
                            gpu_surface_performance.gpu.readback_slots <= 2,
                        "the asynchronous export submits and collects every frame within its PBO pool");
            }

            const auto sync_target = outputPath(
                temporary_directory, container, "gpu-export-small-budget-sync-fallback");
            std::atomic_bool sync_not_canceled{false};
            motion::ui::MotionExportPerformanceSummary sync_performance;
            motion::ui::MotionExportRenderOptions sync_options{true, gpu_surface.get()};
            sync_options.async_readback_staging_budget_bytes_for_testing = 1;
            motion::ui::MotionVideoExporter::exportVideo(
                snapshot, settingsFor(sync_target, container, encoder), sync_not_canceled,
                {}, &sync_performance, sync_options);
            require(sync_performance.gpu.readback_frames_submitted == 0 &&
                        (sync_performance.gpu.composition_frames == 0 ||
                         sync_performance.readback_mode == "gpu_sync"),
                    "an insufficient PBO staging budget selects synchronous GPU readback");
            require(videoFramesMatch(decoded, decodeVideo(sync_target), 4),
                    "synchronous GPU readback fallback preserves frame output");

            if (gpu_surface_performance.gpu.readback_frames_submitted > 0) {
                const auto recovery_target = outputPath(
                    temporary_directory, container, "gpu-export-async-collection-recovery");
                std::atomic_bool recovery_not_canceled{false};
                motion::ui::MotionExportPerformanceSummary recovery_performance;
                motion::ui::MotionExportRenderOptions recovery_options{
                    true, gpu_surface.get()};
                recovery_options.fail_async_readback_collection_number_for_testing = 2;
                motion::ui::MotionVideoExporter::exportVideo(
                    snapshot, settingsFor(recovery_target, container, encoder),
                    recovery_not_canceled, {}, &recovery_performance, recovery_options);
                require(recovery_performance.readback_mode == "pbo_async_cpu_fallback" &&
                            recovery_performance.gpu.failures == 1 &&
                            recovery_performance.gpu.fallback_frames > 0 &&
                            recovery_performance.frames_rendered == 9 &&
                            recovery_performance.write_count == 9,
                        "a collection failure rerenders outstanding and remaining frames on CPU");
                require(videoFramesMatch(decoded, decodeVideo(recovery_target), 4),
                        "mid-pipeline CPU recovery preserves output order and frame parity");

                const auto queued_cancel_target = outputPath(
                    temporary_directory, container, "gpu-export-queued-cancel-preserves-output");
                {
                    std::ofstream output(queued_cancel_target, std::ios::binary);
                    output << "existing-queued-cancel-destination";
                }
                const auto queued_cancel_bytes = fileBytes(queued_cancel_target);
                std::atomic_bool queued_cancel{false};
                motion::ui::MotionExportPerformanceSummary queued_cancel_performance;
                bool queued_cancel_reported = false;
                try {
                    motion::ui::MotionVideoExporter::exportVideo(
                        snapshot,
                        settingsFor(queued_cancel_target, container, encoder), queued_cancel,
                        [&queued_cancel](int value) {
                            if (value > 0) queued_cancel.store(true, std::memory_order_release);
                        },
                        &queued_cancel_performance,
                        motion::ui::MotionExportRenderOptions{true, gpu_surface.get()});
                } catch (const motion::ui::MotionExportCancelled&) {
                    queued_cancel_reported = true;
                }
                require(queued_cancel_reported &&
                            queued_cancel_performance.gpu.readback_frames_submitted >= 2,
                        "canceling with submitted readbacks terminates the queued GPU pipeline");
                require(fileBytes(queued_cancel_target) == queued_cancel_bytes,
                        "queued GPU cancellation preserves the existing destination");
            }
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

        const auto encoder_failure_target = outputPath(
            temporary_directory, container, "preserve-on-encoder-failure");
        {
            std::ofstream output(encoder_failure_target, std::ios::binary);
            output << "existing-encoder-destination";
        }
        const auto encoder_failure_bytes = fileBytes(encoder_failure_target);
        std::atomic_bool encoder_not_canceled{false};
        motion::ui::MotionExportRenderOptions encoder_failure_options;
        encoder_failure_options.fail_encoder_write_number_for_testing = 1;
        bool encoder_failure_reported = false;
        try {
            motion::ui::MotionVideoExporter::exportVideo(
                snapshot, settingsFor(encoder_failure_target, container, encoder),
                encoder_not_canceled, {}, nullptr, encoder_failure_options);
        } catch (const std::exception&) {
            encoder_failure_reported = true;
        }
        require(encoder_failure_reported,
                "an encoder-thread failure is propagated to the export caller");
        require(fileBytes(encoder_failure_target) == encoder_failure_bytes,
                "an encoder failure preserves the existing destination");
        for (const auto& entry : std::filesystem::directory_iterator(temporary_directory.path().toStdString())) {
            require(entry.path().filename().string().find(".rendering-motion-") == std::string::npos,
                    "failed and canceled export temporary files are removed");
        }

        testMainWindowExportAction(temporary_directory);

        QObject worker_receiver;
        std::optional<motion::ui::MotionExportResult> cancelled_result;
        motion::ui::MotionExportWorker cancelled_worker(
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
        motion::ui::MotionExportWorker failed_worker(
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
            require(summary.find("schema_version=\"3\"") != std::string::npos &&
                        summary.find("gpu_composition_requested=") != std::string::npos &&
                        summary.find("gpu_backend_used=") != std::string::npos &&
                        summary.find("gpu_readback_mode=") != std::string::npos &&
                        summary.find("gpu_composition_attempts=") != std::string::npos &&
                        summary.find("gpu_composition_frames=") != std::string::npos &&
                        summary.find("gpu_composition_fallback_frames=") != std::string::npos &&
                        summary.find("gpu_composition_failures=") != std::string::npos &&
                        summary.find("gpu_composition_uploaded_bytes=") != std::string::npos &&
                        summary.find("gpu_composition_readback_bytes=") != std::string::npos &&
                        summary.find("gpu_upload_average_ms=") != std::string::npos &&
                        summary.find("gpu_draw_submission_average_ms=") != std::string::npos &&
                        summary.find("gpu_readback_average_ms=") != std::string::npos &&
                        summary.find("gpu_async_readback_slots=") != std::string::npos &&
                        summary.find("gpu_readback_frames_submitted=") != std::string::npos &&
                        summary.find("gpu_readback_frames_collected=") != std::string::npos &&
                        summary.find("gpu_readback_fence_waits=") != std::string::npos &&
                        summary.find("gpu_readback_submit_average_ms=") != std::string::npos &&
                        summary.find("gpu_readback_fence_wait_average_ms=") != std::string::npos &&
                        summary.find("gpu_readback_copy_average_ms=") != std::string::npos &&
                        summary.find("readback_peak_pending_frames=") != std::string::npos &&
                        summary.find("readback_peak_pending_bytes=") != std::string::npos &&
                        summary.find("encoder_queue_wait_ms=") != std::string::npos &&
                        summary.find("encoder_queue_peak_frames=") != std::string::npos &&
                        summary.find("encoder_queue_peak_bytes=") != std::string::npos &&
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
            if (summary.find("gpu_readback_mode=\"pbo_async\"") != std::string::npos) {
                const auto submitted = summaryNumber(summary, "gpu_readback_frames_submitted");
                const auto collected = summaryNumber(summary, "gpu_readback_frames_collected");
                const auto slot_count = summaryNumber(summary, "gpu_async_readback_slots");
                const auto pending_frames = summaryNumber(summary, "readback_peak_pending_frames");
                const auto pending_bytes = summaryNumber(summary, "readback_peak_pending_bytes");
                const auto encoder_frames = summaryNumber(summary, "encoder_queue_peak_frames");
                require(submitted.has_value() && collected.has_value() && *collected <= *submitted &&
                            slot_count.has_value() &&
                            *slot_count >= 1 && *slot_count <= 2 && pending_frames.has_value() &&
                            *pending_frames <= 3 && pending_bytes.has_value() &&
                            *pending_bytes <= 128ULL * 1024 * 1024 && encoder_frames.has_value() &&
                            *encoder_frames <= 1,
                        "PBO export metrics stay bounded, including interrupted jobs");
                if (summary.find("outcome=\"completed\"") != std::string::npos)
                    require(submitted == summaryNumber(summary, "frames_rendered") &&
                            submitted == summaryNumber(summary, "write_count") &&
                            *submitted > 0 && collected == submitted && encoder_frames == 1,
                        "completed PBO exports collect and encode every frame in order");
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
