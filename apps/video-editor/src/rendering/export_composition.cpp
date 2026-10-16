#include "rendering/export_composition.h"
#include "logging/logger.h"

#include <algorithm>
#include <thread>
#include <QOpenGLContext>

namespace rendering::detail {
namespace {
namespace composition = creative_suite::composition;

class NativeExportGpu final : public ExportGpuCompositor {
public:
    explicit NativeExportGpu(QOffscreenSurface* surface, composition::GraphicsBackend backend) : backend_(composition::createGpuFrameCompositor(surface,
        composition::OpenGlPrecisionPolicy::Automatic, QOpenGLContext::globalShareContext(), {}, backend)) {}
    composition::OpenGlCompositionResult compose(int width, int height,
        const std::vector<composition::CompositionLayer>& layers,
        const composition::OpenGlFrameCompositor::CancellationPredicate& cancel,
        composition::OpenGlCompositionTimings* timings) override {
        return backend_->compose(width, height, layers, cancel, timings);
    }
    composition::OpenGlResourceUsage resourceUsage() const noexcept override { return backend_->resourceUsage(); }
private:
    std::unique_ptr<composition::GpuFrameCompositor> backend_;
};

logging::Context jobContext(const RenderJob& job) {
    logging::Context context{{"job_id", std::to_string(job.id)},
        {"output_path", job.settings.output_path.toUtf8().toStdString()},
        {"width", std::to_string(job.settings.width)}, {"height", std::to_string(job.settings.height)},
        {"video_encoder", job.settings.video_encoder_name.toStdString()}};
    std::string paths;
    for (const auto& track : job.project_snapshot.timeline_tracks) for (const auto& clip : track.clips) {
        if (clip.source_path.empty()) continue;
        if (!paths.empty()) paths += "; ";
        const auto path = clip.source_path.u8string();
        paths.append(reinterpret_cast<const char*>(path.data()), path.size());
        if (clip.image_editor_variant && !clip.image_editor_variant->published_output_path.empty()) {
            const auto published = clip.image_editor_variant->published_output_path.u8string();
            paths += " [published: ";
            paths.append(reinterpret_cast<const char*>(published.data()), published.size());
            paths += "]";
        }
    }
    context.emplace_back("sources", std::move(paths));
    return context;
}
} // namespace

ExportMetricsScope::ExportMetricsScope(const RenderJob& job, const OfflineExportOptions& options)
    : job_(job), options_(options) { metrics.gpu_requested = job.settings.gpu_composition_enabled; }

ExportMetricsScope::~ExportMetricsScope() {
    metrics.total_nanoseconds = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start_).count());
    try {
        auto context = jobContext(job_);
        context.emplace_back("diagnostic_schema_version", "2");
        context.emplace_back("requested_backend", metrics.gpu_requested ? std::string(composition::backendName(job_.settings.graphics_backend)) : "cpu");
        context.emplace_back("effective_backend", metrics.gpu_frames ? (metrics.cpu_frames ? "mixed" : std::string(composition::backendName(job_.settings.graphics_backend)))
                                                                  : (metrics.cpu_frames ? "cpu" : "none"));
        context.emplace_back("outcome", metrics.outcome == ExportOutcome::Completed ? "completed" :
            metrics.outcome == ExportOutcome::Canceled ? "canceled" : "failed");
        const auto add = [&](const char* name, std::uint64_t value) { context.emplace_back(name, std::to_string(value)); };
        add("cpu_frames", metrics.cpu_frames); add("gpu_frames", metrics.gpu_frames);
        add("encoded_frames", metrics.encoded_frames); add("fallback_frames", metrics.fallback_frames);
        add("gpu_failures", metrics.gpu_failures);
        add("preparation_ns", metrics.preparation_nanoseconds); add("composition_ns", metrics.composition_nanoseconds);
        add("cpu_composition_ns", metrics.cpu_composition_nanoseconds); add("gpu_composition_ns", metrics.gpu_composition_nanoseconds);
        add("upload_ns", metrics.upload_nanoseconds); add("draw_submission_ns", metrics.draw_submission_nanoseconds);
        add("readback_ns", metrics.readback_nanoseconds); add("encoding_ns", metrics.encoding_nanoseconds);
        add("audio_ns", metrics.audio_nanoseconds); add("finalization_ns", metrics.finalization_nanoseconds);
        add("total_ns", metrics.total_nanoseconds); add("uploaded_bytes", metrics.uploaded_bytes);
        add("readback_bytes", metrics.readback_bytes); add("peak_known_gpu_bytes", metrics.peak_known_gpu_bytes);
        add("peak_cpu_frame_bytes", metrics.peak_cpu_frame_bytes); add("peak_prepared_source_bytes", metrics.peak_prepared_source_bytes);
        add("native_encoded_frames", metrics.native_encoded_frames); add("native_video_imports", metrics.native_video_imports);
        add("native_conversion_ns", metrics.native_conversion_nanoseconds);
        add("decoded_hardware_frames", metrics.decoded_hardware_frames); add("decoded_software_frames", metrics.decoded_software_frames);
        add("decoded_downloaded_frames", metrics.decoded_downloaded_frames);
        add("decoded_downloaded_bytes", metrics.decoded_downloaded_bytes); add("encoding_uploaded_bytes", metrics.encoding_uploaded_bytes);
        add("decoder_reserved_gpu_bytes", metrics.decoder_reserved_gpu_bytes); add("graph_peak_gpu_bytes", metrics.graph_peak_gpu_bytes);
        add("encoder_reserved_gpu_bytes", metrics.encoder_reserved_gpu_bytes);
        add("async_readback_submissions", metrics.async_readback_submissions);
        add("async_readback_wait_ns", metrics.async_readback_wait_nanoseconds);
        add("async_readback_slots", metrics.async_readback_slots);add("peak_pending_frames", metrics.peak_pending_frames);
        add("decode_packet_ns", metrics.decode_packet_nanoseconds); add("decode_receive_ns", metrics.decode_receive_nanoseconds);
        add("decode_conversion_ns", metrics.decode_conversion_nanoseconds); add("graph_ns", metrics.graph_nanoseconds);
        logging::Logger::instance().log(logging::Level::Info, "export", "performance_metrics", "Offline export summary.", context);
        if (options_.metrics_callback) options_.metrics_callback(metrics);
    } catch (...) {
        // Diagnostics must not replace a render exception or terminate shutdown.
        logging::Logger::instance().log(logging::Level::Error, "export", "metrics_callback",
            "Publishing the export summary failed.");
    }
}

ExportComposition::ExportComposition(const RenderJob& job, const OfflineExportOptions& options, OfflineExportMetrics& metrics)
    : job_(job), options_(options), metrics_(metrics) {}

unsigned ExportComposition::prepareAsync() {
    if(job_.settings.graphics_backend!=composition::GraphicsBackend::Vulkan || !job_.settings.gpu_composition_enabled ||
       options_.gpu_factory || options_.disable_async_readback_for_testing || qEnvironmentVariableIntValue("CREATIVE_SUITE_VULKAN_SERIAL")==1)return 0;
    async_gpu_=composition::createGpuFrameCompositor(nullptr,composition::OpenGlPrecisionPolicy::Automatic,nullptr,{},composition::GraphicsBackend::Vulkan);
    const auto result=async_gpu_->prepareAsyncReadback(job_.settings.width,job_.settings.height,options_.async_staging_budget_bytes,2,&async_slots_);
    if(result.status!=composition::OpenGlCompositionStatus::Complete){
        if(result.status==composition::OpenGlCompositionStatus::Failed)fallback(result,0,0);
        async_gpu_.reset();async_slots_=0;
    }
    metrics_.async_readback_slots=async_slots_;return async_slots_;
}
composition::OpenGlReadbackResult ExportComposition::collectAsync(composition::GpuReadbackTicket ticket,const std::atomic_bool& canceled) {
    const bool inject=options_.fail_async_collection_for_testing && ++collections_==options_.fail_async_collection_for_testing;
    auto result=async_gpu_->collectAsyncReadback(ticket,[&]{return canceled.load();},inject);
    metrics_.async_readback_wait_nanoseconds+=result.fence_wait_nanoseconds;
    metrics_.readback_nanoseconds+=result.fence_wait_nanoseconds+result.copy_nanoseconds;
    if(result.status==composition::OpenGlCompositionStatus::Complete){metrics_.readback_bytes+=result.bytes;++metrics_.gpu_frames;}
    return result;
}
void ExportComposition::stopAsync(const composition::OpenGlReadbackResult& result,std::int64_t output,std::int64_t timeline) {
    if(async_gpu_)async_gpu_->discardAsyncReadbacks();
    async_gpu_.reset();async_slots_=0;
    if(result.status==composition::OpenGlCompositionStatus::Failed)
        fallback({result.status,{},result.operation,result.cause,result.error_code},output,timeline);
}
void ExportComposition::reduceAsync() {
    if(!async_gpu_)return;
    async_gpu_->discardAsyncReadbacks();
    if(async_slots_>1){
        const auto result=async_gpu_->prepareAsyncReadback(job_.settings.width,job_.settings.height,options_.async_staging_budget_bytes,1,&async_slots_);
        if(result.status==composition::OpenGlCompositionStatus::Failed)fallback(result,0,0);
        if(result.status!=composition::OpenGlCompositionStatus::Complete){async_gpu_.reset();async_slots_=0;}
    }else{async_gpu_.reset();async_slots_=0;}
    metrics_.async_readback_slots=async_slots_;
}

bool ExportComposition::nativeDelivery() const noexcept {
    return job_.settings.gpu_composition_enabled && !failed_ && !cpu_source_recovery_ && options_.gpu_surface && !options_.gpu_factory && job_.settings.graphics_backend != composition::GraphicsBackend::Vulkan && job_.settings.graphics_backend != composition::GraphicsBackend::Cpu;
}

composition::GpuFrameCompositor* ExportComposition::graphBackend() {
    if (job_.settings.graphics_backend == composition::GraphicsBackend::Cpu || !job_.settings.gpu_composition_enabled || failed_ || cpu_source_recovery_ || (!options_.gpu_surface && job_.settings.graphics_backend != composition::GraphicsBackend::Vulkan)) return nullptr;
    if (!graph_gpu_) graph_gpu_ = composition::createGpuFrameCompositor(options_.gpu_surface,
        composition::OpenGlPrecisionPolicy::Automatic, QOpenGLContext::globalShareContext(),
        std::make_shared<composition::OpenGlTexturePoolBudget>(256ULL * 1024 * 1024, 8), job_.settings.graphics_backend);
    return graph_gpu_.get();
}

void ExportComposition::fallback(const composition::OpenGlCompositionResult& result,
    std::int64_t output_frame, std::int64_t timeline_frame, bool rgba_recovery) {
    const bool technical = result.status == composition::OpenGlCompositionStatus::Failed;
    if (technical || !warned_) {
        auto context = jobContext(job_);
        context.emplace_back("output_frame", std::to_string(output_frame));
        context.emplace_back("timeline_frame", std::to_string(timeline_frame));
        context.emplace_back("error_code", std::to_string(result.error_code));
        context.emplace_back("fallback", rgba_recovery && !technical ? "rgba" : "cpu");
        logging::Logger::instance().log(technical ? logging::Level::Error : logging::Level::Warning,
            "export-gpu", result.operation, result.cause, context);
    }
    if (!warned_) {
        warned_ = true;
        if (options_.warning_callback) {
            try { options_.warning_callback(rgba_recovery && !technical ?
                "Export is continuing with a compatible processing path." : "Export is continuing with CPU fallback.", result.error_code); }
            catch (...) { logging::Logger::instance().log(logging::Level::Error, "export", "warning_callback", "The export warning callback failed."); }
        }
    }
    if (technical) { failed_ = true; ++metrics_.gpu_failures; gpu_.reset(); }
}

std::optional<creative_suite::media::RgbaFrame> ExportComposition::compose(const std::vector<composition::CompositionLayer>& layers,
    std::int64_t output_frame, std::int64_t timeline_frame, const std::atomic_bool& canceled,
    creative_suite::media::NativeVideoFramePool* native_pool,
    creative_suite::media::NativeVideoFramePtr* native_output, composition::GpuReadbackTicket* async_ticket) {
    if (canceled.load()) throw ExportCanceled{};
    ExportTimedScope total(metrics_.composition_nanoseconds);
    if (native_output) native_output->reset();
    if(async_ticket)*async_ticket={};
    if(async_ticket && async_gpu_ && async_slots_ && !failed_ && !cpu_source_recovery_){
        if(options_.disable_async_readback_for_testing)throw ExportAsyncBackpressure{};
        composition::OpenGlCompositionTimings timings;
        const auto start=std::chrono::steady_clock::now();
        composition::OpenGlReadbackResult submitted;
        try {
            auto batch=async_gpu_->beginCommandBatch([&]{return canceled.load();});
            auto texture=async_gpu_->composeTexture(job_.settings.width,job_.settings.height,layers,[&]{return canceled.load();},&timings);
            submitted={texture.status,{}, {},texture.operation,texture.cause,texture.error_code};
            if(texture.status==composition::OpenGlCompositionStatus::Complete)submitted=async_gpu_->submitAsyncReadback(texture.frame,[&]{return canceled.load();});
        }catch(const std::exception& error){submitted={composition::OpenGlCompositionStatus::Failed,{}, {},"vulkan-export-submit",error.what()};}
        metrics_.gpu_composition_nanoseconds+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count());
        metrics_.upload_nanoseconds+=timings.upload_nanoseconds;metrics_.draw_submission_nanoseconds+=timings.draw_submission_nanoseconds;
        metrics_.uploaded_bytes+=timings.uploaded_bytes;
        metrics_.peak_known_gpu_bytes=std::max(metrics_.peak_known_gpu_bytes,async_gpu_->resourceUsage().peak_known_bytes);
        if(submitted.status==composition::OpenGlCompositionStatus::Cancelled || canceled.load())throw ExportCanceled{};
        if(submitted.status==composition::OpenGlCompositionStatus::Busy)throw ExportAsyncBackpressure{};
        if(submitted.status==composition::OpenGlCompositionStatus::Complete){
            *async_ticket=submitted.ticket;++metrics_.async_readback_submissions;
            return creative_suite::media::RgbaFrame{job_.settings.width,job_.settings.height,job_.settings.width*4,{}};
        }
        fallback({submitted.status,{},submitted.operation,submitted.cause,submitted.error_code},output_frame,timeline_frame);
    }
    if (native_pool && native_output && nativeDelivery()) {
        ExportTimedScope gpu_time(metrics_.gpu_composition_nanoseconds);
        if (!native_gpu_) native_gpu_ = composition::createGpuFrameCompositor(options_.gpu_surface,
            composition::OpenGlPrecisionPolicy::Automatic, QOpenGLContext::globalShareContext());
        composition::OpenGlCompositionTimings timings;
        auto texture = native_gpu_->composeTexture(job_.settings.width, job_.settings.height, layers,
            [&] { return canceled.load(); }, &timings);
        // Offline export can wait for released targets' GPU fences without
        // blocking the GUI or discarding the selected native encoding path.
        const auto capacity_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
        while (texture.status == composition::OpenGlCompositionStatus::Busy &&
               std::chrono::steady_clock::now() < capacity_deadline) {
            if (canceled.load()) throw ExportCanceled{};
            const auto collected = native_gpu_->collectReleasedTextureFrames();
            if (collected.status == composition::OpenGlCompositionStatus::Failed) {
                texture = {collected.status, {}, collected.operation, collected.cause, collected.error_code};
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            texture = native_gpu_->composeTexture(job_.settings.width, job_.settings.height, layers,
                [&] { return canceled.load(); }, &timings);
        }
        metrics_.upload_nanoseconds += timings.upload_nanoseconds;
        metrics_.uploaded_bytes += timings.uploaded_bytes;
        metrics_.native_video_imports += timings.native_video_imports;
        metrics_.native_conversion_nanoseconds += timings.native_video_conversion_nanoseconds;
        metrics_.draw_submission_nanoseconds += timings.draw_submission_nanoseconds;
        metrics_.peak_known_gpu_bytes = std::max(metrics_.peak_known_gpu_bytes, native_gpu_->resourceUsage().peak_known_bytes);
        composition::OpenGlCompositionResult copied{texture.status, {}, texture.operation, texture.cause, texture.error_code};
        if (texture.status == composition::OpenGlCompositionStatus::Complete && texture.frame) {
            auto destination = native_pool->acquire();
            if (destination) copied = native_gpu_->copyToNative(texture.frame, destination, [&] { return canceled.load(); });
            else copied = {composition::OpenGlCompositionStatus::Busy, {}, "native-output-pool", "All native encoding frames are retained."};
            if (copied.status == composition::OpenGlCompositionStatus::Complete) {
                *native_output = std::move(destination); ++metrics_.gpu_frames;
                return creative_suite::media::RgbaFrame{job_.settings.width, job_.settings.height, job_.settings.width * 4};
            }
        }
        if (copied.status == composition::OpenGlCompositionStatus::Cancelled || canceled.load()) throw ExportCanceled{};
        fallback(copied, output_frame, timeline_frame, true);
    }
    if (job_.settings.gpu_composition_enabled && job_.settings.graphics_backend != composition::GraphicsBackend::Cpu && !failed_ && !cpu_source_recovery_) {
        composition::OpenGlCompositionTimings timings;
        composition::OpenGlCompositionResult result;
        {
            ExportTimedScope gpu_time(metrics_.gpu_composition_nanoseconds);
            try {
                if (!gpu_) gpu_ = options_.gpu_factory ? options_.gpu_factory(options_.gpu_surface)
                                                       : std::make_unique<NativeExportGpu>(options_.gpu_surface, job_.settings.graphics_backend);
                if (gpu_) result = gpu_->compose(job_.settings.width, job_.settings.height, layers,
                    [&] { return canceled.load(std::memory_order_acquire); }, &timings);
                else result = {composition::OpenGlCompositionStatus::Failed, {}, "create-export-backend", "The export GPU adapter is unavailable."};
            } catch (const std::exception& error) {
                result = {composition::OpenGlCompositionStatus::Failed, {}, "compose-export", error.what()};
            }
        }
        metrics_.upload_nanoseconds += timings.upload_nanoseconds;
        metrics_.draw_submission_nanoseconds += timings.draw_submission_nanoseconds;
        metrics_.readback_nanoseconds += timings.readback_nanoseconds;
        metrics_.uploaded_bytes += timings.uploaded_bytes; metrics_.readback_bytes += timings.readback_bytes;
        metrics_.native_video_imports += timings.native_video_imports;
        metrics_.native_conversion_nanoseconds += timings.native_video_conversion_nanoseconds;
        if (gpu_) metrics_.peak_known_gpu_bytes = std::max(metrics_.peak_known_gpu_bytes, gpu_->resourceUsage().peak_known_bytes);
        if (result.status == composition::OpenGlCompositionStatus::Cancelled || canceled.load()) throw ExportCanceled{};
        if (result.status == composition::OpenGlCompositionStatus::Complete && result.frame &&
            result.frame->width == job_.settings.width && result.frame->height == job_.settings.height &&
            result.frame->stride >= result.frame->width * 4 &&
            result.frame->rgba_pixels.size() >= static_cast<std::size_t>(result.frame->stride) * result.frame->height) {
            ++metrics_.gpu_frames; return std::move(result.frame);
        }
        if (result.status != composition::OpenGlCompositionStatus::Unsupported && result.status != composition::OpenGlCompositionStatus::Failed)
            result = {composition::OpenGlCompositionStatus::Failed, {}, "compose-export", "The GPU adapter returned an incomplete RGBA frame or an unexpected status."};
        fallback(result, output_frame, timeline_frame);
    }
    if (canceled.load()) throw ExportCanceled{};
    if (std::any_of(layers.begin(), layers.end(), [](const auto& layer) {
            return layer.native_frame || layer.texture_frame;
        })) throw CpuCompositionSourcesRequired{};
    std::optional<creative_suite::media::RgbaFrame> result;
    {
        ExportTimedScope cpu_time(metrics_.cpu_composition_nanoseconds);
        result = composition::FrameCompositor::compose(job_.settings.width, job_.settings.height, layers);
    }
    if (canceled.load()) throw ExportCanceled{};
    if (result) { ++metrics_.cpu_frames; if (job_.settings.gpu_composition_enabled) ++metrics_.fallback_frames; }
    return result;
}
} // namespace rendering::detail
