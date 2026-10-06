#include "composition_viewer.h"
#include "diagnostics/performance_metrics.h"

#include <QPaintEvent>
#include <QPainter>
#include <QColor>
#include <QPen>
#include <QSizeF>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace motion::ui {
namespace {

const QColor kSurroundColor(45, 48, 54);
const QColor kCanvasColor(224, 226, 230);
const QColor kCanvasBorderColor(24, 26, 30);
const QColor kGuideColor(255, 183, 54);

} // namespace

CompositionViewer::CompositionViewer(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("motion-composition-viewer"));
    setMinimumSize(180, 240);
    setAutoFillBackground(false);
}

void CompositionViewer::setComposition(
    model::CanvasSize canvas_size,
    std::optional<QPointF> selected_layer_anchor)
{
    canvas_size_ = canvas_size;
    selected_layer_anchor_ = std::move(selected_layer_anchor);
    rendered_frame_.reset();
    rendered_frame_generation_ = 0;
    rendered_frame_paint_pending_ = false;
    update();
}

void CompositionViewer::setRenderedFrame(
    creative_suite::media::RgbaFramePtr frame,
    std::uint64_t request_generation)
{
    rendered_frame_ = std::move(frame);
    rendered_frame_generation_ = request_generation;
    rendered_frame_paint_pending_ = rendered_frame_ != nullptr && request_generation != 0;
    update();
}

void CompositionViewer::setSelectedLayerAnchor(std::optional<QPointF> selected_layer_anchor)
{
    selected_layer_anchor_ = std::move(selected_layer_anchor);
    update();
}

creative_suite::media::RgbaFramePtr CompositionViewer::renderedFrame() const noexcept
{
    return rendered_frame_;
}

void CompositionViewer::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    const auto record_paint = [this] {
        if (!rendered_frame_paint_pending_) return;
        rendered_frame_paint_pending_ = false;
        diagnostics::PerformanceMetrics::instance().recordViewerPaint(
            rendered_frame_generation_);
    };

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), kSurroundColor);

    if (canvas_size_.width <= 0 || canvas_size_.height <= 0) {
        record_paint();
        return;
    }

    const QRectF available = QRectF(rect()).adjusted(24.0, 42.0, -24.0, -24.0);
    if (available.width() <= 0.0 || available.height() <= 0.0) {
        record_paint();
        return;
    }

    const double scale = std::min(
        available.width() / static_cast<double>(canvas_size_.width),
        available.height() / static_cast<double>(canvas_size_.height));
    const QSizeF canvas_size(
        static_cast<double>(canvas_size_.width) * scale,
        static_cast<double>(canvas_size_.height) * scale);
    const QRectF canvas_rect(
        available.center().x() - canvas_size.width() / 2.0,
        available.center().y() - canvas_size.height() / 2.0,
        canvas_size.width(),
        canvas_size.height());

    painter.setPen(Qt::NoPen);
    painter.setBrush(kCanvasColor);
    painter.drawRect(canvas_rect);
    if (rendered_frame_ != nullptr && rendered_frame_->width > 0 &&
        rendered_frame_->width <= std::numeric_limits<int>::max() / 4 &&
        rendered_frame_->height > 0 && rendered_frame_->stride >= rendered_frame_->width * 4 &&
        rendered_frame_->rgba_pixels.size() >=
            static_cast<std::size_t>(rendered_frame_->stride) *
                static_cast<std::size_t>(rendered_frame_->height)) {
        const QImage frame_image(
            rendered_frame_->rgba_pixels.data(),
            rendered_frame_->width,
            rendered_frame_->height,
            rendered_frame_->stride,
            QImage::Format_RGBA8888);
        painter.drawImage(canvas_rect, frame_image);
    }
    painter.setPen(QPen(kCanvasBorderColor, 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(canvas_rect);

    painter.setPen(QColor(235, 237, 240));
    painter.drawText(
        QRectF(16.0, 12.0, width() - 32.0, 20.0),
        Qt::AlignLeft | Qt::AlignVCenter,
        QStringLiteral("%1 x %2 px").arg(canvas_size_.width).arg(canvas_size_.height));

    if (!selected_layer_anchor_.has_value()) {
        record_paint();
        return;
    }

    const QPointF guide(
        canvas_rect.left() + selected_layer_anchor_->x() * canvas_rect.width(),
        canvas_rect.top() + selected_layer_anchor_->y() * canvas_rect.height());
    if (!std::isfinite(guide.x()) || !std::isfinite(guide.y()) || !rect().contains(guide.toPoint())) {
        record_paint();
        return;
    }

    painter.setPen(QPen(kGuideColor, 2.0));
    constexpr qreal guide_radius = 9.0;
    painter.drawLine(guide + QPointF(-guide_radius, 0.0), guide + QPointF(guide_radius, 0.0));
    painter.drawLine(guide + QPointF(0.0, -guide_radius), guide + QPointF(0.0, guide_radius));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(guide, 3.5, 3.5);
    record_paint();
}

} // namespace motion::ui
