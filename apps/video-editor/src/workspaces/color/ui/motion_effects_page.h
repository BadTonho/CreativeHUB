#pragma once

#include <QWidget>

namespace ui {

// Motion Effects page of the Color workspace's Motion Effects dock: temporal
// noise reduction, spatial noise reduction and motion blur.
//
// Stage 1 (visual only): controls hold local state and change no image,
// effect or project data. There is no noise-reduction or motion-blur
// processing yet; see docs/video-editor/features/COLOR_PANELS_DOCK.md.
// Control object names are "colorMotionEffects*".
class MotionEffectsPage final : public QWidget {
public:
    explicit MotionEffectsPage(QWidget* parent = nullptr);

    // Restores every control to its documented default.
    void restoreDefaults();

private:
    void updateTemporalControlsEnabled();
};

}  // namespace ui
