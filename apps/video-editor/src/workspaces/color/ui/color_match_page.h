#pragma once

#include <QWidget>

namespace ui {

// Color Match page of the Color workspace's Motion Effects dock.
//
// Stage 1 (visual only): the chart, presets and controls hold local state and
// change no image, grade or project data. Chart alignment, sampling and the
// Match computation are not implemented; see docs/video-editor/features/
// COLOR_PANELS_DOCK.md. Control object names are "colorColorMatch*".
class ColorMatchPage final : public QWidget {
public:
    explicit ColorMatchPage(QWidget* parent = nullptr);

    // Restores the preset, gamma/color-space choices, color temperature and
    // white level to their documented defaults.
    void restoreDefaults();
};

}  // namespace ui
