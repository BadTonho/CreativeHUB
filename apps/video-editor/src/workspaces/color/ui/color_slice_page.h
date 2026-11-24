#pragma once

#include <creative_suite/effects/effects.h>

#include <QWidget>

#include <functional>

namespace ui {

// Color Slice page of the Color workspace's Sizing dock: a global row plus one
// column of controls for each of seven color slices (Red, Skin, Yellow, Green,
// Cyan, Blue, Magenta).
//
// Stage 2: wired to video.color_slice (SliderA = Density, SliderB = Saturation)
// when a handler is attached; standalone instances keep local Stage 1 defaults.
// Control object names are "colorSlice*".
class ColorSlicePage final : public QWidget {
public:
    explicit ColorSlicePage(QWidget* parent = nullptr);

    void setEffectChangedHandler(
        std::function<void(const creative_suite::effects::EffectInstance&)> on_changed);
    void setEffect(const creative_suite::effects::EffectInstance& effect);

    // Restores every control to its documented default.
    void restoreDefaults();

private:
    void emitEffect();

    bool updating_ = false;
    std::function<void(const creative_suite::effects::EffectInstance&)> on_changed_;
};

}  // namespace ui
