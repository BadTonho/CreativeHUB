#pragma once

#include <QWidget>

class QLabel;

namespace ui {

// Camera Raw page of the Color workspace's Motion Effects dock.
//
// Stage 1 (visual only): the controls hold local state and change no image,
// effect parameter or project data. Raw-source detection, decoding and
// persistence are not implemented; see docs/video-editor/features/
// COLOR_PANELS_DOCK.md. Control defaults are exposed through the object names
// documented there ("colorCameraRaw*").
class CameraRawPage final : public QWidget {
public:
    explicit CameraRawPage(QWidget* parent = nullptr);

    // Text for the detected camera format shown in the page header. Empty
    // when no raw source is selected.
    void setCameraFormat(const QString& text);

    // Restores every control to its documented default.
    void restoreDefaults();

private:
    void updateDecodeControlsEnabled();

    QLabel* camera_format_ = nullptr;
};

}  // namespace ui
