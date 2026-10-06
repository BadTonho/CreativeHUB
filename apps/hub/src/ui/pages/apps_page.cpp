#include "apps_page.h"
#include "../theme/hub_palette.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>

namespace creative_suite::hub {

AppsPage::AppsPage(AppCatalog* catalog, QWidget* parent)
    : QWidget(parent)
    , m_catalog(catalog)
{
    setupUi();

    if (m_catalog) {
        connect(m_catalog, &AppCatalog::catalogReloaded, this, &AppsPage::refreshCards);
        connect(m_catalog, &AppCatalog::appUpdated, this, &AppsPage::refreshCards);
    }

    refreshCards();
}

void AppsPage::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(16);

    // Header title and subtitle
    auto* titleLabel = new QLabel(QStringLiteral("Aplicativos da Suíte"), this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 800; color: #ffffff; background: transparent;"));
    mainLayout->addWidget(titleLabel);

    auto* subLabel = new QLabel(QStringLiteral("Gerencie, abra e atualize seus aplicativos de criação."), this);
    subLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #888894; background: transparent;"));
    mainLayout->addWidget(subLabel);

    // Filter pills (Todos, Instalados, Disponíveis)
    auto* filterLayout = new QHBoxLayout();
    filterLayout->setSpacing(8);

    const QStringList filterNames = {
        QStringLiteral("Todos"),
        QStringLiteral("Instalados"),
        QStringLiteral("Disponíveis para baixar")
    };

    for (int i = 0; i < filterNames.size(); ++i) {
        auto* btn = new QPushButton(filterNames[i], this);
        btn->setCursor(Qt::PointingHandCursor);
        m_filterButtons.push_back(btn);
        filterLayout->addWidget(btn);

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            onFilterTabClicked(i);
        });
    }

    filterLayout->addStretch();
    mainLayout->addLayout(filterLayout);

    // Scroll area for app cards
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* scrollContainer = new QWidget(scrollArea);
    scrollContainer->setStyleSheet(QStringLiteral("background: transparent;"));

    m_cardsLayout = new QVBoxLayout(scrollContainer);
    m_cardsLayout->setContentsMargins(0, 8, 0, 8);
    m_cardsLayout->setSpacing(12);
    m_cardsLayout->addStretch();

    scrollArea->setWidget(scrollContainer);
    mainLayout->addWidget(scrollArea);

    onFilterTabClicked(0);
}

void AppsPage::onFilterTabClicked(int index) {
    m_activeFilter = static_cast<CategoryFilter>(index);

    for (size_t i = 0; i < m_filterButtons.size(); ++i) {
        if (static_cast<int>(i) == index) {
            m_filterButtons[i]->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background-color: %1;"
                "   color: #ffffff;"
                "   border-radius: 14px;"
                "   padding: 5px 14px;"
                "   font-size: 12px;"
                "   font-weight: 600;"
                "   border: none;"
                "}"
            ).arg(HubPalette::accentPrimary.name()));
        } else {
            m_filterButtons[i]->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background-color: #24242c;"
                "   color: #90909c;"
                "   border-radius: 14px;"
                "   padding: 5px 14px;"
                "   font-size: 12px;"
                "   font-weight: 500;"
                "   border: 1px solid #363640;"
                "}"
                "QPushButton:hover {"
                "   background-color: #2e2e38;"
                "   color: #d0d0dc;"
                "}"
            ));
        }
    }

    applyFilters();
}

void AppsPage::setFilterQuery(const QString& query) {
    m_searchQuery = query.trimmed().toLower();
    applyFilters();
}

void AppsPage::refreshCards() {
    if (!m_catalog) {
        return;
    }

    // Clean up existing cards
    for (auto* card : m_cardWidgets) {
        m_cardsLayout->removeWidget(card);
        delete card;
    }
    m_cardWidgets.clear();

    const auto& apps = m_catalog->apps();
    for (const auto& app : apps) {
        auto* card = new AppCardWidget(this);
        card->setAppInfo(app);

        connect(card, &AppCardWidget::openRequested, this, &AppsPage::openAppRequested);
        connect(card, &AppCardWidget::downloadRequested, this, &AppsPage::downloadAppRequested);
        connect(card, &AppCardWidget::cancelDownloadRequested, this, &AppsPage::cancelDownloadRequested);

        m_cardWidgets.push_back(card);
        // Insert before stretch
        m_cardsLayout->insertWidget(static_cast<int>(m_cardWidgets.size() - 1), card);
    }

    applyFilters();
}

void AppsPage::applyFilters() {
    for (auto* card : m_cardWidgets) {
        const auto& app = card->appInfo();

        bool matchesSearch = true;
        if (!m_searchQuery.isEmpty()) {
            matchesSearch = app.name().toLower().contains(m_searchQuery) ||
                            app.description().toLower().contains(m_searchQuery) ||
                            app.tagLine().toLower().contains(m_searchQuery);
        }

        bool matchesCategory = true;
        if (m_activeFilter == CategoryFilter::Installed) {
            matchesCategory = app.isInstalled();
        } else if (m_activeFilter == CategoryFilter::Available) {
            matchesCategory = !app.isInstalled();
        }

        card->setVisible(matchesSearch && matchesCategory);
    }
}

} // namespace creative_suite::hub
