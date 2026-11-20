#pragma once

#include <QWidget>

namespace ui {

// RGB Mixer page of the Color workspace's Motion Effects dock.
//
// Stage 1 (visual only): the bars and value fields are linked to each other
// and hold local state; they do not edit any grade, the image or project data.
// There is no channel-mixer effect yet; see docs/video-editor/features/
// COLOR_PANELS_DOCK.md. Control object names are "colorRgbMixer*".
class RgbMixerPage final : public QWidget {
public:
    explicit RgbMixerPage(QWidget* parent = nullptr);

    // Restores every control to its documented default (identity mix,
    // Monochrome off, Preserve Luminance on).
    void restoreDefaults();
};

}  // namespace ui
