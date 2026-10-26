#include "audio_filter_session.h"
#include "logging/logger.h"
#include <algorithm>
#include <cstring>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
extern "C" {
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/buffersink.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
}

namespace media {
namespace {
void check(int code, const char* operation) {
    if (code >= 0) return;
    char message[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, message, sizeof(message));
    logging::Logger::instance().log(logging::Level::Error, "audio_processing", operation,
        message, {{"error_code", std::to_string(code)}});
    throw AudioFilterError(std::string(operation) + ": " + message);
}
struct FrameDelete { void operator()(AVFrame* frame) const { av_frame_free(&frame); } };
using Frame = std::unique_ptr<AVFrame, FrameDelete>;
}
struct AudioFilterSession::Impl {
    AVFilterGraph* graph = nullptr;
    AVFilterContext* source = nullptr;
    AVFilterContext* sink = nullptr;
    int rate = 0;
    int channels = 0;
    int latency = 0;
    int skip = 0;
    std::int64_t cursor = 0;
    ~Impl() { avfilter_graph_free(&graph); }
};
AudioFilterSession::AudioFilterSession(const timeline::AudioEffectInstance& effect, int rate, int channels)
    : impl_(std::make_unique<Impl>()) {
    if (rate <= 0 || channels <= 0) throw std::invalid_argument("Invalid audio filter format.");
    auto& state = *impl_;
    state.rate = rate;
    state.channels = channels;
    std::ostringstream filters;
    filters.imbue(std::locale::classic());
    if (const auto* cleanup = std::get_if<timeline::AudioDialogueCleanupParameters>(&effect.parameters)) {
        filters << "highpass=f=" << std::min(cleanup->highpass_frequency_hz, rate * .45)
            << ",deesser=i=" << cleanup->deesser_intensity << ":m=0.5:f=0.5";
    } else if (const auto* noise = std::get_if<timeline::AudioNoiseReductionParameters>(&effect.parameters)) {
        if (noise->reduction_db == 0) filters << "anull";
        else {
            filters << "afftdn=nr=" << noise->reduction_db << ":nf=" << noise->noise_floor_db
                << ":tn=0:nl=average";
            state.latency = 2 * (rate / 80);
            state.skip = state.latency;
        }
    } else if (const auto* repair = std::get_if<timeline::AudioRepairParameters>(&effect.parameters)) {
        if (repair->hum_frequency_hz != 0) {
            for (int harmonic = 1; harmonic <= 3; ++harmonic)
                filters << "bandreject=f=" << std::min<double>(repair->hum_frequency_hz * harmonic, rate * .45)
                    << ":t=q:w=30,";
        }
        if (repair->declick_enabled) {
            // Overlap-save preserves the first sample without overlap-add's
            // initial amplitude ramp. Its buffering latency is supplied by pull.
            filters << "adeclick=w=55:o=75:a=2:m=save:t=" << repair->declick_sensitivity;
            state.latency = std::max(100, static_cast<int>(rate * .055));
        } else filters << "anull";
    } else throw std::invalid_argument("Unsupported FFmpeg audio processor.");
    state.graph = avfilter_graph_alloc();
    if (!state.graph) check(AVERROR(ENOMEM), "allocate_filter_graph");
    state.graph->nb_threads = 1;
    AVChannelLayout layout{};
    av_channel_layout_default(&layout, channels);
    char layout_text[128]{};
    av_channel_layout_describe(&layout, layout_text, sizeof(layout_text));
    av_channel_layout_uninit(&layout);
    const auto arguments = "time_base=1/" + std::to_string(rate) + ":sample_rate=" + std::to_string(rate) +
        ":sample_fmt=flt:channel_layout=" + layout_text;
    const auto* input_filter = avfilter_get_by_name("abuffer");
    const auto* output_filter = avfilter_get_by_name("abuffersink");
    if (!input_filter || !output_filter) check(AVERROR_FILTER_NOT_FOUND, "find_audio_filter_endpoints");
    check(avfilter_graph_create_filter(&state.source, input_filter, "input",
        arguments.c_str(), nullptr, state.graph), "create_filter_input");
    check(avfilter_graph_create_filter(&state.sink, output_filter, "output",
        nullptr, nullptr, state.graph), "create_filter_output");
    AVFilterInOut* input = avfilter_inout_alloc();
    AVFilterInOut* output = avfilter_inout_alloc();
    if (!input || !output) {
        avfilter_inout_free(&input); avfilter_inout_free(&output);
        check(AVERROR(ENOMEM), "allocate_filter_endpoints");
    }
    input->name = av_strdup("out"); input->filter_ctx = state.sink;
    output->name = av_strdup("in"); output->filter_ctx = state.source;
    if (!input->name || !output->name) {
        avfilter_inout_free(&input); avfilter_inout_free(&output);
        check(AVERROR(ENOMEM), "allocate_filter_endpoint_names");
    }
    const auto description = filters.str() + ",aformat=sample_fmts=flt";
    const int parsed = avfilter_graph_parse_ptr(state.graph, description.c_str(), &input, &output, nullptr);
    avfilter_inout_free(&input); avfilter_inout_free(&output);
    check(parsed, "parse_audio_filter");
    check(avfilter_graph_config(state.graph, nullptr), "configure_audio_filter");
}
AudioFilterSession::~AudioFilterSession() = default;
AudioFilterSession::AudioFilterSession(AudioFilterSession&&) noexcept = default;
AudioFilterSession& AudioFilterSession::operator=(AudioFilterSession&&) noexcept = default;
int AudioFilterSession::latencySamples() const noexcept { return impl_->latency; }
void AudioFilterSession::push(std::span<const float> samples) {
    auto& state = *impl_;
    if (samples.empty()) return;
    if (samples.size() % state.channels != 0) throw std::invalid_argument("Unaligned audio filter input.");
    Frame frame(av_frame_alloc());
    if (!frame) check(AVERROR(ENOMEM), "allocate_filter_frame");
    frame->format = AV_SAMPLE_FMT_FLT;
    frame->sample_rate = state.rate;
    av_channel_layout_default(&frame->ch_layout, state.channels);
    frame->nb_samples = static_cast<int>(samples.size() / state.channels);
    frame->pts = state.cursor;
    check(av_frame_get_buffer(frame.get(), 0), "allocate_filter_samples");
    std::memcpy(frame->data[0], samples.data(), samples.size_bytes());
    check(av_buffersrc_add_frame_flags(state.source, frame.get(), AV_BUFFERSRC_FLAG_KEEP_REF), "push_audio_filter");
    state.cursor += frame->nb_samples;
}
void AudioFilterSession::drain() {
    check(av_buffersrc_add_frame_flags(impl_->source, nullptr, 0), "drain_audio_filter");
}
std::vector<float> AudioFilterSession::pull() {
    auto& state = *impl_;
    std::vector<float> result;
    Frame frame(av_frame_alloc());
    if (!frame) check(AVERROR(ENOMEM), "allocate_filter_output");
    while (true) {
        const auto code = av_buffersink_get_frame(state.sink, frame.get());
        if (code == AVERROR(EAGAIN) || code == AVERROR_EOF) break;
        check(code, "pull_audio_filter");
        const auto skip = std::min(state.skip, frame->nb_samples);
        state.skip -= skip;
        const auto* samples = reinterpret_cast<const float*>(frame->data[0]);
        result.insert(result.end(), samples + skip * state.channels,
            samples + frame->nb_samples * state.channels);
        av_frame_unref(frame.get());
    }
    return result;
}
} // namespace media
