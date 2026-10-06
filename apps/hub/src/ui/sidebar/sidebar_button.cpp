#include "sidebar_button.h"
#include "../theme/hub_palette.h"

#include <QHBoxLayout>

namespace creative_suite::hub {

SidebarButton::SidebarButton(const QString& title, QWidget* parent)
    : QPushButton(parent)
    , m_title(title)
{
    setFixedHeight(40);
    setCursor(Qt::PointingHandCursor);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 0, 14, 0);

    setText(title);

    m_badgeLabel = new QLabel(this);
    m_badgeLabel->setVisible(false);
    m_badgeLabel->setStyleSheet(QStringLiteral(
        "background-color: %1;"
        "color: #ffffff;"
        "border-radius: 9px;"
        "font-size: 10px;"
        "font-weight: 700;"
        "padding: 2px 6px;"
    ).arg(HubPalette::accentPrimary.name()));

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
    if (m_active) {
        setStyleSheet(QStringLiteral(R"(
            QPushButton {
                background-color: #2b2b34;
                color: #ffffff;
                font-weight: 700;
                font-size: 13px;
                text-align: left;
                padding-left: 12px;
                border-left: 3px solid %1;
                border-top: none;
                border-right: none;
                border-bottom: none;
                border-radius: 0px;
            }
        )").arg(HubPalette::accentPrimary.name()));
    } else {
        setStyleSheet(QStringLiteral(R"(
            QPushButton {
                background-color: transparent;
                color: #9898a4;
                font-weight: 500;
                font-size: 13px;
                text-align: left;
                padding-left: 15px;
                border: none;
            }
            QPushButton:hover {
                background-color: #26262e;
                color: #e0e0ea;
            }
        )"));
    }
}

} // namespace creative_suite::hub
