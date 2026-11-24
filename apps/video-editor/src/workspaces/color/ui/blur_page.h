#pragma once

#include <creative_suite/effects/effects.h>

#include <QWidget>

#include <functional>

namespace ui {

// Blur - Blur page of the Color workspace's Sizing dock: Radius, H/V Ratio and
// Scaling bars per color channel, link toggles and Coring/Level/Mix footer.
//
// Stage 2: wired to video.color_blur when a handler is attached; standalone
// instances keep local Stage 1 defaults. Control object names are "colorBlur*".
class BlurPage final : public QWidget {
public:
    explicit BlurPage(QWidget* parent = nullptr);

    void setEffectChangedHandler(
        std::function<void(const creative_suite::effects::EffectInstance&)> on_changed);
    void setEffect(const creative_suite::effects::EffectInstance& effect);

    // Restores every control to its documented default.
    void restoreDefaults();

private:
    void emitEffect();

    bool updating_ = false;
    int mode_ = 0;
    std::function<void(const creative_suite::effects::EffectInstance&)> on_changed_;
};

}  // namespace ui
