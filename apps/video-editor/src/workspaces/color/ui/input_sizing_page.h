#pragma once

#include <QWidget>

namespace ui {

// Sizing - Input Sizing page of the Color workspace's Sizing dock: Sizing
// fields (Pan, Tilt, Zoom, Rotate, Width, Height, Pitch, Yaw, Flip) and a
// disabled Blanking section.
//
// Stage 1 (visual only): controls hold local state and change no image,
// transform or project data. This is separate from the clip transform controls
// in the Edit workspace. See docs/video-editor/features/COLOR_SIZING_DOCK.md.
// Control object names are "colorInputSizing*".
class InputSizingPage final : public QWidget {
public:
    explicit InputSizingPage(QWidget* parent = nullptr);

    // Restores every control to its documented default.
    void restoreDefaults();
};

}  // namespace ui
