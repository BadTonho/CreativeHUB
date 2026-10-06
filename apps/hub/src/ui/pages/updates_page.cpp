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
    mainLayout->setContentsMargins(28, 24, 28, 24);
    mainLayout->setSpacing(20);

    auto* headerRow = new QHBoxLayout();

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);

    auto* titleLabel = new QLabel(QStringLiteral("Atualizações Disponíveis"), this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 800; color: #ffffff; background: transparent;"));
    titleCol->addWidget(titleLabel);

    auto* subLabel = new QLabel(QStringLiteral("Mantenha seus aplicativos sempre na versão mais estável, rápida e com novos recursos."), this);
    subLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #8e8e9e; background: transparent;"));
    titleCol->addWidget(subLabel);

    headerRow->addLayout(titleCol);
    headerRow->addStretch();

    m_checkUpdatesButton = new QPushButton(QStringLiteral("Verificar Atualizações"), this);
    m_checkUpdatesButton->setStyleSheet(HubStyle::secondaryButtonStyle());
    m_checkUpdatesButton->setCursor(Qt::PointingHandCursor);
    connect(m_checkUpdatesButton, &QPushButton::clicked, this, &UpdatesPage::checkUpdatesRequested);
    headerRow->addWidget(m_checkUpdatesButton);

    mainLayout->addLayout(headerRow);

    m_contentLayout = new QGridLayout();
    m_contentLayout->setSpacing(20);
    m_contentLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_emptyStateLabel = new QLabel(this);
    m_emptyStateLabel->setAlignment(Qt::AlignCenter);
    m_emptyStateLabel->setStyleSheet(QStringLiteral(
        "color: #d0d0d0;"
        "font-size: 14px;"
        "font-weight: 600;"
        "line-height: 1.6;"
        "padding: 56px 24px;"
        "background-color: #161616;"
        "border: 1px solid #262626;"
        "border-radius: 14px;"
    ));
    m_emptyStateLabel->setText(QStringLiteral("✓ Tudo atualizado!\nSeus aplicativos estão na versão mais recente, estável e rápida."));
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
            connect(card, &AppCardWidget::detailsRequested, this, [this, card](const QString& appId) {
                QRect originRect = card->rect();
                if (window()) {
                    originRect = QRect(card->mapTo(window(), QPoint(0, 0)), card->size());
                }
                emit appDetailsRequested(appId, originRect);
            });
            m_updateCards.push_back(card);

            const int row = updateIndex / columns;
            const int col = updateIndex % columns;
            m_contentLayout->addWidget(card, row, col);
            ++updateIndex;
        }
    }

    const bool hasUpdates = (updateIndex > 0);
    m_emptyStateLabel->setVisible(!hasUpdates);
}

} // namespace creative_suite::hub
