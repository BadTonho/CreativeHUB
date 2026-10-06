#include "settings_page.h"
#include "../../diagnostics/hub_logger.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"
#include "../../model/storage_manager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QStandardPaths>

#include <QScrollArea>

namespace creative_suite::hub {

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void SettingsPage::setupUi() {
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* scrollContainer = new QWidget(scrollArea);
    scrollContainer->setStyleSheet(QStringLiteral("background: transparent;"));

    auto* mainLayout = new QVBoxLayout(scrollContainer);
    mainLayout->setContentsMargins(28, 24, 28, 24);
    mainLayout->setSpacing(20);

    // Title & Subtitle
    auto* titleLabel = new QLabel(QStringLiteral("Configurações do Hub"), scrollContainer);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 800; color: #ffffff; background: transparent;"));
    mainLayout->addWidget(titleLabel);

    auto* subLabel = new QLabel(QStringLiteral("Personalize caminhos de instalação, notificações e preferências do sistema."), scrollContainer);
    subLabel->setWordWrap(true);
    subLabel->setStyleSheet(QStringLiteral("font-size: 13px; color: #8e8e9e; background: transparent; margin-bottom: 4px;"));
    mainLayout->addWidget(subLabel);

    // Section 1: Instalação (Card)
    auto* installCard = new QFrame(scrollContainer);
    installCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #161616;"
        "   border: 1px solid #262626;"
        "   border-radius: 12px;"
        "   padding: 14px 18px;"
        "}"
    ));
    auto* installLayout = new QVBoxLayout(installCard);
    installLayout->setSpacing(10);

    auto* installSectionLabel = new QLabel(QStringLiteral("PASTA DE INSTALAÇÃO DOS APLICATIVOS"), installCard);
    installSectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #777777; letter-spacing: 0.8px; background: transparent;"));
    installLayout->addWidget(installSectionLabel);

    auto* pathRow = new QHBoxLayout();
    pathRow->setSpacing(10);

    const QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/apps");
    m_installPathEdit = new QLineEdit(defaultPath, installCard);
    m_installPathEdit->setReadOnly(true);
    m_installPathEdit->setFixedHeight(36);
    m_installPathEdit->setStyleSheet(HubStyle::searchInputStyle());
    pathRow->addWidget(m_installPathEdit, 1);

    auto* browseBtn = new QPushButton(QStringLiteral("Alterar..."), installCard);
    browseBtn->setFixedHeight(36);
    browseBtn->setCursor(Qt::PointingHandCursor);
    browseBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(browseBtn, &QPushButton::clicked, this, &SettingsPage::onBrowseInstallPath);
    pathRow->addWidget(browseBtn);

    installLayout->addLayout(pathRow);
    mainLayout->addWidget(installCard);

    // Section 2: Geral (Card)
    auto* generalCard = new QFrame(scrollContainer);
    generalCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #161616;"
        "   border: 1px solid #262626;"
        "   border-radius: 12px;"
        "   padding: 14px 18px;"
        "}"
    ));
    auto* generalLayout = new QVBoxLayout(generalCard);
    generalLayout->setSpacing(12);

    auto* generalSectionLabel = new QLabel(QStringLiteral("COMPORTAMENTO GERAL"), generalCard);
    generalSectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #777777; letter-spacing: 0.8px; background: transparent;"));
    generalLayout->addWidget(generalSectionLabel);

    m_autostartCheck = new QCheckBox(QStringLiteral("Iniciar o Creative Suite Hub junto com o Windows"), generalCard);
    m_autostartCheck->setMinimumHeight(24);
    m_autostartCheck->setStyleSheet(QStringLiteral(
        "QCheckBox { color: #d0d0d0; font-size: 13px; font-weight: 500; spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #383838; background: #1a1a1a; }"
        "QCheckBox::indicator:checked { background: #ffffff; border-color: #ffffff; }"
    ));
    generalLayout->addWidget(m_autostartCheck);

    m_notificationsCheck = new QCheckBox(QStringLiteral("Notificar automaticamente sobre novas versões disponíveis"), generalCard);
    m_notificationsCheck->setChecked(true);
    m_notificationsCheck->setMinimumHeight(24);
    m_notificationsCheck->setStyleSheet(QStringLiteral(
        "QCheckBox { color: #d0d0d0; font-size: 13px; font-weight: 500; spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #383838; background: #1a1a1a; }"
        "QCheckBox::indicator:checked { background: #ffffff; border-color: #ffffff; }"
    ));
    generalLayout->addWidget(m_notificationsCheck);

    mainLayout->addWidget(generalCard);

    // Section 3: Diagnósticos e Logs (Card)
    auto* diagCard = new QFrame(scrollContainer);
    diagCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #161616;"
        "   border: 1px solid #262626;"
        "   border-radius: 12px;"
        "   padding: 14px 18px;"
        "}"
    ));
    auto* diagLayout = new QVBoxLayout(diagCard);
    diagLayout->setSpacing(10);

    auto* diagSectionLabel = new QLabel(QStringLiteral("DIAGNÓSTICO E REGISTROS DE ERRO"), diagCard);
    diagSectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #777777; letter-spacing: 0.8px; background: transparent;"));
    diagLayout->addWidget(diagSectionLabel);

    auto* logRow = new QHBoxLayout();
    logRow->setSpacing(10);

    m_logPathEdit = new QLineEdit(HubLogger::instance().logFilePath(), diagCard);
    m_logPathEdit->setReadOnly(true);
    m_logPathEdit->setFixedHeight(36);
    m_logPathEdit->setStyleSheet(HubStyle::searchInputStyle());
    logRow->addWidget(m_logPathEdit, 1);

    auto* openLogBtn = new QPushButton(QStringLiteral("Abrir Pasta"), diagCard);
    openLogBtn->setFixedHeight(36);
    openLogBtn->setCursor(Qt::PointingHandCursor);
    openLogBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(openLogBtn, &QPushButton::clicked, this, &SettingsPage::onOpenLogFolder);
    logRow->addWidget(openLogBtn);

    diagLayout->addLayout(logRow);
    mainLayout->addWidget(diagCard);

    // Section 4: Armazenamento e Cache (Card)
    auto* storageCard = new QFrame(scrollContainer);
    storageCard->setStyleSheet(QStringLiteral(
        "QFrame {"
        "   background-color: #161616;"
        "   border: 1px solid #262626;"
        "   border-radius: 12px;"
        "   padding: 14px 18px;"
        "}"
    ));
    auto* storageLayout = new QVBoxLayout(storageCard);
    storageLayout->setSpacing(12);

    auto* storageSectionLabel = new QLabel(QStringLiteral("ARMAZENAMENTO E CACHE DA SUÍTE"), storageCard);
    storageSectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #777777; letter-spacing: 0.8px; background: transparent;"));
    storageLayout->addWidget(storageSectionLabel);

    auto* storageDescLabel = new QLabel(
        QStringLiteral("Gerencie o espaço em disco utilizado para pré-visualizações de linha do tempo, proxies de vídeo e arquivos temporários."),
        storageCard
    );
    storageDescLabel->setWordWrap(true);
    storageDescLabel->setStyleSheet(QStringLiteral("font-size: 12px; color: #888888; background: transparent;"));
    storageLayout->addWidget(storageDescLabel);

    auto* cacheInfoRow = new QHBoxLayout();
    cacheInfoRow->setSpacing(8);

    auto* cacheText = new QLabel(QStringLiteral("Espaço total em cache da suíte:"), storageCard);
    cacheText->setStyleSheet(QStringLiteral("color: #d0d0d0; font-size: 13px; font-weight: 500; background: transparent;"));
    cacheInfoRow->addWidget(cacheText);

    m_cacheSizeLabel = new QLabel(QStringLiteral("Calculando..."), storageCard);
    m_cacheSizeLabel->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 13px; font-weight: 800; background: transparent;"));
    cacheInfoRow->addWidget(m_cacheSizeLabel);
    cacheInfoRow->addStretch();

    storageLayout->addLayout(cacheInfoRow);

    auto* cacheActionsRow = new QHBoxLayout();
    cacheActionsRow->setSpacing(10);

    auto* clearCacheBtn = new QPushButton(QStringLiteral("Limpar Cache Seguro"), storageCard);
    clearCacheBtn->setFixedHeight(36);
    clearCacheBtn->setCursor(Qt::PointingHandCursor);
    clearCacheBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(clearCacheBtn, &QPushButton::clicked, this, &SettingsPage::onClearCache);
    cacheActionsRow->addWidget(clearCacheBtn);

    auto* openCacheBtn = new QPushButton(QStringLiteral("Abrir Pasta de Cache"), storageCard);
    openCacheBtn->setFixedHeight(36);
    openCacheBtn->setCursor(Qt::PointingHandCursor);
    openCacheBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(openCacheBtn, &QPushButton::clicked, this, &SettingsPage::onOpenCacheFolder);
    cacheActionsRow->addWidget(openCacheBtn);

    cacheActionsRow->addStretch();
    storageLayout->addLayout(cacheActionsRow);

    m_cacheStatusNote = new QLabel(storageCard);
    m_cacheStatusNote->setVisible(false);
    m_cacheStatusNote->setStyleSheet(QStringLiteral("color: #ffffff; font-size: 12px; font-weight: 600; background: transparent;"));
    storageLayout->addWidget(m_cacheStatusNote);

    mainLayout->addWidget(storageCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContainer);
    rootLayout->addWidget(scrollArea);

    refreshCacheSize();
}

void SettingsPage::refreshCacheSize() {
    const qint64 bytes = StorageManager::calculateTotalCacheSize();
    if (m_cacheSizeLabel) {
        m_cacheSizeLabel->setText(StorageManager::formatBytes(bytes));
    }
}

void SettingsPage::onClearCache() {
    StorageManager::clearSuiteCache();
    refreshCacheSize();
    if (m_cacheStatusNote) {
        m_cacheStatusNote->setText(QStringLiteral("✓ Cache da suíte liberado com sucesso!"));
        m_cacheStatusNote->setVisible(true);
    }
}

void SettingsPage::onOpenCacheFolder() {
    const QString cachePath = StorageManager::defaultCachePath();
    QDir().mkpath(cachePath);
    QDesktopServices::openUrl(QUrl::fromLocalFile(cachePath));
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
