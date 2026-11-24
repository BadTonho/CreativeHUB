#pragma once

#include <QWidget>

#include <functional>

namespace ui {

// Tracker - Window page of the Color workspace's Sizing dock: transport
// buttons, tracking-channel toggles, a timeline graph with a draggable
// playhead, and the tracker method footer.
//
// Stage 2: wired to PowerWindowTracker in ColorWorkspace when handlers are
// attached; standalone instances keep local Stage 1 behavior.
// Control object names are "colorTracker*".
class TrackerPage final : public QWidget {
public:
    explicit TrackerPage(QWidget* parent = nullptr);

    void setHandlers(std::function<void(int direction)> on_track,
                     std::function<void()> on_stop,
                     std::function<void()> on_clear);
    void setTrackingState(bool has_window, bool tracking_active);

    // Restores every control and the playhead to their documented defaults.
    void restoreDefaults();

private:
    std::function<void(int)> on_track_;
    std::function<void()> on_stop_;
    std::function<void()> on_clear_;
};

}  // namespace ui
