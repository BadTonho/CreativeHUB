#include "settings_page.h"
#include "../../diagnostics/hub_logger.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
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
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(20);

    // Title
    auto* titleLabel = new QLabel(QStringLiteral("Configurações do Hub"), this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 20px; font-weight: 800; color: #ffffff; background: transparent;"));
    mainLayout->addWidget(titleLabel);

    // Section 1: Instalação
    auto* installSectionLabel = new QLabel(QStringLiteral("PASTA DE INSTALAÇÃO DOS APLICATIVOS"), this);
    installSectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #787884; background: transparent;"));
    mainLayout->addWidget(installSectionLabel);

    auto* pathRow = new QHBoxLayout();
    pathRow->setSpacing(8);

    const QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/apps");
    m_installPathEdit = new QLineEdit(defaultPath, this);
    m_installPathEdit->setReadOnly(true);
    m_installPathEdit->setStyleSheet(HubStyle::searchInputStyle());
    pathRow->addWidget(m_installPathEdit);

    auto* browseBtn = new QPushButton(QStringLiteral("Alterar..."), this);
    browseBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(browseBtn, &QPushButton::clicked, this, &SettingsPage::onBrowseInstallPath);
    pathRow->addWidget(browseBtn);

    mainLayout->addLayout(pathRow);

    // Section 2: Geral
    auto* generalSectionLabel = new QLabel(QStringLiteral("GERAL"), this);
    generalSectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #787884; background: transparent; margin-top: 10px;"));
    mainLayout->addWidget(generalSectionLabel);

    m_autostartCheck = new QCheckBox(QStringLiteral("Iniciar o Creative Suite Hub junto com o Windows"), this);
    m_autostartCheck->setStyleSheet(QStringLiteral("color: #d0d0da; font-size: 13px;"));
    mainLayout->addWidget(m_autostartCheck);

    m_notificationsCheck = new QCheckBox(QStringLiteral("Notificar automaticamente sobre novas versões disponíveis"), this);
    m_notificationsCheck->setChecked(true);
    m_notificationsCheck->setStyleSheet(QStringLiteral("color: #d0d0da; font-size: 13px;"));
    mainLayout->addWidget(m_notificationsCheck);

    // Section 3: Diagnósticos e Logs
    auto* diagSectionLabel = new QLabel(QStringLiteral("DIAGNÓSTICO E REGISTROS DE ERRO"), this);
    diagSectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 700; color: #787884; background: transparent; margin-top: 10px;"));
    mainLayout->addWidget(diagSectionLabel);

    auto* logRow = new QHBoxLayout();
    logRow->setSpacing(8);

    m_logPathEdit = new QLineEdit(HubLogger::instance().logFilePath(), this);
    m_logPathEdit->setReadOnly(true);
    m_logPathEdit->setStyleSheet(HubStyle::searchInputStyle());
    logRow->addWidget(m_logPathEdit);

    auto* openLogBtn = new QPushButton(QStringLiteral("Abrir Pasta"), this);
    openLogBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(openLogBtn, &QPushButton::clicked, this, &SettingsPage::onOpenLogFolder);
    logRow->addWidget(openLogBtn);

    mainLayout->addLayout(logRow);

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
