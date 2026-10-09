#include "motion_video_export.h"

#include "rendering/composition_frame_renderer.h"

#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/media_error.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
}

#include <QMetaObject>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <condition_variable>
#include <deque>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace motion::ui {
namespace {

std::string pathForLog(const std::filesystem::path& path)
{
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

std::string gpuBackendUsed(const MotionExportPerformanceSummary& performance)
{
    if (performance.frames_rendered == 0) return "none";
    if (performance.gpu.composition_frames == 0) {
        return performance.gpu_composition_requested ? "cpu_fallback" : "cpu";
    }
    return performance.gpu.fallback_frames == 0 ? "gpu" : "mixed";
}

std::string averageMilliseconds(std::uint64_t total_nanoseconds, std::uint64_t count)
{
    return count == 0 ? "N/A" : std::to_string(
        static_cast<double>(total_nanoseconds) / static_cast<double>(count) / 1'000'000.0);
}

void checkCancelled(const std::atomic_bool& cancel_requested)
{
    if (cancel_requested.load(std::memory_order_acquire)) throw MotionExportCancelled{};
}

void recordElapsed(std::uint64_t& target, std::chrono::steady_clock::duration elapsed) noexcept
{
    const auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    if (nanoseconds <= 0) return;
    const auto value = static_cast<std::uint64_t>(nanoseconds);
    target += value;
}

std::filesystem::path temporaryPathFor(const std::filesystem::path& target)
{
    auto name = target.stem();
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto thread_id = std::hash<std::thread::id>{}(std::this_thread::get_id());
    name += std::filesystem::path(
        ".rendering-motion-" + std::to_string(stamp) + "-" + std::to_string(thread_id));
    name += target.extension();
    return target.parent_path() / name;
}

bool verifyVideoOutput(const std::filesystem::path& path)
{
    const auto path_utf8 = pathForLog(path);
    AVFormatContext* input = nullptr;
    if (avformat_open_input(&input, path_utf8.c_str(), nullptr, nullptr) < 0) {
        if (input != nullptr) avformat_close_input(&input);
        return false;
    }
    const int info = avformat_find_stream_info(input, nullptr);
    const int video = info >= 0
        ? av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0)
        : info;
    avformat_close_input(&input);
    return video >= 0;
}

AVRational toAvRate(model::FrameRate rate)
{
    if (rate.numerator <= 0 || rate.denominator <= 0 ||
        !std::isfinite(rate.asDouble()) || rate.asDouble() <= 0.0 || rate.asDouble() > 1000.0) {
        throw std::runtime_error("The export frame rate is invalid or outside the supported range.");
    }
    if (rate.numerator <= std::numeric_limits<int>::max() &&
        rate.denominator <= std::numeric_limits<int>::max()) {
        return AVRational{static_cast<int>(rate.numerator),
                          static_cast<int>(rate.denominator)};
    }
    const auto rational = av_d2q(rate.asDouble(), 1001000);
    if (rational.num <= 0 || rational.den <= 0) {
        throw std::runtime_error("FFmpeg could not represent the export frame rate.");
    }
    return rational;
}

std::int64_t checkedOutputFrameCount(
    std::int64_t composition_frames,
    AVRational composition_rate,
    AVRational output_rate)
{
    const auto frames = av_rescale_q_rnd(
        composition_frames,
        AVRational{composition_rate.den, composition_rate.num},
        AVRational{output_rate.den, output_rate.num},
        static_cast<AVRounding>(AV_ROUND_UP | AV_ROUND_PASS_MINMAX));
    if (frames <= 0 || frames > 100'000'000) {
        throw std::runtime_error(
            "The calculated export exceeds the supported 100,000,000-frame limit.");
    }
    return frames;
}

std::int64_t compositionFrameForOutput(
    std::int64_t output_frame,
    AVRational composition_rate,
    AVRational output_rate)
{
    const auto frame = av_rescale_q_rnd(
        output_frame,
        AVRational{output_rate.den, output_rate.num},
        AVRational{composition_rate.den, composition_rate.num},
        static_cast<AVRounding>(AV_ROUND_DOWN | AV_ROUND_PASS_MINMAX));
    if (frame < 0) throw std::runtime_error("An output frame mapped to an invalid composition frame.");
    return frame;
}

class EncoderConsumer final {
public:
    explicit EncoderConsumer(creative_suite::media::VideoEncodingSettings settings,
                             std::uint64_t fail_write_number_for_testing)
        : thread_([this, settings = std::move(settings), fail_write_number_for_testing]() mutable {
            run(std::move(settings), fail_write_number_for_testing);
        })
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return ready_; });
        if (failure_) {
            const auto failure = failure_;
            lock.unlock();
            if (thread_.joinable()) thread_.join();
            std::rethrow_exception(failure);
        }
        rethrowFailure(lock);
    }

    ~EncoderConsumer() { abort(); }
    EncoderConsumer(const EncoderConsumer&) = delete;
    EncoderConsumer& operator=(const EncoderConsumer&) = delete;

    void enqueue(std::int64_t frame_index, creative_suite::media::RgbaFrame frame)
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return (!pending_ && !active_) || failure_; });
        rethrowFailure(lock);
        pending_.emplace(Packet{frame_index, std::move(frame)});
        condition_.notify_all();
    }

    void waitIdle()
    {
        std::unique_lock lock(mutex_);
        condition_.wait(lock, [this] { return (!pending_ && !active_) || failure_; });
        rethrowFailure(lock);
    }

    void finish()
    {
        waitIdle();
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            finish_encoder_ = true;
        }
        condition_.notify_all();
        join();
        std::unique_lock lock(mutex_);
        rethrowFailure(lock);
    }

    void abort() noexcept
    {
        {
            std::lock_guard lock(mutex_);
            if (joined_) return;
            stopping_ = true;
            finish_encoder_ = false;
            pending_.reset();
        }
        condition_.notify_all();
        join();
    }

    [[nodiscard]] std::uint64_t lastWriteNanoseconds() const noexcept
    {
        std::lock_guard lock(mutex_);
        return last_write_nanoseconds_;
    }

    [[nodiscard]] std::uint64_t outstandingFrames() const noexcept
    {
        std::lock_guard lock(mutex_);
        return (active_ || pending_) ? 1 : 0;
    }

private:
    struct Packet {
        std::int64_t index;
        creative_suite::media::RgbaFrame frame;
    };

    void rethrowFailure(std::unique_lock<std::mutex>& lock)
    {
        const auto failure = failure_;
        lock.unlock();
        if (failure) std::rethrow_exception(failure);
        lock.lock();
    }

    void join() noexcept
    {
        if (thread_.joinable()) thread_.join();
        std::lock_guard lock(mutex_);
        joined_ = true;
    }

    void run(creative_suite::media::VideoEncodingSettings settings,
             std::uint64_t fail_write_number_for_testing) noexcept
    {
        try {
            creative_suite::media::VideoEncoder encoder(std::move(settings));
            std::uint64_t write_number = 0;
            {
                std::lock_guard lock(mutex_);
                ready_ = true;
            }
            condition_.notify_all();
            for (;;) {
                std::optional<Packet> packet;
                bool stop = false;
                bool finish_encoder = false;
                {
                    std::unique_lock lock(mutex_);
                    condition_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
                    stop = stopping_ && !pending_.has_value();
                    finish_encoder = finish_encoder_;
                    if (!stop) {
                        packet = std::move(pending_);
                        pending_.reset();
                        active_ = true;
                    }
                }
                condition_.notify_all();
                if (stop) {
                    if (finish_encoder) encoder.finish();
                    return;
                }
                ++write_number;
                if (fail_write_number_for_testing != 0 &&
                    write_number == fail_write_number_for_testing)
                    throw std::runtime_error(
                        "Injected FFmpeg encoder write failure for regression coverage.");
                const auto started = std::chrono::steady_clock::now();
                encoder.writeVideo(packet->frame, packet->index);
                const auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started).count();
                {
                    std::lock_guard lock(mutex_);
                    last_write_nanoseconds_ = elapsed_ns > 0
                        ? static_cast<std::uint64_t>(elapsed_ns) : 0;
                    active_ = false;
                }
                condition_.notify_all();
            }
        } catch (...) {
            {
                std::lock_guard lock(mutex_);
                failure_ = std::current_exception();
                ready_ = true;
                active_ = false;
                pending_.reset();
            }
            condition_.notify_all();
        }
    }

    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::optional<Packet> pending_;
    std::exception_ptr failure_;
    bool ready_ = false;
    bool active_ = false;
    bool stopping_ = false;
    bool finish_encoder_ = false;
    bool joined_ = false;
    std::uint64_t last_write_nanoseconds_ = 0;
    std::thread thread_;
};

struct PendingOutputFrame {
    std::int64_t output_index = 0;
    PreviewRequest request;
    std::optional<creative_suite::composition::OpenGlReadbackTicket> ticket;
    creative_suite::media::RgbaFramePtr frame;
    std::optional<creative_suite::media::RgbaFrame> owned_frame;
};

class ExportPipelineGuard final {
public:
    ExportPipelineGuard(EncoderConsumer& encoder, CompositionFrameRenderer& renderer) noexcept
        : encoder_(encoder), renderer_(renderer) {}
    ~ExportPipelineGuard()
    {
        if (released_) return;
        encoder_.abort();
        renderer_.shutdown();
    }
    void release() noexcept { released_ = true; }
private:
    EncoderConsumer& encoder_;
    CompositionFrameRenderer& renderer_;
    bool released_ = false;
};

} // namespace

void MotionVideoExporter::exportVideo(
    const MotionExportSnapshot& snapshot,
    const MotionExportSettings& settings,
    const std::atomic_bool& cancel_requested,
    ProgressCallback report_progress,
    MotionExportPerformanceSummary* performance_summary,
    MotionExportRenderOptions render_options)
{
    const auto export_started = std::chrono::steady_clock::now();
    struct ElapsedRecorder {
        MotionExportPerformanceSummary* summary;
        std::chrono::steady_clock::time_point started;
        ~ElapsedRecorder()
        {
            if (summary != nullptr) recordElapsed(
                summary->elapsed_nanoseconds, std::chrono::steady_clock::now() - started);
        }
    } elapsed_recorder{performance_summary, export_started};
    if (performance_summary != nullptr) {
        performance_summary->gpu_composition_requested =
            render_options.gpu_composition_enabled;
        performance_summary->gpu_surface_available = render_options.gpu_surface != nullptr;
    }

    if (snapshot.canvas_size.width <= 0 || snapshot.canvas_size.height <= 0 ||
        snapshot.frame_rate.numerator <= 0 || snapshot.frame_rate.denominator <= 0 ||
        snapshot.layers.empty()) {
        throw std::runtime_error("The composition has no renderable layers or valid settings.");
    }
    if (settings.output_path.empty() || settings.container_name.empty() ||
        settings.video_encoder_name.empty() || settings.width <= 0 || settings.height <= 0 ||
        settings.width > 16384 || settings.height > 16384 ||
        !std::isfinite(settings.video_bitrate_mbps) ||
        settings.video_bitrate_mbps < 0.1 || settings.video_bitrate_mbps > 500.0) {
        throw std::runtime_error("The video export settings are invalid.");
    }
    const auto target = std::filesystem::absolute(settings.output_path);
    if (!std::filesystem::is_directory(target.parent_path())) {
        throw std::runtime_error("The output folder does not exist: " +
                                 pathForLog(target.parent_path()));
    }

    std::int64_t duration_frames = 0;
    for (const auto& layer : snapshot.layers) {
        if (layer.timeline_start_frame < 0 || layer.duration_frames <= 0 ||
            layer.timeline_start_frame >
                std::numeric_limits<std::int64_t>::max() - layer.duration_frames) {
            throw std::runtime_error("A layer has invalid timing for video export.");
        }
        duration_frames = std::max(
            duration_frames, layer.timeline_start_frame + layer.duration_frames);
    }
    if (duration_frames <= 0) {
        throw std::runtime_error("The composition has no positive layer duration to export.");
    }

    const auto composition_rate = toAvRate(snapshot.frame_rate);
    const auto output_rate = toAvRate(settings.frame_rate);
    const auto output_frame_count = checkedOutputFrameCount(
        duration_frames, composition_rate, output_rate);
    const auto temporary = temporaryPathFor(target);
    std::error_code remove_error;
    std::filesystem::remove(temporary, remove_error);
    if (remove_error) throw std::runtime_error("A stale export temporary file could not be removed.");
    bool published = false;

    try {
        creative_suite::media::VideoEncodingSettings encoding;
        encoding.output_path = temporary;
        encoding.container_name = settings.container_name;
        encoding.video_encoder_name = settings.video_encoder_name;
        encoding.width = settings.width;
        encoding.height = settings.height;
        encoding.frame_rate_numerator = output_rate.num;
        encoding.frame_rate_denominator = output_rate.den;
        encoding.video_bitrate_mbps = settings.video_bitrate_mbps;
        EncoderConsumer encoder(std::move(encoding),
            render_options.fail_encoder_write_number_for_testing);
        CompositionFrameRenderer frame_renderer(
            false,
            render_options.gpu_composition_enabled,
            render_options.gpu_surface,
            performance_summary != nullptr ? &performance_summary->gpu : nullptr,
            render_options.async_readback_staging_budget_bytes_for_testing);
        ExportPipelineGuard pipeline_guard(encoder, frame_renderer);
        int last_reported_progress = -1;
        std::int64_t next_output_frame = 0;
        std::int64_t frames_enqueued = 0;
        std::int64_t frames_encoded = 0;
        unsigned async_slots = 1;
        std::uint64_t async_collection_attempts = 0;
        bool force_cpu_after_async_failure = false;
        std::deque<PendingOutputFrame> pending;
        const auto output_frame_bytes = static_cast<std::uint64_t>(snapshot.canvas_size.width) *
            static_cast<std::uint64_t>(snapshot.canvas_size.height) * 4;
        const auto make_request = [&](std::int64_t output_frame) {
            const auto composition_frame = std::min(
                duration_frames - 1,
                compositionFrameForOutput(output_frame, composition_rate, output_rate));
            PreviewRequest request;
            request.canvas_size = snapshot.canvas_size;
            request.frame_rate = snapshot.frame_rate;
            request.black_canvas_when_empty = true;
            for (const auto& layer : snapshot.layers) {
                if (!layer.visible || composition_frame < layer.timeline_start_frame ||
                    composition_frame - layer.timeline_start_frame >= layer.duration_frames) {
                    continue;
                }
                PreviewLayerSnapshot active;
                active.id = layer.id;
                active.kind = layer.kind;
                active.source_path = layer.source_path;
                active.local_frame = composition_frame - layer.timeline_start_frame;
                active.source_frame_count = layer.source_frame_count;
                active.source_start_frame = layer.source_start_frame;
                active.source_frame_rate = layer.source_frame_rate;
                active.transform = layer.transform;
                active.keyframes = layer.keyframes;
                active.content = layer.content;
                active.effects = layer.effects;
                if (layer.kind == model::LayerKind::Image) {
                    const auto still = snapshot.still_frames.find(layer.source_path);
                    if (still != snapshot.still_frames.end()) active.still_frame = still->second;
                }
                request.layers.push_back(std::move(active));
            }
            return request;
        };
        const auto record_render = [&](std::chrono::steady_clock::time_point started) {
            if (performance_summary == nullptr) return;
            const auto duration = std::chrono::steady_clock::now() - started;
            recordElapsed(performance_summary->render_total_nanoseconds, duration);
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
            if (ns > 0) performance_summary->render_maximum_nanoseconds = std::max(
                performance_summary->render_maximum_nanoseconds,
                static_cast<std::uint64_t>(ns));
            ++performance_summary->render_count;
        };
        const auto update_pending_peaks = [&](bool encoder_frame_pending = false) {
            std::uint64_t pbo_frames = 0;
            std::uint64_t cpu_pending_frames = 0;
            for (const auto& item : pending) {
                if (item.ticket) ++pbo_frames;
                else if (item.frame || item.owned_frame) ++cpu_pending_frames;
            }
            const auto encoding_frames = encoder_frame_pending || encoder.outstandingFrames() != 0
                ? 1ULL : 0ULL;
            const auto pbo_bytes = performance_summary != nullptr
                ? performance_summary->gpu.readback_slots * output_frame_bytes : 0;
            const auto staging_frames = pbo_frames + cpu_pending_frames + encoding_frames;
            const auto staging_bytes = pbo_bytes +
                (cpu_pending_frames + encoding_frames) * output_frame_bytes;
            if (performance_summary != nullptr) {
                performance_summary->readback_peak_pending_frames = std::max(
                    performance_summary->readback_peak_pending_frames, staging_frames);
                performance_summary->readback_peak_pending_bytes = std::max(
                    performance_summary->readback_peak_pending_bytes, staging_bytes);
                performance_summary->encoder_queue_peak_frames = std::max(
                    performance_summary->encoder_queue_peak_frames, encoding_frames);
                if (encoding_frames != 0) performance_summary->encoder_queue_peak_bytes =
                    std::max(performance_summary->encoder_queue_peak_bytes, output_frame_bytes);
            }
        };
        const auto account_encoded = [&] {
            while (frames_encoded < frames_enqueued) {
                ++frames_encoded;
                if (performance_summary != nullptr) {
                    ++performance_summary->write_count;
                    ++performance_summary->frames_rendered;
                    const auto write_ns = encoder.lastWriteNanoseconds();
                    performance_summary->write_total_nanoseconds += write_ns;
                    performance_summary->write_maximum_nanoseconds = std::max(
                        performance_summary->write_maximum_nanoseconds, write_ns);
                }
                if (report_progress) {
                    const auto progress = static_cast<int>(frames_encoded * 99 / output_frame_count);
                    if (progress != last_reported_progress) {
                        report_progress(progress);
                        last_reported_progress = progress;
                    }
                }
            }
        };
        const auto wait_for_encoder = [&] {
            const auto started = std::chrono::steady_clock::now();
            encoder.waitIdle();
            if (performance_summary != nullptr) recordElapsed(
                performance_summary->encoder_queue_wait_nanoseconds,
                std::chrono::steady_clock::now() - started);
            account_encoded();
        };
        const auto enqueue_frame = [&](PendingOutputFrame& item,
                                       creative_suite::media::RgbaFrame frame) {
            checkCancelled(cancel_requested);
            encoder.enqueue(item.output_index, std::move(frame));
            ++frames_enqueued;
            update_pending_peaks(true);
        };
        const auto recover_pending_to_cpu = [&](std::deque<PendingOutputFrame> frames,
                                                 const std::string& operation,
                                                 const std::string& cause,
                                                 std::int64_t error_code,
                                                 bool failure_already_counted) {
            frame_renderer.recoverAsyncReadbackFailure();
            force_cpu_after_async_failure = true;
            if (performance_summary != nullptr) {
                if (!failure_already_counted) ++performance_summary->gpu.failures;
                const auto discarded = static_cast<std::uint64_t>(std::count_if(
                    frames.begin(), frames.end(), [](const auto& item) {
                        return item.ticket.has_value();
                    }));
                performance_summary->gpu.composition_frames =
                    performance_summary->gpu.composition_frames > discarded
                        ? performance_summary->gpu.composition_frames - discarded : 0;
                performance_summary->readback_mode = "pbo_async_cpu_fallback";
            }
            creative_suite::diagnostics::Logger::instance().log(
                creative_suite::diagnostics::Level::Error,
                "motion_export", "async_readback_fallback",
                cause.empty() ? "Asynchronous GPU readback failed; rerendering remaining frames on CPU."
                              : cause,
                {{"gpu_operation", operation},
                 {"error_code", std::to_string(error_code)},
                 {"next_output_frame", frames.empty() ? std::to_string(next_output_frame)
                                                        : std::to_string(frames.front().output_index)}});
            for (auto& item : frames) {
                checkCancelled(cancel_requested);
                if (!item.ticket && item.frame && !item.owned_frame) {
                    enqueue_frame(item, *item.frame);
                    wait_for_encoder();
                    continue;
                }
                const auto render_started = std::chrono::steady_clock::now();
                item.frame = frame_renderer.render(item.request,
                    [&cancel_requested] {
                        return cancel_requested.load(std::memory_order_acquire);
                    }, true);
                record_render(render_started);
                if (!item.frame)
                    throw std::runtime_error("CPU recovery could not compose output frame " +
                        std::to_string(item.output_index) + ".");
                enqueue_frame(item, *item.frame);
                wait_for_encoder();
            }
        };

        while (next_output_frame < output_frame_count || !pending.empty()) {
            checkCancelled(cancel_requested);
            while (next_output_frame < output_frame_count && pending.size() < async_slots) {
                const auto output_frame = next_output_frame;
                auto request = make_request(output_frame);
                std::optional<creative_suite::composition::OpenGlReadbackTicket> ticket;
                bool async_failure = false;
                const auto render_started = std::chrono::steady_clock::now();
                auto frame = frame_renderer.render(request,
                    [&cancel_requested] {
                        return cancel_requested.load(std::memory_order_acquire);
                    }, true, PreviewRequestMode::Interactive,
                    force_cpu_after_async_failure ? nullptr : &ticket, &async_failure);
                record_render(render_started);
                checkCancelled(cancel_requested);
                if (!ticket && !frame)
                    throw std::runtime_error("Motion Studio could not compose output frame " +
                                             std::to_string(output_frame) + ".");
                pending.push_back({output_frame, std::move(request), ticket, std::move(frame)});
                ++next_output_frame;
                if (ticket && performance_summary != nullptr &&
                    performance_summary->readback_mode == "cpu")
                    performance_summary->readback_mode = "pbo_async";
                if (!force_cpu_after_async_failure && frame_renderer.asyncReadbackSlotCount() > 0)
                    async_slots = frame_renderer.asyncReadbackSlotCount();
                update_pending_peaks();
                if (async_failure) {
                    auto failed_frames = std::move(pending);
                    pending.clear();
                    wait_for_encoder();
                    recover_pending_to_cpu(std::move(failed_frames),
                        "submit-pbo-readback", "PBO submission failed during the export pipeline.",
                        0, true);
                    async_slots = 1;
                    break;
                }
            }

            if (pending.empty()) continue;
            // Give the dedicated encoder time to finish while new GPU work is
            // submitted. A transfer failure then leaves no queued frame that
            // would have to be replaced out of order.
            wait_for_encoder();
            auto item = std::move(pending.front());
            pending.pop_front();
            if (item.ticket) {
                ++async_collection_attempts;
                creative_suite::composition::OpenGlReadbackResult collected;
                if (render_options.fail_async_readback_collection_number_for_testing != 0 &&
                    async_collection_attempts ==
                        render_options.fail_async_readback_collection_number_for_testing) {
                    collected.status =
                        creative_suite::composition::OpenGlCompositionStatus::Failed;
                    collected.ticket = *item.ticket;
                    collected.operation = "injected-collect-pbo-ticket";
                    collected.cause = "Injected asynchronous readback failure for regression coverage.";
                    collected.error_code = -1;
                } else {
                    collected = frame_renderer.collectAsyncReadback(
                        *item.ticket, [&cancel_requested] {
                            return cancel_requested.load(std::memory_order_acquire);
                        });
                }
                if (collected.status ==
                    creative_suite::composition::OpenGlCompositionStatus::Cancelled ||
                    cancel_requested.load(std::memory_order_acquire))
                    throw MotionExportCancelled{};
                if (collected.status !=
                    creative_suite::composition::OpenGlCompositionStatus::Complete ||
                    !collected.frame) {
                    pending.push_front(std::move(item));
                    recover_pending_to_cpu(std::move(pending), collected.operation,
                        collected.cause, collected.error_code, false);
                    pending.clear();
                    async_slots = 1;
                    continue;
                }
                item.owned_frame.emplace(std::move(*collected.frame));
            }
            if (!item.frame && !item.owned_frame)
                throw std::runtime_error("Motion Studio produced an empty frame for encoding.");
            if (item.owned_frame) {
                enqueue_frame(item, std::move(*item.owned_frame));
                item.owned_frame.reset();
            } else {
                enqueue_frame(item, *item.frame);
            }
        }

        checkCancelled(cancel_requested);
        wait_for_encoder();
        frame_renderer.shutdown();
        encoder.finish();
        pipeline_guard.release();
        account_encoded();
        if (performance_summary != nullptr &&
            performance_summary->readback_mode == "cpu" &&
            performance_summary->gpu_composition_requested)
            performance_summary->readback_mode =
                performance_summary->gpu.composition_frames != 0 ? "gpu_sync" : "cpu_fallback";
        if (!verifyVideoOutput(temporary)) {
            throw std::runtime_error("FFmpeg could not verify the completed video output.");
        }
        checkCancelled(cancel_requested);
        if (!creative_suite::media::publishEncodedFileAtomically(temporary, target)) {
            throw std::runtime_error("The completed video could not replace the destination file.");
        }
        published = true;
        if (report_progress) report_progress(100);
    } catch (...) {
        if (!published) {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
        }
        throw;
    }
}

MotionVideoExportWorker::MotionVideoExportWorker(
    QObject* receiver,
    MotionExportSnapshot snapshot,
    MotionExportSettings settings,
    ProgressHandler progress_handler,
    FinishedHandler finished_handler,
    MotionExportRenderOptions render_options)
    : receiver_(receiver),
      snapshot_(std::move(snapshot)),
      settings_(std::move(settings)),
      render_options_(render_options),
      cancel_requested_(std::make_shared<std::atomic_bool>(false)),
      progress_handler_(std::move(progress_handler)),
      finished_handler_(std::move(finished_handler))
{
    setObjectName(QStringLiteral("motion-video-export-worker"));
}

MotionVideoExportWorker::~MotionVideoExportWorker()
{
    cancelAndWait();
}

void MotionVideoExportWorker::cancel() noexcept
{
    cancel_requested_->store(true, std::memory_order_release);
}

void MotionVideoExportWorker::cancelAndWait()
{
    cancel();
    if (isRunning()) wait();
}

void MotionVideoExportWorker::run()
{
    MotionExportResult result;
    result.output_path = settings_.output_path;
    MotionExportPerformanceSummary performance;
    const auto worker_started = std::chrono::steady_clock::now();
    try {
        MotionVideoExporter::exportVideo(snapshot_, settings_, *cancel_requested_,
            [this](int progress) {
                if (receiver_.isNull()) return;
                const auto receiver = receiver_;
                const auto handler = progress_handler_;
                QMetaObject::invokeMethod(receiver.data(), [receiver, handler, progress] {
                    if (!receiver.isNull() && handler) handler(progress);
                }, Qt::QueuedConnection);
            }, &performance, render_options_);
        result.succeeded = true;
    } catch (const MotionExportCancelled&) {
        result.cancelled = true;
    } catch (const creative_suite::media::MediaError& error) {
        result.error_message = error.what();
        if (error.error_code().has_value()) result.error_code = *error.error_code();
    } catch (const creative_suite::media::VideoEncodingError& error) {
        result.error_message = error.what();
        result.error_code = error.errorCode();
    } catch (const std::exception& error) {
        result.error_message = error.what();
    } catch (...) {
        result.error_message = "Unknown video export failure.";
    }

    if (performance.elapsed_nanoseconds == 0) {
        recordElapsed(performance.elapsed_nanoseconds,
                      std::chrono::steady_clock::now() - worker_started);
    }

    const auto elapsed_seconds = static_cast<double>(performance.elapsed_nanoseconds) /
        1'000'000'000.0;
    const auto achieved_fps = elapsed_seconds > 0.0
        ? static_cast<double>(performance.frames_rendered) / elapsed_seconds : 0.0;
    const auto output_fps = settings_.frame_rate.asDouble();
    creative_suite::diagnostics::Logger::instance().log(
        creative_suite::diagnostics::Level::Info,
        "motion_performance", "export_summary",
        "Motion Studio video export performance summary.",
        {{"schema_version", "3"},
         {"outcome", result.succeeded ? "completed" :
             (result.cancelled ? "cancelled" : "failed")},
         {"gpu_composition_requested", performance.gpu_composition_requested
             ? "true" : "false"},
         {"gpu_surface_available", performance.gpu_surface_available
             ? "true" : "false"},
         {"gpu_backend_used", gpuBackendUsed(performance)},
         {"gpu_readback_mode", performance.readback_mode},
         {"gpu_composition_attempts",
             std::to_string(performance.gpu.composition_attempts)},
         {"gpu_composition_frames",
             std::to_string(performance.gpu.composition_frames)},
         {"gpu_composition_fallback_frames",
             std::to_string(performance.gpu.fallback_frames)},
         {"gpu_composition_failures", std::to_string(performance.gpu.failures)},
         {"gpu_composition_uploaded_bytes",
             std::to_string(performance.gpu.uploaded_bytes)},
         {"gpu_composition_readback_bytes",
             std::to_string(performance.gpu.readback_bytes)},
         {"gpu_composition_uploaded_layers",
             std::to_string(performance.gpu.uploaded_layers)},
         {"gpu_color_adjustment_count",
             std::to_string(performance.gpu.color_adjustment_count)},
         {"gpu_gaussian_blur_count",
             std::to_string(performance.gpu.gaussian_blur_count)},
         {"gpu_upload_average_ms", averageMilliseconds(
             performance.gpu.upload_nanoseconds,
             performance.gpu.composition_attempts)},
         {"gpu_draw_submission_average_ms", averageMilliseconds(
             performance.gpu.draw_submission_nanoseconds,
             performance.gpu.composition_attempts)},
         {"gpu_readback_average_ms", averageMilliseconds(
             performance.gpu.readback_nanoseconds,
             performance.gpu.readback_frames_collected != 0
                 ? performance.gpu.readback_frames_collected
                 : performance.gpu.composition_attempts)},
         {"gpu_async_readback_slots",
             std::to_string(performance.gpu.readback_slots)},
         {"gpu_readback_frames_submitted",
             std::to_string(performance.gpu.readback_frames_submitted)},
         {"gpu_readback_frames_collected",
             std::to_string(performance.gpu.readback_frames_collected)},
         {"gpu_readback_fence_waits",
             std::to_string(performance.gpu.readback_fence_waits)},
         {"gpu_readback_submit_average_ms", averageMilliseconds(
             performance.gpu.readback_submit_nanoseconds,
             performance.gpu.readback_frames_submitted)},
         {"gpu_readback_fence_wait_average_ms", averageMilliseconds(
             performance.gpu.readback_wait_nanoseconds,
             performance.gpu.readback_frames_collected)},
         {"gpu_readback_copy_average_ms", averageMilliseconds(
             performance.gpu.readback_copy_nanoseconds,
             performance.gpu.readback_frames_collected)},
         {"readback_peak_pending_frames",
             std::to_string(performance.readback_peak_pending_frames)},
         {"readback_peak_pending_bytes",
             std::to_string(performance.readback_peak_pending_bytes)},
         {"encoder_queue_wait_ms", std::to_string(
             static_cast<double>(performance.encoder_queue_wait_nanoseconds) / 1'000'000.0)},
         {"encoder_queue_peak_frames",
             std::to_string(performance.encoder_queue_peak_frames)},
         {"encoder_queue_peak_bytes",
             std::to_string(performance.encoder_queue_peak_bytes)},
         {"elapsed_ms", std::to_string(elapsed_seconds * 1000.0)},
         {"frames_rendered", std::to_string(performance.frames_rendered)},
         {"render_count", std::to_string(performance.render_count)},
         {"render_average_ms", performance.render_count == 0 ? "N/A" :
             std::to_string(static_cast<double>(performance.render_total_nanoseconds) /
                 static_cast<double>(performance.render_count) / 1'000'000.0)},
         {"render_maximum_ms", std::to_string(
             static_cast<double>(performance.render_maximum_nanoseconds) / 1'000'000.0)},
         {"write_count", std::to_string(performance.write_count)},
         {"write_average_ms", performance.write_count == 0 ? "N/A" :
             std::to_string(static_cast<double>(performance.write_total_nanoseconds) /
                 static_cast<double>(performance.write_count) / 1'000'000.0)},
         {"write_maximum_ms", std::to_string(
             static_cast<double>(performance.write_maximum_nanoseconds) / 1'000'000.0)},
         {"output_width", std::to_string(settings_.width)},
         {"output_height", std::to_string(settings_.height)},
         {"output_frame_rate_numerator", std::to_string(settings_.frame_rate.numerator)},
         {"output_frame_rate_denominator", std::to_string(settings_.frame_rate.denominator)},
         {"achieved_frames_per_second", std::to_string(achieved_fps)},
         {"realtime_factor", output_fps > 0.0
             ? std::to_string(achieved_fps / output_fps) : "N/A"}});

    if (!result.succeeded && !result.cancelled) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_export", "render_video", result.error_message,
            {{"output_path", pathForLog(result.output_path)},
             {"error_code", result.error_code != -1
                  ? std::to_string(result.error_code) : std::string{}}});
    }
    if (receiver_.isNull()) return;
    const auto receiver = receiver_;
    const auto handler = finished_handler_;
    QMetaObject::invokeMethod(receiver.data(), [receiver, handler, result = std::move(result)]() mutable {
        if (!receiver.isNull() && handler) handler(std::move(result));
    }, Qt::QueuedConnection);
}

} // namespace motion::ui
