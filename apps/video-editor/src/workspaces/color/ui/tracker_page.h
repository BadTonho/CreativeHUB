#pragma once

#include <QWidget>

namespace ui {

// Tracker - Window page of the Color workspace's Sizing dock: transport
// buttons, tracking-channel toggles, a timeline graph with a draggable
// playhead, and the tracker method footer.
//
// Stage 1 (visual only): controls hold local state; nothing is tracked, played
// or persisted. This is separate from the Color graph's existing Power Window
// tracker. See docs/video-editor/features/COLOR_SIZING_DOCK.md. Control object
// names are "colorTracker*".
class TrackerPage final : public QWidget {
public:
    explicit TrackerPage(QWidget* parent = nullptr);

    // Restores every control and the playhead to their documented defaults.
    void restoreDefaults();
};

}  // namespace ui
