#pragma once

#include <QWidget>

namespace ui {

// Smart Mask page of the Color workspace's Sizing dock: a toolbar, a timeline
// with an empty-mask message and a Quality / refinement panel.
//
// Stage 1 (visual only): controls hold local state; no mask is created,
// tracked or refined, and there is no model or inference. See
// docs/video-editor/features/COLOR_SIZING_DOCK.md. Control object names are
// "colorSmartMask*".
class SmartMaskPage final : public QWidget {
public:
    explicit SmartMaskPage(QWidget* parent = nullptr);

    // Restores every control and the playhead to their documented defaults.
    void restoreDefaults();

private:
    void updateRefineEnabled();
};

}  // namespace ui
