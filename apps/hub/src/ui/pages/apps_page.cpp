#include "apps_page.h"
#include "../theme/hub_palette.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>

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
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* scrollContainer = new QWidget(scrollArea);
    scrollContainer->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* containerLayout = new QVBoxLayout(scrollContainer);
    containerLayout->setContentsMargins(28, 24, 28, 24);
    containerLayout->setSpacing(22);

    // 1. Hero Showcase Banner
    auto* heroCard = new QFrame(scrollContainer);
    heroCard->setObjectName(QStringLiteral("HeroCard"));
    heroCard->setStyleSheet(QStringLiteral(
        "#HeroCard {"
        "   background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:1, stop:0 #1a1638, stop:0.5 #141322, stop:1 #0f0f18);"
        "   border: 1px solid #2d2854;"
        "   border-radius: 16px;"
        "}"
    ));
    auto* heroLayout = new QVBoxLayout(heroCard);
    heroLayout->setContentsMargins(26, 22, 26, 22);
    heroLayout->setSpacing(10);

    auto* heroBadge = new QLabel(QStringLiteral("★ ECOSSISTEMA CRIATIVO INTEGRADO"), heroCard);
    heroBadge->setStyleSheet(QStringLiteral(
        "background-color: rgba(99, 102, 241, 0.22);"
        "color: #a5b4fc;"
        "border: 1px solid rgba(99, 102, 241, 0.4);"
        "border-radius: 10px;"
        "padding: 3px 10px;"
        "font-size: 10px;"
        "font-weight: 800;"
        "letter-spacing: 0.8px;"
        "background: transparent;"
    ));
    heroLayout->addWidget(heroBadge, 0, Qt::AlignLeft);

    auto* heroTitle = new QLabel(QStringLiteral("Crie sem limites."), heroCard);
    heroTitle->setStyleSheet(QStringLiteral(
        "font-size: 24px; font-weight: 900; color: #ffffff; background: transparent; letter-spacing: -0.3px;"
    ));
    heroLayout->addWidget(heroTitle);

    auto* heroSub = new QLabel(
        QStringLiteral("Aplicativos de alta performance para edição audiovisual, imagens raster e motion design profissional."),
        heroCard
    );
    heroSub->setWordWrap(true);
    heroSub->setStyleSheet(QStringLiteral(
        "font-size: 13px; color: #9da3b8; background: transparent; line-height: 1.4;"
    ));
    heroLayout->addWidget(heroSub);

    // Feature highlights pills row
    auto* pillsRow = new QHBoxLayout();
    pillsRow->setSpacing(8);
    const QStringList highlights = {
        QStringLiteral("✦ Motor Nativo C++ / GPU"),
        QStringLiteral("✦ Suíte Compartilhada"),
        QStringLiteral("✦ 100% Local e Seguro")
    };
    for (const auto& tag : highlights) {
        auto* tagLabel = new QLabel(tag, heroCard);
        tagLabel->setStyleSheet(QStringLiteral(
            "background-color: rgba(255, 255, 255, 0.05);"
            "color: #cbd5e1;"
            "border: 1px solid rgba(255, 255, 255, 0.08);"
            "border-radius: 6px;"
            "padding: 3px 9px;"
            "font-size: 11px;"
            "font-weight: 600;"
        ));
        pillsRow->addWidget(tagLabel);
    }
    pillsRow->addStretch();
    heroLayout->addLayout(pillsRow);

    containerLayout->addWidget(heroCard);

    // 2. Section Header & Filter Pills
    auto* sectionRow = new QHBoxLayout();
    sectionRow->setSpacing(12);

    auto* sectionTitle = new QLabel(QStringLiteral("EXPLORAR APLICATIVOS"), scrollContainer);
    sectionTitle->setStyleSheet(QStringLiteral(
        "font-size: 11px; font-weight: 800; color: #6b6b7c; letter-spacing: 1px; background: transparent;"
    ));
    sectionRow->addWidget(sectionTitle);
    sectionRow->addStretch();

    const QStringList filterNames = {
        QStringLiteral("Todos"),
        QStringLiteral("Instalados"),
        QStringLiteral("Disponíveis para baixar")
    };

    for (int i = 0; i < filterNames.size(); ++i) {
        auto* btn = new QPushButton(filterNames[i], scrollContainer);
        btn->setCursor(Qt::PointingHandCursor);
        m_filterButtons.push_back(btn);
        sectionRow->addWidget(btn);

        connect(btn, &QPushButton::clicked, this, [this, i]() {
            onFilterTabClicked(i);
        });
    }

    containerLayout->addLayout(sectionRow);

    // 3. App Cards Grid
    m_cardsLayout = new QGridLayout();
    m_cardsLayout->setContentsMargins(0, 0, 0, 0);
    m_cardsLayout->setSpacing(20);
    m_cardsLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    containerLayout->addLayout(m_cardsLayout);
    containerLayout->addStretch();

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
                "   background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 #4f46e5, stop:1 #6366f1);"
                "   color: #ffffff;"
                "   border-radius: 14px;"
                "   padding: 5px 16px;"
                "   font-size: 12px;"
                "   font-weight: 700;"
                "   border: 1px solid rgba(255, 255, 255, 0.15);"
                "}"
            ));
        } else {
            m_filterButtons[i]->setStyleSheet(QStringLiteral(
                "QPushButton {"
                "   background-color: #1a1a24;"
                "   color: #8c8c9e;"
                "   border-radius: 14px;"
                "   padding: 5px 16px;"
                "   font-size: 12px;"
                "   font-weight: 600;"
                "   border: 1px solid #282836;"
                "}"
                "QPushButton:hover {"
                "   background-color: #242432;"
                "   color: #d0d0e0;"
                "   border-color: #38384a;"
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

void AppsPage::applyFilters() {
    int visibleIndex = 0;
    constexpr int columns = 3;

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
