#include "rendering/image_document_renderer.h"
#include "image_document_session.h"
#include "image_editor_performance_metrics.h"
#include "import_export/image_exporter.h"

#include <creative_suite/system_monitor/performance_usage.h>
#include <creative_suite/system_monitor/system_memory_usage.h>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QSaveFile>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTransform>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <numeric>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

using namespace image_editor;

namespace {

struct ProfileDefinition {
    const char* name;
    QSize canvas_size;
    int editable_layers = 4;
    int strokes_per_layer = 16;
    int mask_strokes_per_layer = 0;
    int raster_repeats_per_layer = 1;
    QSize raster_size{512, 512};
    bool include_text_and_shapes = true;
    bool export_images = false;
};

QString syntheticUuid(std::uint64_t value) {
    return QStringLiteral("00000000-0000-4000-8000-%1")
        .arg(static_cast<qulonglong>(value), 12, 16, QLatin1Char('0'));
}

QImage makePattern(const QSize& size, int seed) {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) return {};
    for (int y = 0; y < size.height(); ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < size.width(); ++x) {
            const int red = (x * 13 + y * 7 + seed * 29) & 0xff;
            const int green = (x * 3 + y * 17 + seed * 47) & 0xff;
            const int blue = (x * 11 + y * 5 + seed * 61) & 0xff;
            row[x] = qRgba(red, green, blue, 255);
        }
    }
    return image;
}

ImageOperation makeStroke(const QSize& size, int index, bool erase, bool mask = false) {
    ImageOperation operation;
    operation.kind = erase ? OperationKind::EraseStroke : OperationKind::PaintStroke;
    QVector<QPointF> points;
    points.reserve(64);
    const int start_x = (index * 73 + 31) % std::max(1, size.width());
    const int start_y = (index * 47 + 19) % std::max(1, size.height());
    for (int point = 0; point < 64; ++point) {
        const int x = (start_x + point * (3 + index % 7)) % std::max(1, size.width());
        const int y = (start_y + point * (5 + index % 11)) % std::max(1, size.height());
        points.append(QPointF(x, y));
    }
    if (erase) {
        const std::uint64_t id = 0x100000000ULL +
            (mask ? 0x20000000ULL : 0ULL) + 0x10000000ULL +
            static_cast<std::uint64_t>(index);
        operation.erase_stroke.id = syntheticUuid(id);
        operation.erase_stroke.points = std::move(points);
        operation.erase_stroke.diameter = 18 + index % 37;
    } else {
        const std::uint64_t id = 0x100000000ULL +
            (mask ? 0x20000000ULL : 0ULL) + static_cast<std::uint64_t>(index);
        operation.paint_stroke.id = syntheticUuid(id);
        operation.paint_stroke.points = std::move(points);
        operation.paint_stroke.diameter = 12 + index % 29;
        operation.paint_stroke.color = mask
            ? QColor(0, 0, 0, 150)
            : QColor((index * 31) & 0xff, (index * 67) & 0xff,
                     (index * 97) & 0xff, 230);
    }
    return operation;
}

ProfileDefinition profileDefinition(const QString& name) {
    ProfileDefinition profile;
    profile.name = "reference";
    profile.canvas_size = QSize(1920, 1080);
    if (name == QStringLiteral("mask-heavy")) {
        profile.name = "mask-heavy";
        profile.strokes_per_layer = 8;
        profile.mask_strokes_per_layer = 64;
    } else if (name == QStringLiteral("stroke-heavy")) {
        profile.name = "stroke-heavy";
        profile.strokes_per_layer = 128;
    } else if (name == QStringLiteral("large-image")) {
        profile.name = "large-image";
        profile.canvas_size = QSize(3840, 2160);
        profile.raster_size = QSize(2048, 2048);
        profile.strokes_per_layer = 24;
    } else if (name == QStringLiteral("repeated-source")) {
        profile.name = "repeated-source";
        profile.raster_size = QSize(2048, 2048);
        profile.raster_repeats_per_layer = 8;
        profile.strokes_per_layer = 4;
        profile.include_text_and_shapes = false;
    } else if (name == QStringLiteral("export")) {
        profile.name = "export";
        profile.export_images = true;
    }
    return profile;
}

ImageExportSnapshot makeSnapshot(const ProfileDefinition& profile,
                                 ImageDocumentData* document,
                                 QHash<QString, QImage>* raster_images) {
    document->base_kind = ImageBaseKind::SourceImage;
    document->source_size = profile.canvas_size;
    document->canvas_size = profile.canvas_size;
    document->canvas_background = Qt::transparent;

    ImageLayerData background;
    background.id = syntheticUuid(1);
    background.name = QStringLiteral("Background");
    background.background = true;
    document->layers.append(background);
    document->root_stack.append({background.id, false});

    ImageGroupData group;
    group.id = syntheticUuid(2);
    group.name = QStringLiteral("Benchmark Group");
    const QImage shared_raster_source = makePattern(profile.raster_size, 7);
    for (int index = 0; index < profile.editable_layers; ++index) {
        ImageLayerData layer;
        layer.id = syntheticUuid(static_cast<std::uint64_t>(10 + index));
        layer.name = QStringLiteral("Layer %1").arg(index + 1);
        layer.operations.reserve(profile.strokes_per_layer +
            profile.raster_repeats_per_layer + 3);
        for (int stroke = 0; stroke < profile.strokes_per_layer; ++stroke)
            layer.operations.append(makeStroke(profile.canvas_size,
                index * profile.strokes_per_layer + stroke,
                stroke % 4 == 3));

        if (profile.include_text_and_shapes && index == 0) {
            ImageOperation shape;
            shape.kind = OperationKind::Shape;
            shape.shape.id = syntheticUuid(0x400000001ULL);
            shape.shape.kind = ImageShapeKind::Rectangle;
            shape.shape.start = QPointF(80, 80);
            shape.shape.end = QPointF(780, 420);
            shape.shape.stroke_color = QColor(250, 210, 60);
            shape.shape.fill_color = QColor(20, 60, 120, 110);
            layer.operations.append(shape);

            ImageOperation text;
            text.kind = OperationKind::Text;
            text.text.id = syntheticUuid(0x400000002ULL);
            text.text.content = QStringLiteral("Image Editor performance sample");
            text.text.font_family = QStringLiteral("Sans Serif");
            text.text.font_pixel_size = 48;
            text.text.color = Qt::white;
            text.text.position = QPointF(96, 180);
            text.text.box_width = 1100;
            layer.operations.append(text);
        }

        for (int repeat = 0; repeat < profile.raster_repeats_per_layer; ++repeat) {
            ImageOperation raster;
            raster.kind = OperationKind::RasterImage;
            raster.raster.id = syntheticUuid(0x500000000ULL +
                static_cast<std::uint64_t>(index * profile.raster_repeats_per_layer + repeat));
            raster.raster.source_size = profile.raster_size;
            QTransform transform;
            transform.translate((index * 211 + repeat * 83) % profile.canvas_size.width(),
                                (index * 97 + repeat * 61) % profile.canvas_size.height());
            const qreal scale = repeat % 2 == 0 ? 0.24 : 0.37;
            transform.scale(scale, scale);
            raster.raster.transform = transform;
            layer.operations.append(raster);
            raster_images->insert(raster.raster.id, shared_raster_source);
        }

        if (profile.mask_strokes_per_layer > 0) {
            ImageLayerMaskData mask;
            for (int stroke = 0; stroke < profile.mask_strokes_per_layer; ++stroke)
                mask.operations.append(makeStroke(profile.canvas_size,
                    index * profile.mask_strokes_per_layer + stroke,
                    stroke % 3 == 2, true));
            layer.mask = std::move(mask);
        }

        if (index < 2) {
            layer.parent_group_id = group.id;
            group.layer_ids.append(layer.id);
        }
        document->layers.append(layer);
        if (index == 1) document->root_stack.append({group.id, true});
        else if (index >= 2) document->root_stack.append({layer.id, false});
    }
    if (!group.layer_ids.isEmpty()) document->groups.append(group);

    ImageExportSnapshot snapshot;
    snapshot.source_image = makePattern(profile.canvas_size, 3);
    snapshot.document = *document;
    snapshot.selected_layer_id = syntheticUuid(10);
    snapshot.selected_group_id = syntheticUuid(2);
    snapshot.raster_images = *raster_images;
    return snapshot;
}

struct SyntheticFixture {
    QString directory;
    QString document_path;
    ImageDocumentData document;
    ImageExportSnapshot export_snapshot;
};

bool createSyntheticFixture(const ProfileDefinition& profile,
                            const QString& temporary_root,
                            SyntheticFixture* fixture,
                            QString* error) {
    if (fixture == nullptr) {
        if (error != nullptr) *error = QStringLiteral("Synthetic fixture output is missing.");
        return false;
    }
    fixture->directory = QDir(temporary_root).filePath(
        QString::fromLatin1(profile.name));
    if (!QDir().mkpath(fixture->directory)) {
        if (error != nullptr) *error = QStringLiteral("Could not create the synthetic fixture directory.");
        return false;
    }

    ImageDocumentData document;
    QHash<QString, QImage> raster_images;
    auto snapshot = makeSnapshot(profile, &document, &raster_images);
    if (snapshot.source_image.isNull() || raster_images.isEmpty() ||
        raster_images.constBegin()->isNull()) {
        if (error != nullptr) *error = QStringLiteral("Could not allocate the synthetic profile images.");
        return false;
    }

    const QString source_path = QDir(fixture->directory).filePath(
        QStringLiteral("synthetic-source.png"));
    const QString raster_path = QDir(fixture->directory).filePath(
        QStringLiteral("synthetic-raster.png"));
    fixture->document_path = QDir(fixture->directory).filePath(
        QStringLiteral("synthetic-document.cimg"));
    if (!snapshot.source_image.save(source_path, "PNG")) {
        if (error != nullptr) *error = QStringLiteral("Could not save the synthetic source image.");
        return false;
    }
    if (!raster_images.constBegin().value().save(raster_path, "PNG")) {
        if (error != nullptr) *error = QStringLiteral("Could not save the synthetic raster source image.");
        return false;
    }

    document.source_path = source_path;
    for (auto& layer : document.layers) {
        for (auto& operation : layer.operations) {
            if (operation.kind == OperationKind::RasterImage)
                operation.raster.source_path = raster_path;
        }
    }
    if (!ImageDocumentStore::saveDocument(fixture->document_path, document, error))
        return false;

    snapshot.document = document;
    fixture->document = std::move(document);
    fixture->export_snapshot = std::move(snapshot);
    return true;
}

std::uint64_t operationCount(const ImageDocumentData& document) {
    std::uint64_t count = static_cast<std::uint64_t>(document.operations.size());
    for (const auto& layer : document.layers) {
        count += static_cast<std::uint64_t>(layer.operations.size());
        if (layer.mask.has_value())
            count += static_cast<std::uint64_t>(layer.mask->operations.size());
    }
    for (const auto& group : document.groups)
        count += static_cast<std::uint64_t>(group.operations.size());
    return count;
}

struct ResourceSnapshot {
    system_monitor::PerformanceSnapshot first;
    system_monitor::PerformanceSnapshot latest;
    std::optional<std::uint64_t> peak_working_set_bytes;
    std::optional<std::uint64_t> peak_private_usage_bytes;
};

class ResourceProbe final {
public:
    ResourceProbe() {
        capture(sampler_.sample(), true);
        thread_ = std::thread([this]() {
            while (true) {
                {
                    std::unique_lock lock(wait_mutex_);
                    if (wait_cv_.wait_for(lock, std::chrono::seconds(1),
                                          [this]() { return stop_; })) break;
                }
                capture(sampler_.sample(), false);
            }
        });
    }

    ~ResourceProbe() {
        stop();
    }

    void stop() {
        {
            std::lock_guard lock(wait_mutex_);
            if (stop_) return;
            stop_ = true;
        }
        wait_cv_.notify_all();
        if (thread_.joinable()) thread_.join();
        capture(sampler_.sample(), false);
    }

    [[nodiscard]] ResourceSnapshot snapshot() const {
        std::lock_guard lock(data_mutex_);
        return data_;
    }

private:
    void capture(const system_monitor::PerformanceSnapshot& sample, bool first) {
        std::lock_guard lock(data_mutex_);
        if (first) data_.first = sample;
        data_.latest = sample;
        if (sample.process_working_set_bytes.has_value() &&
            (!data_.peak_working_set_bytes.has_value() ||
             *sample.process_working_set_bytes > *data_.peak_working_set_bytes))
            data_.peak_working_set_bytes = sample.process_working_set_bytes;
        if (sample.process_private_usage_bytes.has_value() &&
            (!data_.peak_private_usage_bytes.has_value() ||
             *sample.process_private_usage_bytes > *data_.peak_private_usage_bytes))
            data_.peak_private_usage_bytes = sample.process_private_usage_bytes;
    }

    mutable std::mutex data_mutex_;
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
    bool stop_ = false;
    system_monitor::PerformanceSampler sampler_;
    ResourceSnapshot data_;
    std::thread thread_;
};

QJsonValue optionalJson(const std::optional<std::uint64_t>& value) {
    return value.has_value()
        ? QJsonValue(static_cast<qint64>(*value)) : QJsonValue(QJsonValue::Null);
}

QJsonValue optionalJson(const std::optional<double>& value) {
    return value.has_value()
        ? QJsonValue(*value) : QJsonValue(QJsonValue::Null);
}

QJsonObject resourcesToJson(const ResourceSnapshot& resources) {
    return QJsonObject{
        {QStringLiteral("working_set_start_bytes"), optionalJson(
            resources.first.process_working_set_bytes)},
        {QStringLiteral("working_set_end_bytes"), optionalJson(
            resources.latest.process_working_set_bytes)},
        {QStringLiteral("working_set_sampled_peak_bytes"), optionalJson(
            resources.peak_working_set_bytes)},
        {QStringLiteral("private_usage_start_bytes"), optionalJson(
            resources.first.process_private_usage_bytes)},
        {QStringLiteral("private_usage_end_bytes"), optionalJson(
            resources.latest.process_private_usage_bytes)},
        {QStringLiteral("private_usage_sampled_peak_bytes"), optionalJson(
            resources.peak_private_usage_bytes)},
        {QStringLiteral("process_cpu_percent_latest"), optionalJson(
            resources.latest.process_cpu_percent)},
    };
}

struct Distribution {
    double average_ms = 0;
    double p50_ms = 0;
    double p95_ms = 0;
    double maximum_ms = 0;
};

Distribution summarize(std::vector<std::uint64_t> samples) {
    Distribution result;
    if (samples.empty()) return result;
    const auto total = std::accumulate(samples.begin(), samples.end(), std::uint64_t{0});
    std::sort(samples.begin(), samples.end());
    const auto at_percentile = [&samples](double fraction) {
        const auto rank = static_cast<std::size_t>(
            std::ceil(fraction * static_cast<double>(samples.size())));
        return samples[std::min(samples.size() - 1, rank == 0 ? 0 : rank - 1)];
    };
    constexpr double ns_to_ms = 1.0 / 1'000'000.0;
    result.average_ms = static_cast<double>(total) * ns_to_ms /
        static_cast<double>(samples.size());
    result.p50_ms = static_cast<double>(at_percentile(0.50)) * ns_to_ms;
    result.p95_ms = static_cast<double>(at_percentile(0.95)) * ns_to_ms;
    result.maximum_ms = static_cast<double>(samples.back()) * ns_to_ms;
    return result;
}

QJsonObject distributionToJson(const Distribution& distribution,
                               std::size_t iteration_count) {
    return QJsonObject{
        {QStringLiteral("iterations"), static_cast<qint64>(iteration_count)},
        {QStringLiteral("average_ms"), distribution.average_ms},
        {QStringLiteral("p50_ms"), distribution.p50_ms},
        {QStringLiteral("p95_ms"), distribution.p95_ms},
        {QStringLiteral("maximum_ms"), distribution.maximum_ms},
    };
}

constexpr std::size_t performance_stage_count =
    static_cast<std::size_t>(ImageEditorPerformanceStage::Count);

struct MeasurementSamples {
    std::vector<std::uint64_t> wall_time_nanoseconds;
    std::array<std::vector<std::uint64_t>, performance_stage_count> stage_time_nanoseconds;
    std::array<std::uint64_t, performance_stage_count> stage_call_counts{};
};

struct RenderedView {
    QImage composite;
    QHash<QString, QImage> layer_and_group_thumbnails;
    QHash<QString, QImage> mask_thumbnails;
};

struct RefreshMeasurement {
    QJsonObject report;
    RenderedView representative_view;
    std::uint64_t maximum_retained_layer_cache_bytes = 0;
};

bool renderSessionView(ImageDocumentSession& session,
                       RenderedView* view,
                       QString* error) {
    if (view == nullptr) {
        if (error != nullptr) *error = QStringLiteral("Rendered view output is missing.");
        return false;
    }
    view->composite = session.renderedImage();
    view->layer_and_group_thumbnails = session.renderedLayerThumbnails(QSize(160, 120));
    view->mask_thumbnails = session.renderedLayerMaskThumbnails(QSize(160, 120));
    if (view->composite.isNull()) {
        if (error != nullptr) *error = QStringLiteral("Synthetic document composition failed.");
        return false;
    }
    if (view->layer_and_group_thumbnails.size() !=
        session.data().layers.size() + session.data().groups.size()) {
        if (error != nullptr) *error = QStringLiteral("Synthetic layer or group thumbnails are incomplete.");
        return false;
    }
    for (auto it = view->layer_and_group_thumbnails.cbegin();
         it != view->layer_and_group_thumbnails.cend(); ++it) {
        if (it.value().isNull()) {
            if (error != nullptr) *error = QStringLiteral("A synthetic layer or group thumbnail is empty.");
            return false;
        }
    }
    for (auto it = view->mask_thumbnails.cbegin(); it != view->mask_thumbnails.cend(); ++it) {
        if (it.value().isNull()) {
            if (error != nullptr) *error = QStringLiteral("A synthetic mask thumbnail is empty.");
            return false;
        }
    }
    return true;
}

bool sameRenderedView(const RenderedView& left, const RenderedView& right) {
    return left.composite == right.composite &&
        left.layer_and_group_thumbnails == right.layer_and_group_thumbnails &&
        left.mask_thumbnails == right.mask_thumbnails;
}

void appendIterationSamples(const ImageEditorPerformanceSnapshot& before,
                            const ImageEditorPerformanceSnapshot& after,
                            std::uint64_t wall_time_nanoseconds,
                            MeasurementSamples* samples) {
    if (samples == nullptr) return;
    samples->wall_time_nanoseconds.push_back(wall_time_nanoseconds);
    for (std::size_t index = 0; index < performance_stage_count; ++index) {
        const auto& before_stage = before.timings[index];
        const auto& after_stage = after.timings[index];
        const auto elapsed = after_stage.total_nanoseconds >= before_stage.total_nanoseconds
            ? after_stage.total_nanoseconds - before_stage.total_nanoseconds : 0U;
        const auto calls = after_stage.count >= before_stage.count
            ? after_stage.count - before_stage.count : 0U;
        samples->stage_time_nanoseconds[index].push_back(elapsed);
        samples->stage_call_counts[index] += calls;
    }
}

QJsonObject measurementToJson(const MeasurementSamples& samples) {
    QJsonObject stage_results;
    for (std::size_t index = 0; index < performance_stage_count; ++index) {
        const auto calls = samples.stage_call_counts[index];
        if (calls == 0) continue;
        const auto& stage_samples = samples.stage_time_nanoseconds[index];
        const auto stage = static_cast<ImageEditorPerformanceStage>(index);
        QJsonObject distribution = distributionToJson(
            summarize(stage_samples), stage_samples.size());
        distribution.insert(QStringLiteral("call_count_total"),
                            static_cast<qint64>(calls));
        stage_results.insert(QString::fromLatin1(
            imageEditorPerformanceStageName(stage)), distribution);
    }
    return QJsonObject{
        {QStringLiteral("iteration_wall_time"), distributionToJson(
            summarize(samples.wall_time_nanoseconds),
            samples.wall_time_nanoseconds.size())},
        {QStringLiteral("stage_time_per_iteration"), stage_results},
    };
}

bool openSyntheticSession(const SyntheticFixture& fixture,
                          ImageDocumentSession* session,
                          QString* error) {
    if (session == nullptr || !session->openDocument(fixture.document_path, error)) {
        if (error != nullptr && error->isEmpty())
            *error = QStringLiteral("Could not open the synthetic benchmark document.");
        return false;
    }
    return true;
}

bool runRefreshWarmups(const SyntheticFixture& fixture,
                       int warmup_iterations,
                       QString* error) {
    for (int iteration = 0; iteration < warmup_iterations; ++iteration) {
        ImageDocumentSession session;
        RenderedView view;
        if (!openSyntheticSession(fixture, &session, error) ||
            !renderSessionView(session, &view, error)) return false;
    }
    return true;
}

bool runRefreshMeasurement(const SyntheticFixture& fixture,
                           int warmup_iterations,
                           int measured_iterations,
                           bool warm_cache,
                           RefreshMeasurement* measurement,
                           QString* error) {
    auto& metrics = ImageEditorPerformanceMetrics::instance();
    metrics.setEnabled(false);
    metrics.reset();
    if (!warm_cache && !runRefreshWarmups(fixture, warmup_iterations, error)) return false;

    MeasurementSamples samples;
    samples.wall_time_nanoseconds.reserve(static_cast<std::size_t>(measured_iterations));
    for (auto& stage_samples : samples.stage_time_nanoseconds)
        stage_samples.reserve(static_cast<std::size_t>(measured_iterations));

    std::optional<ImageDocumentSession> warm_session;
    if (warm_cache) {
        warm_session.emplace();
        if (!openSyntheticSession(fixture, &*warm_session, error)) return false;
        RenderedView view;
        // The initial refresh primes the session's thumbnail caches and is excluded.
        if (!renderSessionView(*warm_session, &view, error)) return false;
        for (int iteration = 0; iteration < warmup_iterations; ++iteration)
            if (!renderSessionView(*warm_session, &view, error)) return false;
    }

    metrics.reset();
    metrics.setEnabled(true);
    for (int iteration = 0; iteration < measured_iterations; ++iteration) {
        std::optional<ImageDocumentSession> cold_session;
        if (!warm_cache) {
            metrics.setEnabled(false);
            cold_session.emplace();
            if (!openSyntheticSession(fixture, &*cold_session, error)) {
                metrics.setEnabled(false);
                return false;
            }
            // Session loading is setup, not part of view refresh timing.
            metrics.reset();
            metrics.setEnabled(true);
        }
        auto& session = warm_cache ? *warm_session : *cold_session;
        const auto before = metrics.snapshot();
        const auto started = std::chrono::steady_clock::now();
        RenderedView view;
        if (!renderSessionView(session, &view, error)) {
            metrics.setEnabled(false);
            return false;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        const auto after = metrics.snapshot();
        appendIterationSamples(before, after,
            elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0U, &samples);
        if (measurement != nullptr) {
            measurement->maximum_retained_layer_cache_bytes = std::max(
                measurement->maximum_retained_layer_cache_bytes,
                session.cachedRasterLayerBytes());
        }
        if (measurement != nullptr) {
            if (iteration == 0) measurement->representative_view = view;
            else if (!sameRenderedView(measurement->representative_view, view)) {
                if (error != nullptr) *error = QStringLiteral(
                    "Synthetic view pixels changed between benchmark iterations.");
                metrics.setEnabled(false);
                return false;
            }
        }
    }
    metrics.setEnabled(false);
    if (measurement != nullptr) measurement->report = measurementToJson(samples);
    return true;
}

bool runExportIteration(const ImageExportSnapshot& snapshot,
                        const QString& temporary_directory,
                        int iteration,
                        QString* error) {
    for (const QString& extension : {QStringLiteral("png"), QStringLiteral("jpg")}) {
        ImageExportOptions options;
        options.scope = ImageExportScope::Composite;
        const QString path = QDir(temporary_directory).filePath(
            QStringLiteral("export-%1.%2").arg(iteration).arg(extension));
        const auto result = exportImageSnapshot(snapshot, path, options);
        if (result.status != ImageExportStatus::Succeeded) {
            if (error != nullptr) *error = result.error;
            return false;
        }
    }
    return true;
}

bool runExportMeasurement(const ProfileDefinition& profile,
                          const SyntheticFixture& fixture,
                          int warmup_iterations,
                          int measured_iterations,
                          MeasurementSamples* samples,
                          QString* error) {
    if (!profile.export_images || samples == nullptr) return true;
    auto& metrics = ImageEditorPerformanceMetrics::instance();
    metrics.setEnabled(false);
    for (int iteration = 0; iteration < warmup_iterations; ++iteration)
        if (!runExportIteration(fixture.export_snapshot, fixture.directory,
                                iteration, error)) return false;

    metrics.reset();
    metrics.setEnabled(true);
    samples->wall_time_nanoseconds.reserve(static_cast<std::size_t>(measured_iterations));
    for (auto& stage_samples : samples->stage_time_nanoseconds)
        stage_samples.reserve(static_cast<std::size_t>(measured_iterations));
    for (int iteration = 0; iteration < measured_iterations; ++iteration) {
        const auto before = metrics.snapshot();
        const auto started = std::chrono::steady_clock::now();
        if (!runExportIteration(fixture.export_snapshot, fixture.directory,
                               warmup_iterations + iteration, error)) {
            metrics.setEnabled(false);
            return false;
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started).count();
        appendIterationSamples(before, metrics.snapshot(),
            elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0U, samples);
    }
    metrics.setEnabled(false);
    return true;
}

QJsonObject runProfile(const ProfileDefinition& profile,
                       int warmup_iterations,
                       int measured_iterations,
                       const QString& temp_directory,
                       QString* error) {
    SyntheticFixture fixture;
    if (!createSyntheticFixture(profile, temp_directory, &fixture, error)) return {};

    ResourceProbe resources;
    RefreshMeasurement cold_refresh;
    if (!runRefreshMeasurement(fixture, warmup_iterations, measured_iterations,
                               false, &cold_refresh, error)) return {};
    RefreshMeasurement warm_refresh;
    if (!runRefreshMeasurement(fixture, warmup_iterations, measured_iterations,
                               true, &warm_refresh, error)) return {};
    const bool pixels_match = sameRenderedView(
        cold_refresh.representative_view, warm_refresh.representative_view);
    if (!pixels_match) {
        if (error != nullptr) *error = QStringLiteral(
            "Cold and warm synthetic view pixels differ.");
        return {};
    }

    MeasurementSamples export_samples;
    if (!runExportMeasurement(profile, fixture, warmup_iterations,
                              measured_iterations, &export_samples, error)) return {};
    resources.stop();
    const auto resource_snapshot = resources.snapshot();

    QJsonObject workload{
        {QStringLiteral("name"), QString::fromLatin1(profile.name)},
        {QStringLiteral("width"), profile.canvas_size.width()},
        {QStringLiteral("height"), profile.canvas_size.height()},
        {QStringLiteral("layer_count_including_background"), fixture.document.layers.size()},
        {QStringLiteral("editable_layer_count"), profile.editable_layers},
        {QStringLiteral("group_count"), fixture.document.groups.size()},
        {QStringLiteral("operation_count"), static_cast<qint64>(operationCount(fixture.document))},
        {QStringLiteral("raster_resource_count"), 1},
        {QStringLiteral("stroke_points_per_stroke"), 64},
        {QStringLiteral("mask_strokes_per_layer"), profile.mask_strokes_per_layer},
        {QStringLiteral("raster_repeats_per_layer"), profile.raster_repeats_per_layer},
        {QStringLiteral("raster_source_width"), profile.raster_size.width()},
        {QStringLiteral("raster_source_height"), profile.raster_size.height()},
        {QStringLiteral("includes_png_jpeg_export"), profile.export_images},
    };
    QJsonObject view_refresh{
        {QStringLiteral("cold"), cold_refresh.report},
        {QStringLiteral("warm"), warm_refresh.report},
        {QStringLiteral("cache_prime_refreshes_excluded"), 1},
        {QStringLiteral("pixel_outputs_match"), pixels_match},
        {QStringLiteral("layer_raster_cache"), QJsonObject{
            {QStringLiteral("limit_bytes"), static_cast<qint64>(
                ImageLayerRasterCache::maximum_bytes)},
            {QStringLiteral("cold_retained_bytes_max"), static_cast<qint64>(
                cold_refresh.maximum_retained_layer_cache_bytes)},
            {QStringLiteral("warm_retained_bytes_max"), static_cast<qint64>(
                warm_refresh.maximum_retained_layer_cache_bytes)},
        }},
    };
    QJsonObject report{
        {QStringLiteral("workload"), workload},
        {QStringLiteral("warmup_iterations"), warmup_iterations},
        {QStringLiteral("measured_iterations"), measured_iterations},
        {QStringLiteral("view_refresh"), view_refresh},
        {QStringLiteral("resource_samples"), resourcesToJson(resource_snapshot)},
    };
    if (profile.export_images)
        report.insert(QStringLiteral("export"), measurementToJson(export_samples));
    return report;
}

QString compilerName() {
#if defined(_MSC_VER)
    return QStringLiteral("MSVC %1").arg(_MSC_VER);
#elif defined(__clang__)
    return QStringLiteral("Clang %1.%2.%3")
        .arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#elif defined(__GNUC__)
    return QStringLiteral("GCC %1.%2.%3")
        .arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#else
    return QStringLiteral("Unknown");
#endif
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("creative-suite-image-editor-benchmark"));
    app.setApplicationVersion(QStringLiteral(CREATIVE_SUITE_APP_VERSION));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Repeatable, synthetic CPU rendering and export measurements for Image Editor."));
    parser.addHelpOption();
    const QCommandLineOption profile_option(
        {QStringLiteral("p"), QStringLiteral("profile")},
        QStringLiteral("Profile: all, reference, mask-heavy, stroke-heavy, large-image, repeated-source, or export."),
        QStringLiteral("name"), QStringLiteral("all"));
    const QCommandLineOption iterations_option(
        {QStringLiteral("i"), QStringLiteral("iterations")},
        QStringLiteral("Measured repetitions per profile (1–1000)."),
        QStringLiteral("count"), QStringLiteral("30"));
    const QCommandLineOption warmup_option(
        {QStringLiteral("w"), QStringLiteral("warmup")},
        QStringLiteral("Warmup repetitions per profile (0–100)."),
        QStringLiteral("count"), QStringLiteral("3"));
    const QCommandLineOption output_option(
        {QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Write the JSON report to this file; stdout is used when omitted."),
        QStringLiteral("path"));
    parser.addOption(profile_option);
    parser.addOption(iterations_option);
    parser.addOption(warmup_option);
    parser.addOption(output_option);
    parser.process(app);

    bool iterations_ok = false;
    bool warmup_ok = false;
    const int iterations = parser.value(iterations_option).toInt(&iterations_ok);
    const int warmups = parser.value(warmup_option).toInt(&warmup_ok);
    if (!iterations_ok || iterations < 1 || iterations > 1000 ||
        !warmup_ok || warmups < 0 || warmups > 100) {
        qCritical("Iteration counts are outside their allowed ranges.");
        return 2;
    }

    const QString selected_profile = parser.value(profile_option);
    const QStringList available_profiles{
        QStringLiteral("reference"), QStringLiteral("mask-heavy"),
        QStringLiteral("stroke-heavy"), QStringLiteral("large-image"),
        QStringLiteral("repeated-source"), QStringLiteral("export")};
    QStringList profiles = selected_profile == QStringLiteral("all")
        ? available_profiles : QStringList{selected_profile};
    for (const auto& name : profiles) {
        if (!available_profiles.contains(name)) {
            qCritical("Unknown benchmark profile.");
            return 2;
        }
    }

    QTemporaryDir temporary_directory;
    if (!temporary_directory.isValid()) {
        qCritical("Could not create temporary benchmark output directory.");
        return 2;
    }
    QJsonArray results;
    for (const auto& name : profiles) {
        QString error;
        const auto profile = profileDefinition(name);
        const auto result = runProfile(profile, warmups, iterations,
            temporary_directory.path(), &error);
        if (result.isEmpty()) {
            const QString cause = error.isEmpty()
                ? QStringLiteral("Unknown synthetic benchmark error.") : error;
            std::fprintf(stderr, "Benchmark profile failed: %s\n",
                         qPrintable(cause));
            return 1;
        }
        results.append(result);
    }

#ifdef NDEBUG
    const QString build_type = QStringLiteral("Release");
#else
    const QString build_type = QStringLiteral("Debug");
#endif
    const QJsonObject report{
        {QStringLiteral("schema_version"), 2},
        {QStringLiteral("application"), QStringLiteral("Image Editor")},
        {QStringLiteral("application_version"), app.applicationVersion()},
        {QStringLiteral("measurement_method"),
            QStringLiteral("session_view_refresh_with_cold_and_warm_thumbnail_and_layer_raster_caches")},
        {QStringLiteral("timestamp_utc"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
        {QStringLiteral("os"), QSysInfo::prettyProductName()},
        {QStringLiteral("os_kernel"), QSysInfo::kernelType() + QStringLiteral(" ") + QSysInfo::kernelVersion()},
        {QStringLiteral("architecture"), QSysInfo::currentCpuArchitecture()},
        {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
        {QStringLiteral("compiler"), compilerName()},
        {QStringLiteral("build_type"), build_type},
        {QStringLiteral("sampling_interval_ms"), 1000},
        {QStringLiteral("seed"), 3},
        {QStringLiteral("profiles"), results},
    };
    const QByteArray json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    const QString output_path = parser.value(output_option);
    if (output_path.isEmpty()) {
        QFile output;
        if (!output.open(stdout, QIODevice::WriteOnly)) {
            qCritical("Could not write benchmark JSON to stdout.");
            return 1;
        }
        output.write(json);
        return 0;
    }
    QSaveFile output(output_path);
    if (!output.open(QIODevice::WriteOnly) || output.write(json) != json.size() ||
        !output.commit()) {
        qCritical("Could not write benchmark JSON report.");
        return 1;
    }
    return 0;
}
