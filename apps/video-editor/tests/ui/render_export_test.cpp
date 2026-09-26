#include "media/video_playback.h"
#include "media/audio_playback.h"
#include "rendering/offline_export_renderer.h"
#include "ui/workspace/pages/render/render_output_capabilities.h"
#include "ui/workspace/pages/render/render_queue_controller.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswscale/swscale.h>
}

#include <QApplication>
#include <QEventLoop>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
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
    ui::RenderContainerOption container;
    ui::RenderEncoderOption video;
    ui::RenderEncoderOption audio;
};

bool softwareEncoder(const std::string& name) {
    constexpr const char* hardware_names[]{
        "_mf", "nvenc", "_qsv", "_amf", "vaapi", "videotoolbox", "_v4l2m2m"};
    return std::none_of(std::begin(hardware_names), std::end(hardware_names),
        [&name](const char* marker) { return name.find(marker) != std::string::npos; });
}

OutputChoice chooseOutput() {
    auto containers = ui::RenderOutputCapabilities::availableContainers();
    std::stable_sort(containers.begin(), containers.end(), [](const auto& left, const auto& right) {
        return left.name == "matroska" && right.name != "matroska";
    });
    for (const auto& container : containers) {
        for (const auto& video : container.video_encoders) {
            if (!softwareEncoder(video.name)) continue;
            for (const auto& audio : container.audio_encoders) {
                if (ui::RenderOutputCapabilities::supportsAudioEncoder(container, audio.name)) {
                    const auto pcm = std::find_if(
                        container.audio_encoders.begin(), container.audio_encoders.end(),
                        [&container](const auto& candidate) {
                            return candidate.name == "pcm_s16le" &&
                                ui::RenderOutputCapabilities::supportsAudioEncoder(
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
    for (int index = 0; index < 30; ++index) {
        requireFfmpeg(av_frame_make_writable(video_frame), "Preparing source video pixels");
        sws_scale(scaler, source_data, source_lines, 0, video->height,
                  video_frame->data, video_frame->linesize);
        video_frame->pts = index;
        muxFrame(output, video, video_stream, video_frame);
    }
    constexpr double pi = 3.14159265358979323846;
    std::int64_t audio_sample = 0;
    while (audio_sample < 48000) {
        const int count = static_cast<int>(std::min<std::int64_t>(
            audio_frame->nb_samples, 48000 - audio_sample));
        requireFfmpeg(av_frame_make_writable(audio_frame), "Preparing source audio samples");
        audio_frame->nb_samples = count;
        auto* samples = reinterpret_cast<std::int16_t*>(audio_frame->data[0]);
        for (int index = 0; index < count; ++index) {
            const auto value = static_cast<std::int16_t>(std::lround(
                std::sin(2.0 * pi * 440.0 * (audio_sample + index) / 48000.0) * 12000.0));
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

ui::RenderJob makeImageJob(
    const OutputChoice& output,
    const std::filesystem::path& image_path,
    const std::filesystem::path& output_path,
    std::uint64_t id,
    std::int64_t duration_frames = 4) {
    ui::RenderJob job;
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
    project::ProjectTrack track;
    track.track_id = 1;
    track.name = "V1";
    project::ProjectClip clip;
    clip.source_path = image_path;
    clip.timeline_start_frame = 0;
    clip.duration_frames = duration_frames;
    clip.kind = timeline::ClipKind::Image;
    track.clips.push_back(clip);
    job.project_snapshot.canvas_width = 32;
    job.project_snapshot.canvas_height = 24;
    job.project_snapshot.timeline_tracks.push_back(std::move(track));
    return job;
}

void verifyExport(const std::filesystem::path& path) {
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
    require(frame_count == 8,
            "The export did not convert four 30 fps timeline frames into eight 60 fps frames.");
    const auto centerPixel = [](const media::VideoFrame& frame, int channel) {
        const auto offset = static_cast<std::size_t>(12 * frame.stride + 16 * 4 + channel);
        return frame.rgba_pixels[offset];
    };
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
    track.clips.front().duration_frames = 2;
    project::ProjectClip incoming;
    incoming.source_path = green_image_path;
    incoming.timeline_start_frame = 2;
    incoming.duration_frames = 2;
    incoming.kind = timeline::ClipKind::Image;
    track.clips.push_back(incoming);
    track.transitions.push_back(project::ProjectTransition{
        0, 1, timeline::TransitionKind::CrossDissolve, 2});
    std::vector<int> progress;
    std::atomic_bool cancel{false};
    rendering::OfflineExportRenderer::render(
        job, cancel, [&progress](int value) { progress.push_back(value); });
    require(std::filesystem::is_regular_file(target),
            "A completed render did not publish its output file.");
    require(!progress.empty() && progress.back() == 100 &&
                std::is_sorted(progress.begin(), progress.end()),
            "Export progress must be monotonic and reach 100 percent.");
    verifyExport(target);

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
        rendering::OfflineExportRenderer::render(canceled_job, already_canceled);
    } catch (const rendering::ExportCanceled&) {
        canceled = true;
    }
    require(canceled, "A canceled export did not stop before publishing.");
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
        rendering::OfflineExportRenderer::render(failed_job, not_canceled);
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "An offline source must fail the export.");
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
    auto already_completed = makeImageJob(
        output, root / "completed-source.png", root / ("already-done." + extension), 200);
    already_completed.status = ui::RenderJobStatus::Completed;
    auto& track = next.project_snapshot.timeline_tracks.front();
    track.clips.front().duration_frames = 2;
    project::ProjectClip incoming;
    incoming.source_path = green_image_path;
    incoming.timeline_start_frame = 2;
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
    require(controller.start({already_completed, failed, next}), "The render queue did not start.");
    QTimer::singleShot(30000, &loop, &QEventLoop::quit);
    loop.exec();
    require(!controller.isRunning(), "The render queue worker did not finish.");
    require(started == std::vector<qulonglong>{201, 202} &&
                failed_ids == std::vector<qulonglong>{201} &&
                completed == std::vector<qulonglong>{202},
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
    rendering::OfflineExportRenderer::render(gaps, canceled);
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
    rendering::OfflineExportRenderer::render(keyed, canceled);
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
    rendering::OfflineExportRenderer::render(text, canceled);
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

void validateEmbeddedAudioMixing(
    const OutputChoice& output,
    const std::filesystem::path& root) {
    const auto extension = output.container.extensions.empty()
        ? std::string("mkv")
        : output.container.extensions.substr(0, output.container.extensions.find(','));
    const auto source = createVideoWithAudioFixture(root);
    auto job = makeImageJob(
        output, source, root / ("audio-mix." + extension), 401, 30);
    auto& track = job.project_snapshot.timeline_tracks.front();
    track.audio_gain = 0.5;
    track.clips.front().kind = timeline::ClipKind::Video;
    track.clips.front().audio_gain = 0.5;
    std::atomic_bool canceled{false};
    rendering::OfflineExportRenderer::render(job, canceled);
    const auto audible_rms = decodedAudioRms(pathFromQString(job.settings.output_path));
    require(audible_rms > 0.04 && audible_rms < 0.10,
            "Embedded video audio was not mixed with the configured track and clip gains.");

    track.audio_muted = true;
    job.id = 402;
    job.settings.output_path = pathToQString(root / ("audio-muted." + extension));
    rendering::OfflineExportRenderer::render(job, canceled);
    require(decodedAudioRms(pathFromQString(job.settings.output_path)) < 0.001,
            "A muted audio track still contributed samples to the export.");
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
    job.settings.width = 1920;
    job.settings.height = 1080;
    job.settings.export_audio = false;
    job.project_snapshot.canvas_width = 1920;
    job.project_snapshot.canvas_height = 1080;

    std::atomic_bool canceled{false};
    rendering::OfflineExportRenderer::render(job, canceled);
    auto decoder = media::VideoPlaybackSession::open(
        pathFromQString(job.settings.output_path));
    const auto first_frame = decoder->decode_next_frame();
    require(first_frame.has_value() && *first_frame != nullptr &&
                (*first_frame)->width == 1920 && (*first_frame)->height == 1080,
            "Playback Preview Quality must not reduce the configured offline export resolution.");
}

}  // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    try {
        QTemporaryDir temporary_directory;
        require(temporary_directory.isValid(), "Could not create a temporary export-test directory.");
        const auto root = pathFromQString(temporary_directory.path());
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
        validateGapsTextAndKeyframes(output, image_path, green_image_path, root);
        validateQueueContinuesAfterFailure(output, image_path, green_image_path, root);
        validateQueueCancellationStopsLaterJobs(output, image_path, root);
        validateConfiguredFullResolutionExport(output, image_path, root);
        validateEmbeddedAudioMixing(output, root);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
