#pragma once

#include <QImage>
#include <QPointF>
#include <QRect>
#include <QWidget>

class QMouseEvent;
class QWheelEvent;

namespace image_editor {

class ImageCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit ImageCanvas(QWidget* parent = nullptr);

    void setImage(QImage image);
    void setCropMode(bool enabled);
    void fitToWindow();
    [[nodiscard]] bool cropMode() const noexcept { return crop_mode_; }
    [[nodiscard]] double zoomFactor() const noexcept { return zoom_; }

signals:
    void cropSelected(const QRect& image_rect);
    void cropModeCancelled();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    [[nodiscard]] QRectF imageTargetRect() const;
    [[nodiscard]] QRect cropToImageCoordinates(const QRectF& selection) const;

    QImage image_;
    double zoom_ = 1.0;
    QPointF pan_;
    bool fit_to_window_ = true;
    bool crop_mode_ = false;
    bool selecting_crop_ = false;
    bool panning_ = false;
    QPointF crop_start_;
    QRectF crop_selection_;
    QPointF pan_start_;
    QPointF initial_pan_;
};

} // namespace image_editor
