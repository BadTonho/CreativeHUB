#include "settings_page.h"
#include <QCoreApplication>
#include "../../diagnostics/hub_logger.h"
#include "../theme/hub_palette.h"
#include "../theme/hub_style.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include <QFrame>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QStandardPaths>
#include <QScrollArea>
#include <QComboBox>
#include <QSignalBlocker>
#include <QSettings>
#include <QDir>
#include <creative_suite/translations/language_preference.h>

namespace creative_suite::hub {

SettingsPage::SettingsPage(QWidget* parent, bool enableStartupIntegration)
    : QWidget(parent)
    , m_enableStartupIntegration(enableStartupIntegration)
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
    auto* titleLabel = new QLabel(QCoreApplication::translate("SettingsPage", "Hub Settings"), scrollContainer);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 22px; font-weight: 800; color: #ffffff; background: transparent;"));
    mainLayout->addWidget(titleLabel);

    auto* subLabel = new QLabel(QCoreApplication::translate("SettingsPage", "Customize installation paths, notifications, and system preferences."), scrollContainer);
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

    auto* installSectionLabel = new QLabel(QCoreApplication::translate("SettingsPage", "APPLICATION INSTALLATION FOLDER"), installCard);
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

    auto* browseBtn = new QPushButton(QCoreApplication::translate("SettingsPage", "Change..."), installCard);
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

    auto* generalSectionLabel = new QLabel(QCoreApplication::translate("SettingsPage", "GENERAL BEHAVIOR"), generalCard);
    generalSectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #777777; letter-spacing: 0.8px; background: transparent;"));
    generalLayout->addWidget(generalSectionLabel);

    using creative_suite::translations::LanguagePreference;
    m_savedLanguagePreference = creative_suite::translations::loadLanguagePreference(
        QStringLiteral("hub"));
    auto* languageRow = new QHBoxLayout();
    auto* languageLabel = new QLabel(
        QCoreApplication::translate("SettingsPage", "Language"), generalCard);
    m_languageCombo = new QComboBox(generalCard);
    m_languageCombo->setObjectName(QStringLiteral("languagePreferenceComboBox"));
    m_languageCombo->addItem(
        QCoreApplication::translate("SettingsPage", "System language"),
        static_cast<int>(LanguagePreference::System));
    m_languageCombo->addItem(
        QCoreApplication::translate("SettingsPage", "English"),
        static_cast<int>(LanguagePreference::English));
    m_languageCombo->addItem(
        QCoreApplication::translate("SettingsPage", "Português (Brasil)"),
        static_cast<int>(LanguagePreference::PortugueseBrazil));
    m_languageCombo->setCurrentIndex(m_languageCombo->findData(
        static_cast<int>(m_savedLanguagePreference)));
    languageRow->addWidget(languageLabel);
    languageRow->addWidget(m_languageCombo, 1);
    generalLayout->addLayout(languageRow);

    m_languageNote = new QLabel(
        QCoreApplication::translate(
            "SettingsPage", "Changes take effect the next time you open this app."),
        generalCard);
    m_languageNote->setObjectName(QStringLiteral("languagePreferenceNote"));
    m_languageNote->setWordWrap(true);
    generalLayout->addWidget(m_languageNote);
    connect(m_languageCombo,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int index) {
        using creative_suite::translations::LanguagePreference;
        const auto selected = static_cast<LanguagePreference>(
            m_languageCombo->itemData(index).toInt());
        QString error;
        if (!creative_suite::translations::saveLanguagePreference(
                QStringLiteral("hub"), selected, &error)) {
            HubLogger::instance().logWarning(
                QStringLiteral("SettingsPage"),
                QStringLiteral("save_language_preference"), error,
                QStringLiteral("application=hub"));
            const QSignalBlocker blocker(m_languageCombo);
            m_languageCombo->setCurrentIndex(m_languageCombo->findData(
                static_cast<int>(m_savedLanguagePreference)));
            m_languageNote->setText(QCoreApplication::translate(
                "SettingsPage", "The language preference could not be saved. See the Hub log."));
            return;
        }
        m_savedLanguagePreference = selected;
        m_languageNote->setText(QCoreApplication::translate(
            "SettingsPage", "Changes take effect the next time you open this app."));
    });

    const bool notify_updates = m_enableStartupIntegration
        ? QSettings().value(QStringLiteral("hub/notify_updates"), true).toBool() : true;
    m_autostartCheck = new QCheckBox(QCoreApplication::translate("SettingsPage", "Start Creative Suite Hub with Windows"), generalCard);
    m_autostartCheck->setObjectName(QStringLiteral("hubAutostartCheckBox"));
    m_autostartCheck->setMinimumHeight(24);
    m_autostartCheck->setStyleSheet(QStringLiteral(
        "QCheckBox { color: #d0d0d0; font-size: 13px; font-weight: 500; spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #383838; background: #1a1a1a; }"
        "QCheckBox::indicator:checked { background: #ffffff; border-color: #ffffff; }"
    ));
    const QString run_key = QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    const QString startup_name = QStringLiteral("CreativeSuiteHub");
#ifdef Q_OS_WIN
    if (m_enableStartupIntegration) {
        QSettings startup(run_key, QSettings::NativeFormat);
        m_autostartCheck->setChecked(startup.contains(startup_name));
        connect(m_autostartCheck, &QCheckBox::toggled, this,
                [this, run_key, startup_name](bool enabled) {
            QSettings startup_settings(run_key, QSettings::NativeFormat);
            if (enabled) {
                startup_settings.setValue(startup_name,
                    QLatin1Char('"') + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + QLatin1Char('"'));
            } else {
                startup_settings.remove(startup_name);
            }
            startup_settings.sync();
            if (startup_settings.status() != QSettings::NoError) {
                HubLogger::instance().logError(QStringLiteral("SettingsPage"),
                    QStringLiteral("set_startup_preference"),
                    QStringLiteral("Could not update the current-user startup entry."),
                    QCoreApplication::applicationFilePath());
                QSignalBlocker blocker(m_autostartCheck);
                m_autostartCheck->setChecked(!enabled);
            }
        });
    } else {
        m_autostartCheck->setChecked(false);
        m_autostartCheck->setEnabled(false);
    }
#else
    m_autostartCheck->setChecked(false);
    m_autostartCheck->setEnabled(false);
    m_autostartCheck->setVisible(false);
#endif
    generalLayout->addWidget(m_autostartCheck);

    m_notificationsCheck = new QCheckBox(QCoreApplication::translate("SettingsPage", "Notify me automatically about available updates"), generalCard);
    m_notificationsCheck->setObjectName(QStringLiteral("hubUpdateNotificationsCheckBox"));
    m_notificationsCheck->setChecked(notify_updates);
    m_notificationsCheck->setMinimumHeight(24);
    m_notificationsCheck->setStyleSheet(QStringLiteral(
        "QCheckBox { color: #d0d0d0; font-size: 13px; font-weight: 500; spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #383838; background: #1a1a1a; }"
        "QCheckBox::indicator:checked { background: #ffffff; border-color: #ffffff; }"
    ));
    generalLayout->addWidget(m_notificationsCheck);
    if (m_enableStartupIntegration) connect(m_notificationsCheck, &QCheckBox::toggled, this, [](bool enabled) {
        QSettings settings;
        settings.setValue(QStringLiteral("hub/notify_updates"), enabled);
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            HubLogger::instance().logError(QStringLiteral("SettingsPage"),
                QStringLiteral("save_update_notification_preference"),
                QStringLiteral("Could not save the update notification preference."));
        }
    });

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

    auto* diagSectionLabel = new QLabel(QCoreApplication::translate("SettingsPage", "DIAGNOSTICS AND ERROR LOGS"), diagCard);
    diagSectionLabel->setStyleSheet(QStringLiteral("font-size: 11px; font-weight: 800; color: #777777; letter-spacing: 0.8px; background: transparent;"));
    diagLayout->addWidget(diagSectionLabel);

    auto* logRow = new QHBoxLayout();
    logRow->setSpacing(10);

    m_logPathEdit = new QLineEdit(HubLogger::instance().logFilePath(), diagCard);
    m_logPathEdit->setReadOnly(true);
    m_logPathEdit->setFixedHeight(36);
    m_logPathEdit->setStyleSheet(HubStyle::searchInputStyle());
    logRow->addWidget(m_logPathEdit, 1);

    auto* openLogBtn = new QPushButton(QCoreApplication::translate("SettingsPage", "Open Folder"), diagCard);
    openLogBtn->setFixedHeight(36);
    openLogBtn->setCursor(Qt::PointingHandCursor);
    openLogBtn->setStyleSheet(HubStyle::secondaryButtonStyle());
    connect(openLogBtn, &QPushButton::clicked, this, &SettingsPage::onOpenLogFolder);
    logRow->addWidget(openLogBtn);

    diagLayout->addLayout(logRow);
    mainLayout->addWidget(diagCard);

    mainLayout->addStretch();

    scrollArea->setWidget(scrollContainer);
    rootLayout->addWidget(scrollArea);
}

void SettingsPage::onBrowseInstallPath() {
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        QCoreApplication::translate("SettingsPage", "Select installation folder"),
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
