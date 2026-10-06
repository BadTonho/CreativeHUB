#include "audio_keyframe_generation.h"

#include <creative_suite/diagnostics/logger.h>

#include <QMetaObject>

#include <string>
#include <utility>

namespace motion::ui {
namespace {

std::string pathForLog(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return {reinterpret_cast<const char*>(encoded.data()), encoded.size()};
}

} // namespace

AudioKeyframeGenerationWorker::AudioKeyframeGenerationWorker(
    QObject* receiver,
    AudioKeyframeGenerationRequest request,
    ProgressHandler progress_handler,
    FinishedHandler finished_handler)
    : receiver_(receiver),
      request_(std::move(request)),
      cancel_requested_(std::make_shared<std::atomic_bool>(false)),
      progress_handler_(std::move(progress_handler)),
      finished_handler_(std::move(finished_handler))
{
    setObjectName(QStringLiteral("motion-audio-keyframe-worker"));
}

AudioKeyframeGenerationWorker::~AudioKeyframeGenerationWorker()
{
    cancelAndWait();
}

void AudioKeyframeGenerationWorker::cancel() noexcept
{
    cancel_requested_->store(true, std::memory_order_release);
}

void AudioKeyframeGenerationWorker::cancelAndWait()
{
    cancel();
    if (isRunning()) wait();
}

void AudioKeyframeGenerationWorker::run()
{
    AudioKeyframeGenerationResult result;
    result.audio_path = request_.audio_path;
    result.layer_id = request_.layer_id;
    result.property = request_.property;
    try {
        const auto envelope = audio::AudioEnvelopeAnalyzer::analyze(
            request_.audio_path, request_.frame_rate, request_.analysis_frame_limit,
            *cancel_requested_, [this](int progress) {
                if (receiver_.isNull()) return;
                const auto receiver = receiver_;
                const auto handler = progress_handler_;
                QMetaObject::invokeMethod(receiver.data(), [receiver, handler, progress] {
                    if (!receiver.isNull() && handler) handler(progress);
                }, Qt::QueuedConnection);
            });
        if (cancel_requested_->load(std::memory_order_acquire))
            throw audio::AudioAnalysisCancelled{};
        result.keyframes = audio::generateAudioKeyframes(
            envelope, request_.layer_duration_frames, request_.property,
            request_.minimum_value, request_.maximum_value,
            request_.maximum_keyframe_count, cancel_requested_.get());
        result.silent = result.keyframes.empty();
        result.succeeded = true;
    } catch (const audio::AudioAnalysisCancelled&) {
        result.cancelled = true;
    } catch (const audio::AudioAnalysisError& error) {
        result.error_message = error.what();
        result.error_code = error.errorCode();
    } catch (const std::exception& error) {
        result.error_message = error.what();
    } catch (...) {
        result.error_message = "Unknown audio keyframe generation failure.";
    }

    if (!result.succeeded && !result.cancelled) {
        creative_suite::diagnostics::Context context{
            {"path", pathForLog(result.audio_path)},
            {"layer_id", std::to_string(result.layer_id)}};
        if (result.error_code != -1)
            context.emplace_back("error_code", std::to_string(result.error_code));
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_audio", "generate_keyframes", result.error_message, context);
    }

    if (receiver_.isNull()) return;
    const auto receiver = receiver_;
    const auto handler = finished_handler_;
    QMetaObject::invokeMethod(receiver.data(),
        [receiver, handler, result = std::move(result)]() mutable {
            if (!receiver.isNull() && handler) handler(std::move(result));
        }, Qt::QueuedConnection);
}

} // namespace motion::ui
