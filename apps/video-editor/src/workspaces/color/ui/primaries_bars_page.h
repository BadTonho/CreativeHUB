#pragma once

#include <QWidget>

namespace ui {

// Primaries - Color Bars page of the Color workspace's Motion Effects dock.
//
// Stage 1 (visual only): sliders and number fields are linked to each other
// and hold local state; they do not edit the Primary Grade node, the image or
// project data. Binding to the existing video.primary_grade parameters is
// stage 2; see docs/video-editor/features/COLOR_PANELS_DOCK.md. Control object
// names are "colorPrimariesBars*".
class PrimariesBarsPage final : public QWidget {
public:
    explicit PrimariesBarsPage(QWidget* parent = nullptr);

    // Restores every control to its documented default.
    void restoreDefaults();
};

}  // namespace ui
