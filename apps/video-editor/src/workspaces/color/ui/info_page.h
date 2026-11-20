#pragma once

#include <QString>
#include <QWidget>

namespace ui {

// Info page of the Color workspace's Keyframes dock: read-only Clip and System
// details shown as label / value pairs.
//
// Stage 1 (visual only): clip fields show a placeholder dash and system fields
// show their default settings; nothing is read from the selected clip or the
// project. See docs/video-editor/features/COLOR_KEYFRAMES_DOCK.md. Label and
// value object names are "colorInfo<Key>Label" and "colorInfo<Key>Value", with
// the keys listed in info_page.cpp.
class InfoPage final : public QWidget {
public:
    explicit InfoPage(QWidget* parent = nullptr);

    // Sets the text of one value by its key (for example "ClipFrameRate").
    // Unknown keys are ignored. This is the hook stage 2 will feed from the
    // selected clip.
    void setValue(const QString& key, const QString& text);

    // Restores the placeholder and default values.
    void restoreDefaults();
};

}  // namespace ui
