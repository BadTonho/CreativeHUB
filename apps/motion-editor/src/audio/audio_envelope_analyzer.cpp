#include "audio_envelope_analyzer.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <utility>

namespace motion::audio {
namespace {

struct FormatCloser final {
    void operator()(AVFormatContext* value) const noexcept
    {
        if (value != nullptr) avformat_close_input(&value);
    }
};

struct CodecCloser final {
    void operator()(AVCodecContext* value) const noexcept
    {
        if (value != nullptr) avcodec_free_context(&value);
    }
};

struct FrameCloser final {
    void operator()(AVFrame* value) const noexcept
    {
        if (value != nullptr) av_frame_free(&value);
    }
};

struct PacketCloser final {
    void operator()(AVPacket* value) const noexcept
    {
        if (value != nullptr) av_packet_free(&value);
    }
};

struct ResamplerCloser final {
    void operator()(SwrContext* value) const noexcept
    {
        if (value != nullptr) swr_free(&value);
    }
};

std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto value = path.u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}

[[noreturn]] void throwFfmpegError(int error_code, const char* operation)
{
    std::array<char, AV_ERROR_MAX_STRING_SIZE> message{};
    av_strerror(error_code, message.data(), message.size());
    throw AudioAnalysisError(std::string(operation) + ": " + message.data(), error_code);
}

std::int64_t sampleBoundary(std::int64_t frame, model::FrameRate frame_rate)
{
    if (frame < 0 || frame_rate.numerator <= 0 || frame_rate.denominator <= 0 ||
        frame_rate.denominator > std::numeric_limits<std::int64_t>::max() /
            AudioEnvelopeAnalyzer::analysis_sample_rate) {
        throw AudioAnalysisError("The composition frame rate is invalid.");
    }
    const auto factor = static_cast<std::int64_t>(
        AudioEnvelopeAnalyzer::analysis_sample_rate) * frame_rate.denominator;
    if (frame != 0 && frame > std::numeric_limits<std::int64_t>::max() / factor) {
        throw AudioAnalysisError("The requested audio analysis range is too long.");
    }
    const auto scaled_frame = frame * factor;
    const auto whole = scaled_frame / frame_rate.numerator;
    const auto remainder = scaled_frame % frame_rate.numerator;
    if (remainder != 0 && whole == std::numeric_limits<std::int64_t>::max()) {
        throw AudioAnalysisError("The requested audio analysis range is too long.");
    }
    return whole + (remainder != 0 ? 1 : 0);
}

void reportProgress(const AudioEnvelopeAnalyzer::ProgressCallback& callback,
                    std::int64_t completed_frames,
                    std::int64_t maximum_frames,
                    int& last_reported)
{
    if (!callback || maximum_frames <= 0) return;
    const auto progress = static_cast<int>(std::clamp<long double>(
        100.0L * static_cast<long double>(completed_frames) /
            static_cast<long double>(maximum_frames), 0.0L, 99.0L));
    if (progress == last_reported) return;
    last_reported = progress;
    callback(progress);
}

} // namespace

AudioAnalysisError::AudioAnalysisError(std::string message, int error_code)
    : std::runtime_error(std::move(message)), error_code_(error_code)
{
}

AudioEnvelope AudioEnvelopeAnalyzer::analyze(
    const std::filesystem::path& audio_path,
    model::FrameRate frame_rate,
    std::int64_t maximum_frames,
    const std::atomic_bool& cancel_requested,
    ProgressCallback report_progress)
{
    if (audio_path.empty() || maximum_frames <= 0 ||
        !model::isSupportedFrameRate(frame_rate)) {
        throw AudioAnalysisError("The audio analysis request is invalid.");
    }
    if (cancel_requested.load(std::memory_order_acquire))
        throw AudioAnalysisCancelled{};

    // Validate the selected-layer range up front. We still decode beyond this boundary so
    // normalization uses the peak level of the complete source file.
    static_cast<void>(sampleBoundary(maximum_frames, frame_rate));
    const auto utf8_path = pathToUtf8(audio_path);
    AVFormatContext* raw_format = nullptr;
    int result = avformat_open_input(&raw_format, utf8_path.c_str(), nullptr, nullptr);
    if (result < 0) throwFfmpegError(result, "Opening the audio source");
    std::unique_ptr<AVFormatContext, FormatCloser> format(raw_format);

    result = avformat_find_stream_info(format.get(), nullptr);
    if (result < 0) throwFfmpegError(result, "Reading the audio source metadata");

    const AVCodec* decoder = nullptr;
    const int audio_stream_index = av_find_best_stream(
        format.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (audio_stream_index < 0 || decoder == nullptr) {
        throwFfmpegError(audio_stream_index < 0 ? audio_stream_index : AVERROR_DECODER_NOT_FOUND,
                         "Finding a supported audio stream");
    }
    AVStream* stream = format->streams[audio_stream_index];
    if (stream == nullptr || stream->codecpar == nullptr) {
        throw AudioAnalysisError("The audio stream has no codec metadata.");
    }

    std::unique_ptr<AVCodecContext, CodecCloser> codec(avcodec_alloc_context3(decoder));
    if (!codec) throw AudioAnalysisError("Allocating the audio decoder failed.");
    result = avcodec_parameters_to_context(codec.get(), stream->codecpar);
    if (result < 0) throwFfmpegError(result, "Reading the audio decoder parameters");
    result = avcodec_open2(codec.get(), decoder, nullptr);
    if (result < 0) throwFfmpegError(result, "Opening the audio decoder");
    if (codec->sample_rate <= 0 || codec->ch_layout.nb_channels <= 0 ||
        codec->sample_fmt == AV_SAMPLE_FMT_NONE) {
        throw AudioAnalysisError("The audio stream has invalid sample format metadata.");
    }

    AVChannelLayout mono_layout = AV_CHANNEL_LAYOUT_MONO;
    SwrContext* raw_resampler = nullptr;
    result = swr_alloc_set_opts2(
        &raw_resampler, &mono_layout, AV_SAMPLE_FMT_FLT, analysis_sample_rate,
        &codec->ch_layout, codec->sample_fmt, codec->sample_rate, 0, nullptr);
    std::unique_ptr<SwrContext, ResamplerCloser> resampler(raw_resampler);
    if (result < 0) throwFfmpegError(result, "Creating the audio analysis resampler");
    if (!resampler) throw AudioAnalysisError("Allocating the audio resampler failed.");
    result = swr_init(resampler.get());
    if (result < 0) throwFfmpegError(result, "Initializing the audio resampler");

    std::unique_ptr<AVFrame, FrameCloser> frame(av_frame_alloc());
    std::unique_ptr<AVPacket, PacketCloser> packet(av_packet_alloc());
    if (!frame || !packet) throw AudioAnalysisError("Allocating audio decode buffers failed.");

    AudioEnvelope envelope;
    envelope.rms_by_frame.reserve(static_cast<std::size_t>(
        std::min<std::int64_t>(maximum_frames, 100'000)));
    std::int64_t current_frame = 0;
    std::int64_t current_sample = 0;
    std::int64_t current_frame_end = sampleBoundary(1, frame_rate);
    std::int64_t frame_sample_count = 0;
    long double frame_square_sum = 0.0L;
    int last_reported_progress = -1;
    auto progress_frame_limit = maximum_frames;
    if (format->duration != AV_NOPTS_VALUE && format->duration > 0) {
        const AVRational frame_time_base{frame_rate.denominator, frame_rate.numerator};
        const auto duration_frames = av_rescale_q_rnd(
            format->duration, AVRational{1, AV_TIME_BASE}, frame_time_base,
            static_cast<AVRounding>(AV_ROUND_UP | AV_ROUND_PASS_MINMAX));
        if (duration_frames > 0) progress_frame_limit = duration_frames;
    }

    const auto checkCancelled = [&] {
        if (cancel_requested.load(std::memory_order_acquire))
            throw AudioAnalysisCancelled{};
    };

    const auto finishFrame = [&] {
        if (frame_sample_count <= 0) return;
        const auto rms = std::sqrt(static_cast<double>(
            frame_square_sum / static_cast<long double>(frame_sample_count)));
        envelope.peak_rms = std::max(envelope.peak_rms, rms);
        if (current_frame < maximum_frames) envelope.rms_by_frame.push_back(rms);
        if (current_frame >= std::numeric_limits<std::int64_t>::max() - 1) {
            throw AudioAnalysisError("The audio source duration is too long to analyze.");
        }
        ++current_frame;
        frame_square_sum = 0.0L;
        frame_sample_count = 0;
        current_frame_end = sampleBoundary(current_frame + 1, frame_rate);
        reportProgress(report_progress, current_frame, progress_frame_limit,
                       last_reported_progress);
    };

    const auto consumeSamples = [&](const float* samples, int sample_count) {
        if (sample_count < 0) throw AudioAnalysisError("The audio decoder returned an invalid sample count.");
        for (int index = 0; index < sample_count; ++index) {
            if ((index & 0x1fff) == 0) checkCancelled();
            const double sample = samples[index];
            if (!std::isfinite(sample)) {
                throw AudioAnalysisError("The decoded audio contains a non-finite sample.");
            }
            if (current_sample == std::numeric_limits<std::int64_t>::max() ||
                frame_sample_count == std::numeric_limits<std::int64_t>::max()) {
                throw AudioAnalysisError("The audio source duration is too long to analyze.");
            }
            frame_square_sum += static_cast<long double>(sample) * sample;
            ++frame_sample_count;
            ++current_sample;
            if (current_sample == current_frame_end) {
                finishFrame();
            }
        }
    };

    const auto convertFrame = [&] {
        if (frame->nb_samples < 0) {
            throw AudioAnalysisError("The audio decoder returned an invalid sample count.");
        }
        const auto delayed_samples = swr_get_delay(resampler.get(), codec->sample_rate);
        if (delayed_samples < 0 || delayed_samples >
                std::numeric_limits<std::int64_t>::max() - frame->nb_samples) {
            throw AudioAnalysisError("The decoded audio frame is too large.");
        }
        const auto output_capacity = av_rescale_rnd(
            delayed_samples + frame->nb_samples, analysis_sample_rate,
            codec->sample_rate, AV_ROUND_UP);
        if (output_capacity < 0 || output_capacity > std::numeric_limits<int>::max()) {
            throw AudioAnalysisError("The decoded audio frame is too large.");
        }
        std::vector<float> mono_samples(static_cast<std::size_t>(output_capacity));
        std::uint8_t* output_data[] = {
            reinterpret_cast<std::uint8_t*>(mono_samples.data())};
        if (frame->extended_data == nullptr) {
            throw AudioAnalysisError("The audio decoder returned an empty sample buffer.");
        }
        const auto channel_count = codec->ch_layout.nb_channels;
        const auto input_plane_count = av_sample_fmt_is_planar(
            static_cast<AVSampleFormat>(frame->format)) ? channel_count : 1;
        std::vector<const std::uint8_t*> input_data(
            static_cast<std::size_t>(input_plane_count));
        for (int plane = 0; plane < input_plane_count; ++plane) {
            input_data[static_cast<std::size_t>(plane)] = frame->extended_data[plane];
            if (input_data[static_cast<std::size_t>(plane)] == nullptr) {
                throw AudioAnalysisError("The audio decoder returned an incomplete sample buffer.");
            }
        }
        const int converted = swr_convert(
            resampler.get(), output_data, static_cast<int>(output_capacity),
            input_data.data(), frame->nb_samples);
        av_frame_unref(frame.get());
        if (converted < 0) throwFfmpegError(converted, "Converting decoded audio samples");
        consumeSamples(mono_samples.data(), converted);
    };

    const auto receiveFrames = [&] {
        while (true) {
            checkCancelled();
            const int receive_result = avcodec_receive_frame(codec.get(), frame.get());
            if (receive_result == AVERROR(EAGAIN) || receive_result == AVERROR_EOF) return;
            if (receive_result < 0) throwFfmpegError(receive_result, "Decoding audio samples");
            convertFrame();
        }
    };

    while (true) {
        checkCancelled();
        result = av_read_frame(format.get(), packet.get());
        if (result == AVERROR_EOF) break;
        if (result < 0) throwFfmpegError(result, "Reading audio packets");
        if (packet->stream_index == audio_stream_index) {
            result = avcodec_send_packet(codec.get(), packet.get());
            if (result == AVERROR(EAGAIN)) {
                receiveFrames();
                result = avcodec_send_packet(codec.get(), packet.get());
            }
            av_packet_unref(packet.get());
            if (result < 0) throwFfmpegError(result, "Submitting an audio packet");
            receiveFrames();
        } else {
            av_packet_unref(packet.get());
        }
    }

    checkCancelled();
    result = avcodec_send_packet(codec.get(), nullptr);
    if (result < 0 && result != AVERROR_EOF) {
        throwFfmpegError(result, "Flushing the audio decoder");
    }
    receiveFrames();

    while (true) {
        checkCancelled();
        const auto delayed_samples = swr_get_delay(resampler.get(), codec->sample_rate);
        if (delayed_samples <= 0) break;
        const auto output_capacity = av_rescale_rnd(
            delayed_samples, analysis_sample_rate, codec->sample_rate, AV_ROUND_UP);
        if (output_capacity <= 0 || output_capacity > std::numeric_limits<int>::max()) break;
        std::vector<float> mono_samples(static_cast<std::size_t>(output_capacity));
        std::uint8_t* output_data[] = {
            reinterpret_cast<std::uint8_t*>(mono_samples.data())};
        const int converted = swr_convert(
            resampler.get(), output_data, static_cast<int>(output_capacity), nullptr, 0);
        if (converted < 0) throwFfmpegError(converted, "Flushing resampled audio");
        if (converted == 0) break;
        consumeSamples(mono_samples.data(), converted);
    }

    finishFrame();
    if (envelope.rms_by_frame.empty()) {
        if (report_progress) report_progress(100);
        return envelope;
    }
    if (report_progress) report_progress(100);
    return envelope;
}

std::vector<creative_suite::animation::Keyframe> generateAudioKeyframes(
    const AudioEnvelope& envelope,
    std::int64_t layer_duration_frames,
    creative_suite::animation::TransformProperty property,
    double minimum_value,
    double maximum_value,
    std::size_t maximum_keyframe_count,
    const std::atomic_bool* cancel_requested)
{
    using creative_suite::animation::InterpolationMode;
    using creative_suite::animation::Keyframe;
    using creative_suite::animation::validKeyframeValue;

    const auto checkCancelled = [cancel_requested] {
        if (cancel_requested != nullptr &&
            cancel_requested->load(std::memory_order_acquire)) {
            throw AudioAnalysisCancelled{};
        }
    };
    checkCancelled();

    if (layer_duration_frames <= 0 || !std::isfinite(minimum_value) ||
        !std::isfinite(maximum_value) || minimum_value >= maximum_value ||
        !validKeyframeValue(property, minimum_value) ||
        !validKeyframeValue(property, maximum_value)) {
        throw AudioAnalysisError("The transform range is invalid.");
    }
    if (!std::isfinite(envelope.peak_rms) || envelope.peak_rms < 0.0) {
        throw AudioAnalysisError("The audio analysis returned an invalid peak level.");
    }
    if (envelope.rms_by_frame.empty()) return {};
    if (envelope.peak_rms == 0.0) return {};

    auto sample_count = envelope.rms_by_frame.size();
    if (static_cast<std::uint64_t>(layer_duration_frames) <
        static_cast<std::uint64_t>(sample_count)) {
        sample_count = static_cast<std::size_t>(layer_duration_frames);
    }
    const bool needs_return_key =
        static_cast<std::uint64_t>(sample_count) <
        static_cast<std::uint64_t>(layer_duration_frames);
    const auto generated_count = sample_count + (needs_return_key ? 1U : 0U);
    if (generated_count > maximum_keyframe_count) {
        throw AudioAnalysisError("The generated animation exceeds the document keyframe limit.");
    }

    std::vector<Keyframe> keyframes;
    keyframes.reserve(generated_count);
    const double output_span = maximum_value - minimum_value;
    if (!std::isfinite(output_span)) {
        throw AudioAnalysisError("The transform range is too large.");
    }
    for (std::size_t index = 0; index < sample_count; ++index) {
        if ((index & 0x1fffU) == 0) checkCancelled();
        const double rms = envelope.rms_by_frame[index];
        if (!std::isfinite(rms) || rms < 0.0) {
            throw AudioAnalysisError("The audio analysis returned an invalid RMS value.");
        }
        const double normalized_level = std::clamp(rms / envelope.peak_rms, 0.0, 1.0);
        const double value = minimum_value + normalized_level * output_span;
        if (!std::isfinite(value) || !validKeyframeValue(property, value)) {
            throw AudioAnalysisError("The audio level produced an invalid transform value.");
        }
        keyframes.push_back(Keyframe{static_cast<std::int64_t>(index), value,
                                     InterpolationMode::Linear, {}});
    }
    if (needs_return_key) {
        keyframes.push_back(Keyframe{static_cast<std::int64_t>(sample_count),
                                     minimum_value, InterpolationMode::Linear, {}});
    }
    checkCancelled();
    return keyframes;
}

} // namespace motion::audio
