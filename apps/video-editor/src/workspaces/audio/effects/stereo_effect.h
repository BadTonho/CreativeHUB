#pragma once

#include "timeline/audio_effects.h"

#include <algorithm>
#include <vector>

#ifdef QT_WIDGETS_LIB
#include <QCheckBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QWidget>
#include <functional>
#endif

namespace audio_effects::stereo {

inline void process(
    std::vector<float>& samples,
    int channel_count,
    const timeline::AudioStereoParameters& p) {
    if (channel_count < 2) return;
    const float width = p.mono_sum ? 0.0F : static_cast<float>(std::clamp(p.width, 0.0, 2.0));
    const float left_bal = static_cast<float>(std::clamp(1.0 - std::max(0.0, p.balance), 0.0, 1.0));
    const float right_bal = static_cast<float>(std::clamp(1.0 + std::min(0.0, p.balance), 0.0, 1.0));
    const std::size_t frames = samples.size() / static_cast<std::size_t>(channel_count);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const std::size_t base = frame * static_cast<std::size_t>(channel_count);
        const float l = samples[base] * left_bal;
        const float r = samples[base + 1] * right_bal;
        const float mid = 0.5F * (l + r);
        const float side = 0.5F * (l - r) * width;
        samples[base] = mid + side;
        samples[base + 1] = mid - side;
    }
}

#ifdef QT_WIDGETS_LIB
template <typename AddDoubleControlFn>
inline void buildEditor(
    QWidget* parent,
    QFormLayout* form,
    const timeline::AudioStereoParameters& p,
    AddDoubleControlFn&& add_double_control,
    std::function<void(timeline::AudioStereoParameters)> on_change) {
    auto state = p;
    add_double_control(form, "Stereo width", "audioFxStereoWidth", 0.0, 200.0, 0, QStringLiteral("%"),
        state.width * 100.0, [state, on_change](double v) mutable {
            state.width = v / 100.0;
            on_change(state);
        });
    add_double_control(form, "Balance", "audioFxStereoBalance", -100.0, 100.0, 0, QStringLiteral("%"),
        state.balance * 100.0, [state, on_change](double v) mutable {
            state.balance = v / 100.0;
            on_change(state);
        });
    auto* mono = new QCheckBox(QCoreApplication::translate("AudioWorkspace", "Mono sum"), parent);
    mono->setChecked(state.mono_sum);
    form->addRow(mono);
    QObject::connect(mono, &QCheckBox::toggled, parent, [state, on_change](bool v) mutable {
        state.mono_sum = v;
        on_change(state);
    });
}
#endif

}  // namespace audio_effects::stereo
