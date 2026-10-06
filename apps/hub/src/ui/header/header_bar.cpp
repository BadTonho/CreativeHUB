#include "header_bar.h"
#include "../theme/hub_palette.h"
#include "../popups/activity_popup.h"
#include "../../model/activity_manager.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QFile>

namespace creative_suite::hub {

namespace {

QPixmap getSuiteLogoPixmap() {
    constexpr int size = 32;

    if (QFile::exists(QStringLiteral(":/icons/hub.png"))) {
        QPixmap iconPix(QStringLiteral(":/icons/hub.png"));
        if (!iconPix.isNull()) {
            return iconPix.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }

    if (QFile::exists(QStringLiteral(":/app-icon/icon.png"))) {
        QPixmap iconPix(QStringLiteral(":/app-icon/icon.png"));
        if (!iconPix.isNull()) {
            return iconPix.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
    }

    // Fallback if resource is not found
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    painter.setBrush(QColor(0x22, 0x22, 0x22));
    painter.setPen(QColor(0x38, 0x38, 0x38));
    painter.drawRoundedRect(QRectF(0, 0, size, size), 8, 8);

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(11);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, QStringLiteral("H"));

    return pixmap;
}

} // namespace

HeaderBar::HeaderBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(62);
    setStyleSheet(QStringLiteral(
        "HeaderBar {"
        "   background-color: %1;"
        "   border-bottom: 1px solid #202020;"
        "}"
    ).arg(HubPalette::headerBackground.name()));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(22, 10, 22, 10);
    layout->setSpacing(12);

    // Official Hub logo icon
    auto* logoLabel = new QLabel(this);
    logoLabel->setPixmap(getSuiteLogoPixmap());
    logoLabel->setFixedSize(32, 32);
    logoLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(logoLabel);

    // Suite brand label
    m_brandTitle = new QLabel(QStringLiteral("Creative Suite"), this);
    m_brandTitle->setStyleSheet(QStringLiteral(
        "font-size: 16px; font-weight: 800; color: #ffffff; letter-spacing: 0.3px; background: transparent;"
    ));
    layout->addWidget(m_brandTitle);

    // Hub pill badge (monochrome)
    auto* hubBadge = new QLabel(QStringLiteral("HUB"), this);
    hubBadge->setStyleSheet(QStringLiteral(
        "background-color: #1e1e1e;"
        "color: #ffffff;"
        "border: 1px solid #333333;"
        "border-radius: 6px;"
        "padding: 2px 7px;"
        "font-size: 10px;"
        "font-weight: 800;"
        "letter-spacing: 0.8px;"
    ));
    layout->addWidget(hubBadge);

    layout->addStretch();

    // Expandable search bar with animated reveal
    m_searchBar = new ExpandableSearchBar(this);
    connect(m_searchBar, &ExpandableSearchBar::searchTextChanged, this, &HeaderBar::searchTextChanged);
    layout->addWidget(m_searchBar);

    // Activity / Notification Bell button
    m_bellButton = new QPushButton(this);
    m_bellButton->setFixedSize(36, 36);
    m_bellButton->setCursor(Qt::PointingHandCursor);
    m_bellButton->setToolTip(QStringLiteral("Central de Atividades e Notificações"));
    m_bellButton->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "   background-color: #1a1a1a;"
        "   border: 1px solid #2e2e2e;"
        "   border-radius: 8px;"
        "}"
        "QPushButton:hover {"
        "   background-color: #242424;"
        "   border-color: #404040;"
        "}"
        "QPushButton:pressed {"
        "   background-color: #161616;"
        "}"
    ));
    connect(m_bellButton, &QPushButton::clicked, this, &HeaderBar::onBellClicked);
    layout->addWidget(m_bellButton);

    m_activityPopup = new ActivityPopup(this);

    connect(&ActivityManager::instance(), &ActivityManager::activitiesChanged,
            this, &HeaderBar::updateBellIcon);

    updateBellIcon();
}

void HeaderBar::updateBellIcon() {
    if (!m_bellButton) return;

    const bool hasUnread = ActivityManager::instance().unreadCount() > 0;

    constexpr int size = 20;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);

    QPen pen(QColor(0xd0, 0xd0, 0xd0), 1.6);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    QPainterPath bell;
    bell.moveTo(10, 4);
    bell.cubicTo(7.5, 4, 6, 6.5, 6, 11);
    bell.lineTo(4, 13.5);
    bell.lineTo(16, 13.5);
    bell.lineTo(14, 11);
    bell.cubicTo(14, 6.5, 12.5, 4, 10, 4);
    p.drawPath(bell);

    // Clapper at bottom
    p.drawLine(8.5, 15, 11.5, 15);

    // Top loop
    p.drawArc(QRectF(8.5, 2, 3, 3), 0, 180 * 16);

    // Unread indicator dot
    if (hasUnread) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0xff, 0xff, 0xff));
        p.drawEllipse(QPointF(14.5, 4.5), 2.5, 2.5);
    }

    m_bellButton->setIcon(QIcon(pixmap));
    m_bellButton->setIconSize(QSize(18, 18));
}

void HeaderBar::onBellClicked() {
    if (!m_activityPopup) return;

    if (m_activityPopup->isVisible()) {
        m_activityPopup->hide();
    } else {
        const QPoint globalPos = m_bellButton->mapToGlobal(QPoint(m_bellButton->width(), m_bellButton->height()));
        m_activityPopup->showAt(globalPos);
    }
}

} // namespace creative_suite::hub
