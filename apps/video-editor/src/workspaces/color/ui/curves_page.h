#pragma once

#include <QWidget>

namespace ui {

// Curves - Custom page of the Color workspace's Sizing dock.
//
// Stage 1 (visual only): the curve graph, channel toggles, strength sliders
// and Soft Clip fields hold local state; they do not edit the Color graph's
// Curves node, the image or project data. The graph draws straight segments
// between its points, not a spline. See docs/video-editor/features/
// COLOR_SIZING_DOCK.md. Control object names are "colorCurves*".
class CurvesPage final : public QWidget {
public:
    explicit CurvesPage(QWidget* parent = nullptr);

    // Restores every control and the graph to their documented defaults.
    void restoreDefaults();
};

}  // namespace ui
