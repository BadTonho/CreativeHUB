#pragma once

#include <creative_suite/effects/color_curves.h>

#include <QWidget>

#include <functional>

namespace ui {

// Qualifier - HSL page of the Color workspace's Sizing dock: Hue, Saturation
// and Luminance range bars with fields, selection tools and a Matte Finesse
// panel.
//
// Stage 2: wired to creative_suite::effects::TonalQualifier when a handler
// is attached; standalone instances keep local Stage 1 defaults.
// Control object names are "colorQualifier*".
class QualifierPage final : public QWidget {
public:
    explicit QualifierPage(QWidget* parent = nullptr);

    void setQualifierChangedHandler(
        std::function<void(const creative_suite::effects::TonalQualifier&)> on_changed);
    void setQualifier(const creative_suite::effects::TonalQualifier& qualifier);

    // Restores every control to its documented default.
    void restoreDefaults();

private:
    void emitQualifier();

    bool updating_ = false;
    std::function<void(const creative_suite::effects::TonalQualifier&)> on_changed_;
};

}  // namespace ui
