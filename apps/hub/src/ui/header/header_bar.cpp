#include "header_bar.h"
#include "../theme/hub_palette.h"

#include <QHBoxLayout>
#include <QPainter>
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
}

} // namespace creative_suite::hub
