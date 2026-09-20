#define SDL_MAIN_HANDLED
#ifdef _WIN32
#define NOMINMAX
#endif

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/rational.h>
#include <libswscale/swscale.h>
}

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

struct Options {
    std::string input;
    std::string output;
    std::string effect;
    int frames = 300;
    bool benchmark = false;
};

static std::string json_escape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (const char character : value) {
        switch (character) {
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        default: result += character; break;
        }
    }
    return result;
}

static void print_usage() {
    std::cout
        << "Usage: creative-suite-cpp-prototype --input <path> [options]\n"
        << "  --benchmark             Decode and render a fixed number of frames\n"
        << "  --frames <number>       Number of frames for benchmark mode\n"
        << "  --effect grayscale      Enable the grayscale shader effect\n"
        << "  --output <json-path>    Write benchmark JSON to a file\n"
        << "  --help                  Show this help\n";
}

static Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            print_usage();
            std::exit(0);
        }
        if (argument == "--benchmark") {
            options.benchmark = true;
            continue;
        }
        if (argument == "--input" || argument == "--frames" || argument == "--effect" || argument == "--output") {
            if (index + 1 >= argc) {
                throw std::runtime_error("Missing value for " + argument);
            }
            const std::string value = argv[++index];
            if (argument == "--input") options.input = value;
            if (argument == "--frames") options.frames = std::stoi(value);
            if (argument == "--effect") options.effect = value;
            if (argument == "--output") options.output = value;
            continue;
        }
        throw std::runtime_error("Unknown argument: " + argument);
    }
    if (options.input.empty()) throw std::runtime_error("--input is required");
    if (options.frames <= 0) throw std::runtime_error("--frames must be greater than zero");
    if (!options.effect.empty() && options.effect != "grayscale") {
        throw std::runtime_error("Only the grayscale effect is supported");
    }
    return options;
}

static void require_ffmpeg(int result, const std::string& operation) {
    if (result < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, message, sizeof(message));
        throw std::runtime_error(operation + ": " + message);
    }
}

struct VideoFrame {
    int width = 0;
    int height = 0;
    int64_t index = 0;
    std::vector<std::uint8_t> rgba;
};

class VideoDecoder {
public:
    ~VideoDecoder() {
        if (sws_) sws_freeContext(sws_);
        if (rgba_frame_) av_frame_free(&rgba_frame_);
        if (decoded_frame_) av_frame_free(&decoded_frame_);
        if (packet_) av_packet_free(&packet_);
        if (decoder_) avcodec_free_context(&decoder_);
        if (format_) avformat_close_input(&format_);
    }

    void open(const std::string& input) {
        require_ffmpeg(avformat_open_input(&format_, input.c_str(), nullptr, nullptr), "Opening input");
        require_ffmpeg(avformat_find_stream_info(format_, nullptr), "Reading stream information");

        const AVCodec* codec = nullptr;
        stream_index_ = av_find_best_stream(format_, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (stream_index_ < 0 || codec == nullptr) throw std::runtime_error("No video stream found");

        stream_ = format_->streams[stream_index_];
        decoder_ = avcodec_alloc_context3(codec);
        if (!decoder_) throw std::runtime_error("Could not allocate decoder context");
        require_ffmpeg(avcodec_parameters_to_context(decoder_, stream_->codecpar), "Copying decoder parameters");
        decoder_->thread_count = 0;
        require_ffmpeg(avcodec_open2(decoder_, codec, nullptr), "Opening decoder");

        decoded_frame_ = av_frame_alloc();
        rgba_frame_ = av_frame_alloc();
        if (!decoded_frame_ || !rgba_frame_) throw std::runtime_error("Could not allocate video frames");

        frame_rate_ = av_guess_frame_rate(format_, stream_, nullptr);
        if (frame_rate_.num <= 0 || frame_rate_.den <= 0) frame_rate_ = AVRational{30, 1};
        if (stream_->nb_frames > 0) {
            total_frames_ = stream_->nb_frames;
        } else if (stream_->duration > 0) {
            total_frames_ = av_rescale_q(stream_->duration, stream_->time_base,
                                         AVRational{frame_rate_.den, frame_rate_.num});
        }
    }

    bool read(VideoFrame& destination) {
        for (;;) {
            int result = avcodec_receive_frame(decoder_, decoded_frame_);
            if (result == 0) {
                convert(decoded_frame_, destination);
                destination.index = next_frame_index_++;
                return true;
            }
            if (result == AVERROR_EOF) return false;
            if (result != AVERROR(EAGAIN)) require_ffmpeg(result, "Receiving decoded frame");

            if (flushing_) return false;

            result = av_read_frame(format_, packet_);
            if (result == AVERROR_EOF) {
                flushing_ = true;
                require_ffmpeg(avcodec_send_packet(decoder_, nullptr), "Flushing decoder");
                continue;
            }
            require_ffmpeg(result, "Reading packet");
            if (packet_->stream_index == stream_index_) {
                require_ffmpeg(avcodec_send_packet(decoder_, packet_), "Sending packet to decoder");
            }
            av_packet_unref(packet_);
        }
    }

    bool seek_frame(int64_t target, VideoFrame& destination) {
        if (target < 0) target = 0;
        const int64_t timestamp = av_rescale_q(target,
                                               AVRational{frame_rate_.den, frame_rate_.num},
                                               stream_->time_base);
        require_ffmpeg(av_seek_frame(format_, stream_index_, timestamp, AVSEEK_FLAG_BACKWARD), "Seeking video");
        avcodec_flush_buffers(decoder_);
        flushing_ = false;
        next_frame_index_ = 0;

        VideoFrame temporary;
        while (read(temporary)) {
            if (temporary.index >= target) {
                destination = std::move(temporary);
                return true;
            }
        }
        return false;
    }

    int width() const { return decoder_->width; }
    int height() const { return decoder_->height; }
    double fps() const { return static_cast<double>(frame_rate_.num) / frame_rate_.den; }
    int64_t total_frames() const { return total_frames_; }

private:
    void convert(const AVFrame* source, VideoFrame& destination) {
        if (!sws_ || source->width != converted_width_ || source->height != converted_height_ || source->format != converted_format_) {
            sws_ = sws_getCachedContext(sws_, source->width, source->height,
                                        static_cast<AVPixelFormat>(source->format),
                                        source->width, source->height, AV_PIX_FMT_RGBA,
                                        SWS_BILINEAR, nullptr, nullptr, nullptr);
            if (!sws_) throw std::runtime_error("Could not create pixel conversion context");
            converted_width_ = source->width;
            converted_height_ = source->height;
            converted_format_ = source->format;
            rgba_storage_.resize(static_cast<size_t>(source->width) * source->height * 4);
            av_image_fill_arrays(rgba_frame_->data, rgba_frame_->linesize, rgba_storage_.data(),
                                 AV_PIX_FMT_RGBA, source->width, source->height, 1);
        }

        sws_scale(sws_, source->data, source->linesize, 0, source->height,
                  rgba_frame_->data, rgba_frame_->linesize);
        destination.width = source->width;
        destination.height = source->height;
        destination.rgba = rgba_storage_;
    }

    AVFormatContext* format_ = nullptr;
    AVCodecContext* decoder_ = nullptr;
    AVStream* stream_ = nullptr;
    AVPacket* packet_ = av_packet_alloc();
    AVFrame* decoded_frame_ = nullptr;
    AVFrame* rgba_frame_ = nullptr;
    SwsContext* sws_ = nullptr;
    std::vector<std::uint8_t> rgba_storage_;
    int stream_index_ = -1;
    int converted_width_ = 0;
    int converted_height_ = 0;
    int converted_format_ = -1;
    int64_t next_frame_index_ = 0;
    int64_t total_frames_ = 0;
    AVRational frame_rate_{30, 1};
    bool flushing_ = false;
};

static std::vector<std::uint8_t> read_binary(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Could not open shader: " + path.string());
    return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(file), {});
}

struct RectData {
    float x;
    float y;
    float width;
    float height;
};

struct EffectData {
    std::uint32_t grayscale;
    float padding[3];
    float tint[4];
};

class GpuPreview {
public:
    ~GpuPreview() { shutdown(); }

    void initialize(int width, int height, const fs::path& shader_dir, bool hidden) {
        if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
        const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | (hidden ? SDL_WINDOW_HIDDEN : 0);
        window_ = SDL_CreateWindow("Creative Suite Prototype - C++", 1280, 720, flags);
        if (!window_) throw std::runtime_error(SDL_GetError());

        device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, true, nullptr);
        if (!device_) throw std::runtime_error(std::string("Creating GPU device: ") + SDL_GetError());
        if (!SDL_ClaimWindowForGPUDevice(device_, window_)) throw std::runtime_error(SDL_GetError());

        width_ = width;
        height_ = height;
        video_texture_ = create_texture(width, height);
        white_texture_ = create_texture(1, 1);
        transfer_buffer_ = create_transfer_buffer(static_cast<size_t>(width) * height * 4);
        upload_texture(white_texture_, {255, 255, 255, 255}, 1, 1);

        auto vertex_code = read_binary(shader_dir / "video.vert.spv");
        auto fragment_code = read_binary(shader_dir / "video.frag.spv");
        vertex_shader_ = create_shader(vertex_code, SDL_GPU_SHADERSTAGE_VERTEX, 0, 1);
        fragment_shader_ = create_shader(fragment_code, SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

        SDL_GPUColorTargetDescription color_target{};
        color_target.format = SDL_GetGPUSwapchainTextureFormat(device_, window_);
        if (color_target.format == SDL_GPU_TEXTUREFORMAT_INVALID) throw std::runtime_error("Invalid swapchain format");

        SDL_GPUGraphicsPipelineCreateInfo pipeline_info{};
        pipeline_info.vertex_shader = vertex_shader_;
        pipeline_info.fragment_shader = fragment_shader_;
        pipeline_info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
        pipeline_info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipeline_info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipeline_info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pipeline_info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        pipeline_info.target_info.color_target_descriptions = &color_target;
        pipeline_info.target_info.num_color_targets = 1;
        pipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipeline_info);
        if (!pipeline_) throw std::runtime_error(std::string("Creating GPU pipeline: ") + SDL_GetError());

        SDL_GPUSamplerCreateInfo sampler_info{};
        sampler_info.min_filter = SDL_GPU_FILTER_LINEAR;
        sampler_info.mag_filter = SDL_GPU_FILTER_LINEAR;
        sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        sampler_ = SDL_CreateGPUSampler(device_, &sampler_info);
        if (!sampler_) throw std::runtime_error(std::string("Creating GPU sampler: ") + SDL_GetError());
    }

    void render(const VideoFrame& frame, bool grayscale, int64_t total_frames) {
        if (!device_) return;
        upload_texture(video_texture_, frame.rgba, frame.width, frame.height);

        SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device_);
        if (!command) throw std::runtime_error(SDL_GetError());
        SDL_GPUTexture* swapchain = nullptr;
        Uint32 swap_width = 0;
        Uint32 swap_height = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(command, window_, &swapchain, &swap_width, &swap_height)) {
            SDL_CancelGPUCommandBuffer(command);
            throw std::runtime_error(SDL_GetError());
        }
        if (!swapchain) {
            SDL_SubmitGPUCommandBuffer(command);
            return;
        }

        SDL_GPUColorTargetInfo target{};
        target.texture = swapchain;
        target.load_op = SDL_GPU_LOADOP_CLEAR;
        target.store_op = SDL_GPU_STOREOP_STORE;
        target.clear_color = SDL_FColor{0.03f, 0.03f, 0.03f, 1.0f};
        target.cycle = true;
        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command, &target, 1, nullptr);
        if (!pass) throw std::runtime_error(SDL_GetError());

        SDL_BindGPUGraphicsPipeline(pass, pipeline_);
        draw_texture(command, pass, video_texture_, RectData{0.0f, 0.18f, 1.0f, 0.82f}, grayscale,
                     {1.0f, 1.0f, 1.0f, 1.0f});
        draw_texture(command, pass, white_texture_, RectData{0.0f, 0.0f, 1.0f, 0.12f}, false,
                     {0.12f, 0.12f, 0.14f, 1.0f});
        const float progress = total_frames > 0
            ? std::clamp(static_cast<float>(frame.index) / static_cast<float>(total_frames), 0.0f, 1.0f)
            : 0.0f;
        draw_texture(command, pass, white_texture_, RectData{0.0f, 0.0f, progress, 0.12f}, false,
                     {0.25f, 0.65f, 0.95f, 1.0f});
        SDL_EndGPURenderPass(pass);
        if (!SDL_SubmitGPUCommandBuffer(command)) throw std::runtime_error(SDL_GetError());
    }

    int64_t timeline_frame(float x, int64_t total_frames) const {
        int window_width = 1280;
        int window_height = 720;
        SDL_GetWindowSize(window_, &window_width, &window_height);
        if (total_frames <= 0 || window_width <= 0) return 0;
        const float normalized = std::clamp(x / static_cast<float>(window_width), 0.0f, 1.0f);
        return static_cast<int64_t>(normalized * static_cast<float>(total_frames - 1));
    }

private:
    SDL_GPUTexture* create_texture(int width, int height) {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        info.width = static_cast<Uint32>(width);
        info.height = static_cast<Uint32>(height);
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        SDL_GPUTexture* texture = SDL_CreateGPUTexture(device_, &info);
        if (!texture) throw std::runtime_error(std::string("Creating GPU texture: ") + SDL_GetError());
        return texture;
    }

    SDL_GPUTransferBuffer* create_transfer_buffer(size_t size) {
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        info.size = static_cast<Uint32>(size);
        SDL_GPUTransferBuffer* buffer = SDL_CreateGPUTransferBuffer(device_, &info);
        if (!buffer) throw std::runtime_error(std::string("Creating transfer buffer: ") + SDL_GetError());
        return buffer;
    }

    SDL_GPUShader* create_shader(const std::vector<std::uint8_t>& code, SDL_GPUShaderStage stage,
                                 Uint32 samplers, Uint32 uniform_buffers) {
        SDL_GPUShaderCreateInfo info{};
        info.code_size = code.size();
        info.code = code.data();
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.stage = stage;
        info.num_samplers = samplers;
        info.num_uniform_buffers = uniform_buffers;
        SDL_GPUShader* shader = SDL_CreateGPUShader(device_, &info);
        if (!shader) throw std::runtime_error(std::string("Creating shader: ") + SDL_GetError());
        return shader;
    }

    void upload_texture(SDL_GPUTexture* texture, const std::vector<std::uint8_t>& bytes, int width, int height) {
        void* mapped = SDL_MapGPUTransferBuffer(device_, transfer_buffer_, true);
        if (!mapped) throw std::runtime_error(SDL_GetError());
        SDL_memcpy(mapped, bytes.data(), bytes.size());
        SDL_UnmapGPUTransferBuffer(device_, transfer_buffer_);

        SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device_);
        if (!command) throw std::runtime_error(SDL_GetError());
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer_buffer_;
        source.pixels_per_row = static_cast<Uint32>(width);
        source.rows_per_layer = static_cast<Uint32>(height);
        SDL_GPUTextureRegion destination{};
        destination.texture = texture;
        destination.w = static_cast<Uint32>(width);
        destination.h = static_cast<Uint32>(height);
        destination.d = 1;
        SDL_UploadToGPUTexture(copy, &source, &destination, true);
        SDL_EndGPUCopyPass(copy);
        if (!SDL_SubmitGPUCommandBuffer(command)) throw std::runtime_error(SDL_GetError());
        if (!SDL_WaitForGPUIdle(device_)) throw std::runtime_error(SDL_GetError());
    }

    void draw_texture(SDL_GPUCommandBuffer* command, SDL_GPURenderPass* pass, SDL_GPUTexture* texture,
                      const RectData& rect, bool grayscale, const std::array<float, 4>& tint) {
        SDL_PushGPUVertexUniformData(command, 0, &rect, sizeof(rect));
        EffectData effect{grayscale ? 1u : 0u, {0.0f, 0.0f, 0.0f}, {tint[0], tint[1], tint[2], tint[3]}};
        SDL_PushGPUFragmentUniformData(command, 0, &effect, sizeof(effect));
        SDL_GPUTextureSamplerBinding binding{texture, sampler_};
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);
        SDL_DrawGPUPrimitives(pass, 4, 1, 0, 0);
    }

    void shutdown() {
        if (device_) SDL_WaitForGPUIdle(device_);
        if (video_texture_) SDL_ReleaseGPUTexture(device_, video_texture_);
        if (white_texture_) SDL_ReleaseGPUTexture(device_, white_texture_);
        if (transfer_buffer_) SDL_ReleaseGPUTransferBuffer(device_, transfer_buffer_);
        if (sampler_) SDL_ReleaseGPUSampler(device_, sampler_);
        if (pipeline_) SDL_ReleaseGPUGraphicsPipeline(device_, pipeline_);
        if (vertex_shader_) SDL_ReleaseGPUShader(device_, vertex_shader_);
        if (fragment_shader_) SDL_ReleaseGPUShader(device_, fragment_shader_);
        if (device_ && window_) SDL_ReleaseWindowFromGPUDevice(device_, window_);
        if (window_) SDL_DestroyWindow(window_);
        if (device_) SDL_DestroyGPUDevice(device_);
        if (window_ || device_) SDL_Quit();
        window_ = nullptr;
        device_ = nullptr;
    }

    SDL_Window* window_ = nullptr;
    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUTexture* video_texture_ = nullptr;
    SDL_GPUTexture* white_texture_ = nullptr;
    SDL_GPUTransferBuffer* transfer_buffer_ = nullptr;
    SDL_GPUSampler* sampler_ = nullptr;
    SDL_GPUShader* vertex_shader_ = nullptr;
    SDL_GPUShader* fragment_shader_ = nullptr;
    SDL_GPUGraphicsPipeline* pipeline_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

static std::uint64_t memory_usage_bytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters))) {
        return static_cast<std::uint64_t>(counters.WorkingSetSize);
    }
    return 0;
#elif defined(__APPLE__)
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    struct rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024ull;
#endif
}

static void write_report(const Options& options, const VideoDecoder& decoder, int decoded_frames,
                         double first_frame_ms, double decode_ms, double preview_ms,
                         std::uint64_t peak_memory, const std::string& error = {}) {
    std::ostringstream report;
    report << std::fixed << std::setprecision(3);
    report << "{\n"
           << "  \"language\": \"cpp\",\n"
           << "  \"prototype_version\": \"0.1.0\",\n"
           << "  \"platform\": \"" << json_escape(
#ifdef _WIN32
               "windows"
#elif defined(__APPLE__)
               "macos"
#else
               "linux"
#endif
           ) << "\",\n"
           << "  \"width\": " << decoder.width() << ",\n"
           << "  \"height\": " << decoder.height() << ",\n"
           << "  \"fps\": " << decoder.fps() << ",\n"
           << "  \"requested_frames\": " << options.frames << ",\n"
           << "  \"decoded_frames\": " << decoded_frames << ",\n"
           << "  \"time_to_first_frame_ms\": " << first_frame_ms << ",\n"
           << "  \"decode_ms\": " << decode_ms << ",\n"
           << "  \"decode_fps\": " << (decode_ms > 0.0 ? decoded_frames * 1000.0 / decode_ms : 0.0) << ",\n"
           << "  \"preview_ms\": " << preview_ms << ",\n"
           << "  \"preview_fps\": " << (preview_ms > 0.0 ? decoded_frames * 1000.0 / preview_ms : 0.0) << ",\n"
           << "  \"peak_memory_bytes\": " << peak_memory << ",\n"
           << "  \"error\": \"" << json_escape(error) << "\",\n"
           << "  \"errors\": " << (error.empty() ? "[]" : "[\"" + json_escape(error) + "\"]") << "\n"
           << "}\n";

    if (options.output.empty()) {
        std::cout << report.str();
    } else {
        std::ofstream output(options.output);
        if (!output) throw std::runtime_error("Could not write report: " + options.output);
        output << report.str();
    }
}

int main(int argc, char** argv) {
    Options options;
    try {
        options = parse_options(argc, argv);
        if (!fs::exists(options.input)) throw std::runtime_error("Input file does not exist: " + options.input);

        VideoDecoder decoder;
        const auto start = Clock::now();
        decoder.open(options.input);
        VideoFrame frame;
        const auto first_decode_start = Clock::now();
        if (!decoder.read(frame)) throw std::runtime_error("Input contains no decodable video frames");
        const double first_decode_ms = std::chrono::duration<double, std::milli>(Clock::now() - first_decode_start).count();
        const double first_frame_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();

        GpuPreview preview;
        preview.initialize(frame.width, frame.height,
                           fs::path(PROTOTYPE_SHADER_DIR), options.benchmark);

        int decoded_frames = 0;
        double decode_ms = first_decode_ms;
        double preview_ms = 0.0;
        std::uint64_t peak_memory = memory_usage_bytes();
        bool grayscale = options.effect == "grayscale";

        if (options.benchmark) {
            for (;;) {
                const auto preview_start = Clock::now();
                preview.render(frame, grayscale, decoder.total_frames());
                preview_ms += std::chrono::duration<double, std::milli>(Clock::now() - preview_start).count();
                ++decoded_frames;
                peak_memory = std::max(peak_memory, memory_usage_bytes());
                if (decoded_frames >= options.frames) break;
                const auto decode_start = Clock::now();
                VideoFrame next;
                if (!decoder.read(next)) break;
                decode_ms += std::chrono::duration<double, std::milli>(Clock::now() - decode_start).count();
                frame = std::move(next);
            }
            write_report(options, decoder, decoded_frames, first_frame_ms, decode_ms, preview_ms, peak_memory);
            return 0;
        }

        bool playing = true;
        bool running = true;
        double next_frame_at = 0.0;
        while (running) {
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    if (event.key.key == SDLK_ESCAPE) running = false;
                    if (event.key.key == SDLK_SPACE) playing = !playing;
                    if (event.key.key == SDLK_G) grayscale = !grayscale;
                    if (event.key.key == SDLK_LEFT || event.key.key == SDLK_RIGHT) {
                        const int64_t delta = event.key.key == SDLK_LEFT ? -1 : 1;
                        VideoFrame sought;
                        if (decoder.seek_frame(std::max<int64_t>(0, frame.index + delta), sought)) frame = std::move(sought);
                    }
                }
                if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN && event.button.button == SDL_BUTTON_LEFT &&
                    event.button.y <= 100.0f) {
                    VideoFrame sought;
                    if (decoder.seek_frame(preview.timeline_frame(event.button.x, decoder.total_frames()), sought)) {
                        frame = std::move(sought);
                    }
                }
            }

            preview.render(frame, grayscale, decoder.total_frames());
            if (playing) {
                const double now = SDL_GetTicksNS() / 1'000'000.0;
                if (now >= next_frame_at) {
                    VideoFrame next;
                    if (decoder.read(next)) {
                        frame = std::move(next);
                        next_frame_at = now + 1000.0 / decoder.fps();
                    } else {
                        playing = false;
                    }
                }
            }
            SDL_Delay(1);
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Prototype error: " << error.what() << "\n";
        return 1;
    }
}
