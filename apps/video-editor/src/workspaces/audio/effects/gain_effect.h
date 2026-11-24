#pragma once

#include "timeline/audio_effects.h"

#include <cmath>
#include <vector>

#ifdef QT_WIDGETS_LIB
#include <QCheckBox>
#include <QCoreApplication>
#include <QFormLayout>
#include <QWidget>
#include <functional>
#endif

namespace audio_effects::gain {

inline void process(
    std::vector<float>& samples,
    const timeline::AudioGainParameters& p) {
    const float scale = static_cast<float>(std::pow(10.0, p.gain_db / 20.0)) *
        (p.invert_phase ? -1.0F : 1.0F);
    for (float& s : samples) s *= scale;
}

#ifdef QT_WIDGETS_LIB
template <typename AddDoubleControlFn>
inline void buildEditor(
    QWidget* parent,
    QFormLayout* form,
    const timeline::AudioGainParameters& p,
    AddDoubleControlFn&& add_double_control,
    std::function<void(timeline::AudioGainParameters)> on_change) {
    auto state = p;
    add_double_control(form, "Gain", "audioFxGainDb", -24.0, 24.0, 1, QStringLiteral(" dB"),
        state.gain_db, [state, on_change](double v) mutable {
            state.gain_db = v;
            on_change(state);
        });
    auto* inv = new QCheckBox(QCoreApplication::translate("AudioWorkspace", "Invert phase"), parent);
    inv->setChecked(state.invert_phase);
    form->addRow(inv);
    QObject::connect(inv, &QCheckBox::toggled, parent, [state, on_change](bool v) mutable {
        state.invert_phase = v;
        on_change(state);
    });
}
#endif

}  // namespace audio_effects::gain
