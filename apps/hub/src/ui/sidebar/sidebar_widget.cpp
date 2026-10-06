#include "sidebar_widget.h"
#include "../theme/hub_palette.h"

#include <QVBoxLayout>
#include <QLabel>

namespace creative_suite::hub {

SidebarWidget::SidebarWidget(QWidget* parent)
    : QWidget(parent)
{
    setFixedWidth(220);
    setStyleSheet(QStringLiteral(
        "SidebarWidget {"
        "   background-color: %1;"
        "   border-right: 1px solid #2e2e38;"
        "}"
    ).arg(HubPalette::sidebarBackground.name()));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 16, 0, 16);
    layout->setSpacing(4);

    auto* sectionLabel = new QLabel(QStringLiteral("BIBLIOTECA"), this);
    sectionLabel->setStyleSheet(QStringLiteral(
        "color: #6c6c78;"
        "font-size: 11px;"
        "font-weight: 700;"
        "padding-left: 16px;"
        "margin-bottom: 6px;"
        "background: transparent;"
    ));
    layout->addWidget(sectionLabel);

    auto* allAppsBtn = new SidebarButton(QStringLiteral("Todos os Aplicativos"), this);
    auto* updatesBtn = new SidebarButton(QStringLiteral("Atualizações"), this);
    auto* settingsBtn = new SidebarButton(QStringLiteral("Configurações"), this);

    m_buttons.push_back(allAppsBtn);
    m_buttons.push_back(updatesBtn);
    m_buttons.push_back(settingsBtn);

    for (size_t i = 0; i < m_buttons.size(); ++i) {
        layout->addWidget(m_buttons[i]);
        connect(m_buttons[i], &QPushButton::clicked, this, [this, i]() {
            selectButton(static_cast<int>(i));
        });
    }

    layout->addStretch();

    // Version tag at the bottom
    auto* versionLabel = new QLabel(QStringLiteral("Hub v0.1.0 (Beta)"), this);
    versionLabel->setStyleSheet(QStringLiteral(
        "color: #555560;"
        "font-size: 10px;"
        "padding-left: 16px;"
        "background: transparent;"
    ));
    layout->addWidget(versionLabel);

    selectButton(0);
}

void SidebarWidget::setCurrentIndex(int index) {
    selectButton(index);
}

void SidebarWidget::setUpdatesCount(int count) {
    if (m_buttons.size() > 1) {
        m_buttons[1]->setBadgeCount(count);
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
