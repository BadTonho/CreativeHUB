#pragma once

#include <QWidget>

namespace ui {

// Color Slice page of the Color workspace's Sizing dock: a global row plus one
// column of controls for each of seven color slices (Red, Skin, Yellow, Green,
// Cyan, Blue, Magenta).
//
// Stage 1 (visual only): controls hold local state and change no image, grade
// or project data. See docs/video-editor/features/COLOR_SIZING_DOCK.md.
// Control object names are "colorSlice*".
class ColorSlicePage final : public QWidget {
public:
    explicit ColorSlicePage(QWidget* parent = nullptr);

    // Restores every control to its documented default.
    void restoreDefaults();
};

}  // namespace ui
