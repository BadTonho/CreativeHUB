#include <creative_suite/media/video_playback.h>
#include <creative_suite/diagnostics/logger.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#ifdef _WIN32
#include <libavutil/hwcontext_d3d11va.h>
#endif
#include <libswscale/swscale.h>
}

#include <cstddef>
#include <atomic>
#include <cstring>
#include <cmath>
#include <chrono>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <utility>

namespace creative_suite::media {
namespace {

struct FormatContextDeleter {
    void operator()(AVFormatContext* context) const noexcept {
        if (context != nullptr) avformat_close_input(&context);
    }
};

struct CodecContextDeleter {
    void operator()(AVCodecContext* context) const noexcept {
        if (context != nullptr) avcodec_free_context(&context);
    }
};

struct PacketDeleter {
    void operator()(AVPacket* packet) const noexcept {
        if (packet != nullptr) av_packet_free(&packet);
    }
};

struct FrameDeleter {
    void operator()(AVFrame* frame) const noexcept {
        if (frame != nullptr) av_frame_free(&frame);
    }
};

struct SwsContextDeleter {
    void operator()(SwsContext* context) const noexcept {
        if (context != nullptr) sws_freeContext(context);
    }
};

using FormatContextPtr = std::unique_ptr<AVFormatContext, FormatContextDeleter>;
using CodecContextPtr = std::unique_ptr<AVCodecContext, CodecContextDeleter>;
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using SwsContextPtr = std::unique_ptr<SwsContext, SwsContextDeleter>;
struct NativeTransferCounters {
    std::atomic<std::uint64_t> frames{0}, bytes{0};
};

class OptionalDurationAccumulator final {
public:
    explicit OptionalDurationAccumulator(std::uint64_t* destination) noexcept
        : destination_(destination),
          started_(destination != nullptr ? Clock::now() : Clock::time_point{}) {}

    ~OptionalDurationAccumulator() {
        if (destination_ == nullptr) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - started_).count();
        if (elapsed > 0) *destination_ += static_cast<std::uint64_t>(elapsed);
    }

    OptionalDurationAccumulator(const OptionalDurationAccumulator&) = delete;
    OptionalDurationAccumulator& operator=(const OptionalDurationAccumulator&) = delete;

private:
    using Clock = std::chrono::steady_clock;
    std::uint64_t* destination_ = nullptr;
    Clock::time_point started_{};
};

class ObserverTimingScope final {
    using Clock = std::chrono::steady_clock;

public:
    ObserverTimingScope(
        DecodeObserver* observer,
        DecodeTimingStage stage) noexcept
        : observer_(observer), stage_(stage) {
        if (observer_ == nullptr || !observer_->is_enabled()) return;
        enabled_ = true;
        started_ = Clock::now();
    }

    ~ObserverTimingScope() {
        if (!enabled_) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - started_).count();
        observer_->record_timing(
            stage_,
            elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0);
    }

    ObserverTimingScope(const ObserverTimingScope&) = delete;
    ObserverTimingScope& operator=(const ObserverTimingScope&) = delete;

private:
    DecodeObserver* observer_ = nullptr;
    DecodeTimingStage stage_ = DecodeTimingStage::PacketIo;
    Clock::time_point started_{};
    bool enabled_ = false;
};

std::string toUtf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return std::string(reinterpret_cast<const char*>(value.data()), value.size());
}

std::string safePathForLog(const std::filesystem::path& path) noexcept {
    try {
        return toUtf8(path);
    } catch (...) {
        return "<unavailable>";
    }
}

std::string ffmpegError(int result) {
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    if (av_strerror(result, message, sizeof(message)) == 0) return message;
    return "Unknown FFmpeg error (" + std::to_string(result) + ")";
}

[[noreturn]] void throwFfmpegError(int result, const std::string& operation) {
    throw MediaError(operation + ": " + ffmpegError(result), result);
}

void validateInputFile(const std::filesystem::path& source_path) {
    if (source_path.empty()) throw MediaError("Media path is empty.");

    std::error_code file_error;
    if (!std::filesystem::is_regular_file(source_path, file_error) || file_error) {
        const auto code = file_error ? std::optional<int>(file_error.value()) : std::nullopt;
        throw MediaError("Input is not a readable regular file: " + toUtf8(source_path), code);
    }
}

VideoFramePtr copyRgbaFrame(
    const AVFrame& frame,
    SwsContextPtr& scaler,
    DecodeObserver* observer) {
    if (frame.width <= 0 || frame.height <= 0) {
        throw MediaError("Decoded video frame has invalid dimensions.");
    }

    const auto width = static_cast<std::size_t>(frame.width);
    const auto height = static_cast<std::size_t>(frame.height);
    constexpr std::size_t bytes_per_pixel = 4;
    if (width > std::numeric_limits<std::size_t>::max() / bytes_per_pixel ||
        width * bytes_per_pixel > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        throw MediaError("Decoded video frame is too large.");
    }

    const std::size_t stride = width * bytes_per_pixel;
    if (height > std::numeric_limits<std::size_t>::max() / stride) {
        throw MediaError("Decoded video frame is too large.");
    }

    auto result = std::make_shared<VideoFrame>();
    result->width = frame.width;
    result->height = frame.height;
    result->stride = static_cast<int>(stride);
    result->rgba_pixels.resize(stride * height);

    ObserverTimingScope conversion_timing(
        observer,
        DecodeTimingStage::PixelConversion);
    auto* cached_scaler = sws_getCachedContext(
        scaler.release(),
        frame.width,
        frame.height,
        static_cast<AVPixelFormat>(frame.format),
        frame.width,
        frame.height,
        AV_PIX_FMT_RGBA,
        SWS_BILINEAR,
        nullptr,
        nullptr,
        nullptr);
    if (cached_scaler == nullptr) {
        throw MediaError("Could not create the video color conversion context.");
    }
    scaler.reset(cached_scaler);

    std::uint8_t* destination_data[4] = {result->rgba_pixels.data(), nullptr, nullptr, nullptr};
    int destination_linesize[4] = {result->stride, 0, 0, 0};
    const int scaled_height = sws_scale(
        scaler.get(),
        frame.data,
        frame.linesize,
        0,
        frame.height,
        destination_data,
        destination_linesize);
    if (scaled_height != frame.height) {
        throw MediaError("Could not convert the decoded video frame to RGBA.");
    }

    return result;
}

bool isValidRational(AVRational value) noexcept {
    return value.num > 0 && value.den > 0;
}

std::optional<std::int64_t> timestampForFrame(
    std::int64_t frame_index,
    AVRational frame_rate,
    AVRational time_base,
    std::int64_t stream_start_time) {
    if (frame_index < 0 || !isValidRational(frame_rate) || !isValidRational(time_base) ||
        stream_start_time == AV_NOPTS_VALUE) {
        return std::nullopt;
    }

    const auto relative_timestamp = av_rescale_q(
        frame_index,
        AVRational{frame_rate.den, frame_rate.num},
        time_base);
    if (relative_timestamp > 0 &&
        stream_start_time > std::numeric_limits<std::int64_t>::max() - relative_timestamp) {
        return std::nullopt;
    }
    if (relative_timestamp < 0 &&
        stream_start_time < std::numeric_limits<std::int64_t>::min() - relative_timestamp) {
        return std::nullopt;
    }
    return stream_start_time + relative_timestamp;
}

std::optional<std::int64_t> frameIndexForTimestamp(
    std::int64_t timestamp,
    AVRational frame_rate,
    AVRational time_base,
    std::int64_t stream_start_time) {
    if (timestamp == AV_NOPTS_VALUE || !isValidRational(frame_rate) ||
        !isValidRational(time_base) || stream_start_time == AV_NOPTS_VALUE) {
        return std::nullopt;
    }

    const long double relative_timestamp =
        static_cast<long double>(timestamp) -
        static_cast<long double>(stream_start_time);
    const long double seconds = relative_timestamp * av_q2d(time_base);
    const long double frame_position = seconds * av_q2d(frame_rate);
    if (!std::isfinite(static_cast<double>(frame_position)) || frame_position < 0.0L ||
        frame_position > static_cast<long double>(std::numeric_limits<std::int64_t>::max())) {
        return std::nullopt;
    }

    return static_cast<std::int64_t>(std::llround(frame_position));
}

void logFailure(const std::filesystem::path& source_path,
                const char* operation,
                const media::MediaError& error) noexcept {
    try {
        diagnostics::Context context{{"path", safePathForLog(source_path)}};
        if (error.error_code().has_value()) {
            context.emplace_back("error_code", std::to_string(*error.error_code()));
        }
        diagnostics::Logger::instance().log(
            diagnostics::Level::Error,
            "media",
            operation,
            error.what(),
            context);
    } catch (...) {
        // Preserve the original media error even if diagnostic context allocation fails.
    }
}

void logHardwareRecovery(const std::filesystem::path& path, const std::string& cause,
                         std::optional<int> code = {}) noexcept {
    try {
        diagnostics::Context context{{"path", safePathForLog(path)}, {"backend", "d3d11va"}};
        if (code) context.emplace_back("error_code", std::to_string(*code));
        diagnostics::Logger::instance().log(diagnostics::Level::Warning, "media",
            "hardware_decode_recovery", cause, context);
    } catch (...) {}
}

AVPixelFormat selectD3D11Format(AVCodecContext*, const AVPixelFormat* formats) {
    for (; *formats != AV_PIX_FMT_NONE; ++formats) {
        if (*formats == AV_PIX_FMT_D3D11) return *formats;
    }
    // Do not silently label FFmpeg's software decoder as a hardware path.
    return AV_PIX_FMT_NONE;
}

bool supportsEightBitHardware(const AVCodecParameters& parameters) {
    if (parameters.codec_id != AV_CODEC_ID_H264 && parameters.codec_id != AV_CODEC_ID_HEVC)
        return false;
    if (parameters.codec_id == AV_CODEC_ID_HEVC && parameters.profile != FF_PROFILE_UNKNOWN &&
        parameters.profile != FF_PROFILE_HEVC_MAIN) return false;
    if (parameters.codec_id == AV_CODEC_ID_H264 && parameters.profile != FF_PROFILE_UNKNOWN &&
        parameters.profile != FF_PROFILE_H264_BASELINE &&
        parameters.profile != FF_PROFILE_H264_CONSTRAINED_BASELINE &&
        parameters.profile != FF_PROFILE_H264_MAIN && parameters.profile != FF_PROFILE_H264_HIGH)
        return false;
    const auto* descriptor = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(parameters.format));
    if (descriptor != nullptr) {
        for (int index = 0; index < descriptor->nb_components; ++index)
            if (descriptor->comp[index].depth != 8) return false;
    }
    return true;
}

void logFailure(const std::filesystem::path& source_path,
                const char* operation,
                const std::exception& error) noexcept {
    try {
        diagnostics::Logger::instance().log(
            diagnostics::Level::Error,
            "media",
            operation,
            error.what(),
            {{"path", safePathForLog(source_path)}});
    } catch (...) {
        // Preserve the original exception even if diagnostic context allocation fails.
    }
}

VideoFramePtr downloadD3D11Frame(const AVFrame& frame, SwsContextPtr& scaler, DecodeObserver* observer) {
    FramePtr transferred(av_frame_alloc());
    if (!transferred) throw MediaError("Could not allocate the hardware transfer frame.");
    const int result = av_hwframe_transfer_data(transferred.get(), &frame, 0);
    if (result < 0) throwFfmpegError(result, "Downloading a D3D11 video frame");
    if (transferred->format == AV_PIX_FMT_NV12) {
        // swscale's NV12 fast converter does not use the same chroma sampling
        // as the established YUV420P reference. Repack losslessly before that
        // conversion so the hardware RGBA delivery retains its pixel contract.
        FramePtr planar(av_frame_alloc());
        if (!planar) throw MediaError("Could not allocate the planar transfer frame.");
        planar->format = frame.color_range == AVCOL_RANGE_JPEG ? AV_PIX_FMT_YUVJ420P : AV_PIX_FMT_YUV420P;
        planar->width = transferred->width; planar->height = transferred->height;
        const int allocation = av_frame_get_buffer(planar.get(), 32);
        if (allocation < 0) throwFfmpegError(allocation, "Allocating planar transfer pixels");
        for (int y = 0; y < planar->height; ++y)
            std::memcpy(planar->data[0] + y * planar->linesize[0],
                transferred->data[0] + y * transferred->linesize[0], planar->width);
        for (int y = 0; y < (planar->height + 1) / 2; ++y) {
            const auto* uv = transferred->data[1] + y * transferred->linesize[1];
            for (int x = 0; x < (planar->width + 1) / 2; ++x) {
                planar->data[1][y * planar->linesize[1] + x] = uv[x * 2];
                planar->data[2][y * planar->linesize[2] + x] = uv[x * 2 + 1];
            }
        }
        return copyRgbaFrame(*planar, scaler, observer);
    }
    return copyRgbaFrame(*transferred, scaler, observer);
}

} // namespace

struct NativeVideoFrame::Impl {
    FramePtr frame;
    std::filesystem::path source_path;
    std::shared_ptr<NativeTransferCounters> transfers;
};
NativeVideoFrame::NativeVideoFrame(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
NativeVideoFrame::~NativeVideoFrame() = default;
void* NativeVideoFrame::retained_frame() const noexcept { return impl_->frame.get(); }
int NativeVideoFrame::width() const noexcept { return impl_->frame->width; }
int NativeVideoFrame::height() const noexcept { return impl_->frame->height; }
D3D11VideoFrameView NativeVideoFrame::d3d11_view() const {
#ifdef _WIN32
    const auto* frames = reinterpret_cast<const AVHWFramesContext*>(impl_->frame->hw_frames_ctx->data);
    const auto* device = static_cast<const AVD3D11VADeviceContext*>(frames->device_ctx->hwctx);
    return {device->device, impl_->frame->data[0],
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(impl_->frame->data[1])),
        width(), height(), frames->sw_format == AV_PIX_FMT_BGRA ? NativeVideoFormat::Bgra8 : NativeVideoFormat::Nv12,
        impl_->frame->color_range == AVCOL_RANGE_JPEG};
#else
    throw MediaError("A D3D11 frame cannot be accessed on this platform.");
#endif
}
void NativeVideoFrame::with_device_lock(const std::function<void()>& operation) const {
#ifdef _WIN32
    const auto* frames = reinterpret_cast<const AVHWFramesContext*>(impl_->frame->hw_frames_ctx->data);
    auto* device = static_cast<AVD3D11VADeviceContext*>(frames->device_ctx->hwctx);
    device->lock(device->lock_ctx);
    struct Unlock { AVD3D11VADeviceContext* device; ~Unlock() { device->unlock(device->lock_ctx); } } unlock{device};
    operation();
#else
    (void)operation;
    throw MediaError("A D3D11 device cannot be used on this platform.");
#endif
}
RgbaFramePtr NativeVideoFrame::download_rgba() const {
    try {
        SwsContextPtr scaler;
        auto result = downloadD3D11Frame(*impl_->frame, scaler, nullptr);
        if (impl_->transfers) {
            ++impl_->transfers->frames;
            impl_->transfers->bytes += static_cast<std::uint64_t>(width()) * height() * 3 / 2;
        }
        return result;
    } catch (const MediaError& error) {
        logFailure(impl_->source_path, "native_frame_download", error); throw;
    } catch (const std::exception& error) {
        logFailure(impl_->source_path, "native_frame_download", error); throw;
    }
}

struct NativeVideoFramePool::Impl {
    AVBufferRef* device = nullptr;
    AVBufferRef* frames = nullptr;
    int width = 0, height = 0;
    unsigned maximum_frames = 0;
    std::vector<NativeVideoFramePtr> retained;
    ~Impl() { av_buffer_unref(&frames); av_buffer_unref(&device); }
};
NativeVideoFramePool::NativeVideoFramePool(int width, int height, unsigned maximum_frames)
    : impl_(std::make_unique<Impl>()) {
    if (width <= 0 || height <= 0 || !maximum_frames || maximum_frames > 4 ||
        std::uint64_t(width) * height * 4 * maximum_frames > 128ULL * 1024 * 1024)
        throw MediaError("Native encoding storage exceeds four frames or 128 MiB.");
#ifdef _WIN32
    auto& p = *impl_; p.width = width; p.height = height; p.maximum_frames = maximum_frames;
    int result = av_hwdevice_ctx_create(&p.device, AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
    if (result < 0) throwFfmpegError(result, "Creating the native encoding device");
    p.frames = av_hwframe_ctx_alloc(p.device);
    if (!p.frames) throw MediaError("Allocating the native encoding pool failed.");
    auto* frames = reinterpret_cast<AVHWFramesContext*>(p.frames->data);
    frames->format = AV_PIX_FMT_D3D11; frames->sw_format = AV_PIX_FMT_BGRA;
    frames->width = width; frames->height = height;
    // Individual textures permit interop registration without array slicing.
    frames->initial_pool_size = 0;
    auto* native = static_cast<AVD3D11VAFramesContext*>(frames->hwctx);
    native->BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    result = av_hwframe_ctx_init(p.frames);
    if (result < 0) throwFfmpegError(result, "Initializing the native encoding pool");
    p.retained.reserve(maximum_frames);
#else
    throw MediaError("Native D3D11 encoding is available only on Windows.");
#endif
}
NativeVideoFramePool::~NativeVideoFramePool() = default;
NativeVideoFramePtr NativeVideoFramePool::acquire() {
    auto& p = *impl_;
    for (const auto& frame : p.retained) {
        const auto* native = static_cast<const AVFrame*>(frame->retained_frame());
        if (frame.use_count() == 1 && native->buf[0] && av_buffer_get_ref_count(native->buf[0]) == 1)
            return frame;
    }
    if (p.retained.size() >= p.maximum_frames) return {};
    auto retained = std::make_unique<NativeVideoFrame::Impl>();
    retained->frame.reset(av_frame_alloc());
    if (!retained->frame) throw MediaError("Allocating a native encoding frame failed.");
    const int result = av_hwframe_get_buffer(p.frames, retained->frame.get(), 0);
    if (result < 0) throwFfmpegError(result, "Acquiring native encoding storage");
    NativeVideoFramePtr frame(new NativeVideoFrame(std::move(retained)));
    p.retained.push_back(frame);
    return frame;
}
std::uint64_t NativeVideoFramePool::reserved_bytes() const noexcept {
    return std::uint64_t(impl_->width) * impl_->height * 4 * impl_->maximum_frames;
}

struct VideoPlaybackSession::Impl {
    struct CachedFrame {
        std::int64_t frame_index = -1;
        VideoFramePtr frame;
        NativeVideoFramePtr native;
        std::uint64_t bytes = 0;
    };

    std::filesystem::path source_path;
    DecodeObserver* observer = nullptr;
    DecodeOptions options;
    DecodeAccelerationDiagnostics acceleration;
    bool hardware_initialized = false;
    bool native_delivery = false;
    NativeVideoFramePtr last_native_frame;
    std::shared_ptr<NativeTransferCounters> native_transfers = std::make_shared<NativeTransferCounters>();
    FormatContextPtr format;
    CodecContextPtr decoder;
    PacketPtr packet;
    FramePtr frame;
    int stream_index = -1;
    AVRational stream_time_base{0, 1};
    AVRational frame_rate{0, 1};
    std::int64_t stream_start_time = AV_NOPTS_VALUE;
    bool flush_sent = false;
    bool end_reached = false;
    bool decoder_position_invalid = false;
    bool cache_decoded_frames = true;
    std::int64_t current_frame_index = -1;
    std::int64_t last_decoded_timestamp = AV_NOPTS_VALUE;
    std::deque<CachedFrame> frame_cache;
    SwsContextPtr scaler;
    std::size_t cached_bytes = 0;
    std::uint64_t cache_hit_count = 0;
    ~Impl() {
        if (!acceleration.hardware_requested) return;
        try {
            diagnostics::Logger::instance().log(diagnostics::Level::Info, "media", "decode_acceleration_summary",
                "Experimental decoding session summary.",
                {{"path", safePathForLog(source_path)}, {"backend", acceleration.backend == DecodeBackend::D3D11 ? "d3d11va" : "cpu"},
                 {"hardware_frames", std::to_string(acceleration.hardware_frames)}, {"software_frames", std::to_string(acceleration.software_frames)},
                 {"downloaded_frames", std::to_string(acceleration.downloaded_frames + native_transfers->frames.load())},
                 {"downloaded_bytes", std::to_string(acceleration.downloaded_bytes + native_transfers->bytes.load())},
                 {"cached_equivalent_bytes", std::to_string(cached_bytes)}, {"recoveries", std::to_string(acceleration.recovery_count)},
                 {"peak_reserved_gpu_bytes", std::to_string(acceleration.peak_reserved_gpu_bytes)},
                 {"fallback_reason", acceleration.fallback_reason}});
        } catch (...) {}
    }
};

constexpr std::size_t max_cached_frames = 8;
constexpr std::size_t max_cached_bytes = 64U * 1024U * 1024U;

void VideoPlaybackSession::cacheFrame(
    VideoPlaybackSession::Impl& impl,
    std::int64_t frame_index,
    const VideoFramePtr& frame, const NativeVideoFramePtr& native) {
    if (frame == nullptr && native == nullptr) return;

    for (auto iterator = impl.frame_cache.begin(); iterator != impl.frame_cache.end(); ++iterator) {
        if (iterator->frame_index != frame_index) continue;
        impl.cached_bytes -= iterator->bytes;
        impl.frame_cache.erase(iterator);
        break;
    }

    const auto bytes = frame ? frame->rgba_pixels.size() :
        static_cast<std::uint64_t>(native->width()) * native->height() * 4;
    impl.cached_bytes += bytes;
    impl.frame_cache.push_back({frame_index, frame, native, bytes});

    while (impl.frame_cache.size() > 1 &&
           (impl.frame_cache.size() > max_cached_frames || impl.cached_bytes > max_cached_bytes)) {
        const auto& oldest = impl.frame_cache.front();
        impl.cached_bytes -= oldest.bytes;
        impl.frame_cache.pop_front();
    }
}

VideoFramePtr VideoPlaybackSession::takeCachedFrame(
    VideoPlaybackSession::Impl& impl,
    std::int64_t frame_index) {
    for (auto iterator = impl.frame_cache.begin(); iterator != impl.frame_cache.end(); ++iterator) {
        if (iterator->frame_index != frame_index) continue;
        if (!iterator->frame) continue;
        ++impl.cache_hit_count;
        auto frame = iterator->frame;
        auto entry = std::move(*iterator);
        impl.frame_cache.erase(iterator);
        impl.frame_cache.push_back(std::move(entry));
        return frame;
    }
    return nullptr;
}

void VideoPlaybackSession::resetDecoderPosition(VideoPlaybackSession::Impl& impl) {
    avcodec_flush_buffers(impl.decoder.get());
    av_packet_unref(impl.packet.get());
    av_frame_unref(impl.frame.get());
    impl.flush_sent = false;
    impl.end_reached = false;
    impl.decoder_position_invalid = false;
    impl.cache_decoded_frames = true;
    impl.current_frame_index = -1;
    impl.last_decoded_timestamp = AV_NOPTS_VALUE;
}

bool VideoPlaybackSession::seekToTimestamp(
    VideoPlaybackSession::Impl& impl,
    std::int64_t frame_index) {
    const auto timestamp = timestampForFrame(
        frame_index,
        impl.frame_rate,
        impl.stream_time_base,
        impl.stream_start_time);
    if (!timestamp.has_value()) return false;

    const bool observe_seek = impl.observer != nullptr && impl.observer->is_enabled();
    const auto seek_started = observe_seek
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{};
    const int seek_result = avformat_seek_file(
        impl.format.get(),
        impl.stream_index,
        std::numeric_limits<std::int64_t>::min(),
        *timestamp,
        *timestamp,
        AVSEEK_FLAG_BACKWARD);
    if (observe_seek) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - seek_started).count();
        if (elapsed >= 0) {
            impl.observer->record_timestamp_seek(
                seek_result >= 0 ? DecodeSeekResult::Succeeded : DecodeSeekResult::Failed,
                static_cast<std::uint64_t>(elapsed));
        }
    }
    if (seek_result < 0) return false;

    resetDecoderPosition(impl);
    return true;
}

VideoPlaybackSession::VideoPlaybackSession(std::unique_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

VideoPlaybackSession::~VideoPlaybackSession() = default;

VideoPlaybackSession::VideoPlaybackSession(VideoPlaybackSession&&) noexcept = default;

VideoPlaybackSession& VideoPlaybackSession::operator=(VideoPlaybackSession&&) noexcept = default;

std::unique_ptr<VideoPlaybackSession> VideoPlaybackSession::open(
    const std::filesystem::path& source_path,
    DecodeObserver* observer) {
    return open(source_path, DecodeOptions{}, observer);
}

std::unique_ptr<VideoPlaybackSession> VideoPlaybackSession::open(
    const std::filesystem::path& source_path, DecodeOptions options,
    DecodeObserver* observer) {
    try {
        return std::unique_ptr<VideoPlaybackSession>(
            new VideoPlaybackSession(openImpl(source_path, observer, options)));
    } catch (const MediaError& error) {
        logFailure(source_path, "playback_open", error);
        throw;
    } catch (const std::exception& error) {
        logFailure(source_path, "playback_open", error);
        throw;
    }
}

std::unique_ptr<VideoPlaybackSession::Impl> VideoPlaybackSession::openImpl(
    const std::filesystem::path& source_path,
    DecodeObserver* observer, DecodeOptions options) {
    validateInputFile(source_path);

    auto impl = std::make_unique<Impl>();
    impl->source_path = source_path;
    impl->observer = observer;
    impl->options = options;
    impl->acceleration.hardware_requested = options.acceleration == DecodeAcceleration::PreferHardware;

    AVFormatContext* raw_format = nullptr;
    const std::string input_path = toUtf8(source_path);
    const int open_result = avformat_open_input(&raw_format, input_path.c_str(), nullptr, nullptr);
    if (open_result < 0) {
        if (raw_format != nullptr) avformat_close_input(&raw_format);
        throwFfmpegError(open_result, "Opening media for playback");
    }
    impl->format.reset(raw_format);

    const int stream_info_result = avformat_find_stream_info(impl->format.get(), nullptr);
    if (stream_info_result < 0) {
        throwFfmpegError(stream_info_result, "Reading media stream information for playback");
    }

    const AVCodec* codec = nullptr;
    impl->stream_index = av_find_best_stream(
        impl->format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (impl->stream_index < 0 || codec == nullptr) {
        throw MediaError("No supported video stream was found for playback.", impl->stream_index);
    }

    AVStream* stream = impl->format->streams[impl->stream_index];
    if (stream == nullptr || stream->codecpar == nullptr) {
        throw MediaError("The video stream has no codec parameters for playback.");
    }

    impl->stream_time_base = stream->time_base;
    impl->stream_start_time = stream->start_time;
    impl->frame_rate = av_guess_frame_rate(impl->format.get(), stream, nullptr);

    impl->decoder.reset(avcodec_alloc_context3(codec));
    if (!impl->decoder) throw MediaError("Could not allocate the video decoder context.");

    const int parameters_result = avcodec_parameters_to_context(
        impl->decoder.get(), stream->codecpar);
    if (parameters_result < 0) {
        throwFfmpegError(parameters_result, "Reading video codec parameters for playback");
    }

    if (impl->acceleration.hardware_requested) {
#ifdef _WIN32
        bool has_configuration = false;
        for (int index = 0; const auto* config = avcodec_get_hw_config(codec, index); ++index) {
            if (config->device_type == AV_HWDEVICE_TYPE_D3D11VA &&
                config->pix_fmt == AV_PIX_FMT_D3D11 &&
                (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
                has_configuration = true;
                break;
            }
        }
        if (!has_configuration || !supportsEightBitHardware(*stream->codecpar)) {
            impl->acceleration.fallback_reason = "The stream is outside the H.264/HEVC 8-bit D3D11 decode contract.";
        } else {
            const int device_result = av_hwdevice_ctx_create(&impl->decoder->hw_device_ctx,
                AV_HWDEVICE_TYPE_D3D11VA, nullptr, nullptr, 0);
            if (device_result < 0) {
                impl->acceleration.fallback_reason = "Creating D3D11 decode device: " + ffmpegError(device_result);
                logHardwareRecovery(source_path, impl->acceleration.fallback_reason, device_result);
            } else {
                impl->hardware_initialized = true;
                impl->decoder->get_format = selectD3D11Format;
                impl->decoder->thread_count = 1;
                impl->decoder->extra_hw_frames = static_cast<int>(max_cached_frames);
                impl->acceleration.backend = DecodeBackend::D3D11;
            }
        }
#else
        impl->acceleration.fallback_reason = "D3D11 decoding is available only on Windows; this platform uses software decoding.";
#endif
    }
    int decoder_result = avcodec_open2(impl->decoder.get(), codec, nullptr);
    if (decoder_result < 0 && impl->hardware_initialized) {
        const auto reason = "Opening D3D11 decoder: " + ffmpegError(decoder_result);
        logHardwareRecovery(source_path, reason, decoder_result);
        auto fallback = openImpl(source_path, observer, DecodeOptions{});
        fallback->options = options;
        fallback->acceleration.hardware_requested = true;
        fallback->acceleration.fallback_reason = reason;
        fallback->acceleration.recovery_count = 1;
        return fallback;
    }
    if (decoder_result < 0) throwFfmpegError(decoder_result, "Opening video decoder for playback");

    impl->packet.reset(av_packet_alloc());
    if (!impl->packet) throw MediaError("Could not allocate a video packet.");

    impl->frame.reset(av_frame_alloc());
    if (!impl->frame) throw MediaError("Could not allocate a decoded video frame.");

    return impl;
}

bool VideoPlaybackSession::decodeRawNextFrame(
    Impl& impl,
    ForwardDecodeDiagnostics* diagnostics) {
    if (impl.end_reached) return false;

    while (true) {
        if (!impl.flush_sent) {
            while (true) {
                OptionalDurationAccumulator packet_duration(
                    diagnostics != nullptr
                        ? &diagnostics->packet_io_nanoseconds
                        : nullptr);
                ObserverTimingScope packet_timing(
                    impl.observer,
                    DecodeTimingStage::PacketIo);
                const int read_result = av_read_frame(impl.format.get(), impl.packet.get());
                if (read_result == AVERROR_EOF) {
                    const int flush_result = avcodec_send_packet(impl.decoder.get(), nullptr);
                    impl.flush_sent = true;
                    if (flush_result < 0 && flush_result != AVERROR_EOF) {
                        throwFfmpegError(flush_result, "Flushing video decoder");
                    }
                    break;
                }
                if (read_result < 0) {
                    throwFfmpegError(read_result, "Reading video packet for playback");
                }

                if (impl.packet->stream_index != impl.stream_index) {
                    av_packet_unref(impl.packet.get());
                    continue;
                }

                const int send_result = avcodec_send_packet(impl.decoder.get(), impl.packet.get());
                av_packet_unref(impl.packet.get());
                if (send_result < 0 && send_result != AVERROR(EAGAIN)) {
                    throwFfmpegError(send_result, "Sending video packet to decoder");
                }
                break;
            }
        }

        ObserverTimingScope receive_timing(
            impl.observer,
            DecodeTimingStage::DecoderReceive);
        int receive_result = 0;
        {
            OptionalDurationAccumulator receive_duration(
                diagnostics != nullptr
                    ? &diagnostics->decoder_receive_nanoseconds
                    : nullptr);
            receive_result = avcodec_receive_frame(
                impl.decoder.get(), impl.frame.get());
        }
        if (receive_result == 0) {
            ++impl.current_frame_index;
            impl.last_decoded_timestamp = impl.frame->best_effort_timestamp;
            if (impl.frame->format == AV_PIX_FMT_D3D11) {
                ++impl.acceleration.hardware_frames;
                if (impl.frame->hw_frames_ctx) {
                    const auto* pool = reinterpret_cast<const AVHWFramesContext*>(impl.frame->hw_frames_ctx->data);
                    impl.acceleration.peak_reserved_gpu_bytes = std::max(impl.acceleration.peak_reserved_gpu_bytes,
                        std::uint64_t(pool->width) * pool->height * 3 / 2 * std::max(1, pool->initial_pool_size));
                }
            }
            else ++impl.acceleration.software_frames;
            if (impl.observer != nullptr) impl.observer->record_acceleration(impl.acceleration);
            return true;
        }
        if (receive_result == AVERROR(EAGAIN)) {
            if (impl.flush_sent) {
                impl.end_reached = true;
                return false;
            }
            continue;
        }
        if (receive_result == AVERROR_EOF) {
            impl.end_reached = true;
            return false;
        }
        throwFfmpegError(receive_result, "Receiving decoded video frame");
    }
}

bool VideoPlaybackSession::decodeNextFrame(
    Impl& impl,
    VideoFramePtr* output_frame,
    ForwardDecodeDiagnostics* diagnostics) {
    if (!decodeRawNextFrame(impl, diagnostics)) return false;

    if (output_frame == nullptr) {
        if (impl.observer != nullptr) impl.observer->record_discarded_frame();
        return true;
    }

    VideoFramePtr decoded_frame;
    {
        OptionalDurationAccumulator conversion_duration(
            diagnostics != nullptr
                ? &diagnostics->target_pixel_conversion_nanoseconds
                : nullptr);
        decoded_frame = convertFrame(impl);
    }
    if (impl.cache_decoded_frames) {
        VideoPlaybackSession::cacheFrame(
            impl,
            impl.current_frame_index,
            decoded_frame);
    }
    *output_frame = std::move(decoded_frame);
    return true;
}

bool VideoPlaybackSession::discardNextFrame(
    Impl& impl,
    ForwardDecodeDiagnostics* diagnostics) {
    return decodeNextFrame(impl, nullptr, diagnostics);
}

std::optional<VideoFramePtr> VideoPlaybackSession::decode_next_frame() {
    const auto requested_frame = impl_->current_frame_index + 1;
    try {
        if (impl_->decoder_position_invalid && impl_->current_frame_index >= 0) {
            return decode_frame_at(impl_->current_frame_index + 1);
        }
        VideoFramePtr frame;
        if (!decodeNextFrame(*impl_, &frame)) return std::nullopt;
        return frame;
    } catch (const MediaError& error) {
        if (recoverInSoftware(error)) return decode_frame_at(requested_frame);
        logFailure(impl_->source_path, "playback_decode", error);
        throw;
    } catch (const std::exception& error) {
        logFailure(impl_->source_path, "playback_decode", error);
        throw;
    }
}

std::optional<DecodedVideoFrame> VideoPlaybackSession::decode_frame_at_native(
    std::int64_t frame_index, const CancellationPredicate& cancel) {
    if (cancel && cancel()) return std::nullopt;
    for (auto iterator = impl_->frame_cache.begin(); iterator != impl_->frame_cache.end(); ++iterator) {
        if (iterator->frame_index != frame_index || !iterator->native) continue;
        ++impl_->cache_hit_count;
        const bool positioned = !impl_->decoder_position_invalid && impl_->current_frame_index == frame_index;
        impl_->current_frame_index = frame_index; impl_->end_reached = false;
        impl_->decoder_position_invalid = !positioned;
        auto entry = std::move(*iterator); impl_->frame_cache.erase(iterator);
        auto native = entry.native; impl_->frame_cache.push_back(std::move(entry));
        return DecodedVideoFrame{{}, std::move(native)};
    }
    impl_->native_delivery = true;
    impl_->last_native_frame.reset();
    struct Guard { VideoPlaybackSession& session; ~Guard() { session.impl_->native_delivery = false; session.impl_->last_native_frame.reset(); } } guard{*this};
    const auto decoded = decode_frame_at(frame_index, cancel);
    if (!decoded) return std::nullopt;
    auto native = impl_->last_native_frame;
    if (native) cacheFrame(*impl_, frame_index, {}, native);
    return DecodedVideoFrame{*decoded, std::move(native)};
}

std::optional<DecodedVideoFrame> VideoPlaybackSession::decode_forward_to_native(
    std::int64_t frame_index, const CancellationPredicate& cancel, ForwardDecodeDiagnostics* diagnostics) {
    impl_->native_delivery = true;
    impl_->last_native_frame.reset();
    struct Guard { VideoPlaybackSession& session; ~Guard() { session.impl_->native_delivery = false; session.impl_->last_native_frame.reset(); } } guard{*this};
    const auto decoded = decode_forward_to(frame_index, cancel, diagnostics);
    if (!decoded) return std::nullopt;
    auto native = impl_->last_native_frame;
    if (native) cacheFrame(*impl_, frame_index, {}, native);
    return DecodedVideoFrame{*decoded, std::move(native)};
}

std::optional<VideoFramePtr> VideoPlaybackSession::decode_forward_to(
    std::int64_t frame_index,
    const CancellationPredicate& should_cancel,
    ForwardDecodeDiagnostics* diagnostics) {
    if (diagnostics != nullptr) *diagnostics = {};
    auto* collected_diagnostics = diagnostics;
    if (collected_diagnostics != nullptr) {
        collected_diagnostics->collected = true;
        collected_diagnostics->attempted = true;
        collected_diagnostics->starting_frame = impl_->current_frame_index;
        collected_diagnostics->requested_frame = frame_index;
    }
    OptionalDurationAccumulator elapsed_duration(
        collected_diagnostics != nullptr
            ? &collected_diagnostics->elapsed_nanoseconds
            : nullptr);
    try {
        if (frame_index < 0) {
            throw MediaError("The requested frame index is negative.");
        }
        if (impl_->decoder_position_invalid || impl_->current_frame_index < 0 ||
            frame_index <= impl_->current_frame_index) {
            return std::nullopt;
        }

        const auto cancelled = [&should_cancel]() {
            return should_cancel && should_cancel();
        };
        while (impl_->current_frame_index < frame_index - 1) {
            if (cancelled()) {
                if (collected_diagnostics != nullptr) {
                    collected_diagnostics->cancelled = true;
                }
                return std::nullopt;
            }
            if (!discardNextFrame(*impl_, collected_diagnostics)) {
                return std::nullopt;
            }
            if (collected_diagnostics != nullptr) {
                ++collected_diagnostics->discarded_intermediate_frames;
            }
        }
        if (cancelled()) {
            if (collected_diagnostics != nullptr) {
                collected_diagnostics->cancelled = true;
            }
            return std::nullopt;
        }

        VideoFramePtr frame;
        if (!decodeNextFrame(*impl_, &frame, collected_diagnostics) ||
            impl_->current_frame_index != frame_index) {
            return std::nullopt;
        }
        if (collected_diagnostics != nullptr) {
            collected_diagnostics->completed = true;
        }
        return frame;
    } catch (const MediaError& error) {
        if (recoverInSoftware(error)) {
            auto frame = decode_frame_at(frame_index, should_cancel);
            if (diagnostics != nullptr) diagnostics->completed = frame.has_value();
            return frame;
        }
        logFailure(impl_->source_path, "playback_forward_decode", error);
        throw;
    } catch (const std::exception& error) {
        logFailure(impl_->source_path, "playback_forward_decode", error);
        throw;
    }
}

std::optional<VideoFramePtr> VideoPlaybackSession::decode_frame_at(std::int64_t frame_index) {
    return decode_frame_at(frame_index, {});
}

std::optional<VideoFramePtr> VideoPlaybackSession::decode_frame_at(
    std::int64_t frame_index,
    const CancellationPredicate& should_cancel) {
    try {
        if (frame_index < 0) {
            throw MediaError("The requested frame index is negative.");
        }

        const auto cancelled = [&should_cancel]() {
            return should_cancel && should_cancel();
        };

        if (cancelled()) return std::nullopt;

        if (const auto cached = takeCachedFrame(*impl_, frame_index); cached != nullptr) {
            const bool decoder_is_already_at_frame =
                !impl_->decoder_position_invalid &&
                impl_->current_frame_index == frame_index;
            impl_->current_frame_index = frame_index;
            impl_->end_reached = false;
            impl_->decoder_position_invalid = !decoder_is_already_at_frame;
            return cached;
        }

        if (!impl_->decoder_position_invalid &&
            frame_index == impl_->current_frame_index + 1) {
            VideoFramePtr frame;
            if (!decodeNextFrame(*impl_, &frame) ||
                impl_->current_frame_index != frame_index) {
                return std::nullopt;
            }
            return frame;
        }

        const auto decodeFromBeginning = [&]() -> std::optional<VideoFramePtr> {
            reset();
            std::optional<VideoFramePtr> frame;
            for (std::int64_t index = 0; index <= frame_index; ++index) {
                if (cancelled()) return std::nullopt;
                VideoFramePtr decoded_frame;
                if (!decodeNextFrame(*impl_, &decoded_frame)) return std::nullopt;
                frame = std::move(decoded_frame);
            }
            return frame;
        };

        if (!seekToTimestamp(*impl_, frame_index)) {
            if (!impl_->decoder_position_invalid && !impl_->end_reached &&
                impl_->current_frame_index >= 0 &&
                frame_index > impl_->current_frame_index) {
                return decode_forward_to(frame_index, cancelled);
            }
            return decodeFromBeginning();
        }

        impl_->cache_decoded_frames = false;
        const auto fallbackToBeginning = [&]() {
            impl_->cache_decoded_frames = true;
            return decodeFromBeginning();
        };

        while (true) {
            if (cancelled()) {
                impl_->cache_decoded_frames = true;
                return std::nullopt;
            }

            if (!decodeRawNextFrame(*impl_)) {
                impl_->cache_decoded_frames = true;
                return std::nullopt;
            }

            const auto decoded_index = frameIndexForTimestamp(
                impl_->last_decoded_timestamp,
                impl_->frame_rate,
                impl_->stream_time_base,
                impl_->stream_start_time);
            if (!decoded_index.has_value() || *decoded_index > frame_index) {
                return fallbackToBeginning();
            }

            impl_->current_frame_index = *decoded_index;
            if (*decoded_index == frame_index) {
                auto frame = convertFrame(*impl_);
                cacheFrame(*impl_, *decoded_index, frame);
                impl_->cache_decoded_frames = true;
                impl_->decoder_position_invalid = false;
                return frame;
            }
            if (impl_->observer != nullptr) {
                impl_->observer->record_discarded_frame();
            }
        }
    } catch (const MediaError& error) {
        impl_->cache_decoded_frames = true;
        if (recoverInSoftware(error)) return decode_frame_at(frame_index, should_cancel);
        logFailure(impl_->source_path, "playback_seek", error);
        throw;
    } catch (const std::exception& error) {
        impl_->cache_decoded_frames = true;
        logFailure(impl_->source_path, "playback_seek", error);
        throw;
    }
}

std::uint64_t VideoPlaybackSession::take_cache_hit_count() noexcept {
    const auto count = impl_->cache_hit_count;
    impl_->cache_hit_count = 0;
    return count;
}

VideoPlaybackSession::CacheSnapshot
VideoPlaybackSession::cache_snapshot() const noexcept {
    if (impl_ == nullptr) return {};
    return CacheSnapshot{
        static_cast<std::uint64_t>(impl_->frame_cache.size()),
        static_cast<std::uint64_t>(impl_->cached_bytes)};
}

void VideoPlaybackSession::reset() {
    const auto source_path = impl_->source_path;
    auto* observer = impl_ != nullptr ? impl_->observer : nullptr;
    // Once a session has recovered, do not retry the same failing device on seek.
    const auto previous = impl_->acceleration;
    const bool native_delivery = impl_->native_delivery;
    auto transfers = impl_->native_transfers;
    auto options = impl_->options;
    if (previous.recovery_count != 0) options.acceleration = DecodeAcceleration::Software;
    impl_ = openImpl(source_path, observer, options);
    impl_->native_delivery = native_delivery;
    impl_->native_transfers = std::move(transfers);
    impl_->acceleration.hardware_requested = previous.hardware_requested;
    // Native leases may outlive a decoder reset; preserve their transfer counters.
    impl_->acceleration.hardware_frames = previous.hardware_frames;
    impl_->acceleration.software_frames = previous.software_frames;
    impl_->acceleration.downloaded_frames = previous.downloaded_frames;
    impl_->acceleration.downloaded_bytes = previous.downloaded_bytes;
    impl_->acceleration.recovery_count = previous.recovery_count;
    impl_->acceleration.peak_reserved_gpu_bytes = previous.peak_reserved_gpu_bytes;
    if (!previous.fallback_reason.empty()) impl_->acceleration.fallback_reason = previous.fallback_reason;
}

VideoFramePtr VideoPlaybackSession::convertFrame(Impl& impl) {
    if (impl.frame->format != AV_PIX_FMT_D3D11)
        return copyRgbaFrame(*impl.frame, impl.scaler, impl.observer);
    if (impl.options.hardware_frame_guard) impl.options.hardware_frame_guard(impl.current_frame_index);
    if (impl.native_delivery) {
        auto retained = std::make_unique<NativeVideoFrame::Impl>();
        retained->frame.reset(av_frame_clone(impl.frame.get()));
        retained->source_path = impl.source_path;
        retained->transfers = impl.native_transfers;
        if (!retained->frame) throw MediaError("Retaining the native decoded frame failed.");
        impl.last_native_frame = NativeVideoFramePtr(new NativeVideoFrame(std::move(retained)));
        return {};
    }
    auto frame = downloadD3D11Frame(*impl.frame, impl.scaler, impl.observer);
    ++impl.acceleration.downloaded_frames;
    impl.acceleration.downloaded_bytes += static_cast<std::uint64_t>(impl.frame->width) * impl.frame->height * 3 / 2;
    if (impl.observer) impl.observer->record_acceleration(impl.acceleration);
    return frame;
}

bool VideoPlaybackSession::recoverInSoftware(const MediaError& error) {
    if (!impl_->hardware_initialized) return false;
    const auto source = impl_->source_path;
    auto* observer = impl_->observer;
    auto diagnostics = impl_->acceleration;
    diagnostics.backend = DecodeBackend::Software;
    diagnostics.fallback_reason = error.what();
    ++diagnostics.recovery_count;
    logHardwareRecovery(source, diagnostics.fallback_reason, error.error_code());
    auto fallback = openImpl(source, observer, DecodeOptions{});
    fallback->native_transfers = impl_->native_transfers;
    fallback->acceleration = std::move(diagnostics);
    impl_ = std::move(fallback);
    if (observer != nullptr) observer->record_acceleration(impl_->acceleration);
    return true;
}

DecodeAccelerationDiagnostics VideoPlaybackSession::acceleration_diagnostics() const {
    auto result = impl_->acceleration;
    result.downloaded_frames += impl_->native_transfers->frames.load();
    result.downloaded_bytes += impl_->native_transfers->bytes.load();
    return result;
}

void VideoPlaybackSession::set_decode_options(DecodeOptions options) {
    if (options.acceleration == impl_->options.acceleration) return;
    impl_ = openImpl(impl_->source_path, impl_->observer, options);
}

std::int64_t VideoPlaybackSession::current_frame_index() const noexcept {
    return impl_->current_frame_index;
}

bool VideoPlaybackSession::at_end() const noexcept {
    return impl_->end_reached;
}

} // namespace creative_suite::media
