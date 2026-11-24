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

namespace audio_effects::reverb {

inline void process(
    std::vector<float>& samples,
    int channel_count,
    int sample_rate,
    const timeline::AudioReverbParameters& p,
    AudioDspState& state) {
    if (channel_count <= 0 || sample_rate <= 0 || p.mix <= 0.0) return;
    static constexpr double kCombMs[4] = {29.7, 37.1, 41.1, 43.7};
    const float fb = static_cast<float>(0.65 + 0.28 * std::clamp(p.room_size, 0.0, 1.0));
    const float damp = static_cast<float>(1.0 - 0.5 * std::clamp(p.damping, 0.0, 1.0));
    const float mix = static_cast<float>(std::clamp(p.mix, 0.0, 1.0));
    for (int c = 0; c < 4; ++c) {
        const std::size_t len = std::max<std::size_t>(
            16, static_cast<std::size_t>(std::lround(kCombMs[c] * (0.6 + 0.8 * p.room_size) * 0.001 * sample_rate))) *
            static_cast<std::size_t>(channel_count);
        if (state.reverb_combs[c].size() != len) {
            state.reverb_combs[c].assign(len, 0.0F);
            state.reverb_pos[c] = 0;
        }
    }
    const std::size_t frames = samples.size() / static_cast<std::size_t>(channel_count);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t base = frame * static_cast<std::size_t>(channel_count);
        for (int ch = 0; ch < channel_count; ++ch) {
            const float dry = samples[base + ch];
            float wet = 0.0F;
            for (int c = 0; c < 4; ++c) {
                auto& buf = state.reverb_combs[c];
                const std::size_t idx = (state.reverb_pos[c] * static_cast<std::size_t>(channel_count) + ch) % buf.size();
                const float y = buf[idx];
                buf[idx] = dry + y * fb * damp;
                wet += y;
            }
            wet *= 0.25F;
            samples[base + ch] = dry * (1.0F - mix) + wet * mix;
        }
        for (int c = 0; c < 4; ++c) {
            const std::size_t fcount = state.reverb_combs[c].size() / static_cast<std::size_t>(channel_count);
            state.reverb_pos[c] = (state.reverb_pos[c] + 1) % std::max<std::size_t>(1, fcount);
        }
    }
}

#ifdef QT_WIDGETS_LIB
template <typename AddDoubleControlFn>
inline void buildEditor(
    QWidget*,
    QFormLayout* form,
    const timeline::AudioReverbParameters& p,
    AddDoubleControlFn&& add_double_control,
    std::function<void(timeline::AudioReverbParameters)> on_change) {
    auto state = p;
    add_double_control(form, "Room size", "audioFxReverbRoom", 0.0, 100.0, 0, QStringLiteral("%"),
        state.room_size * 100.0, [state, on_change](double v) mutable {
            state.room_size = v / 100.0;
            on_change(state);
        });
    add_double_control(form, "Damping", "audioFxReverbDamping", 0.0, 100.0, 0, QStringLiteral("%"),
        state.damping * 100.0, [state, on_change](double v) mutable {
            state.damping = v / 100.0;
            on_change(state);
        });
    add_double_control(form, "Mix", "audioFxReverbMix", 0.0, 100.0, 0, QStringLiteral("%"),
        state.mix * 100.0, [state, on_change](double v) mutable {
            state.mix = v / 100.0;
            on_change(state);
        });
}
#endif

}  // namespace audio_effects::reverb
