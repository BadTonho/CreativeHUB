#pragma once

#include "media/video_frame.h"

#include <QImage>
#include <QLabel>

class QResizeEvent;

class PreviewWidget final : public QLabel {
public:
    explicit PreviewWidget(QWidget* parent = nullptr);

    void setFrame(const media::VideoFrame& frame);
    void clearFrame(const QString& message);

protected:
    void resizeEvent(QResizeEvent* event) override;

private:
    void updatePixmap();

    QImage frame_image_;
};
