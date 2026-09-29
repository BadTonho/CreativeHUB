#include "motion_video_export.h"

#include "composition_frame_renderer.h"

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

void checkCancelled(const std::atomic_bool& cancel_requested)
{
    if (cancel_requested.load(std::memory_order_acquire)) throw MotionExportCancelled{};
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

} // namespace

void MotionVideoExporter::exportVideo(
    const MotionExportSnapshot& snapshot,
    const MotionExportSettings& settings,
    const std::atomic_bool& cancel_requested,
    ProgressCallback report_progress)
{
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
        creative_suite::media::VideoEncoder encoder(std::move(encoding));
        CompositionFrameRenderer frame_renderer;
        int last_reported_progress = -1;

        for (std::int64_t output_frame = 0;
             output_frame < output_frame_count;
             ++output_frame) {
            checkCancelled(cancel_requested);
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
                active.source_frame_rate = layer.source_frame_rate;
                active.transform = layer.transform;
                active.keyframes = layer.keyframes;
                active.content = layer.content;
                if (layer.kind == model::LayerKind::Image) {
                    const auto still = snapshot.still_frames.find(layer.source_path);
                    if (still != snapshot.still_frames.end()) active.still_frame = still->second;
                }
                request.layers.push_back(std::move(active));
            }

            const auto frame = frame_renderer.render(
                request,
                [&cancel_requested] {
                    return cancel_requested.load(std::memory_order_acquire);
                },
                true);
            checkCancelled(cancel_requested);
            if (frame == nullptr) {
                throw std::runtime_error("Motion Studio could not compose output frame " +
                                         std::to_string(output_frame) + ".");
            }
            encoder.writeVideo(*frame, output_frame);
            if (report_progress) {
                const auto progress = static_cast<int>(
                    (output_frame + 1) * 99 / output_frame_count);
                if (progress != last_reported_progress) {
                    report_progress(progress);
                    last_reported_progress = progress;
                }
            }
        }

        checkCancelled(cancel_requested);
        encoder.finish();
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
    FinishedHandler finished_handler)
    : receiver_(receiver),
      snapshot_(std::move(snapshot)),
      settings_(std::move(settings)),
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
    try {
        MotionVideoExporter::exportVideo(snapshot_, settings_, *cancel_requested_,
            [this](int progress) {
                if (receiver_.isNull()) return;
                const auto receiver = receiver_;
                const auto handler = progress_handler_;
                QMetaObject::invokeMethod(receiver.data(), [receiver, handler, progress] {
                    if (!receiver.isNull() && handler) handler(progress);
                }, Qt::QueuedConnection);
            });
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
