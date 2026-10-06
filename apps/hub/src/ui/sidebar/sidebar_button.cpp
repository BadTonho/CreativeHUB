#include "sidebar_button.h"
#include "../theme/hub_palette.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPolygonF>

namespace creative_suite::hub {

namespace {

QIcon createAppsIcon(const QColor& color) {
    constexpr int size = 20;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawRoundedRect(QRectF(2, 2, 6.5, 6.5), 2, 2);
    p.drawRoundedRect(QRectF(11.5, 2, 6.5, 6.5), 2, 2);
    p.drawRoundedRect(QRectF(2, 11.5, 6.5, 6.5), 2, 2);
    p.drawRoundedRect(QRectF(11.5, 11.5, 6.5, 6.5), 2, 2);
    return QIcon(pixmap);
}

QIcon createUpdatesIcon(const QColor& color) {
    constexpr int size = 20;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawArc(QRectF(3, 3, 14, 14), 40 * 16, 260 * 16);

    p.setBrush(color);
    p.setPen(Qt::NoPen);
    QPolygonF arrow;
    arrow << QPointF(12, 1) << QPointF(17, 4) << QPointF(13, 7);
    p.drawPolygon(arrow);
    return QIcon(pixmap);
}

QIcon createSettingsIcon(const QColor& color) {
    constexpr int size = 20;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 2.0);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(QPointF(10, 10), 4, 4);
    p.drawLine(QPointF(10, 2), QPointF(10, 5));
    p.drawLine(QPointF(10, 15), QPointF(10, 18));
    p.drawLine(QPointF(2, 10), QPointF(5, 10));
    p.drawLine(QPointF(15, 10), QPointF(18, 10));
    return QIcon(pixmap);
}

} // namespace

SidebarButton::SidebarButton(const QString& title, QWidget* parent)
    : QPushButton(parent)
    , m_title(title)
{
    setFixedHeight(42);
    setCursor(Qt::PointingHandCursor);
    setIconSize(QSize(18, 18));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 12, 0);

    setText(QStringLiteral("  ") + title);

    m_badgeLabel = new QLabel(this);
    m_badgeLabel->setVisible(false);
    m_badgeLabel->setStyleSheet(QStringLiteral(
        "background-color: #6366f1;"
        "color: #ffffff;"
        "border-radius: 9px;"
        "font-size: 10px;"
        "font-weight: 800;"
        "padding: 1px 7px;"
    ));

    layout->addStretch();
    layout->addWidget(m_badgeLabel);

    updateVisuals();
}

void SidebarButton::setActive(bool active) {
    m_active = active;
    updateVisuals();
}

void SidebarButton::setBadgeCount(int count) {
    m_badgeCount = count;
    if (m_badgeCount > 0) {
        m_badgeLabel->setText(QString::number(m_badgeCount));
        m_badgeLabel->setVisible(true);
    } else {
        m_badgeLabel->setVisible(false);
    }
}

void SidebarButton::updateVisuals() {
    QColor iconColor = m_active ? QColor(0xa5, 0xb4, 0xfc) : QColor(0x82, 0x82, 0x94);

    if (m_title.contains(QStringLiteral("Aplicativos"))) {
        setIcon(createAppsIcon(iconColor));
    } else if (m_title.contains(QStringLiteral("Atualizações"))) {
        setIcon(createUpdatesIcon(iconColor));
    } else if (m_title.contains(QStringLiteral("Configurações"))) {
        setIcon(createSettingsIcon(iconColor));
    }

    if (m_active) {
        setStyleSheet(QStringLiteral(R"(
            QPushButton {
                background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 rgba(99, 102, 241, 0.22), stop:1 rgba(99, 102, 241, 0.08));
                color: #ffffff;
                font-weight: 700;
                font-size: 13px;
                text-align: left;
                padding-left: 12px;
                border: 1px solid rgba(99, 102, 241, 0.35);
                border-radius: 8px;
            }
        )"));
    } else {
        setStyleSheet(QStringLiteral(R"(
            QPushButton {
                background-color: transparent;
                color: #9090a2;
                font-weight: 500;
                font-size: 13px;
                text-align: left;
                padding-left: 12px;
                border: 1px solid transparent;
                border-radius: 8px;
            }
            QPushButton:hover {
                background-color: #1e1e28;
                color: #e2e2ec;
                border-color: #2b2b3a;
            }
        )"));
    }
}

} // namespace creative_suite::hub
