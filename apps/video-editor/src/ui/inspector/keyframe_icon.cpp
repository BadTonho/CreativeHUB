#include "ui/inspector/keyframe_icon.h"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>

#include <algorithm>

namespace ui {
namespace {

class KeyframeIconEngine final : public QIconEngine {
public:
    QIconEngine* clone() const override { return new KeyframeIconEngine; }
    QString key() const override { return QStringLiteral("InspectorKeyframeDiamond"); }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode,
                   QIcon::State state) override {
        QPixmap result(size);
        result.fill(Qt::transparent);
        QPainter painter(&result);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return result;
    }

    void paint(QPainter* painter, const QRect& rect,
               QIcon::Mode mode, QIcon::State state) override {
        if (painter == nullptr || rect.isEmpty()) return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const qreal side = std::min(rect.width(), rect.height());
        const qreal line_width = std::max(1.0, side * 0.09);
        const qreal inset = line_width;
        const QRectF bounds(rect);
        const QPointF center = bounds.center();
        const qreal half_side = side / 2.0 - inset;
        const bool filled = state == QIcon::On;
        QColor color = filled ? QColor(QStringLiteral("#e8b94f"))
                              : QColor(QStringLiteral("#d5dbe5"));
        if (mode == QIcon::Disabled) color.setAlpha(110);

        QPainterPath diamond;
        diamond.moveTo(center.x(), center.y() - half_side);
        diamond.lineTo(center.x() + half_side, center.y());
        diamond.lineTo(center.x(), center.y() + half_side);
        diamond.lineTo(center.x() - half_side, center.y());
        diamond.closeSubpath();

        painter->setPen(QPen(color, line_width, Qt::SolidLine,
                             Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(filled ? QBrush(color) : QBrush(Qt::NoBrush));
        painter->drawPath(diamond);
        painter->restore();
    }
};

} // namespace

QIcon keyframeIcon() {
    return QIcon(new KeyframeIconEngine);
}

} // namespace ui
