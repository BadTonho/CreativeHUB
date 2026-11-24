#pragma once

#include "workspaces/audio/effects/audio_effect_dsp_common.h"

#include <cmath>
#include <vector>

#ifdef QT_WIDGETS_LIB
#include <QFormLayout>
#include <QWidget>
#include <functional>
#endif

namespace audio_effects::lfe_filter {

inline void process(
    std::vector<float>& samples,
    int channel_count,
    int sample_rate,
    const timeline::AudioLfeFilterParameters& p,
    AudioDspState& state) {
    if (channel_count <= 0 || sample_rate <= 0) return;
    if (static_cast<int>(state.lfe_delays.size()) != channel_count) {
        state.lfe_delays.assign(static_cast<std::size_t>(channel_count), {});
    }
    const double freq = safeFrequency(p.cutoff_hz, sample_rate);
    const double omega = 2.0 * 3.14159265358979323846 * freq / sample_rate;
    const double sin_w = std::sin(omega);
    const double cos_w = std::cos(omega);
    const double alpha = sin_w / std::sqrt(2.0);
    const auto coeffs = normalizeBiquad(
        (1.0 - cos_w) * 0.5, 1.0 - cos_w, (1.0 - cos_w) * 0.5,
        1.0 + alpha, -2.0 * cos_w, 1.0 - alpha);
    const std::size_t frames = samples.size() / static_cast<std::size_t>(channel_count);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t base = frame * static_cast<std::size_t>(channel_count);
        for (int ch = 0; ch < channel_count; ++ch) {
            auto& d = state.lfe_delays[static_cast<std::size_t>(ch)];
            const double x = samples[base + ch];
            const double y = coeffs.b0 * x + coeffs.b1 * d.x1 + coeffs.b2 * d.x2 -
                coeffs.a1 * d.y1 - coeffs.a2 * d.y2;
            d.x2 = d.x1; d.x1 = x; d.y2 = d.y1; d.y1 = y;
            samples[base + ch] = static_cast<float>(y);
        }
    }
}

#ifdef QT_WIDGETS_LIB
template <typename AddDoubleControlFn>
inline void buildEditor(
    QWidget*,
    QFormLayout* form,
    const timeline::AudioLfeFilterParameters& p,
    AddDoubleControlFn&& add_double_control,
    std::function<void(timeline::AudioLfeFilterParameters)> on_change) {
    auto state = p;
    add_double_control(form, "Cutoff frequency", "audioFxLfeCutoff", 40.0, 250.0, 0, QStringLiteral(" Hz"),
        state.cutoff_hz, [state, on_change](double v) mutable {
            state.cutoff_hz = v;
            on_change(state);
        });
}
#endif

}  // namespace audio_effects::lfe_filter
