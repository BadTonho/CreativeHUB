#include "sidebar_widget.h"
#include "../theme/hub_palette.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>

namespace creative_suite::hub {

SidebarWidget::SidebarWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(232);
    setStyleSheet(QStringLiteral(
        "SidebarWidget {"
        "   background-color: %1;"
        "   border-right: 1px solid #202020;"
        "}"
    ).arg(HubPalette::sidebarBackground.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 18, 12, 16);
    layout->setSpacing(6);

    auto* sectionLabel = new QLabel(QStringLiteral("BIBLIOTECA"), this);
    sectionLabel->setStyleSheet(QStringLiteral(
        "color: #555555;"
        "font-size: 10px;"
        "font-weight: 800;"
        "letter-spacing: 1.2px;"
        "padding-left: 8px;"
        "margin-bottom: 6px;"
        "background: transparent;"
    ));
    layout->addWidget(sectionLabel);

    auto* allAppsBtn = new SidebarButton(QStringLiteral("Todos os Aplicativos"), this);
    auto* projectsBtn = new SidebarButton(QStringLiteral("Projetos"), this);
    auto* updatesBtn = new SidebarButton(QStringLiteral("Atualizações"), this);
    auto* settingsBtn = new SidebarButton(QStringLiteral("Configurações"), this);

    m_buttons.push_back(allAppsBtn);
    m_buttons.push_back(projectsBtn);
    m_buttons.push_back(updatesBtn);
    m_buttons.push_back(settingsBtn);

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        layout->addWidget(m_buttons[i]);
        connect(m_buttons[i], &QPushButton::clicked, this, [this, i]() {
            selectButton(static_cast<int>(i));
        });
    }

    layout->addStretch();

    // Sleek monochrome status footer card
    auto* footerCard = new QFrame(this);
    footerCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #161616;"
        "   border: 1px solid #242424;"
        "   border-radius: 8px;"
        "   padding: 4px 8px;"
        "}"
    ));
    auto* footerLayout = new QHBoxLayout(footerCard);
    footerLayout->setContentsMargins(6, 6, 6, 6);
    footerLayout->setSpacing(8);

    auto* statusDot = new QLabel(QStringLiteral("●"), footerCard);
    statusDot->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 9px; background: transparent;"));
    footerLayout->addWidget(statusDot);

#ifndef CREATIVE_SUITE_VERSION_HUB
#define CREATIVE_SUITE_VERSION_HUB "0.1.0"
#endif
    auto* versionLabel = new QLabel(
        QStringLiteral("Hub v%1 • Ativo").arg(QStringLiteral(CREATIVE_SUITE_VERSION_HUB)), footerCard);
    versionLabel->setStyleSheet(QStringLiteral(
        "color: #888888;"
        "font-size: 11px;"
        "font-weight: 600;"
        "background: transparent;"
    ));
    footerLayout->addWidget(versionLabel, 1);

    layout->addWidget(footerCard);

    selectButton(0);
}

void SidebarWidget::setCurrentIndex(int index) {
    selectButton(index);
}

void SidebarWidget::setUpdatesCount(int count) {
    if (m_buttons.size() > 2) {
        m_buttons[2]->setBadgeCount(count);
    }
}

void SidebarWidget::selectButton(int index) {
    if (index < 0 || index >= static_cast<int>(m_buttons.size())) {
        return;
    }

    m_currentIndex = index;
    for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i) {
        m_buttons[i]->setActive(i == index);
    }

    emit pageSelected(index);
}

} // namespace creative_suite::hub
