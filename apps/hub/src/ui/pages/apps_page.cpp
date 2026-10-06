#include "apps_page.h"
#include "../theme/hub_palette.h"

#include <QVBoxLayout>
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
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Scroll area for entire page content
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* scrollContainer = new QWidget(m_scrollArea);
    scrollContainer->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* containerLayout = new QVBoxLayout(scrollContainer);
    containerLayout->setContentsMargins(28, 24, 28, 24);
    containerLayout->setSpacing(20);

    // Header section: Title and Subtitle, with filter buttons on clean dedicated row
    auto* headerLayout = new QVBoxLayout();
    headerLayout->setSpacing(14);

    auto* titleCol = new QVBoxLayout();
    titleCol->setSpacing(4);

    auto* titleLabel = new QLabel(QStringLiteral("Aplicativos da Suíte"), scrollContainer);
    titleLabel->setStyleSheet(QStringLiteral(
        "font-size: 22px; font-weight: 800; color: #ffffff; background: transparent;"
    ));
    titleCol->addWidget(titleLabel);

    auto* subLabel = new QLabel(QStringLiteral("Gerencie, abra e atualize seus aplicativos de criação."), scrollContainer);
    subLabel->setWordWrap(true);
    subLabel->setStyleSheet(QStringLiteral(
        "font-size: 13px; color: #888888; background: transparent;"
    ));
    titleCol->addWidget(subLabel);

    headerLayout->addLayout(titleCol);

    // Filter Buttons in flexible horizontal row right below title
    auto* filterRow = new QHBoxLayout();
    filterRow->setSpacing(8);

    const QStringList filterNames = {
        QStringLiteral("Todos"),
        QStringLiteral("Instalados"),
        QStringLiteral("Disponíveis")
    };

    for (int i = 0; i < filterNames.size(); ++i) {
        auto* btn = new QPushButton(filterNames[i], scrollContainer);
        btn->setCursor(Qt::PointingHandCursor);
        m_filterButtons.push_back(btn);
        filterRow->addWidget(btn);

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            onFilterTabClicked(i);
        });
    }
    filterRow->addStretch();
    headerLayout->addLayout(filterRow);

    containerLayout->addLayout(headerLayout);

    // App Cards Grid
    m_cardsLayout = new QGridLayout();
    m_cardsLayout->setContentsMargins(0, 8, 0, 0);
    m_cardsLayout->setSpacing(20);
    m_cardsLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    containerLayout->addLayout(m_cardsLayout);
    containerLayout->addStretch();

    m_scrollArea->setWidget(scrollContainer);
    mainLayout->addWidget(m_scrollArea);

    onFilterTabClicked(0);
}

void AppsPage::onFilterTabClicked(int index) {
    m_activeFilter = static_cast<CategoryFilter>(index);

    for (size_t i = 0; i < m_filterButtons.size(); ++i) {
        if (static_cast<int>(i) == index) {
            m_filterButtons[i]->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background-color: #ffffff;"
                "   color: #000000;"
                "   border-radius: 14px;"
                "   padding: 6px 16px;"
                "   font-size: 12px;"
                "   font-weight: 700;"
                "   border: none;"
                "}"
            ));
        } else {
            m_filterButtons[i]->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background-color: #181818;"
                "   color: #888888;"
                "   border-radius: 14px;"
                "   padding: 6px 16px;"
                "   font-size: 12px;"
                "   font-weight: 600;"
                "   border: 1px solid #282828;"
                "}"
                "QPushButton:hover {"
                "   background-color: #222222;"
                "   color: #ffffff;"
                "   border-color: #383838;"
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
        connect(card, &AppCardWidget::detailsRequested, this, [this, card](const QString& appId) {
            QRect originRect = card->rect();
            if (window()) {
                originRect = QRect(card->mapTo(window(), QPoint(0, 0)), card->size());
            }
            emit appDetailsRequested(appId, originRect);
        });

        m_cardWidgets.push_back(card);
    }

    applyFilters();
}

void AppsPage::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    applyFilters();
}

void AppsPage::applyFilters() {
    int visibleIndex = 0;

    int availableWidth = width() - 56;
    if (m_scrollArea && m_scrollArea->viewport() && m_scrollArea->viewport()->width() > 0) {
        availableWidth = m_scrollArea->viewport()->width() - 56;
    }
    constexpr int cardW = AppCardWidget::kCardWidth;
    constexpr int spacing = 20;
    const int columns = std::max(1, (availableWidth + spacing) / (cardW + spacing));

    for (auto* card : m_cardWidgets) {
        const auto& app = card->appInfo();

        bool matchesSearch = true;
        if (!m_searchQuery.isEmpty()) {
            matchesSearch = app.name().toLower().contains(m_searchQuery) ||
                            app.tagLine().toLower().contains(m_searchQuery);
        }

        bool matchesCategory = true;
        if (m_activeFilter == CategoryFilter::Installed) {
            matchesCategory = app.isInstalled();
        } else if (m_activeFilter == CategoryFilter::Available) {
            matchesCategory = !app.isInstalled();
        }

        const bool visible = matchesSearch && matchesCategory;
        card->setVisible(visible);

        m_cardsLayout->removeWidget(card);
        if (visible) {
            const int row = visibleIndex / columns;
            const int col = visibleIndex % columns;
            m_cardsLayout->addWidget(card, row, col);
            ++visibleIndex;
        }
    }
}

} // namespace creative_suite::hub
