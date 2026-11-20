#pragma once

#include <QWidget>

namespace ui {

// Key page of the Color workspace's Sizing dock: a matte preview and the Key
// Input, Key Output and Qualifier gain/offset sections.
//
// Stage 1 (visual only): controls hold local state and the preview shows a
// fixed full-white matte; nothing is keyed or persisted. See
// docs/video-editor/features/COLOR_SIZING_DOCK.md. Control object names are
// "colorKey*".
class KeyPage final : public QWidget {
public:
    explicit KeyPage(QWidget* parent = nullptr);

    // Restores every control to its documented default.
    void restoreDefaults();
};

}  // namespace ui
