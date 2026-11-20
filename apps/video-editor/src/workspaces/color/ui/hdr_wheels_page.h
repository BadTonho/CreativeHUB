#pragma once

#include <QWidget>

namespace ui {

// High Dynamic Range - Color Wheels page of the Color workspace's Motion
// Effects dock.
//
// Stage 1 (visual only): the wheels, sliders and fields are linked to each
// other and hold local state; they do not edit any grade, the image or project
// data. There is no HDR grade effect yet; see docs/video-editor/features/
// COLOR_PANELS_DOCK.md. Control object names are "colorHdrWheels*".
class HdrWheelsPage final : public QWidget {
public:
    explicit HdrWheelsPage(QWidget* parent = nullptr);

    // Restores every control to its documented default.
    void restoreDefaults();
};

}  // namespace ui
