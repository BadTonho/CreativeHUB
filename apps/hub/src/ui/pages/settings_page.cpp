#include "settings_page.h"
#include "../../diagnostics/hub_logger.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QStandardPaths>

namespace creative_suite::hub {

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void SettingsPage::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(28, 24, 28, 24);
    mainLayout->setSpacing(20);

    // Title & Subtitle
    auto* titleLabel = new QLabel(QStringLiteral("Configurações do Hub"), this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 800; color: #ffffff; background: transparent;"));
    mainLayout->addWidget(titleLabel);

    auto* subLabel = new QLabel(QStringLiteral("Personalize caminhos de instalação, notificações e preferências do sistema."), this);
    subLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #8e8e9e; background: transparent; margin-bottom: 4px;"));
    mainLayout->addWidget(subLabel);

    // Section 1: Instalação (Card)
    auto* installCard = new QFrame(this);
    installCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #1a1a24;"
        "   border: 1px solid #282838;"
        "   border-radius: 12px;"
        "   padding: 12px 16px;"
        "}"
    ));
    auto* installLayout = new QVBoxLayout(installCard);
    installLayout->setSpacing(10);

    auto* installSectionLabel = new QLabel(QStringLiteral("PASTA DE INSTALAÇÃO DOS APLICATIVOS"), installCard);
    installSectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #6b6b7c; letter-spacing: 0.8px; background: transparent;"));
    installLayout->addWidget(installSectionLabel);

    auto* pathRow = new QHBoxLayout();
    pathRow->setSpacing(10);

    const QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/apps");
    m_installPathEdit = new QLineEdit(defaultPath, installCard);
    m_installPathEdit->setReadOnly(true);
    m_installPathEdit->setStyleSheet(HubStyle::searchInputStyle());
    pathRow->addWidget(m_installPathEdit, 1);

    auto* browseBtn = new QPushButton(QStringLiteral("Alterar..."), installCard);
    browseBtn->setCursor(Qt::PointingHandCursor);
    browseBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(browseBtn, &QPushButton::clicked, this, &SettingsPage::onBrowseInstallPath);
    pathRow->addWidget(browseBtn);

    installLayout->addLayout(pathRow);
    mainLayout->addWidget(installCard);

    // Section 2: Geral (Card)
    auto* generalCard = new QFrame(this);
    generalCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #1a1a24;"
        "   border: 1px solid #282838;"
        "   border-radius: 12px;"
        "   padding: 12px 16px;"
        "}"
    ));
    auto* generalLayout = new QVBoxLayout(generalCard);
    generalLayout->setSpacing(12);

    auto* generalSectionLabel = new QLabel(QStringLiteral("COMPORTAMENTO GERAL"), generalCard);
    generalSectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #6b6b7c; letter-spacing: 0.8px; background: transparent;"));
    generalLayout->addWidget(generalSectionLabel);

    m_autostartCheck = new QCheckBox(QStringLiteral("Iniciar o Creative Suite Hub junto com o Windows"), generalCard);
    m_autostartCheck->setStyleSheet(QStringLiteral(
        "QCheckBox { color: #d0d0dc; font-size: 13px; font-weight: 500; spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #3c3c4e; background: #151520; }"
        "QCheckBox::indicator:checked { background: #6366f1; border-color: #6366f1; }"
    ));
    generalLayout->addWidget(m_autostartCheck);

    m_notificationsCheck = new QCheckBox(QStringLiteral("Notificar automaticamente sobre novas versões disponíveis"), generalCard);
    m_notificationsCheck->setChecked(true);
    m_notificationsCheck->setStyleSheet(QStringLiteral(
        "QCheckBox { color: #d0d0dc; font-size: 13px; font-weight: 500; spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #3c3c4e; background: #151520; }"
        "QCheckBox::indicator:checked { background: #6366f1; border-color: #6366f1; }"
    ));
    generalLayout->addWidget(m_notificationsCheck);

    mainLayout->addWidget(generalCard);

    // Section 3: Diagnósticos e Logs (Card)
    auto* diagCard = new QFrame(this);
    diagCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #1a1a24;"
        "   border: 1px solid #282838;"
        "   border-radius: 12px;"
        "   padding: 12px 16px;"
        "}"
    ));
    auto* diagLayout = new QVBoxLayout(diagCard);
    diagLayout->setSpacing(10);

    auto* diagSectionLabel = new QLabel(QStringLiteral("DIAGNÓSTICO E REGISTROS DE ERRO"), diagCard);
    diagSectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #6b6b7c; letter-spacing: 0.8px; background: transparent;"));
    diagLayout->addWidget(diagSectionLabel);

    auto* logRow = new QHBoxLayout();
    logRow->setSpacing(10);

    m_logPathEdit = new QLineEdit(HubLogger::instance().logFilePath(), diagCard);
    m_logPathEdit->setReadOnly(true);
    m_logPathEdit->setStyleSheet(HubStyle::searchInputStyle());
    logRow->addWidget(m_logPathEdit, 1);

    auto* openLogBtn = new QPushButton(QStringLiteral("Abrir Pasta"), diagCard);
    openLogBtn->setCursor(Qt::PointingHandCursor);
    openLogBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(openLogBtn, &QPushButton::clicked, this, &SettingsPage::onOpenLogFolder);
    logRow->addWidget(openLogBtn);

    diagLayout->addLayout(logRow);
    mainLayout->addWidget(diagCard);

    mainLayout->addStretch();
}

void SettingsPage::onBrowseInstallPath() {
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        QStringLiteral("Selecionar pasta de instalação"),
        m_installPathEdit->text()
    );
    if (!dir.isEmpty()) {
        m_installPathEdit->setText(dir);
    }
}

void SettingsPage::onOpenLogFolder() {
    const QString logPath = HubLogger::instance().logFilePath();
    const QString dirPath = QFileInfo(logPath).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
}

} // namespace creative_suite::hub
