#pragma once

#include <creative_suite/effects/effects.h>

#include <QWidget>

#include <functional>

namespace ui {

// Sizing - Input Sizing page of the Color workspace's Sizing dock: Sizing
// fields (Pan, Tilt, Zoom, Rotate, Width, Height, Pitch, Yaw, Flip).
//
// Stage 2: wired to video.input_sizing (Input Sizing + Node Sizing modes) when
// a handler is attached; standalone instances keep local Stage 1 defaults.
// Control object names are "colorInputSizing*".
class InputSizingPage final : public QWidget {
public:
    explicit InputSizingPage(QWidget* parent = nullptr);

    void setEffectChangedHandler(
        std::function<void(const creative_suite::effects::EffectInstance&)> on_changed);
    void setEffect(const creative_suite::effects::EffectInstance& effect);

    // Restores every control to its documented default.
    void restoreDefaults();

private:
    void emitEffect();

    bool updating_ = false;
    int mode_ = 1;
    std::function<void(const creative_suite::effects::EffectInstance&)> on_changed_;
};

}  // namespace ui
