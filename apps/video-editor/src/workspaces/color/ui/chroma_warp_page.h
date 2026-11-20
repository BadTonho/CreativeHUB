#pragma once

#include <QWidget>

namespace ui {

// Color Warp - Chroma Warp page of the Color workspace's Sizing dock: a color
// plane with draggable pins and a Tools / Pin side panel.
//
// Stage 1 (visual only): pins, tools and sliders hold local state and change no
// image, grade or project data. The plane is an approximate painted gamut, not
// a color-managed chromaticity diagram. See docs/video-editor/features/
// COLOR_SIZING_DOCK.md. Control object names are "colorChromaWarp*".
class ChromaWarpPage final : public QWidget {
public:
    explicit ChromaWarpPage(QWidget* parent = nullptr);

    // Restores the pins, tool, mode, grid and sliders to their documented
    // defaults.
    void restoreDefaults();
};

}  // namespace ui
