#pragma once

#include "workspaces/audio/effects/audio_effect_dsp_common.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef QT_WIDGETS_LIB
#include <QCheckBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QWidget>
#include <functional>
#endif

namespace audio_effects::limiter {

inline void process(
    std::vector<float>& samples,
    int channel_count,
    int sample_rate,
    const timeline::AudioLimiterParameters& p,
    AudioDspState& state) {
    if (channel_count <= 0 || sample_rate <= 0) return;
    const double ceiling = std::pow(10.0, p.ceiling_db / 20.0);
    if (p.soft_clip) {
        const float c = static_cast<float>(ceiling);
        for (float& s : samples) {
            s = c * std::tanh(s / std::max(1.0e-5F, c));
        }
        return;
    }
    const double release_coeff = std::exp(-1.0 / (std::max(1.0, p.release_ms) * 0.001 * sample_rate));
    const std::size_t frames = samples.size() / static_cast<std::size_t>(channel_count);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t base = frame * static_cast<std::size_t>(channel_count);
        double peak = 0.0;
        for (int ch = 0; ch < channel_count; ++ch) {
            peak = std::max(peak, static_cast<double>(std::abs(samples[base + ch])));
        }
        const double target_gain = peak > ceiling ? (ceiling / peak) : 1.0;
        if (target_gain < state.limiter_gain) {
            state.limiter_gain = target_gain;
        } else {
            state.limiter_gain = release_coeff * state.limiter_gain + (1.0 - release_coeff) * target_gain;
        }
        const float g = static_cast<float>(state.limiter_gain);
        for (int ch = 0; ch < channel_count; ++ch) {
            samples[base + ch] *= g;
        }
    }
}

#ifdef QT_WIDGETS_LIB
template <typename AddDoubleControlFn>
inline void buildEditor(
    QWidget* parent,
    QFormLayout* form,
    const timeline::AudioLimiterParameters& p,
    AddDoubleControlFn&& add_double_control,
    std::function<void(timeline::AudioLimiterParameters)> on_change) {
    auto state = p;
    add_double_control(form, "Ceiling", "audioFxLimiterCeiling", -12.0, 0.0, 1, QStringLiteral(" dB"),
        state.ceiling_db, [state, on_change](double v) mutable {
            state.ceiling_db = v;
            on_change(state);
        });
    add_double_control(form, "Release", "audioFxLimiterRelease", 5.0, 500.0, 0, QStringLiteral(" ms"),
        state.release_ms, [state, on_change](double v) mutable {
            state.release_ms = v;
            on_change(state);
        });
    auto* soft = new QCheckBox(QCoreApplication::translate("AudioWorkspace", "Soft clip"), parent);
    soft->setChecked(state.soft_clip);
    form->addRow(soft);
    QObject::connect(soft, &QCheckBox::toggled, parent, [state, on_change](bool v) mutable {
        state.soft_clip = v;
        on_change(state);
    });
}
#endif

}  // namespace audio_effects::limiter
