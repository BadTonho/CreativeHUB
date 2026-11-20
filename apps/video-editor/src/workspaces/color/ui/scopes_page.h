#pragma once

#include <QWidget>

namespace ui {

// Scopes page of the Color workspace's Keyframes dock: a mode dropdown and an
// empty graticule for the selected scope kind.
//
// Stage 1 (visual only): the graph draws only its graticule and a "No frame"
// message; no image is analyzed. The Color workspace's existing scope panel is
// unchanged and is not used here yet. See docs/video-editor/features/
// COLOR_KEYFRAMES_DOCK.md. Control object names are "colorScopes*".
class ScopesPage final : public QWidget {
public:
    explicit ScopesPage(QWidget* parent = nullptr);

    // Restores the Parade mode.
    void restoreDefaults();
};

}  // namespace ui
