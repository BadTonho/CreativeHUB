#include "header_bar.h"
#include "../theme/hub_palette.h"

#include <QHBoxLayout>
#include <QPainter>
#include <QLinearGradient>

namespace creative_suite::hub {

namespace {

QPixmap createSuiteLogoPixmap() {
    constexpr int size = 30;
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);

    QLinearGradient grad(0, 0, size, size);
    grad.setColorAt(0.0, QColor(0x63, 0x66, 0xf1));
    grad.setColorAt(1.0, QColor(0xa8, 0x55, 0xf7));

    painter.setBrush(grad);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(QRectF(0, 0, size, size), 8, 8);

    painter.setPen(Qt::white);
    QFont font = painter.font();
    font.setPointSize(10);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, size, size), Qt::AlignCenter, QStringLiteral("CS"));

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
        "   border-bottom: 1px solid #222230;"
        "}"
    ).arg(HubPalette::headerBackground.name()));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(22, 10, 22, 10);
    layout->setSpacing(12);

    // Logo icon
    auto* logoLabel = new QLabel(this);
    logoLabel->setPixmap(createSuiteLogoPixmap());
    logoLabel->setFixedSize(30, 30);
    layout->addWidget(logoLabel);

    // Suite brand label
    m_brandTitle = new QLabel(QStringLiteral("Creative Suite"), this);
    m_brandTitle->setStyleSheet(QStringLiteral(
        "font-size: 16px; font-weight: 800; color: #ffffff; letter-spacing: 0.3px; background: transparent;"
    ));
    layout->addWidget(m_brandTitle);

    // Hub pill badge
    auto* hubBadge = new QLabel(QStringLiteral("HUB"), this);
    hubBadge->setStyleSheet(QStringLiteral(
        "background-color: rgba(99, 102, 241, 0.18);"
        "color: #818cf8;"
        "border: 1px solid rgba(99, 102, 241, 0.35);"
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
}

} // namespace creative_suite::hub
