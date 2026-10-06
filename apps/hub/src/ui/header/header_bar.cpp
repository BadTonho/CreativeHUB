#include "header_bar.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"

#include <QHBoxLayout>

namespace creative_suite::hub {

HeaderBar::HeaderBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(60);
    setStyleSheet(QStringLiteral(
        "HeaderBar {"
        "   background-color: %1;"
        "   border-bottom: 1px solid #2e2e38;"
        "}"
    ).arg(HubPalette::headerBackground.name()));

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(20, 10, 20, 10);
    layout->setSpacing(16);

    // Suite brand label
    m_brandTitle = new QLabel(QStringLiteral("Creative Suite"), this);
    m_brandTitle->setStyleSheet(QStringLiteral("font-size: 16px; font-weight: 800; color: #ffffff; letter-spacing: 0.5px; background: transparent;"));
    layout->addWidget(m_brandTitle);

    layout->addStretch();

    // Search bar
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Buscar aplicativos..."));
    m_searchEdit->setFixedWidth(260);
    m_searchEdit->setStyleSheet(HubStyle::searchInputStyle());
    connect(m_searchEdit, &QLineEdit::textChanged, this, &HeaderBar::searchTextChanged);
    layout->addWidget(m_searchEdit);

    // Refresh / Check for updates button
    m_refreshButton = new QPushButton(QStringLiteral("Verificar Atualizações"), this);
    m_refreshButton->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(m_refreshButton, &QPushButton::clicked, this, &HeaderBar::refreshRequested);
    layout->addWidget(m_refreshButton);
}

} // namespace creative_suite::hub
