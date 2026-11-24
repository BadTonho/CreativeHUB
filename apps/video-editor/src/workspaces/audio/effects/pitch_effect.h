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

namespace audio_effects::pitch {

inline void process(
    std::vector<float>& samples,
    int channel_count,
    int sample_rate,
    const timeline::AudioPitchParameters& p,
    AudioDspState& state) {
    if (channel_count <= 0 || sample_rate <= 0 || std::abs(p.semitones) <= 1.0e-4) return;
    const std::size_t win_frames = std::max<std::size_t>(512, static_cast<std::size_t>(sample_rate * 0.04));
    const std::size_t buf_frames = win_frames * 2;
    const std::size_t needed = buf_frames * static_cast<std::size_t>(channel_count);
    if (state.pitch_buffer.size() != needed) {
        state.pitch_buffer.assign(needed, 0.0F);
        state.pitch_write = 0;
        state.pitch_phase = 0.0;
    }
    const double ratio = std::pow(2.0, p.semitones / 12.0);
    const double phase_step = (1.0 - ratio) / static_cast<double>(win_frames);
    const std::size_t frames = samples.size() / static_cast<std::size_t>(channel_count);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t base = frame * static_cast<std::size_t>(channel_count);
        for (int ch = 0; ch < channel_count; ++ch) {
            state.pitch_buffer[state.pitch_write * static_cast<std::size_t>(channel_count) + ch] = samples[base + ch];
        }
        const double ph0 = state.pitch_phase;
        const double ph1 = std::fmod(ph0 + 0.5, 1.0);
        const double d0 = ph0 * static_cast<double>(win_frames);
        const double d1 = ph1 * static_cast<double>(win_frames);
        const float w0 = static_cast<float>(0.5 - 0.5 * std::cos(2.0 * 3.14159265358979323846 * ph0));
        const float w1 = 1.0F - w0;
        const std::size_t i0 = (state.pitch_write + buf_frames - static_cast<std::size_t>(d0)) % buf_frames;
        const std::size_t i1 = (state.pitch_write + buf_frames - static_cast<std::size_t>(d1)) % buf_frames;
        for (int ch = 0; ch < channel_count; ++ch) {
            const float s0 = state.pitch_buffer[i0 * static_cast<std::size_t>(channel_count) + ch];
            const float s1 = state.pitch_buffer[i1 * static_cast<std::size_t>(channel_count) + ch];
            samples[base + ch] = s0 * w0 + s1 * w1;
        }
        state.pitch_phase += phase_step;
        if (state.pitch_phase >= 1.0) state.pitch_phase -= 1.0;
        if (state.pitch_phase < 0.0) state.pitch_phase += 1.0;
        state.pitch_write = (state.pitch_write + 1) % buf_frames;
    }
}

#ifdef QT_WIDGETS_LIB
template <typename AddDoubleControlFn>
inline void buildEditor(
    QWidget*,
    QFormLayout* form,
    const timeline::AudioPitchParameters& p,
    AddDoubleControlFn&& add_double_control,
    std::function<void(timeline::AudioPitchParameters)> on_change) {
    auto state = p;
    add_double_control(form, "Pitch shift", "audioFxPitchSemitones", -12.0, 12.0, 1, QStringLiteral(" st"),
        state.semitones, [state, on_change](double v) mutable {
            state.semitones = v;
            on_change(state);
        });
}
#endif

}  // namespace audio_effects::pitch
