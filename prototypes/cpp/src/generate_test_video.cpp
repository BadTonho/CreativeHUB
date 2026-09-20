#include <filesystem>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
}

namespace fs = std::filesystem;

static void require_ffmpeg(int result, const std::string& operation) {
    if (result < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE]{};
        av_strerror(result, message, sizeof(message));
        throw std::runtime_error(operation + ": " + message);
    }
}

static void print_usage() {
    std::cout
        << "Usage: creative-suite-test-video-generator --output <path> [options]\n"
        << "  --frames <number>       Number of frames, default 120\n"
        << "  --width <number>        Frame width, default 640\n"
        << "  --height <number>       Frame height, default 360\n"
        << "  --fps <number>          Frame rate, default 30\n"
        << "  --help                  Show this help\n";
}

struct Options {
    fs::path output;
    int frames = 120;
    int width = 640;
    int height = 360;
    int fps = 30;
};

static Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            print_usage();
            std::exit(0);
        }
        if (argument == "--output" || argument == "--frames" || argument == "--width" ||
            argument == "--height" || argument == "--fps") {
            if (index + 1 >= argc) throw std::runtime_error("Missing value for " + argument);
            const std::string value = argv[++index];
            if (argument == "--output") options.output = value;
            if (argument == "--frames") options.frames = std::stoi(value);
            if (argument == "--width") options.width = std::stoi(value);
            if (argument == "--height") options.height = std::stoi(value);
            if (argument == "--fps") options.fps = std::stoi(value);
            continue;
        }
        throw std::runtime_error("Unknown argument: " + argument);
    }
    if (options.output.empty()) throw std::runtime_error("--output is required");
    if (options.frames <= 0 || options.width <= 0 || options.height <= 0 || options.fps <= 0) {
        throw std::runtime_error("frames, width, height, and fps must be greater than zero");
    }
    if ((options.width % 2) != 0 || (options.height % 2) != 0) {
        throw std::runtime_error("width and height must be even for YUV420P");
    }
    return options;
}

static void fill_frame(AVFrame* frame, int frame_index) {
    for (int y = 0; y < frame->height; ++y) {
        auto* row = frame->data[0] + y * frame->linesize[0];
        for (int x = 0; x < frame->width; ++x) {
            row[x] = static_cast<std::uint8_t>((x * 3 + y * 5 + frame_index * 7) % 256);
        }
    }
    for (int y = 0; y < frame->height / 2; ++y) {
        auto* u_row = frame->data[1] + y * frame->linesize[1];
        auto* v_row = frame->data[2] + y * frame->linesize[2];
        for (int x = 0; x < frame->width / 2; ++x) {
            u_row[x] = static_cast<std::uint8_t>(64 + ((x * 2 + frame_index * 3) % 128));
            v_row[x] = static_cast<std::uint8_t>(192 - ((y * 2 + frame_index * 5) % 128));
        }
    }
}

static void generate_video(const Options& options) {
    if (!options.output.parent_path().empty()) fs::create_directories(options.output.parent_path());

    AVFormatContext* format = nullptr;
    AVCodecContext* codec_context = nullptr;
    AVStream* stream = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    bool io_open = false;
    bool header_written = false;

    auto cleanup = [&]() {
        if (header_written) av_write_trailer(format);
        if (io_open) avio_closep(&format->pb);
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codec_context);
        if (format) avformat_free_context(format);
    };

    try {
        require_ffmpeg(avformat_alloc_output_context2(&format, nullptr, "matroska",
                                                       options.output.string().c_str()),
                       "Creating output format");
        format->flags |= AVFMT_FLAG_BITEXACT;
        const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_FFV1);
        if (!codec) throw std::runtime_error("FFV1 encoder is not available in this FFmpeg build");

        stream = avformat_new_stream(format, nullptr);
        if (!stream) throw std::runtime_error("Creating output stream failed");

        codec_context = avcodec_alloc_context3(codec);
        if (!codec_context) throw std::runtime_error("Allocating encoder context failed");
        codec_context->codec_id = codec->id;
        codec_context->codec_type = AVMEDIA_TYPE_VIDEO;
        codec_context->width = options.width;
        codec_context->height = options.height;
        codec_context->pix_fmt = AV_PIX_FMT_YUV420P;
        codec_context->time_base = AVRational{1, options.fps};
        codec_context->framerate = AVRational{options.fps, 1};
        codec_context->gop_size = 1;
        codec_context->max_b_frames = 0;
        if (format->oformat->flags & AVFMT_GLOBALHEADER) codec_context->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        require_ffmpeg(avcodec_open2(codec_context, codec, nullptr), "Opening FFV1 encoder");
        require_ffmpeg(avcodec_parameters_from_context(stream->codecpar, codec_context),
                       "Copying encoder parameters");
        stream->time_base = codec_context->time_base;

        if (!(format->oformat->flags & AVFMT_NOFILE)) {
            require_ffmpeg(avio_open(&format->pb, options.output.string().c_str(), AVIO_FLAG_WRITE),
                           "Opening output file");
            io_open = true;
        }
        require_ffmpeg(avformat_write_header(format, nullptr), "Writing output header");
        header_written = true;

        frame = av_frame_alloc();
        packet = av_packet_alloc();
        if (!frame || !packet) throw std::runtime_error("Allocating encoder frame or packet failed");
        frame->format = codec_context->pix_fmt;
        frame->width = codec_context->width;
        frame->height = codec_context->height;
        require_ffmpeg(av_frame_get_buffer(frame, 32), "Allocating encoder frame buffer");

        auto drain = [&]() {
            for (;;) {
                const int result = avcodec_receive_packet(codec_context, packet);
                if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) return;
                require_ffmpeg(result, "Receiving encoded packet");
                av_packet_rescale_ts(packet, codec_context->time_base, stream->time_base);
                packet->stream_index = stream->index;
                require_ffmpeg(av_interleaved_write_frame(format, packet), "Writing encoded packet");
                av_packet_unref(packet);
            }
        };

        for (int index = 0; index < options.frames; ++index) {
            require_ffmpeg(av_frame_make_writable(frame), "Preparing encoder frame");
            fill_frame(frame, index);
            frame->pts = index;
            require_ffmpeg(avcodec_send_frame(codec_context, frame), "Sending frame to encoder");
            drain();
        }
        require_ffmpeg(avcodec_send_frame(codec_context, nullptr), "Flushing encoder");
        drain();
        cleanup();
    } catch (...) {
        cleanup();
        throw;
    }
}

int main(int argc, char** argv) {
    try {
        generate_video(parse_options(argc, argv));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test video generation error: " << error.what() << "\n";
        return 1;
    }
}
