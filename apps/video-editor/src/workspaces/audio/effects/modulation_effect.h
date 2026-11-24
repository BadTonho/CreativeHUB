#pragma once

#include "workspaces/audio/effects/audio_effect_dsp_common.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef QT_WIDGETS_LIB
#include <QComboBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QWidget>
#include <functional>
#endif

namespace audio_effects::modulation {

inline void process(
    std::vector<float>& samples,
    int channel_count,
    int sample_rate,
    const timeline::AudioModulationParameters& p,
    AudioDspState& state) {
    if (channel_count <= 0 || sample_rate <= 0 || p.mix <= 0.0 || p.depth <= 0.0) return;
    const std::size_t buf_frames = std::max<std::size_t>(2048, static_cast<std::size_t>(sample_rate * 0.1));
    const std::size_t needed = buf_frames * static_cast<std::size_t>(channel_count);
    if (state.mod_buffer.size() != needed) {
        state.mod_buffer.assign(needed, 0.0F);
        state.mod_write = 0;
    }
    const double base_ms = (p.mode == 1) ? 2.0 : 12.0;
    const double sweep_ms = (p.mode == 1) ? 3.0 * p.depth : 6.0 * p.depth;
    const float mix = static_cast<float>(std::clamp(p.mix, 0.0, 1.0));
    const double phase_inc = 2.0 * 3.14159265358979323846 * p.rate_hz / sample_rate;
    const std::size_t frames = samples.size() / static_cast<std::size_t>(channel_count);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double delay_samples = (base_ms + sweep_ms * (0.5 + 0.5 * std::sin(state.mod_phase))) * 0.001 * sample_rate;
        state.mod_phase += phase_inc;
        if (state.mod_phase > 6.283185307179586) state.mod_phase -= 6.283185307179586;
        const std::size_t d0 = std::min<std::size_t>(buf_frames - 2, static_cast<std::size_t>(delay_samples));
        const float frac = static_cast<float>(delay_samples - d0);
        const std::size_t r0 = (state.mod_write + buf_frames - d0) % buf_frames;
        const std::size_t r1 = (state.mod_write + buf_frames - d0 - 1) % buf_frames;
        const std::size_t base = frame * static_cast<std::size_t>(channel_count);
        for (int ch = 0; ch < channel_count; ++ch) {
            const float dry = samples[base + ch];
            state.mod_buffer[state.mod_write * static_cast<std::size_t>(channel_count) + ch] = dry;
            const float s0 = state.mod_buffer[r0 * static_cast<std::size_t>(channel_count) + ch];
            const float s1 = state.mod_buffer[r1 * static_cast<std::size_t>(channel_count) + ch];
            const float wet = s0 + (s1 - s0) * frac;
            samples[base + ch] = dry * (1.0F - mix) + wet * mix;
        }
        state.mod_write = (state.mod_write + 1) % buf_frames;
    }
}

#ifdef QT_WIDGETS_LIB
template <typename AddDoubleControlFn>
inline void buildEditor(
    QWidget* parent,
    QFormLayout* form,
    const timeline::AudioModulationParameters& p,
    AddDoubleControlFn&& add_double_control,
    std::function<void(timeline::AudioModulationParameters)> on_change) {
    auto state = p;
    auto* mode = new QComboBox(parent);
    mode->addItem(QCoreApplication::translate("AudioWorkspace", "Chorus"), 0);
    mode->addItem(QCoreApplication::translate("AudioWorkspace", "Flanger"), 1);
    mode->addItem(QCoreApplication::translate("AudioWorkspace", "Modulation"), 2);
    mode->setCurrentIndex(mode->findData(state.mode));
    form->addRow(QCoreApplication::translate("AudioWorkspace", "Mode"), mode);
    QObject::connect(mode, qOverload<int>(&QComboBox::currentIndexChanged), parent, [state, on_change, mode](int) mutable {
        state.mode = mode->currentData().toInt();
        on_change(state);
    });
    add_double_control(form, "Rate", "audioFxModRate", 0.1, 10.0, 1, QStringLiteral(" Hz"),
        state.rate_hz, [state, on_change](double v) mutable {
            state.rate_hz = v;
            on_change(state);
        });
    add_double_control(form, "Depth", "audioFxModDepth", 0.0, 100.0, 0, QStringLiteral("%"),
        state.depth * 100.0, [state, on_change](double v) mutable {
            state.depth = v / 100.0;
            on_change(state);
        });
    add_double_control(form, "Mix", "audioFxModMix", 0.0, 100.0, 0, QStringLiteral("%"),
        state.mix * 100.0, [state, on_change](double v) mutable {
            state.mix = v / 100.0;
            on_change(state);
        });
}
#endif

}  // namespace audio_effects::modulation
