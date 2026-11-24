#pragma once

#include "workspaces/audio/effects/audio_effect_dsp_common.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef QT_WIDGETS_LIB
#include <QFormLayout>
#include <QWidget>
#include <functional>
#endif

namespace audio_effects::delay {

inline void process(
    std::vector<float>& samples,
    int channel_count,
    int sample_rate,
    const timeline::AudioDelayParameters& p,
    AudioDspState& state) {
    if (channel_count <= 0 || sample_rate <= 0 || p.mix <= 0.0) return;
    const std::size_t delay_frames = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::lround(p.delay_ms * 0.001 * sample_rate)));
    const std::size_t buf_frames = std::max<std::size_t>(delay_frames + 1, static_cast<std::size_t>(sample_rate * 2));
    const std::size_t needed = buf_frames * static_cast<std::size_t>(channel_count);
    if (state.delay_buffer.size() != needed) {
        state.delay_buffer.assign(needed, 0.0F);
        state.delay_write = 0;
    }
    const float fb = static_cast<float>(std::clamp(p.feedback, 0.0, 0.9));
    const float mix = static_cast<float>(std::clamp(p.mix, 0.0, 1.0));
    const std::size_t frames = samples.size() / static_cast<std::size_t>(channel_count);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t read_frame = (state.delay_write + buf_frames - delay_frames) % buf_frames;
        const std::size_t base = frame * static_cast<std::size_t>(channel_count);
        const std::size_t rbase = read_frame * static_cast<std::size_t>(channel_count);
        const std::size_t wbase = state.delay_write * static_cast<std::size_t>(channel_count);
        for (int ch = 0; ch < channel_count; ++ch) {
            const float dry = samples[base + ch];
            const float delayed = state.delay_buffer[rbase + ch];
            state.delay_buffer[wbase + ch] = dry + delayed * fb;
            samples[base + ch] = dry * (1.0F - mix) + delayed * mix;
        }
        state.delay_write = (state.delay_write + 1) % buf_frames;
    }
}

#ifdef QT_WIDGETS_LIB
template <typename AddDoubleControlFn>
inline void buildEditor(
    QWidget*,
    QFormLayout* form,
    const timeline::AudioDelayParameters& p,
    AddDoubleControlFn&& add_double_control,
    std::function<void(timeline::AudioDelayParameters)> on_change) {
    auto state = p;
    add_double_control(form, "Delay time", "audioFxDelayMs", 1.0, 2000.0, 0, QStringLiteral(" ms"),
        state.delay_ms, [state, on_change](double v) mutable {
            state.delay_ms = v;
            on_change(state);
        });
    add_double_control(form, "Feedback", "audioFxDelayFeedback", 0.0, 90.0, 0, QStringLiteral("%"),
        state.feedback * 100.0, [state, on_change](double v) mutable {
            state.feedback = v / 100.0;
            on_change(state);
        });
    add_double_control(form, "Mix", "audioFxDelayMix", 0.0, 100.0, 0, QStringLiteral("%"),
        state.mix * 100.0, [state, on_change](double v) mutable {
            state.mix = v / 100.0;
            on_change(state);
        });
}
#endif

}  // namespace audio_effects::delay
