#pragma once

#include <QImage>
#include <QColor>
#include <QPoint>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QWidget>

class QEvent;
class QMouseEvent;
class QWheelEvent;

namespace image_editor {

class ImageCanvas final : public QWidget {
    Q_OBJECT

public:
    explicit ImageCanvas(QWidget* parent = nullptr);

    void setImage(QImage image, bool resetView = true);
    void setCropMode(bool enabled);
    void setPaintMode(bool enabled);
    void setBrush(QColor color, int diameter);
    void fitToWindow();
    [[nodiscard]] bool cropMode() const noexcept { return crop_mode_; }
    [[nodiscard]] bool paintMode() const noexcept { return paint_mode_; }
    [[nodiscard]] double zoomFactor() const noexcept { return zoom_; }

signals:
    void cropSelected(const QRect& image_rect);
    void paintStrokeSelected(const QVector<QPointF>& image_points,
                             const QColor& color,
                             int diameter);
    void brushDiameterChanged(int diameter);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    [[nodiscard]] QRectF imageTargetRect() const;
    [[nodiscard]] QRect cropToImageCoordinates(const QRectF& selection) const;
    [[nodiscard]] QPointF widgetToImageCoordinates(const QPointF& position) const;
    void appendPaintPoint(const QPointF& point);
    void updateHoverCursor(const QPointF& position);

    QImage image_;
    double zoom_ = 1.0;
    QPointF pan_;
    bool fit_to_window_ = true;
    bool crop_mode_ = false;
    bool paint_mode_ = false;
    bool selecting_crop_ = false;
    bool painting_ = false;
    bool resizing_brush_ = false;
    bool panning_ = false;
    QPointF crop_start_;
    QRectF crop_selection_;
    QPointF pan_start_;
    QPointF initial_pan_;
    QPointF brush_resize_start_;
    QPoint brush_resize_global_start_;
    int brush_resize_initial_diameter_ = 12;
    QVector<QPointF> paint_points_;
    QColor brush_color_ = Qt::black;
    int brush_diameter_ = 12;
    QPointF brush_cursor_position_;
    bool brush_cursor_visible_ = false;
};

} // namespace image_editor
