#pragma once

#include <QWidget>

namespace ui {

// Window page of the Color workspace's Sizing dock: a list of shape windows
// with add/delete actions and Transform and Softness fields for the selected
// one.
//
// Stage 1 (visual only): rows, selection and fields hold local state and
// change no image, matte or project data. This is separate from the Color
// graph's existing Power Windows. See docs/video-editor/features/
// COLOR_SIZING_DOCK.md. Control object names are "colorWindow*".
class WindowPage final : public QWidget {
public:
    explicit WindowPage(QWidget* parent = nullptr);

    // Restores the five sample rows, no selection and every field's default.
    void restoreDefaults();
};

}  // namespace ui
