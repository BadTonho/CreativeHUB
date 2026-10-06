#include "updates_page.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"

#include <QVBoxLayout>
#include <QHBoxLayout>

namespace creative_suite::hub {

UpdatesPage::UpdatesPage(AppCatalog* catalog, QWidget* parent)
    : QWidget(parent)
    , m_catalog(catalog)
{
    setupUi();

    if (m_catalog) {
        connect(m_catalog, &AppCatalog::catalogReloaded, this, &UpdatesPage::refreshUpdates);
        connect(m_catalog, &AppCatalog::appUpdated, this, &UpdatesPage::refreshUpdates);
    }

    refreshUpdates();
}

void UpdatesPage::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(16);

    auto* headerRow = new QHBoxLayout();

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(2);

    auto* titleLabel = new QLabel(QStringLiteral("Atualizações Disponíveis"), this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 800; color: #ffffff; background: transparent;"));
    titleCol->addWidget(titleLabel);

    auto* subLabel = new QLabel(QStringLiteral("Mantenha seus aplicativos sempre na versão mais estável e rápida."), this);
    subLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #888894; background: transparent;"));
    titleCol->addWidget(subLabel);

    headerRow->addLayout(titleCol);
    headerRow->addStretch();

    m_updateAllButton = new QPushButton(QStringLiteral("Atualizar Todos"), this);
    m_updateAllButton->setStyleSheet(HubStyle::primaryButtonStyle());
    m_updateAllButton->setVisible(false);
    connect(m_updateAllButton, &QPushButton::clicked, this, &UpdatesPage::updateAllRequested);
    headerRow->addWidget(m_updateAllButton);

    mainLayout->addLayout(headerRow);

    m_contentLayout = new QGridLayout();
    m_contentLayout->setSpacing(18);
    m_contentLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_emptyStateLabel = new QLabel(this);
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    m_emptyStateLabel->setStyleSheet(QStringLiteral(
        "color: #888896;"
        "font-size: 14px;"
        "padding: 60px 20px;"
        "background-color: #202026;"
        "border: 1px dashed #343440;"
        "border-radius: 10px;"
    ));
    m_emptyStateLabel->setText(QStringLiteral("✓ Todos os seus aplicativos estão atualizados!"));
    m_contentLayout->addWidget(m_emptyStateLabel, 0, 0, 1, 3);

    mainLayout->addLayout(m_contentLayout);
    mainLayout->addStretch();
}

void UpdatesPage::refreshUpdates() {
    if (!m_catalog) {
        return;
    }

    for (auto* card : m_updateCards) {
        m_contentLayout->removeWidget(card);
        delete card;
    }
    m_updateCards.clear();

    const auto& apps = m_catalog->apps();
    int updateIndex = 0;
    constexpr int columns = 3;

    for (const auto& app : apps) {
        if (app.hasUpdate()) {
            auto* card = new AppCardWidget(this);
            card->setAppInfo(app);
            connect(card, &AppCardWidget::downloadRequested, this, &UpdatesPage::updateAppRequested);
            m_updateCards.push_back(card);

            const int row = updateIndex / columns;
            const int col = updateIndex % columns;
            m_contentLayout->addWidget(card, row, col);
            ++updateIndex;
        }
    }

    const bool hasUpdates = (updateIndex > 0);
    m_emptyStateLabel->setVisible(!hasUpdates);
    m_updateAllButton->setVisible(hasUpdates);
}

} // namespace creative_suite::hub
