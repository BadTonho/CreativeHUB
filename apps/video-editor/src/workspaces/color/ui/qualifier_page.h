#pragma once

#include <QWidget>

namespace ui {

// Qualifier - HSL page of the Color workspace's Sizing dock: Hue, Saturation
// and Luminance range bars with fields, selection tools and a Matte Finesse
// panel.
//
// Stage 1 (visual only): controls hold local state and change no image, matte
// or project data. This is separate from the Color graph's existing qualifier
// matte refinement. See docs/video-editor/features/COLOR_SIZING_DOCK.md.
// Control object names are "colorQualifier*".
class QualifierPage final : public QWidget {
public:
    explicit QualifierPage(QWidget* parent = nullptr);

    // Restores every control to its documented default.
    void restoreDefaults();
};

}  // namespace ui
