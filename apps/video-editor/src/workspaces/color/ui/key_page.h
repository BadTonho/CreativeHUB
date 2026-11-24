#pragma once

#include <creative_suite/effects/effects.h>

#include <QWidget>

#include <functional>

namespace ui {

// Key page of the Color workspace's Sizing dock: a matte preview and the Key
// Input, Key Output and Qualifier gain/offset sections.
//
// Stage 2: wired to video.color_key when a handler is attached; standalone
// instances keep local Stage 1 defaults. Control object names are "colorKey*".
class KeyPage final : public QWidget {
public:
    explicit KeyPage(QWidget* parent = nullptr);

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
