#pragma once

#include <QWidget>

namespace ui {

// Keyframes page of the Color workspace's Keyframes dock: a track list
// (Master, Corrector 1, Sizing) beside a timeline with a playhead and keyframe
// markers.
//
// Stage 1 (visual only): the tracks are fixed sample rows and the keyframes are
// local markers; nothing is read from or written to the Color graph or the
// project. See docs/video-editor/features/COLOR_KEYFRAMES_DOCK.md. Control
// object names are "colorKeyframes*".
class KeyframesPage final : public QWidget {
public:
    explicit KeyframesPage(QWidget* parent = nullptr);

    // Restores the playhead, the sample keyframes and every control.
    void restoreDefaults();
};

}  // namespace ui
