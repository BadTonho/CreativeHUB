#pragma once

#include <QWidget>

namespace ui {

// Blur - Blur page of the Color workspace's Sizing dock: Radius, H/V Ratio and
// Scaling bars per color channel, link toggles and a disabled footer row.
//
// Stage 1 (visual only): controls hold local state and change no image, effect
// or project data. See docs/video-editor/features/COLOR_SIZING_DOCK.md. This
// is separate from the existing Gaussian Blur effect. Control object names are
// "colorBlur*".
class BlurPage final : public QWidget {
public:
    explicit BlurPage(QWidget* parent = nullptr);

    // Restores every control to its documented default.
    void restoreDefaults();
};

}  // namespace ui
