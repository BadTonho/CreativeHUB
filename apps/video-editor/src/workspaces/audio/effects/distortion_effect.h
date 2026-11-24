#pragma once

#include "timeline/audio_effects.h"

#include <algorithm>
#include <cmath>
#include <vector>

#ifdef QT_WIDGETS_LIB
#include <QFormLayout>
#include <QWidget>
#include <functional>
#endif

namespace audio_effects::distortion {

inline void process(
    std::vector<float>& samples,
    const timeline::AudioDistortionParameters& p) {
    if (p.drive <= 0.0 || p.mix <= 0.0) return;
    const float gain = static_cast<float>(1.0 + p.drive * 14.0);
    const float mix = static_cast<float>(std::clamp(p.mix, 0.0, 1.0));
    const float norm = 1.0F / std::tanh(gain);
    for (float& s : samples) {
        const float wet = std::tanh(s * gain) * norm;
        s = s * (1.0F - mix) + wet * mix;
    }
}

#ifdef QT_WIDGETS_LIB
template <typename AddDoubleControlFn>
inline void buildEditor(
    QWidget*,
    QFormLayout* form,
    const timeline::AudioDistortionParameters& p,
    AddDoubleControlFn&& add_double_control,
    std::function<void(timeline::AudioDistortionParameters)> on_change) {
    auto state = p;
    add_double_control(form, "Drive", "audioFxDistDrive", 0.0, 100.0, 0, QStringLiteral("%"),
        state.drive * 100.0, [state, on_change](double v) mutable {
            state.drive = v / 100.0;
            on_change(state);
        });
    add_double_control(form, "Mix", "audioFxDistMix", 0.0, 100.0, 0, QStringLiteral("%"),
        state.mix * 100.0, [state, on_change](double v) mutable {
            state.mix = v / 100.0;
            on_change(state);
        });
}
#endif

}  // namespace audio_effects::distortion
