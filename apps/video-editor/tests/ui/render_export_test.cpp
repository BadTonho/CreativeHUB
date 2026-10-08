#include "media/video_playback.h"
#include "media/audio_playback.h"
#include "rendering/offline_export_renderer.h"
#include "rendering/render_output_capabilities.h"
#include "workspaces/render/queue/render_queue_controller.h"
#include "logging/logger.h"
#include "playback/playback_worker.h"

#include <creative_suite/effects/effects.h>
#ifdef CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS
#include "image_document_session.h"
#endif

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

#include <QApplication>
#include <QColor>
#include <QEventLoop>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>
#include <QOffscreenSurface>
#include <QSurfaceFormat>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <thread>
#include <utility>
#include <exception>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool native_gpu_mode = false;
bool compare_before_encoding = true;
rendering::OfflineExportOptions native_options;
rendering::OfflineExportMetrics last_metrics;

class CheckedNativeGpu final : public rendering::ExportGpuCompositor {
public:
    explicit CheckedNativeGpu(QOffscreenSurface* surface) : gpu_(surface) {}
    creative_suite::composition::OpenGlCompositionResult compose(int width, int height,
        const std::vector<creative_suite::composition::CompositionLayer>& layers,
        const creative_suite::composition::OpenGlFrameCompositor::CancellationPredicate& cancel,
        creative_suite::composition::OpenGlCompositionTimings* timings) override {
        auto result = gpu_.compose(width, height, layers, cancel, timings);
        if (result.frame && compare_before_encoding) {
            auto cpu = creative_suite::composition::FrameCompositor::compose(width, height, layers);
            require(cpu && cpu->rgba_pixels.size() == result.frame->rgba_pixels.size(), "Pre-encoding GPU geometry differs.");
            for (std::size_t i = 0; i < cpu->rgba_pixels.size(); ++i)
                require(std::abs(int(cpu->rgba_pixels[i]) - int(result.frame->rgba_pixels[i])) <= (i % 4 == 3 ? 0 : 2),
                    "Pre-encoding CPU/GPU parity failed.");
        }
        return result;
    }
    creative_suite::composition::OpenGlResourceUsage resourceUsage() const noexcept override { return gpu_.resourceUsage(); }
private:
    creative_suite::composition::OpenGlFrameCompositor gpu_;
};

void renderJob(const rendering::RenderJob& job, const std::atomic_bool& canceled,
    rendering::OfflineExportRenderer::ProgressCallback progress = {}) {
    auto options = native_options;
    options.metrics_callback = [](const auto& metrics) { last_metrics = metrics; };
    std::exception_ptr failure;
    const auto run = [&] {
        try { rendering::OfflineExportRenderer::render(job, canceled, progress, options); }
        catch (...) { failure = std::current_exception(); }
    };
    if (native_gpu_mode) { std::thread worker(run); worker.join(); } else run();
    if (failure) std::rethrow_exception(failure);
    if (native_gpu_mode && job.settings.gpu_composition_enabled)
        require(last_metrics.gpu_frames == last_metrics.encoded_frames && last_metrics.cpu_frames == 0 &&
            last_metrics.gpu_frames > 0 && last_metrics.readback_bytes > 0,
            "Native export silently used CPU fallback.");
}

std::filesystem::path pathFromQString(const QString& value) {
    const auto utf8 = value.toUtf8();
    const auto bytes = std::u8string(
        reinterpret_cast<const char8_t*>(utf8.constData()),
        static_cast<std::size_t>(utf8.size()));
    return std::filesystem::path(bytes);
}

QString pathToQString(const std::filesystem::path& value) {
    const auto utf8 = value.u8string();
    return QString::fromUtf8(
        reinterpret_cast<const char*>(utf8.data()),
        static_cast<qsizetype>(utf8.size()));
}

struct OutputChoice {
    rendering::RenderContainerOption container;
    rendering::RenderEncoderOption video;
    rendering::RenderEncoderOption audio;
};

bool softwareEncoder(const std::string& name) {
    constexpr const char* hardware_names[]{
        "_mf", "nvenc", "_qsv", "_amf", "vaapi", "videotoolbox", "_v4l2m2m"};
    return std::none_of(std::begin(hardware_names), std::end(hardware_names),
        [&name](const char* marker) { return name.find(marker) != std::string::npos; });
}

OutputChoice chooseOutput() {
    auto containers = rendering::RenderOutputCapabilities::availableContainers();
    std::stable_sort(containers.begin(), containers.end(), [](const auto& left, const auto& right) {
        return left.name == "matroska" && right.name != "matroska";
    });
    for (const auto& container : containers) {
        auto encoders = container.video_encoders;
        std::stable_sort(encoders.begin(), encoders.end(), [](const auto& a, const auto& b) {
            return a.name == "ffv1" && b.name != "ffv1";
        });
        for (const auto& video : encoders) {
            if (!softwareEncoder(video.name)) continue;
            for (const auto& audio : container.audio_encoders) {
                if (rendering::RenderOutputCapabilities::supportsAudioEncoder(container, audio.name)) {
                    const auto pcm = std::find_if(
                        container.audio_encoders.begin(), container.audio_encoders.end(),
                        [&container](const auto& candidate) {
                            return candidate.name == "pcm_s16le" &&
                                rendering::RenderOutputCapabilities::supportsAudioEncoder(
                                    container, candidate.name);
                        });
                    return {container, video,
                            pcm != container.audio_encoders.end() ? *pcm : audio};
                }
            }
        }
    }
    throw std::runtime_error("The FFmpeg build has no compatible software video/audio output pair.");
}

void requireFfmpeg(int result, const char* operation) {
    if (result >= 0) return;
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(result, message, sizeof(message));
    throw std::runtime_error(std::string(operation) + ": " + message);
}

void muxFrame(
    AVFormatContext* output,
    AVCodecContext* encoder,
    AVStream* stream,
    AVFrame* frame) {
    requireFfmpeg(avcodec_send_frame(encoder, frame), "Encoding a source fixture frame");
    while (true) {
        AVPacket* packet = av_packet_alloc();
        require(packet != nullptr, "Could not allocate a source fixture packet.");
        const int received = avcodec_receive_packet(encoder, packet);
        if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
            av_packet_free(&packet);
            break;
        }
        requireFfmpeg(received, "Receiving a source fixture packet");
        packet->stream_index = stream->index;
        av_packet_rescale_ts(packet, encoder->time_base, stream->time_base);
        const int written = av_interleaved_write_frame(output, packet);
        av_packet_free(&packet);
        requireFfmpeg(written, "Writing a source fixture packet");
    }
}

std::filesystem::path createVideoWithAudioFixture(const std::filesystem::path& root) {
    const auto path = root / "video-with-audio.mkv";
    const auto path_u8 = path.u8string();
    const std::string path_utf8(
        reinterpret_cast<const char*>(path_u8.data()), path_u8.size());
    AVFormatContext* output = nullptr;
    requireFfmpeg(avformat_alloc_output_context2(
                      &output, nullptr, "matroska", path_utf8.c_str()),
                  "Creating a source AV container");
    require(output != nullptr, "Could not allocate a source AV container.");
    const AVCodec* video_encoder = avcodec_find_encoder_by_name("mpeg4");
    const AVCodec* audio_encoder = avcodec_find_encoder_by_name("pcm_s16le");
    require(video_encoder != nullptr && audio_encoder != nullptr,
            "The FFmpeg build does not provide the deterministic fixture encoders.");

    AVCodecContext* video = avcodec_alloc_context3(video_encoder);
    AVCodecContext* audio = avcodec_alloc_context3(audio_encoder);
    require(video != nullptr && audio != nullptr,
            "Could not allocate source fixture encoder contexts.");
    video->width = 32;
    video->height = 24;
    video->pix_fmt = AV_PIX_FMT_YUV420P;
    video->time_base = AVRational{1, 30};
    video->framerate = AVRational{30, 1};
    video->bit_rate = 500000;
    audio->sample_rate = 48000;
    audio->sample_fmt = AV_SAMPLE_FMT_S16;
    audio->time_base = AVRational{1, 48000};
    audio->bit_rate = 1536000;
    av_channel_layout_default(&audio->ch_layout, 2);
    if (output->oformat->flags & AVFMT_GLOBALHEADER) {
        video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    requireFfmpeg(avcodec_open2(video, video_encoder, nullptr),
                  "Opening the source video encoder");
    requireFfmpeg(avcodec_open2(audio, audio_encoder, nullptr),
                  "Opening the source audio encoder");
    AVStream* video_stream = avformat_new_stream(output, nullptr);
    AVStream* audio_stream = avformat_new_stream(output, nullptr);
    require(video_stream != nullptr && audio_stream != nullptr,
            "Could not create source fixture streams.");
    video_stream->time_base = video->time_base;
    audio_stream->time_base = audio->time_base;
    requireFfmpeg(avcodec_parameters_from_context(video_stream->codecpar, video),
                  "Copying source video parameters");
    requireFfmpeg(avcodec_parameters_from_context(audio_stream->codecpar, audio),
                  "Copying source audio parameters");
    if ((output->oformat->flags & AVFMT_NOFILE) == 0) {
        requireFfmpeg(avio_open(&output->pb, path_utf8.c_str(), AVIO_FLAG_WRITE),
                      "Opening the source fixture file");
    }
    requireFfmpeg(avformat_write_header(output, nullptr), "Writing the source fixture header");

    AVFrame* video_frame = av_frame_alloc();
    AVFrame* audio_frame = av_frame_alloc();
    require(video_frame != nullptr && audio_frame != nullptr,
            "Could not allocate source fixture frames.");
    video_frame->format = video->pix_fmt;
    video_frame->width = video->width;
    video_frame->height = video->height;
    requireFfmpeg(av_frame_get_buffer(video_frame, 32), "Allocating source video pixels");
    audio_frame->format = audio->sample_fmt;
    audio_frame->sample_rate = audio->sample_rate;
    audio_frame->nb_samples = 1024;
    requireFfmpeg(av_channel_layout_copy(&audio_frame->ch_layout, &audio->ch_layout),
                  "Copying source audio layout");
    requireFfmpeg(av_frame_get_buffer(audio_frame, 0), "Allocating source audio samples");
    auto* scaler = sws_getContext(
        video->width, video->height, AV_PIX_FMT_RGBA,
        video->width, video->height, video->pix_fmt,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    require(scaler != nullptr, "Could not create the source fixture pixel converter.");

    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(video->width) * video->height * 4U);
    for (int pixel = 0; pixel < video->width * video->height; ++pixel) {
        rgba[static_cast<std::size_t>(pixel) * 4U] = 20;
        rgba[static_cast<std::size_t>(pixel) * 4U + 1] = 40;
        rgba[static_cast<std::size_t>(pixel) * 4U + 2] = 220;
        rgba[static_cast<std::size_t>(pixel) * 4U + 3] = 255;
    }
    const std::uint8_t* source_data[4]{rgba.data(), nullptr, nullptr, nullptr};
    const int source_lines[4]{video->width * 4, 0, 0, 0};
    for (int index = 0; index < 60; ++index) {
        requireFfmpeg(av_frame_make_writable(video_frame), "Preparing source video pixels");
        const bool green_frame = index >= 44;
        for (int pixel = 0; pixel < video->width * video->height; ++pixel) {
            const auto offset = static_cast<std::size_t>(pixel) * 4U;
            rgba[offset] = green_frame ? 20 : static_cast<std::uint8_t>(220 - index * 2);
            rgba[offset + 1] = green_frame ? 220 : static_cast<std::uint8_t>(30 + index * 3);
            rgba[offset + 2] = 30;
        }
        sws_scale(scaler, source_data, source_lines, 0, video->height,
                  video_frame->data, video_frame->linesize);
        video_frame->pts = index;
        muxFrame(output, video, video_stream, video_frame);
    }
    constexpr double pi = 3.14159265358979323846;
    std::int64_t audio_sample = 0;
    while (audio_sample < 96000) {
        const int count = static_cast<int>(std::min<std::int64_t>(
            audio_frame->nb_samples, 96000 - audio_sample));
        requireFfmpeg(av_frame_make_writable(audio_frame), "Preparing source audio samples");
        audio_frame->nb_samples = count;
        auto* samples = reinterpret_cast<std::int16_t*>(audio_frame->data[0]);
        for (int index = 0; index < count; ++index) {
            const auto source_sample = audio_sample + index;
            const auto amplitude = source_sample >= 64000 ? 12000.0 : 1000.0;
            const auto value = static_cast<std::int16_t>(std::lround(
                std::sin(2.0 * pi * 440.0 * source_sample / 48000.0) * amplitude));
            samples[index * 2] = value;
            samples[index * 2 + 1] = value;
        }
        audio_frame->pts = audio_sample;
        muxFrame(output, audio, audio_stream, audio_frame);
        audio_sample += count;
    }
    muxFrame(output, video, video_stream, nullptr);
    muxFrame(output, audio, audio_stream, nullptr);
    requireFfmpeg(av_write_trailer(output), "Finishing the source fixture container");
    if (output->pb != nullptr) avio_closep(&output->pb);
    sws_freeContext(scaler);
    av_frame_free(&video_frame);
    av_frame_free(&audio_frame);
    avcodec_free_context(&video);
    avcodec_free_context(&audio);
    avformat_free_context(output);
    return path;
}

std::filesystem::path createIndependentAudioFixture(
    const std::filesystem::path& root) {
    constexpr std::uint32_t sample_rate = 48000;
    constexpr std::uint16_t channels = 2;
    constexpr std::uint16_t bits_per_sample = 16;
    constexpr std::uint32_t frame_count = sample_rate * 2;
    constexpr std::uint32_t block_align = channels * bits_per_sample / 8;
    constexpr std::uint32_t data_size = frame_count * block_align;
    const auto path = root / "independent-audio.wav";
    const auto write_u16 = [](std::ostream& stream, std::uint16_t value) {
        stream.put(static_cast<char>(value & 0xff));
        stream.put(static_cast<char>((value >> 8) & 0xff));
    };
    const auto write_u32 = [](std::ostream& stream, std::uint32_t value) {
        for (int byte = 0; byte < 4; ++byte) {
            stream.put(static_cast<char>((value >> (byte * 8)) & 0xff));
        }
    };
    std::ofstream file(path, std::ios::binary);
    file.write("RIFF", 4);
    write_u32(file, 36 + data_size);
    file.write("WAVEfmt ", 8);
    write_u32(file, 16);
    write_u16(file, 1);
    write_u16(file, channels);
    write_u32(file, sample_rate);
    write_u32(file, sample_rate * block_align);
    write_u16(file, static_cast<std::uint16_t>(block_align));
    write_u16(file, bits_per_sample);
    file.write("data", 4);
    write_u32(file, data_size);
    for (std::uint32_t index = 0; index < frame_count; ++index) {
        const auto sample = static_cast<std::int16_t>(index < sample_rate / 4
            ? 0
            : 12000);
        write_u16(file, static_cast<std::uint16_t>(sample));
        write_u16(file, static_cast<std::uint16_t>(sample));
    }
    require(static_cast<bool>(file), "Could not write an independent WAV fixture.");
    return path;
}

rendering::RenderJob makeImageJob(
    const OutputChoice& output,
    const std::filesystem::path& image_path,
    const std::filesystem::path& output_path,
    std::uint64_t id,
    std::int64_t duration_frames = 4) {
    rendering::RenderJob job;
    job.id = id;
    job.display_name = QStringLiteral("Fixture %1").arg(id);
    job.settings.output_path = pathToQString(output_path);
    job.settings.container_name = QString::fromStdString(output.container.name);
    job.settings.video_encoder_name = QString::fromStdString(output.video.name);
    job.settings.audio_encoder_name = QString::fromStdString(output.audio.name);
    job.settings.width = 32;
    job.settings.height = 24;
    job.settings.frame_rate = 60.0;
    job.settings.video_bitrate_mbps = 1.0;
    job.settings.audio_bitrate_kbps = 128;
    job.settings.export_audio = true;
    job.settings.gpu_composition_enabled = native_gpu_mode;
    project::ProjectTrack track;
    track.track_id = 1;
    track.name = "V1";
    project::ProjectClip clip;
    clip.source_path = image_path;
    clip.timeline_start_frame = 0;
    clip.duration_frames = duration_frames;
    clip.kind = timeline::ClipKind::Image;
    track.clips.push_back(clip);
    job.project_snapshot.canvas_width = 1920;
    job.project_snapshot.canvas_height = 1080;
    job.project_snapshot.timeline_tracks.push_back(std::move(track));
    return job;
}

void verifyExport(
    const std::filesystem::path& path,
    int expected_video_frames = 8) {
    AVFormatContext* format = nullptr;
    const auto utf8 = path.u8string();
    const std::string native_path(
        reinterpret_cast<const char*>(utf8.data()), utf8.size());
    require(avformat_open_input(&format, native_path.c_str(), nullptr, nullptr) >= 0,
            "The exported file could not be reopened by FFmpeg.");
    require(avformat_find_stream_info(format, nullptr) >= 0,
            "The exported file did not expose its stream metadata.");
    require(av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0) >= 0,
            "The exported file is missing its video stream.");
    require(av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0,
            "The exported file is missing its configured audio stream.");
    avformat_close_input(&format);

    auto decoder = media::VideoPlaybackSession::open(path);
    std::vector<media::VideoFramePtr> decoded_frames;
    int frame_count = 0;
    while (const auto frame = decoder->decode_next_frame()) {
        require(*frame != nullptr && (*frame)->width == 32 && (*frame)->height == 24,
                "An exported video frame had the wrong output dimensions.");
        decoded_frames.push_back(*frame);
        ++frame_count;
    }
    require(frame_count == expected_video_frames,
            "The export did not produce the expected number of output frames.");
    const auto centerPixel = [](const media::VideoFrame& frame, int channel) {
        const auto offset = static_cast<std::size_t>(12 * frame.stride + 16 * 4 + channel);
        return frame.rgba_pixels[offset];
    };
    if (!(centerPixel(*decoded_frames[4], 0) > 65 &&
          centerPixel(*decoded_frames[4], 1) > 65 &&
          centerPixel(*decoded_frames[6], 1) > centerPixel(*decoded_frames[6], 0))) {
        std::fprintf(stderr, "transition frame4=%u,%u,%u frame6=%u,%u,%u\n",
            centerPixel(*decoded_frames[4], 0), centerPixel(*decoded_frames[4], 1),
            centerPixel(*decoded_frames[4], 2), centerPixel(*decoded_frames[6], 0),
            centerPixel(*decoded_frames[6], 1), centerPixel(*decoded_frames[6], 2));
    }
    require(centerPixel(*decoded_frames[4], 0) > 65 &&
                centerPixel(*decoded_frames[4], 1) > 65 &&
                centerPixel(*decoded_frames[6], 1) > centerPixel(*decoded_frames[6], 0),
            "The export did not blend and complete the configured cross-dissolve.");
}

void validateDirectExport(
    const OutputChoice& output,
    const std::filesystem::path& image_path,
    const std::filesystem::path& green_image_path,
    const std::filesystem::path& root) {
    const auto extension = output.container.extensions.empty()
        ? std::string("mkv")
        : output.container.extensions.substr(0, output.container.extensions.find(','));
    const auto target = root / ("completed." + extension);
    auto job = makeImageJob(output, image_path, target, 101);
    auto& track = job.project_snapshot.timeline_tracks.front();
    track.clips.front().duration_frames = 4;
    project::ProjectClip incoming;
    incoming.source_path = green_image_path;
    incoming.timeline_start_frame = 2;
    incoming.duration_frames = 4;
    incoming.kind = timeline::ClipKind::Image;
    track.clips.push_back(incoming);
    track.transitions.push_back(project::ProjectTransition{
        0, 1, timeline::TransitionKind::CrossDissolve, 2});
    std::vector<int> progress;
    std::atomic_bool cancel{false};
    renderJob(
        job, cancel, [&progress](int value) { progress.push_back(value); });
    require(std::filesystem::is_regular_file(target),
            "A completed render did not publish its output file.");
    require(!progress.empty() && progress.back() == 100 &&
                std::is_sorted(progress.begin(), progress.end()),
            "Export progress must be monotonic and reach 100 percent.");
    verifyExport(target, 12);

    const auto sentinel_path = root / ("preserved." + extension);
    const std::string sentinel = "keep previous destination";
    {
        std::ofstream file(sentinel_path, std::ios::binary);
        file << sentinel;
    }
    auto canceled_job = makeImageJob(output, image_path, sentinel_path, 102);
    std::atomic_bool already_canceled{true};
    bool canceled = false;
    try {
        renderJob(canceled_job, already_canceled);
    } catch (const rendering::ExportCanceled&) {
        canceled = true;
    }
    require(canceled && last_metrics.outcome == rendering::ExportOutcome::Canceled && last_metrics.encoded_frames == 0,
            "A canceled export did not stop before publishing or emit its summary.");
    std::ifstream preserved(sentinel_path, std::ios::binary);
    const std::string preserved_contents{
        std::istreambuf_iterator<char>(preserved), std::istreambuf_iterator<char>()};
    require(preserved_contents == sentinel,
            "Canceling an export must preserve the previous destination file.");
    const auto temporary_prefix = sentinel_path.stem().string() + ".rendering-102-";
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        require(entry.path().filename().string().find(temporary_prefix) != 0,
                "Canceling an export must remove its temporary output file.");
    }

    auto failed_job = makeImageJob(output, root / "missing-source.png", sentinel_path, 103);
    std::atomic_bool not_canceled{false};
    bool failed = false;
    try {
        renderJob(failed_job, not_canceled);
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed && last_metrics.outcome == rendering::ExportOutcome::Failed &&
            last_metrics.encoded_frames == 0 && last_metrics.total_nanoseconds > 0,
            "An offline source must fail the export and emit its summary.");
    std::ifstream preserved_after_failure(sentinel_path, std::ios::binary);
    const std::string after_failure{
        std::istreambuf_iterator<char>(preserved_after_failure),
        std::istreambuf_iterator<char>()};
    require(after_failure == sentinel,
            "A failed export must preserve the previous destination file.");
    const auto failed_temporary_prefix = sentinel_path.stem().string() + ".rendering-103-";
    for (const auto& entry : std::filesystem::directory_iterator(root)) {
        require(entry.path().filename().string().find(failed_temporary_prefix) != 0,
                "A failed export must remove its temporary output file.");
    }
}

void validateVisualEffectExport(
    const OutputChoice& output,
    const std::filesystem::path& image_path,
    const std::filesystem::path& root) {
    const auto extension = output.container.extensions.empty()
        ? std::string("mkv")
        : output.container.extensions.substr(0, output.container.extensions.find(','));
    const auto target = root / ("visual-effects." + extension);
    auto job = makeImageJob(output, image_path, target, 111, 2);
    job.project_snapshot.timeline_tracks.front().clips.front().effects = {
        creative_suite::effects::makeDefaultInstance("video.grayscale")};
    std::atomic_bool canceled{false};
    renderJob(job, canceled);

    auto decoder = media::VideoPlaybackSession::open(target);
    const auto frame = decoder->decode_next_frame();
    require(frame.has_value() && *frame != nullptr,
            "The effect export did not contain a decodable video frame.");
    const auto offset = static_cast<std::size_t>(
        (12 * (*frame)->stride) + 16 * 4);
    const auto red = (*frame)->rgba_pixels[offset];
    const auto green = (*frame)->rgba_pixels[offset + 1];
    const auto blue = (*frame)->rgba_pixels[offset + 2];
    const auto maximum = std::max({red, green, blue});
    const auto minimum = std::min({red, green, blue});
    require(maximum - minimum <= 12 && red > 40 && red < 115,
            "Offline export did not apply the grayscale effect to the image clip.");

    const auto disabled_target = root / ("visual-effects-disabled." + extension);
    auto disabled_job = makeImageJob(output, image_path, disabled_target, 112, 2);
    auto disabled_grayscale = creative_suite::effects::makeDefaultInstance(
        "video.grayscale");
    disabled_grayscale.enabled = false;
    disabled_job.project_snapshot.timeline_tracks.front().clips.front().effects = {
        disabled_grayscale};
    renderJob(disabled_job, canceled);
    auto disabled_decoder = media::VideoPlaybackSession::open(disabled_target);
    const auto disabled_frame = disabled_decoder->decode_next_frame();
    require(disabled_frame.has_value() && *disabled_frame != nullptr,
            "The disabled-effect export did not contain a decodable video frame.");

    const auto baseline_target = root / ("visual-effects-baseline." + extension);
    auto baseline_job = makeImageJob(output, image_path, baseline_target, 113, 2);
    renderJob(baseline_job, canceled);
    auto baseline_decoder = media::VideoPlaybackSession::open(baseline_target);
    const auto baseline_frame = baseline_decoder->decode_next_frame();
    require(baseline_frame.has_value() && *baseline_frame != nullptr &&
                (*disabled_frame)->rgba_pixels == (*baseline_frame)->rgba_pixels,
            "Offline export changed the frame for a disabled visual effect.");
}

void validateFusionPreviewExportParity(
    const OutputChoice& output,
    const std::filesystem::path& image_path,
    const std::filesystem::path& root) {
    using namespace fusion::nodes;

    const auto extension = output.container.extensions.empty()
        ? std::string("mkv")
        : output.container.extensions.substr(0, output.container.extensions.find(','));
    const auto overlay_path = root / "fusion-overlay.png";
    QImage overlay(1, 1, QImage::Format_RGBA8888);
    overlay.fill(QColor(20, 40, 240, 128));
    require(overlay.save(pathToQString(overlay_path)),
            "Could not create the deterministic Fusion overlay fixture.");

    auto graph = makePassthroughGraph();
    Node overlay_input;
    overlay_input.id = 3;
    overlay_input.type = NodeType::Input;
    overlay_input.source_path = overlay_path;
    overlay_input.source_is_still = true;
    Node merge;
    merge.id = 4;
    merge.type = NodeType::Merge;
    Node effect;
    effect.id = 5;
    effect.type = NodeType::Effect;
    effect.effect = creative_suite::effects::makeDefaultInstance(
        "video.brightness");
    require(creative_suite::effects::setParameterValue(
                effect.effect, "amount", 4.0),
            "Could not configure the known Fusion effect-node fixture.");
    effect.effect_parameter_keyframes.push_back(
        {"amount", {{0, 4.0}, {3, 40.0}}});
    graph.nodes.push_back(overlay_input);
    graph.nodes.push_back(merge);
    graph.nodes.push_back(effect);
    graph.connections = {{1, 4, 0}, {3, 4, 1}, {4, 5, 0}, {5, 2, 0}};
    graph.next_id = 6;
    require(static_cast<bool>(validate(graph)),
            "The known Fusion Preview/Render parity graph is invalid.");

    const QImage source_image(pathToQString(image_path));
    require(!source_image.isNull(), "Could not read the base Fusion image fixture.");
    const auto rgba_image = source_image.convertToFormat(QImage::Format_RGBA8888);
    std::vector<std::uint8_t> source_pixels(
        static_cast<std::size_t>(rgba_image.width()) *
        static_cast<std::size_t>(rgba_image.height()) * 4U);
    for (int y = 0; y < rgba_image.height(); ++y) {
        const auto row_offset = static_cast<std::size_t>(y) *
            static_cast<std::size_t>(rgba_image.width()) * 4U;
        std::copy_n(rgba_image.constScanLine(y),
                    static_cast<std::size_t>(rgba_image.width()) * 4U,
                    source_pixels.begin() + static_cast<std::ptrdiff_t>(row_offset));
    }

    playback::PlaybackWorker worker;
    std::vector<playback::VideoFramePtr> preview_frames;
    QObject::connect(
        &worker,
        &playback::PlaybackWorker::frameReady,
        [&preview_frames](playback::VideoFramePtr frame, qint64, quint64, quint64) {
            preview_frames.push_back(std::move(frame));
        });
    playback::CompositionLayerSpec preview_layer;
    preview_layer.source_path = pathToQString(image_path);
    preview_layer.frame_rate = 30.0;
    preview_layer.timeline_start_frame = 0;
    preview_layer.segment_frame_count = 4;
    preview_layer.track_index = 0;
    preview_layer.clip_index = 0;
    preview_layer.kind = timeline::ClipKind::Image;
    preview_layer.still_frame = std::make_shared<const media::VideoFrame>(media::VideoFrame{
        rgba_image.width(), rgba_image.height(), rgba_image.width() * 4,
        std::move(source_pixels)});
    preview_layer.node_graph = graph;
    worker.setActiveCompositionClip(0, 0);
    worker.setComposition({preview_layer}, {}, 900);
    worker.renderCompositionFrame(0, 0, 900);
    worker.renderCompositionFrame(3, 3, 900);
    require(preview_frames.size() == 2 && preview_frames[0] != nullptr &&
                preview_frames[1] != nullptr,
            "Preview did not evaluate both frames of the animated Fusion graph.");

    const auto target = root / ("fusion-parity." + extension);
    auto job = makeImageJob(output, image_path, target, 901, 4);
    job.settings.export_audio = false;
    job.project_snapshot.timeline_tracks.front().clips.front().node_graph = graph;
    std::atomic_bool canceled{false};
    renderJob(job, canceled);
    auto decoder = media::VideoPlaybackSession::open(target);
    std::vector<media::VideoFramePtr> rendered_frames;
    for (int frame = 0; frame < 8; ++frame) {
        auto rendered = decoder->decode_next_frame();
        require(rendered.has_value() && *rendered != nullptr,
                "Render did not evaluate every frame of the animated Fusion graph.");
        rendered_frames.push_back(std::move(*rendered));
    }

    const auto center_pixel = [](const media::VideoFrame& frame) {
        const auto offset = static_cast<std::size_t>(frame.height / 2) *
            static_cast<std::size_t>(frame.stride) +
            static_cast<std::size_t>(frame.width / 2) * 4U;
        return std::array<int, 3>{frame.rgba_pixels[offset],
                                   frame.rgba_pixels[offset + 1],
                                   frame.rgba_pixels[offset + 2]};
    };
    for (const auto [preview_index, rendered_index] : {
             std::pair{std::size_t{0}, std::size_t{0}},
             std::pair{std::size_t{1}, std::size_t{6}}}) {
        const auto preview_pixel = center_pixel(*preview_frames[preview_index]);
        const auto rendered_pixel = center_pixel(*rendered_frames[rendered_index]);
        for (std::size_t channel = 0; channel < preview_pixel.size(); ++channel) {
            require(std::abs(preview_pixel[channel] - rendered_pixel[channel]) <= 32,
                    "Preview and Render produced different pixels for an animated Fusion graph.");
        }
    }
}

void validateQueueContinuesAfterFailure(
    const OutputChoice& output,
    const std::filesystem::path& image_path,
    const std::filesystem::path& green_image_path,
    const std::filesystem::path& root) {
    const auto extension = output.container.extensions.empty()
        ? std::string("mkv")
        : output.container.extensions.substr(0, output.container.extensions.find(','));
    auto failed = makeImageJob(output, root / "missing.png", root / ("failure." + extension), 201);
    auto next = makeImageJob(output, image_path, root / ("queue-success." + extension), 202);
    auto cpu_next = makeImageJob(output, image_path, root / ("queue-cpu." + extension), 203);
    cpu_next.settings.gpu_composition_enabled = false;
    auto already_completed = makeImageJob(
        output, root / "completed-source.png", root / ("already-done." + extension), 200);
    already_completed.status = rendering::RenderJobStatus::Completed;
    auto& track = next.project_snapshot.timeline_tracks.front();
    track.clips.front().duration_frames = 2;
    project::ProjectClip incoming;
    incoming.source_path = green_image_path;
    incoming.timeline_start_frame = 0;
    incoming.duration_frames = 2;
    incoming.kind = timeline::ClipKind::Image;
    track.clips.push_back(incoming);
    track.transitions.push_back(project::ProjectTransition{
        0, 1, timeline::TransitionKind::CrossDissolve, 2});

    ui::RenderQueueController controller;
    QEventLoop loop;
    std::vector<qulonglong> started;
    std::vector<qulonglong> completed;
    std::vector<qulonglong> failed_ids;
    std::vector<qulonglong> progress_ids;
    QObject::connect(&controller, &ui::RenderQueueController::jobStarted,
                     &loop, [&started](qulonglong id) { started.push_back(id); });
    QObject::connect(&controller, &ui::RenderQueueController::jobCompleted,
                     &loop, [&completed](qulonglong id) { completed.push_back(id); });
    QObject::connect(&controller, &ui::RenderQueueController::jobFailed,
                     &loop, [&failed_ids](qulonglong id, const QString&, const QString&) {
                         failed_ids.push_back(id);
                     });
    QObject::connect(&controller, &ui::RenderQueueController::jobProgress,
                     &loop, [&progress_ids](qulonglong id, int) { progress_ids.push_back(id); });
    QObject::connect(&controller, &ui::RenderQueueController::queueFinished,
                     &loop, &QEventLoop::quit);
    require(controller.start({already_completed, failed, next, cpu_next}), "The render queue did not start.");
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();
    require(!controller.isRunning(), "The render queue worker did not finish.");
    require(started == std::vector<qulonglong>{201, 202, 203} &&
                failed_ids == std::vector<qulonglong>{201} &&
                completed == std::vector<qulonglong>{202, 203},
            "Completed jobs must be skipped and a failed job must not stop later jobs.");
    require(std::find(progress_ids.begin(), progress_ids.end(), 202) != progress_ids.end(),
            "The successful queued job did not report progress.");
    require(std::filesystem::is_regular_file(pathFromQString(next.settings.output_path)),
            "The later successful job did not publish its output.");
}

void validateGapsTextAndKeyframes(
    const OutputChoice& output,
    const std::filesystem::path& image_path,
    const std::filesystem::path& green_image_path,
    const std::filesystem::path& root) {
    const auto extension = output.container.extensions.empty()
        ? std::string("mkv")
        : output.container.extensions.substr(0, output.container.extensions.find(','));
    std::atomic_bool canceled{false};
    const auto decodeFrames = [](const std::filesystem::path& path) {
        auto decoder = media::VideoPlaybackSession::open(path);
        std::vector<media::VideoFramePtr> frames;
        while (const auto frame = decoder->decode_next_frame()) {
            frames.push_back(*frame);
        }
        return frames;
    };
    const auto centerPixel = [](const media::VideoFrame& frame, int channel) {
        const auto offset = static_cast<std::size_t>(12 * frame.stride + 16 * 4 + channel);
        return frame.rgba_pixels[offset];
    };

    auto gaps = makeImageJob(
        output, image_path, root / ("gaps." + extension), 501, 1);
    gaps.settings.export_audio = false;
    project::ProjectClip after_gap;
    after_gap.source_path = green_image_path;
    after_gap.timeline_start_frame = 3;
    after_gap.duration_frames = 1;
    after_gap.kind = timeline::ClipKind::Image;
    gaps.project_snapshot.timeline_tracks.front().clips.push_back(after_gap);
    renderJob(gaps, canceled);
    const auto gap_frames = decodeFrames(pathFromQString(gaps.settings.output_path));
    require(gap_frames.size() == 8 &&
                centerPixel(*gap_frames[2], 0) < 60 &&
                centerPixel(*gap_frames[2], 1) < 60 &&
                centerPixel(*gap_frames[2], 2) < 60,
            "An uncovered Timeline interval was not rendered as black frames.");

    auto keyed = makeImageJob(
        output, image_path, root / ("keyframes." + extension), 502, 4);
    keyed.settings.export_audio = false;
    auto& keyed_clip = keyed.project_snapshot.timeline_tracks.front().clips.front();
    keyed_clip.keyframes.position_x = {{0, 0.5}, {3, 0.0}};
    renderJob(keyed, canceled);
    const auto keyed_frames = decodeFrames(pathFromQString(keyed.settings.output_path));
    require(keyed_frames.size() == 8 &&
                centerPixel(*keyed_frames[0], 0) > 80 &&
                centerPixel(*keyed_frames[6], 0) < 60,
            "Export did not evaluate the clip position keyframes.");

    auto text = makeImageJob(
        output, image_path, root / ("text." + extension), 503, 4);
    text.settings.export_audio = false;
    auto& text_clips = text.project_snapshot.timeline_tracks.front().clips;
    text_clips.clear();
    project::ProjectClip title;
    title.timeline_start_frame = 0;
    title.duration_frames = 4;
    title.kind = timeline::ClipKind::Text;
    title.text.content = "Render";
    title.text.font_size_pixels = 16.0;
    text_clips.push_back(title);
    renderJob(text, canceled);
    const auto text_frames = decodeFrames(pathFromQString(text.settings.output_path));
    require(!text_frames.empty(), "A text-only Timeline did not produce video frames.");
    bool visible_text = false;
    const auto& frame = *text_frames.front();
    for (int y = 0; y < frame.height && !visible_text; ++y) {
        for (int x = 0; x < frame.width; ++x) {
            const auto offset = static_cast<std::size_t>(y * frame.stride + x * 4);
            if (frame.rgba_pixels[offset] > 50 ||
                frame.rgba_pixels[offset + 1] > 50 ||
                frame.rgba_pixels[offset + 2] > 50) {
                visible_text = true;
                break;
            }
        }
    }
    require(visible_text, "A text clip did not appear in the exported composition.");
}

void validateQueueCancellationStopsLaterJobs(
    const OutputChoice& output,
    const std::filesystem::path& image_path,
    const std::filesystem::path& root) {
    const auto extension = output.container.extensions.empty()
        ? std::string("mkv")
        : output.container.extensions.substr(0, output.container.extensions.find(','));
    auto current = makeImageJob(
        output, image_path, root / ("cancelled." + extension), 301, 100000);
    auto later = makeImageJob(
        output, image_path, root / ("after-cancel." + extension), 302);

    ui::RenderQueueController controller;
    QEventLoop loop;
    std::vector<qulonglong> started;
    std::vector<qulonglong> canceled;
    QObject::connect(&controller, &ui::RenderQueueController::jobStarted,
                     &loop, [&started, &controller](qulonglong id) {
                         started.push_back(id);
                         if (id == 301) controller.cancel();
                     });
    QObject::connect(&controller, &ui::RenderQueueController::jobCanceled,
                     &loop, [&canceled](qulonglong id) { canceled.push_back(id); });
    QObject::connect(&controller, &ui::RenderQueueController::queueFinished,
                     &loop, &QEventLoop::quit);
    require(controller.start({current, later}), "The cancelable queue did not start.");
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();
    require(!controller.isRunning() && started == std::vector<qulonglong>{301} &&
                canceled == std::vector<qulonglong>{301},
            "Cancel must stop the active job and leave later jobs unstarted.");
    require(!std::filesystem::exists(pathFromQString(current.settings.output_path)) &&
                !std::filesystem::exists(pathFromQString(later.settings.output_path)),
            "Canceled and unstarted jobs must not publish output files.");

    auto pending = makeImageJob(
        output, image_path, root / ("cancel-before-start." + extension), 303, 100000);
    auto pending_later = makeImageJob(
        output, image_path, root / ("after-cancel-before-start." + extension), 304);
    ui::RenderQueueController pending_controller;
    QEventLoop pending_loop;
    std::vector<qulonglong> pending_started;
    std::vector<qulonglong> pending_canceled;
    QObject::connect(&pending_controller, &ui::RenderQueueController::jobStarted,
                     &pending_loop, [&pending_started](qulonglong id) {
                         pending_started.push_back(id);
                     });
    QObject::connect(&pending_controller, &ui::RenderQueueController::jobCanceled,
                     &pending_loop, [&pending_canceled](qulonglong id) {
                         pending_canceled.push_back(id);
                     });
    QObject::connect(&pending_controller, &ui::RenderQueueController::queueFinished,
                     &pending_loop, &QEventLoop::quit);
    require(pending_controller.start({pending, pending_later}),
            "The not-yet-started queue did not start.");
    pending_controller.cancel();
    QTimer::singleShot(15000, &pending_loop, &QEventLoop::quit);
    pending_loop.exec();
    require(!pending_controller.isRunning() &&
                (pending_started.empty() ||
                 pending_started == std::vector<qulonglong>{303}) &&
                pending_canceled == std::vector<qulonglong>{303},
            "Canceling as the queue starts must mark its first job canceled and leave later jobs unstarted.");
    require(!std::filesystem::exists(pathFromQString(pending.settings.output_path)) &&
                !std::filesystem::exists(pathFromQString(pending_later.settings.output_path)),
            "Canceling as the queue starts must not publish either destination.");
}

double decodedAudioRms(const std::filesystem::path& path) {
    auto decoder = media::AudioPlaybackSession::open(path, {48000, 2});
    require(decoder->has_audio(), "The rendered file did not contain an audio stream.");
    long double sum_squares = 0.0L;
    std::size_t count = 0;
    for (int attempt = 0; attempt < 100 && count < 24000U * 2U; ++attempt) {
        auto chunk = decoder->decode_samples(4096);
        if (!chunk.has_value()) break;
        for (const auto sample : chunk->samples) {
            const long double normalized = static_cast<long double>(sample) / 32768.0L;
            sum_squares += normalized * normalized;
            ++count;
        }
    }
    return count == 0 ? 0.0 : std::sqrt(static_cast<double>(sum_squares / count));
}

double decodedAudioRmsRange(
    const std::filesystem::path& path,
    std::int64_t start_sample,
    std::size_t sample_count) {
    auto decoder = media::AudioPlaybackSession::open(path, {48000, 2});
    require(decoder->has_audio(), "The rendered file did not contain an audio stream.");
    decoder->seek_to_sample_index(start_sample);
    const auto target_values = sample_count * 2U;
    long double sum_squares = 0.0L;
    std::size_t count = 0;
    while (count < target_values) {
        auto chunk = decoder->decode_samples(
            std::min<std::size_t>(4096, (target_values - count + 1U) / 2U));
        if (!chunk.has_value()) break;
        for (const auto sample : chunk->samples) {
            if (count >= target_values) break;
            const long double normalized = static_cast<long double>(sample) / 32768.0L;
            sum_squares += normalized * normalized;
            ++count;
        }
    }
    return count == 0 ? 0.0 : std::sqrt(static_cast<double>(sum_squares / count));
}

void validateEmbeddedAudioMixing(
    const OutputChoice& output,
    const std::filesystem::path& root) {
    const auto extension = output.container.extensions.empty()
        ? std::string("mkv")
        : output.container.extensions.substr(0, output.container.extensions.find(','));
    const auto source = createVideoWithAudioFixture(root);
    const auto decodeFrames = [](const std::filesystem::path& path) {
        auto decoder = media::VideoPlaybackSession::open(path);
        std::vector<media::VideoFramePtr> frames;
        while (const auto frame = decoder->decode_next_frame()) {
            frames.push_back(*frame);
        }
        return frames;
    };
    const auto centerPixel = [](const media::VideoFrame& frame, int channel) {
        const auto offset = static_cast<std::size_t>(
            (frame.height / 2) * frame.stride + (frame.width / 2) * 4 + channel);
        return frame.rgba_pixels[offset];
    };
    auto job = makeImageJob(
        output, source, root / ("audio-mix." + extension), 401, 24);
    job.project_snapshot.timeline_frame_rate = {24, 1};
    auto& track = job.project_snapshot.timeline_tracks.front();
    track.audio_gain = 0.5;
    track.clips.front().kind = timeline::ClipKind::Video;
    track.clips.front().source_start_frame = 30;
    track.clips.front().source_duration_frames = 30;
    track.clips.front().audio_gain = 0.5;
    std::atomic_bool canceled{false};
    renderJob(job, canceled);
    auto video_decoder = media::VideoPlaybackSession::open(
        pathFromQString(job.settings.output_path));
    std::vector<media::VideoFramePtr> output_frames;
    while (const auto frame = video_decoder->decode_next_frame()) {
        output_frames.push_back(*frame);
    }
    require(output_frames.size() == 60,
            "A 24 fps timeline exported at 60 fps did not preserve one second of duration.");
    const auto center_offset = static_cast<std::size_t>(
        (output_frames[30]->height / 2) * output_frames[30]->stride +
        (output_frames[30]->width / 2) * 4);
    require(output_frames[30]->rgba_pixels[center_offset + 1] >
                output_frames[30]->rgba_pixels[center_offset],
            "The 60 fps export did not map its frame to the trimmed source position at 30 fps.");
    const auto audible_rms = decodedAudioRms(pathFromQString(job.settings.output_path));
    require(audible_rms > 0.025 && audible_rms < 0.05,
            "Embedded video audio was not mixed with the configured track and clip gains.");

    auto linked_job = makeImageJob(
        output, source, root / ("linked-video-audio." + extension), 407, 24);
    linked_job.project_snapshot.timeline_frame_rate = {24, 1};
    auto& linked_video_track = linked_job.project_snapshot.timeline_tracks.front();
    linked_video_track.audio_gain = 0.5;
    auto& linked_video_clip = linked_video_track.clips.front();
    linked_video_clip.kind = timeline::ClipKind::Video;
    linked_video_clip.source_start_frame = 30;
    linked_video_clip.source_duration_frames = 30;
    linked_video_clip.audio_gain = 0.5;
    linked_video_clip.linked_clip_id = 2;
    linked_video_clip.audio_extracted = true;
    project::ProjectTrack linked_audio_track;
    linked_audio_track.track_id = 2;
    linked_audio_track.name = "Audio 1";
    linked_audio_track.kind = timeline::TrackKind::Audio;
    linked_audio_track.audio_gain = 0.5;
    project::ProjectClip linked_audio_clip;
    linked_audio_clip.clip_id = 2;
    linked_audio_clip.source_path = source;
    linked_audio_clip.kind = timeline::ClipKind::Audio;
    linked_audio_clip.duration_frames = 24;
    linked_audio_clip.source_start_time_us = 1000000;
    linked_audio_clip.source_duration_time_us = 1000000;
    linked_audio_clip.audio_gain = 0.5;
    linked_audio_clip.linked_clip_id = linked_video_clip.clip_id;
    linked_audio_track.clips.push_back(linked_audio_clip);
    linked_job.project_snapshot.timeline_tracks.push_back(linked_audio_track);
    renderJob(linked_job, canceled);
    const auto linked_rms = decodedAudioRms(
        pathFromQString(linked_job.settings.output_path));
    require(std::abs(linked_rms - audible_rms) < 0.001,
            "A linked video Audio companion was omitted or mixed a second copy of the embedded stream.");

    auto dissolve_job = makeImageJob(
        output, source, root / ("audio-dissolve." + extension), 403, 24);
    dissolve_job.project_snapshot.timeline_frame_rate = {24, 1};
    auto& dissolve_track = dissolve_job.project_snapshot.timeline_tracks.front();
    auto& outgoing = dissolve_track.clips.front();
    outgoing.kind = timeline::ClipKind::Video;
    outgoing.source_start_frame = 0;
    outgoing.source_duration_frames = 30;
    project::ProjectClip incoming;
    incoming.source_path = source;
    incoming.timeline_start_frame = 16;
    incoming.source_start_frame = 35;
    incoming.duration_frames = 20;
    incoming.source_duration_frames = 25;
    incoming.kind = timeline::ClipKind::Video;
    dissolve_track.clips.push_back(incoming);
    dissolve_track.transitions.push_back(project::ProjectTransition{
        0, 1, timeline::TransitionKind::CrossDissolve, 8});
    renderJob(dissolve_job, canceled);
    const auto dissolve_path = pathFromQString(dissolve_job.settings.output_path);
    const auto before_cut_rms = decodedAudioRmsRange(dissolve_path, 28800, 14400);
    const auto after_cut_rms = decodedAudioRmsRange(dissolve_path, 50400, 24000);
    require(before_cut_rms < 0.04 && after_cut_rms > 0.12 &&
                after_cut_rms > before_cut_rms * 4.0,
            "Cross Dissolve audio did not stay on the original hard cut or start from the incoming local frame D.");

    track.audio_muted = true;
    job.id = 402;
    job.settings.output_path = pathToQString(root / ("audio-muted." + extension));
    renderJob(job, canceled);
    require(decodedAudioRms(pathFromQString(job.settings.output_path)) < 0.001,
            "A muted audio track still contributed samples to the export.");

    const auto independent_source = createIndependentAudioFixture(root);
    auto independent_job = makeImageJob(
        output, source, root / ("independent-audio-mix." + extension), 404, 24);
    independent_job.project_snapshot.timeline_frame_rate = {24, 1};
    auto& visual_track = independent_job.project_snapshot.timeline_tracks.front();
    visual_track.clips.front().kind = timeline::ClipKind::Video;
    visual_track.clips.front().duration_frames = 24;
    visual_track.clips.front().source_start_frame = 0;
    visual_track.clips.front().source_duration_frames = 24;
    visual_track.clips.front().audio_gain = 0.5;
    project::ProjectTrack independent_track;
    independent_track.track_id = 2;
    independent_track.name = "Audio 1";
    independent_track.kind = timeline::TrackKind::Audio;
    independent_track.audio_gain = 0.5;
    project::ProjectClip independent_clip;
    independent_clip.clip_id = 2;
    independent_clip.source_path = independent_source;
    independent_clip.kind = timeline::ClipKind::Audio;
    independent_clip.duration_frames = 48;
    independent_clip.source_start_time_us = 0;
    independent_clip.source_duration_time_us = 2000000;
    independent_clip.audio_gain = 0.5;
    independent_track.clips.push_back(independent_clip);
    independent_job.project_snapshot.timeline_tracks.push_back(independent_track);
    renderJob(independent_job, canceled);
    const auto independent_output = pathFromQString(
        independent_job.settings.output_path);
    const auto independent_frames = decodeFrames(independent_output);
    require(independent_frames.size() == 120 &&
                centerPixel(*independent_frames[90], 0) < 2 &&
                centerPixel(*independent_frames[90], 1) < 2 &&
                decodedAudioRmsRange(independent_output, 0, 24000) > 0.04 &&
                decodedAudioRmsRange(independent_output, 72000, 24000) > 0.04,
            "Independent and embedded audio were not mixed through the audio tail with black frames.");

    auto crossfade_job = makeImageJob(
        output, source, root / ("independent-audio-crossfade." + extension),
        408, 24);
    crossfade_job.project_snapshot.timeline_frame_rate = {24, 1};
    auto& crossfade_video_track = crossfade_job.project_snapshot.timeline_tracks.front();
    crossfade_video_track.clips.front().kind = timeline::ClipKind::Video;
    crossfade_video_track.clips.front().source_duration_frames = 24;
    crossfade_video_track.audio_muted = true;
    project::ProjectTrack crossfade_audio_track;
    crossfade_audio_track.track_id = 2;
    crossfade_audio_track.name = "Audio 1";
    crossfade_audio_track.kind = timeline::TrackKind::Audio;
    crossfade_audio_track.audio_gain = 0.5;
    project::ProjectClip outgoing_audio;
    outgoing_audio.clip_id = 2;
    outgoing_audio.source_path = independent_source;
    outgoing_audio.kind = timeline::ClipKind::Audio;
    outgoing_audio.duration_frames = 48;
    outgoing_audio.source_duration_time_us = 2'000'000;
    outgoing_audio.audio_gain = 0.5;
    project::ProjectClip incoming_audio = outgoing_audio;
    incoming_audio.clip_id = 3;
    incoming_audio.timeline_start_frame = 36;
    crossfade_audio_track.clips = {outgoing_audio, incoming_audio};
    crossfade_audio_track.transitions.push_back(project::ProjectTransition{
        0, 1, timeline::TransitionKind::AudioCrossfade, 12});
    crossfade_job.project_snapshot.timeline_tracks.push_back(crossfade_audio_track);
    renderJob(crossfade_job, canceled);
    const auto crossfade_output = pathFromQString(
        crossfade_job.settings.output_path);
    const auto outside_crossfade_rms = decodedAudioRmsRange(
        crossfade_output, 57600, 4800);
    const auto inside_crossfade_rms = decodedAudioRmsRange(
        crossfade_output, 86400, 4800);
    const auto after_crossfade_rms = decodedAudioRmsRange(
        crossfade_output, 105600, 4800);
    require(outside_crossfade_rms > 0.05 &&
                inside_crossfade_rms > outside_crossfade_rms * 1.2 &&
                after_crossfade_rms > outside_crossfade_rms * 0.85 &&
                after_crossfade_rms < outside_crossfade_rms * 1.15,
            "Export did not mix the Audio Crossfade with an equal-power gain curve.");

    independent_track.audio_muted = true;
    independent_job.id = 405;
    independent_job.project_snapshot.timeline_tracks.back() = independent_track;
    independent_job.settings.output_path = pathToQString(
        root / ("independent-audio-muted." + extension));
    renderJob(independent_job, canceled);
    require(decodedAudioRmsRange(
                pathFromQString(independent_job.settings.output_path),
                72000, 24000) < 0.001,
            "Muting an independent Audio track did not silence its audio tail.");

    independent_job.id = 406;
    independent_job.settings.export_audio = false;
    independent_job.settings.output_path = pathToQString(
        root / ("independent-audio-disabled." + extension));
    renderJob(independent_job, canceled);
    auto no_audio_decoder = media::AudioPlaybackSession::open(
        pathFromQString(independent_job.settings.output_path), {48000, 2});
    require(!no_audio_decoder->has_audio(),
            "Audio-disabled export still emitted an audio stream.");
}

void validateConfiguredFullResolutionExport(
    const OutputChoice& output,
    const std::filesystem::path& image_path,
    const std::filesystem::path& root) {
    const auto extension = output.container.extensions.empty()
        ? std::string("mkv")
        : output.container.extensions.substr(0, output.container.extensions.find(','));
    auto job = makeImageJob(
        output, image_path, root / ("full-resolution." + extension), 104, 1);
    for (const auto size : native_gpu_mode ? std::vector<QSize>{{1920, 1080}, {2560, 1440}, {3840, 2160}, {2160, 3840}}
                                          : std::vector<QSize>{{1920, 1080}}) {
    job.settings.width = size.width();
    job.settings.height = size.height();
    job.settings.export_audio = false;
    job.project_snapshot.canvas_width = 1920;
    job.project_snapshot.canvas_height = 1080;

    std::atomic_bool canceled{false};
    renderJob(job, canceled);
    auto decoder = media::VideoPlaybackSession::open(
        pathFromQString(job.settings.output_path));
    const auto first_frame = decoder->decode_next_frame();
    require(first_frame.has_value() && *first_frame != nullptr &&
                (*first_frame)->width == size.width() && (*first_frame)->height == size.height(),
            "Playback Preview Quality must not reduce the configured offline export resolution.");
    }

    auto portrait_job = makeImageJob(
        output, image_path, root / ("portrait-project." + extension), 105, 1);
    portrait_job.settings.width = 1080;
    portrait_job.settings.height = 1920;
    portrait_job.settings.export_audio = false;
    portrait_job.project_snapshot.canvas_width = 1080;
    portrait_job.project_snapshot.canvas_height = 1920;
    std::atomic_bool canceled{false};
    renderJob(portrait_job, canceled);
    auto portrait_decoder = media::VideoPlaybackSession::open(
        pathFromQString(portrait_job.settings.output_path));
    const auto portrait_frame = portrait_decoder->decode_next_frame();
    require(portrait_frame.has_value() && *portrait_frame != nullptr &&
                (*portrait_frame)->width == 1080 && (*portrait_frame)->height == 1920,
            "A portrait project did not render at its configured portrait output resolution.");
}

class FaultGpu final : public rendering::ExportGpuCompositor {
public:
    FaultGpu(std::vector<creative_suite::composition::OpenGlCompositionStatus> statuses, int& calls, bool malformed = false)
        : statuses_(std::move(statuses)), calls_(calls), malformed_(malformed) {}
    creative_suite::composition::OpenGlCompositionResult compose(int width, int height,
        const std::vector<creative_suite::composition::CompositionLayer>& layers,
        const creative_suite::composition::OpenGlFrameCompositor::CancellationPredicate&,
        creative_suite::composition::OpenGlCompositionTimings*) override {
        using Status = creative_suite::composition::OpenGlCompositionStatus;
        const auto status = calls_ < static_cast<int>(statuses_.size()) ? statuses_[calls_] : Status::Complete;
        ++calls_;
        if (malformed_) return {Status::Complete, creative_suite::media::RgbaFrame{width, height, 1, {}}};
        if (status == Status::Complete) return {status, creative_suite::composition::FrameCompositor::compose(width, height, layers)};
        return {status, {}, "controlled-export-fault", "Controlled export GPU failure.", -37};
    }
private:
    std::vector<creative_suite::composition::OpenGlCompositionStatus> statuses_;
    int& calls_;
    bool malformed_;
};

void validateGpuFallback(const OutputChoice& output, const std::filesystem::path& image, const std::filesystem::path& root) {
    using Status = creative_suite::composition::OpenGlCompositionStatus;
    auto job = makeImageJob(output, image, root / "gpu-fault.mkv", 400);
    job.settings.gpu_composition_enabled = true;
    std::atomic_bool canceled{false};
    for (auto statuses : {std::vector{Status::Unsupported}, std::vector{Status::Failed},
                          std::vector{Status::Complete, Status::Failed}, std::vector{Status::Busy}, std::vector{Status::Cancelled}}) {
        int calls = 0, warnings = 0, summaries = 0;
        bool warning_logged = false, warning_code_valid = false;
        rendering::OfflineExportMetrics metrics;
        rendering::OfflineExportOptions options;
        options.gpu_factory = [&](QOffscreenSurface*) { return std::make_unique<FaultGpu>(statuses, calls); };
        options.warning_callback = [&](const auto&, auto code) {
            ++warnings; warning_code_valid = code == -37 || statuses[0] == Status::Busy;
            std::ifstream log(logging::Logger::instance().log_path());
            const std::string contents{std::istreambuf_iterator<char>(log), {}};
            warning_logged = contents.find("controlled-export-fault") != std::string::npos || statuses[0] == Status::Busy;
        };
        options.metrics_callback = [&](const auto& result) { metrics = result; ++summaries; };
        std::ofstream(job.settings.output_path.toStdString(), std::ios::binary) << "previous output";
        bool was_canceled = false;
        try { rendering::OfflineExportRenderer::render(job, canceled, {}, options); }
        catch (const rendering::ExportCanceled&) { was_canceled = true; }
        require(summaries == 1 && metrics.total_nanoseconds > 0, "Export failed to summarize an outcome.");
        if (statuses[0] == Status::Cancelled) {
            std::ifstream previous(job.settings.output_path.toStdString());
            const std::string contents{std::istreambuf_iterator<char>(previous), {}};
            require(was_canceled && warnings == 0 && metrics.outcome == rendering::ExportOutcome::Canceled &&
                contents == "previous output", "GPU cancellation damaged previous output or reported an error.");
        } else {
            const bool unsupported = statuses[0] == Status::Unsupported;
            const bool mid_failure = statuses[0] == Status::Complete;
            require(!was_canceled && warning_logged && warning_code_valid && metrics.outcome == rendering::ExportOutcome::Completed && warnings == 1 &&
                metrics.encoded_frames == 8 && metrics.cpu_frames + metrics.gpu_frames == 8 &&
                metrics.cpu_frames == (unsupported ? 1 : mid_failure ? 7 : 8) && metrics.fallback_frames == metrics.cpu_frames &&
                calls == (unsupported ? 8 : mid_failure ? 2 : 1) &&
                metrics.gpu_failures == (unsupported ? 0 : 1), "GPU fallback dropped frames or retried a technical failure.");
        }
    }
    // Default adapter with no GUI surface must keep exporting through CPU.
    rendering::OfflineExportOptions unavailable;
    rendering::OfflineExportMetrics unavailable_metrics;
    unavailable.metrics_callback = [&](const auto& metrics) { unavailable_metrics = metrics; };
    rendering::OfflineExportRenderer::render(job, canceled, {}, unavailable);
    require(unavailable_metrics.cpu_frames == 8 && unavailable_metrics.gpu_failures == 1,
        "Missing export context did not latch CPU fallback.");
    int factories = 0, calls = 0;
    rendering::OfflineExportOptions retry;
    retry.gpu_factory = [&](QOffscreenSurface*) {
        ++factories; calls = 0;
        return std::make_unique<FaultGpu>(factories == 1 ? std::vector{Status::Failed} : std::vector<Status>{}, calls);
    };
    rendering::OfflineExportRenderer::render(job, canceled, {}, retry);
    rendering::OfflineExportRenderer::render(job, canceled, {}, retry);
    require(factories == 2 && calls == 8, "A retry did not create a fresh export GPU adapter.");
    rendering::OfflineExportOptions throwing;
    throwing.gpu_factory = [](QOffscreenSurface*) -> std::unique_ptr<rendering::ExportGpuCompositor> {
        throw std::runtime_error("Controlled adapter construction failure.");
    };
    throwing.metrics_callback = [&](const auto& metrics) { unavailable_metrics = metrics; };
    rendering::OfflineExportRenderer::render(job, canceled, {}, throwing);
    require(unavailable_metrics.gpu_failures == 1 && unavailable_metrics.cpu_frames == 8,
        "Adapter construction exception prevented CPU fallback.");
    calls = 0;
    rendering::OfflineExportOptions malformed;
    malformed.gpu_factory = [&](QOffscreenSurface*) { return std::make_unique<FaultGpu>(std::vector<Status>{}, calls, true); };
    malformed.metrics_callback = [&](const auto& metrics) { unavailable_metrics = metrics; };
    malformed.warning_callback = [](const auto&, auto) { throw std::runtime_error("Controlled warning callback failure."); };
    rendering::OfflineExportRenderer::render(job, canceled, {}, malformed);
    require(calls == 1 && unavailable_metrics.cpu_frames == 8 && unavailable_metrics.gpu_failures == 1,
        "An incomplete GPU frame or warning callback failure broke export fallback.");
    malformed.metrics_callback = [](const auto&) { throw std::runtime_error("Controlled metrics callback failure."); };
    rendering::OfflineExportRenderer::render(job, canceled, {}, malformed);
    std::ofstream(job.settings.output_path.toStdString(), std::ios::binary) << "previous output";
    rendering::OfflineExportMetrics cancellation;
    rendering::OfflineExportOptions cancel_options;
    cancel_options.metrics_callback = [&](const auto& metrics) { cancellation = metrics; };
    bool canceled_after_frame = false;
    try {
        rendering::OfflineExportRenderer::render(job, canceled, [&](int) { canceled.store(true); }, cancel_options);
    } catch (const rendering::ExportCanceled&) { canceled_after_frame = true; }
    canceled.store(false);
    require(canceled_after_frame && cancellation.outcome == rendering::ExportOutcome::Canceled && cancellation.encoded_frames == 1,
        "Cancellation after frame submission omitted its partial summary.");
}

void validateActiveQueueShutdown(const OutputChoice& output, const std::filesystem::path& image, const std::filesystem::path& root) {
    auto job = makeImageJob(output, image, root / "closed-queue.mkv", 450, 100000);
    std::ofstream(job.settings.output_path.toStdString(), std::ios::binary) << "previous output";
    QEventLoop loop;
    bool first_frame = false;
    {
        ui::RenderQueueController controller;
        QObject::connect(&controller, &ui::RenderQueueController::jobProgress, &loop,
            [&](qulonglong, int) { first_frame = true; loop.quit(); });
        require(controller.start({job}), "Shutdown fixture did not start.");
        QTimer::singleShot(30000, &loop, &QEventLoop::quit); loop.exec();
        require(first_frame && controller.isRunning(), "Shutdown fixture did not retain active worker resources.");
    }
    QCoreApplication::processEvents();
    std::ifstream previous(job.settings.output_path.toStdString());
    const std::string contents{std::istreambuf_iterator<char>(previous), {}};
    require(contents == "previous output", "Closing a render queue replaced the prior output.");
}

void validateLosslessParity(const OutputChoice& output, const std::filesystem::path& image, const std::filesystem::path& root) {
    if (!native_gpu_mode) return;
    require(output.video.name == "ffv1", "Native pixel export comparison requires the available FFV1 lossless encoder.");
    auto cpu = makeImageJob(output, image, root / "lossless-cpu.mkv", 550);
    cpu.settings.gpu_composition_enabled = false;
    cpu.project_snapshot.timeline_tracks[0].clips[0].transform.rotation_degrees = 23;
    auto gpu = cpu; gpu.id = 551; gpu.settings.output_path = pathToQString(root / "lossless-gpu.mkv");
    gpu.settings.gpu_composition_enabled = true;
    std::atomic_bool cancel{false}; renderJob(cpu, cancel); renderJob(gpu, cancel);
    auto a = media::VideoPlaybackSession::open(pathFromQString(cpu.settings.output_path));
    auto b = media::VideoPlaybackSession::open(pathFromQString(gpu.settings.output_path));
    int frames = 0;
    while (const auto expected = a->decode_next_frame()) {
        const auto actual = b->decode_next_frame();
        require(actual && *actual && (*expected)->width == (*actual)->width && (*expected)->height == (*actual)->height,
            "Lossless GPU output duration or geometry differs.");
        for (std::size_t i = 0; i < (*expected)->rgba_pixels.size(); ++i)
            require(std::abs(int((*expected)->rgba_pixels[i]) - int((*actual)->rgba_pixels[i])) <= (i % 4 == 3 ? 0 : 6),
                "Decoded lossless CPU/GPU output differs after encoder color conversion.");
        ++frames;
    }
    require(frames == 8 && !b->decode_next_frame(), "Lossless export dropped or duplicated frames.");
}

#ifdef CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS
void validatePublishedImage(const OutputChoice& output, const std::filesystem::path& root) {
    const auto source = root / "producer-source.png", published = root / "published.png";
    QImage pixels(16, 16, QImage::Format_RGBA8888); pixels.fill(QColor(150, 90, 230));
    require(pixels.save(pathToQString(source)), "Producer source save failed.");
    image_editor::ImageDocumentSession producer;
    QString error;
    require(producer.createCanvas(QSize(16, 16), Qt::transparent, &error) &&
        producer.importRasterImages(image_editor::prepareRasterImport({pathToQString(source)}).images, {}, &error) &&
        producer.addLayerMask(producer.selectedLayerId()) &&
        producer.applyLayerMaskEraseStroke({QPointF(8, 8)}, 4, &error) && producer.exportImage(pathToQString(published), &error),
        "Masked PNG publication failed.");
    auto job = makeImageJob(output, published, root / "published-export.mkv", 500, 1);
    // The original source remains unchanged; export resolves the linked publication.
    job.project_snapshot.timeline_tracks[0].clips[0].source_path = source;
    job.project_snapshot.timeline_tracks[0].clips[0].image_editor_variant = media::LinkedImageReference{
        "export-publication", root / "producer.cimg", published};
    std::atomic_bool cancel{false};
    renderJob(job, cancel);
    auto decoder = media::VideoPlaybackSession::open(pathFromQString(job.settings.output_path));
    auto old = *decoder->decode_next_frame(); decoder.reset();
    require(producer.applyLayerMaskEraseStroke({QPointF(3, 3)}, 7, &error) && producer.exportImage(pathToQString(published), &error),
        "Masked PNG republication failed.");
    renderJob(job, cancel);
    decoder = media::VideoPlaybackSession::open(pathFromQString(job.settings.output_path));
    auto refreshed = *decoder->decode_next_frame();
    require(old->rgba_pixels != refreshed->rgba_pixels, "Export reused stale published Image Editor pixels.");
}
#endif

void benchmarkExport(const OutputChoice& output, const std::filesystem::path& image, const std::filesystem::path& root) {
    compare_before_encoding = false;
    QImage background(1920, 1080, QImage::Format_RGBA8888);
    QImage transparent(1280, 720, QImage::Format_RGBA8888);
    for (auto* frame : {&background, &transparent}) for (int y = 0; y < frame->height(); ++y) for (int x = 0; x < frame->width(); ++x) {
        auto* p = frame->scanLine(y) + x * 4;
        p[0] = static_cast<uchar>((x * 17 + y * 23) % 256);
        p[1] = static_cast<uchar>((x * 37 + y * 3) % 256);
        p[2] = static_cast<uchar>((x * 7 + y * 41) % 256);
        p[3] = frame == &background ? 255 : static_cast<uchar>((x * 29 + y * 31) % 256);
    }
    const auto background_path = root / "benchmark-background.png", overlay_path = root / "benchmark-overlay.png";
    require(background.save(pathToQString(background_path)) && transparent.save(pathToQString(overlay_path)), "Benchmark fixtures failed.");
    (void)image;
    for (const auto size : {QSize(1920, 1080), QSize(2560, 1440), QSize(3840, 2160)}) for (bool gpu : {false, true}) {
        auto job = makeImageJob(output, background_path, root / (gpu ? "bench-gpu.mkv" : "bench-cpu.mkv"), 600, 6);
        job.settings.width = size.width(); job.settings.height = size.height();
        job.settings.frame_rate = 30; job.settings.export_audio = false; job.settings.gpu_composition_enabled = gpu;
        auto overlay = job.project_snapshot.timeline_tracks[0].clips[0];
        overlay.source_path = overlay_path;
        overlay.transform.scale = .72; overlay.transform.opacity = .6;
        project::ProjectTrack track; track.track_id = 2; track.clips.push_back(overlay);
        job.project_snapshot.timeline_tracks.insert(job.project_snapshot.timeline_tracks.begin(), track);
        overlay.transform.rotation_degrees = 23;
        track.track_id = 3; track.clips = {overlay};
        job.project_snapshot.timeline_tracks.insert(job.project_snapshot.timeline_tracks.begin(), track);
        std::atomic_bool cancel{false};
        renderJob(job, cancel);
        const auto& m = last_metrics;
        std::printf("export_benchmark output=%dx%d backend=%s frames=%llu total_ms=%.3f preparation_ms=%.3f composition_ms=%.3f encoding_ms=%.3f upload_ms=%.3f draw_ms=%.3f readback_ms=%.3f upload_bytes=%llu readback_bytes=%llu gpu_peak_bytes=%llu cpu_frame_peak_bytes=%llu sources_peak_bytes=%llu encoder=%s\n",
            size.width(), size.height(), gpu ? "gpu" : "cpu", static_cast<unsigned long long>(m.encoded_frames),
            m.total_nanoseconds / 1e6, m.preparation_nanoseconds / 1e6, m.composition_nanoseconds / 1e6, m.encoding_nanoseconds / 1e6,
            m.upload_nanoseconds / 1e6, m.draw_submission_nanoseconds / 1e6, m.readback_nanoseconds / 1e6,
            static_cast<unsigned long long>(m.uploaded_bytes), static_cast<unsigned long long>(m.readback_bytes),
            static_cast<unsigned long long>(m.peak_known_gpu_bytes), static_cast<unsigned long long>(m.peak_cpu_frame_bytes),
            static_cast<unsigned long long>(m.peak_prepared_source_bytes), job.settings.video_encoder_name.toUtf8().constData());
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    native_gpu_mode = argc > 1 && (std::string(argv[1]) == "--native-gpu" || std::string(argv[1]) == "--benchmark");
    QSurfaceFormat format; format.setVersion(3, 2); format.setProfile(QSurfaceFormat::CoreProfile);
    QSurfaceFormat::setDefaultFormat(format);
    QApplication application(argc, argv);
    try {
        QTemporaryDir temporary_directory;
        require(temporary_directory.isValid(), "Could not create a temporary export-test directory.");
        const auto root = pathFromQString(temporary_directory.path());
        require(logging::Logger::instance().initialize(root), "Export test log unavailable.");
        auto surface = native_gpu_mode ? creative_suite::composition::OpenGlFrameCompositor::createSurface() : nullptr;
        if (native_gpu_mode) {
            if (!surface) return 77;
            creative_suite::composition::OpenGlCompositionResult probe;
            std::thread worker([&] {
                creative_suite::composition::OpenGlFrameCompositor gpu(surface.get()); probe = gpu.compose(8, 8, {});
            }); worker.join();
            if (!probe.frame && (probe.operation == "create-context" || probe.operation == "check-context")) return 77;
            require(probe.frame.has_value(), "Native export GPU probe failed on an available context.");
            native_options.gpu_surface = surface.get();
            native_options.gpu_factory = [](QOffscreenSurface* value) { return std::make_unique<CheckedNativeGpu>(value); };
        }
        const auto image_path = root / "source.png";
        const auto green_image_path = root / "green.png";
        QImage source(4, 4, QImage::Format_RGBA8888);
        source.fill(QColor(210, 40, 30, 255));
        require(source.save(pathToQString(image_path)),
                "Could not create a deterministic still-image fixture.");
        source.fill(QColor(20, 220, 50, 255));
        require(source.save(pathToQString(green_image_path)),
                "Could not create a second deterministic still-image fixture.");

        const auto output = chooseOutput();
        validateDirectExport(output, image_path, green_image_path, root);
        validateVisualEffectExport(output, image_path, root);
        validateFusionPreviewExportParity(output, image_path, root);
        validateGapsTextAndKeyframes(output, image_path, green_image_path, root);
        validateQueueContinuesAfterFailure(output, image_path, green_image_path, root);
        validateQueueCancellationStopsLaterJobs(output, image_path, root);
        validateConfiguredFullResolutionExport(output, image_path, root);
        validateEmbeddedAudioMixing(output, root);
        validateGpuFallback(output, image_path, root);
        validateActiveQueueShutdown(output, image_path, root);
        validateLosslessParity(output, image_path, root);
#ifdef CREATIVE_SUITE_TEST_IMAGE_EDITOR_MASKS
        validatePublishedImage(output, root);
#endif
        if (argc > 1 && std::string(argv[1]) == "--benchmark") benchmarkExport(output, image_path, root);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
