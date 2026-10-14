#include "function_palette_item_delegate.h"
#include "ui/effects/effects_catalog.h"

#include <QIconEngine>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <utility>

namespace ui {
namespace {

QColor accentFor(const QString& id) {
    if (id.contains("mask")) return QColor("#d8aa64");
    if (id == "text.text") return QColor("#d591d5");
    if (id == "video.adjustment_clip") return QColor("#ac95df");
    if (id == "video.background" || id == "video.fusion_composition") return QColor("#57bba6");
    if (id.startsWith("audio.")) return QColor("#a89cde");
    return QColor("#729fda");
}

void drawGlyph(QPainter& p, const QString& id) {
    if (id == "video.grayscale") {
        for (int i = 0; i < 3; ++i) {
            p.fillRect(QRectF(3 + i * 5, 4, 4, 12), QColor::fromRgb(100 + i * 65, 100 + i * 65, 100 + i * 65));
        }
    } else if (id == "video.brightness") {
        p.drawEllipse(QRectF(6, 6, 8, 8));
        for (int i = 0; i < 8; ++i) {
            p.save(); p.translate(10, 10); p.rotate(i * 45);
            p.drawLine(QPointF(0, -7), QPointF(0, -9)); p.restore();
        }
    } else if (id == "video.contrast") {
        p.drawEllipse(QRectF(3, 3, 14, 14));
        QPainterPath half; half.moveTo(10, 3); half.arcTo(QRectF(3, 3, 14, 14), 90, 180); half.closeSubpath();
        p.fillPath(half, p.pen().color());
    } else if (id == "video.saturation") {
        QPainterPath drop; drop.moveTo(10, 2);
        drop.cubicTo(8, 6, 3, 9, 4, 13); drop.cubicTo(5, 20, 16, 20, 16, 13);
        drop.cubicTo(17, 9, 12, 6, 10, 2); p.drawPath(drop);
        p.drawLine(QPointF(7, 13), QPointF(9, 15));
    } else if (id == "video.background") {
        p.drawRoundedRect(QRectF(2, 4, 16, 12), 2, 2);
        auto fill = p.pen().color(); fill.setAlpha(110);
        p.fillRect(QRectF(4, 6, 12, 8), fill);
    } else if (id == "video.fusion_composition") {
        p.drawLine(QPointF(6, 5), QPointF(14, 10)); p.drawLine(QPointF(6, 15), QPointF(14, 10));
        p.drawRoundedRect(QRectF(1, 2, 5, 5), 1, 1);
        p.drawRoundedRect(QRectF(1, 13, 5, 5), 1, 1);
        p.drawRoundedRect(QRectF(14, 7, 5, 6), 1, 1);
    } else if (id == "video.adjustment_clip") {
        for (int i = 0; i < 3; ++i) {
            const auto x = 4.0 + i * 6;
            const auto y = i == 1 ? 7.0 : 13.0;
            p.drawLine(QPointF(x, 3), QPointF(x, y - 2));
            p.drawEllipse(QRectF(x - 2, y - 2, 4, 4));
            p.drawLine(QPointF(x, y + 2), QPointF(x, 17));
        }
    } else if (id == "audio.gain") {
        for (int i = 0; i < 4; ++i) {
            const auto height = i % 2 == 0 ? 8.0 : 14.0;
            p.drawRoundedRect(QRectF(3 + i * 4, 10 - height / 2, 2, height), 1, 1);
        }
    } else if (id == "transitions.cross_dissolve") {
        p.drawRect(QRectF(2, 4, 16, 12));
        p.drawLine(QPointF(3, 5), QPointF(17, 15));
        p.drawLine(QPointF(3, 15), QPointF(17, 5));
    } else if (id == "transitions.fade_to_black") {
        for (int i = 0; i < 4; ++i) {
            auto color = p.pen().color(); color.setAlpha(255 - i * 55);
            p.fillRect(QRectF(2 + i * 4, 4, 3, 12), color);
        }
    } else if (id == "text.text") {
        p.drawLine(QPointF(3, 4), QPointF(17, 4));
        p.drawLine(QPointF(3, 4), QPointF(3, 7)); p.drawLine(QPointF(17, 4), QPointF(17, 7));
        p.drawLine(QPointF(10, 4), QPointF(10, 17)); p.drawLine(QPointF(7, 17), QPointF(13, 17));
    } else if (id == "fusion.input") {
        p.drawRoundedRect(QRectF(9, 3, 9, 14), 2, 2);
        p.drawLine(QPointF(1, 10), QPointF(12, 10));
        p.drawLine(QPointF(8, 6), QPointF(12, 10)); p.drawLine(QPointF(8, 14), QPointF(12, 10));
    } else if (id == "fusion.transform") {
        p.drawRect(QRectF(6, 6, 8, 8));
        p.drawLine(QPointF(1, 4), QPointF(1, 1)); p.drawLine(QPointF(1, 1), QPointF(4, 1));
        p.drawLine(QPointF(19, 16), QPointF(19, 19)); p.drawLine(QPointF(19, 19), QPointF(16, 19));
        p.drawLine(QPointF(1, 1), QPointF(5, 5)); p.drawLine(QPointF(15, 15), QPointF(19, 19));
    } else if (id == "fusion.color") {
        p.drawEllipse(QRectF(6, 2, 8, 8));
        p.drawEllipse(QRectF(2, 9, 8, 8)); p.drawEllipse(QRectF(10, 9, 8, 8));
    } else if (id == "fusion.merge") {
        p.drawRoundedRect(QRectF(2, 2, 11, 11), 1, 1);
        p.drawRoundedRect(QRectF(8, 8, 10, 10), 1, 1);
        p.drawLine(QPointF(11, 13), QPointF(15, 13)); p.drawLine(QPointF(13, 11), QPointF(13, 15));
    } else if (id == "fusion.rectangle_mask" || id == "fusion.ellipse_mask") {
        auto pen = p.pen(); pen.setStyle(Qt::DashLine); p.setPen(pen);
        if (id == "fusion.rectangle_mask") p.drawRoundedRect(QRectF(2, 4, 16, 12), 1, 1);
        else p.drawEllipse(QRectF(2, 3, 16, 14));
    } else {
        p.drawRoundedRect(QRectF(3, 3, 14, 14), 3, 3);
        p.drawEllipse(QRectF(8, 8, 4, 4));
    }
}

class FunctionIconEngine final : public QIconEngine {
public:
    explicit FunctionIconEngine(QString id) : id_(std::move(id)) {}
    QIconEngine* clone() const override { return new FunctionIconEngine(id_); }
    void paint(QPainter* painter, const QRect& rect, QIcon::Mode mode, QIcon::State) override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.x(), rect.y()); painter->scale(rect.width() / 28.0, rect.height() / 28.0);
        const auto accent = mode == QIcon::Disabled ? QColor("#9299a4") : accentFor(id_);
        auto fill = accent; fill.setAlpha(24);
        painter->setPen(Qt::NoPen); painter->setBrush(fill);
        painter->drawRoundedRect(QRectF(0, 0, 28, 28), 6, 6);
        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(accent, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->translate(4, 4); drawGlyph(*painter, id_);
        painter->restore();
    }
    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override {
        QPixmap result(size); result.fill(Qt::transparent);
        QPainter painter(&result); paint(&painter, QRect(QPoint(), size), mode, state);
        return result;
    }
private:
    QString id_;
};

} // namespace

QIcon functionPaletteIcon(const QString& id) { return QIcon(new FunctionIconEngine(id)); }

void FunctionPaletteItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                                       const QModelIndex& index) const {
    const auto id = index.data(Qt::UserRole).toString();
    const bool dark = option.palette.color(QPalette::Base).lightness() < 128;
    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;
    const bool enabled = (option.state & QStyle::State_Enabled) && effects::isDraggable(id);
    const QRect card = option.rect.adjusted(1, 2, -1, -2);
    const QColor fill = selected ? (dark ? QColor("#263e56") : QColor("#e0edfc"))
        : hovered ? (dark ? QColor("#38414d") : QColor("#e6edf6"))
        : (dark ? QColor("#30343b") : QColor("#f1f3f6"));
    const QColor border = selected ? QColor("#6aabed")
        : hovered ? QColor("#738399") : (dark ? QColor("#434b57") : QColor("#d2d8e0"));
    painter->save(); painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(border, (selected && (option.state & QStyle::State_HasFocus)) ? 2.0 : 1.0));
    painter->setBrush(fill); painter->drawRoundedRect(card.adjusted(1, 1, -1, -1), 6, 6);
    const QRect icon_rect(card.left() + 10, card.center().y() - 14, 28, 28);
    const auto icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
    icon.paint(painter, icon_rect, Qt::AlignCenter, enabled ? QIcon::Normal : QIcon::Disabled);
    const QRect text_rect(icon_rect.right() + 11, card.top(), card.right() - icon_rect.right() - 23, card.height());
    painter->setFont(option.font);
    painter->setPen(enabled ? (dark ? QColor("#e6eaf1") : QColor("#202936")) : QColor("#8b939f"));
    painter->drawText(text_rect, Qt::AlignLeft | Qt::AlignVCenter,
        option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, text_rect.width()));
    painter->restore();
}

QSize FunctionPaletteItemDelegate::sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const {
    return QSize(200, 44);
}

} // namespace ui
