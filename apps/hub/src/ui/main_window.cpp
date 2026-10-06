#include "main_window.h"
#include "dialogs/app_details_dialog.h"
#include "theme/hub_palette.h"
#include "theme/hub_style.h"
#include "../diagnostics/hub_logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

namespace creative_suite::hub {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Creative Suite Hub"));
    setWindowIcon(QIcon(QStringLiteral(":/app-icon/icon.png")));
    resize(1020, 680);
    setMinimumSize(880, 560);

    setStyleSheet(HubStyle::globalStyleSheet());

    setupUi();
    scanInstalledApps();
}

void MainWindow::setupUi() {
    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // Top Header
    m_headerBar = new HeaderBar(this);
    connect(m_headerBar, &HeaderBar::searchTextChanged, this, [this](const QString& text) {
        if (m_appsPage) {
            m_appsPage->setFilterQuery(text);
        }
    });
    rootLayout->addWidget(m_headerBar);

    // Body: Sidebar + Stacked Pages
    auto* bodyLayout = new QHBoxLayout();
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    m_sidebarWidget = new SidebarWidget(this);
    connect(m_sidebarWidget, &SidebarWidget::pageSelected, this, [this](int index) {
        if (m_pagesStack) {
            m_pagesStack->setCurrentIndex(index);
        }
    });
    bodyLayout->addWidget(m_sidebarWidget);

    m_pagesStack = new QStackedWidget(this);

    m_appsPage = new AppsPage(&m_catalog, this);
    connect(m_appsPage, &AppsPage::appDetailsRequested, this, &MainWindow::onShowAppDetails);
    connect(m_appsPage, &AppsPage::openAppRequested, this, &MainWindow::onOpenApp);
    connect(m_appsPage, &AppsPage::downloadAppRequested, this, &MainWindow::onDownloadApp);
    connect(m_appsPage, &AppsPage::cancelDownloadRequested, this, &MainWindow::onCancelDownload);
    m_pagesStack->addWidget(m_appsPage);

    m_updatesPage = new UpdatesPage(&m_catalog, this);
    connect(m_updatesPage, &UpdatesPage::appDetailsRequested, this, &MainWindow::onShowAppDetails);
    connect(m_updatesPage, &UpdatesPage::checkUpdatesRequested, this, &MainWindow::onRefreshApps);
    connect(m_updatesPage, &UpdatesPage::updateAppRequested, this, &MainWindow::onDownloadApp);
    m_pagesStack->addWidget(m_updatesPage);

    m_settingsPage = new SettingsPage(this);
    m_pagesStack->addWidget(m_settingsPage);

    bodyLayout->addWidget(m_pagesStack, 1);
    rootLayout->addLayout(bodyLayout, 1);

    // Download simulation timer for UI visual demo
    m_downloadTimer = new QTimer(this);
    connect(m_downloadTimer, &QTimer::timeout, this, &MainWindow::simulateDownloadStep);
}

void MainWindow::scanInstalledApps() {
    int updatesCount = 0;

    for (const auto& app : m_catalog.apps()) {
        if (m_launcher.isInstalled(app)) {
            m_catalog.updateAppStatus(app.id(), AppStatus::Installed);
            m_catalog.updateAppVersion(app.id(), app.latestVersion());
        }
    }

    m_sidebarWidget->setUpdatesCount(updatesCount);
    if (m_updatesPage) {
        m_updatesPage->refreshUpdates();
    }
}

void MainWindow::onShowAppDetails(const QString& appId) {
    auto appOpt = m_catalog.findApp(appId);
    if (!appOpt.has_value()) {
        return;
    }

    HubLogger::instance().logInfo(
        QStringLiteral("MainWindow"),
        QStringLiteral("onShowAppDetails"),
        QStringLiteral("Exibindo detalhes do aplicativo"),
        appId
    );

    AppDetailsDialog dialog(*appOpt, this);
    connect(&dialog, &AppDetailsDialog::actionRequested, this, [this](const QString& id) {
        auto opt = m_catalog.findApp(id);
        if (opt.has_value()) {
            if (opt->isInstalled()) {
                if (opt->hasUpdate()) {
                    onDownloadApp(id);
                } else {
                    onOpenApp(id);
                }
            } else {
                onDownloadApp(id);
            }
        }
    });
    dialog.exec();
}

void MainWindow::onRefreshApps() {
    HubLogger::instance().logInfo(QStringLiteral("MainWindow"), QStringLiteral("onRefreshApps"), QStringLiteral("Verificando aplicativos instalados"));
    scanInstalledApps();
}

void MainWindow::onOpenApp(const QString& appId) {
    auto appOpt = m_catalog.findApp(appId);
    if (!appOpt.has_value()) {
        return;
    }

    const auto& app = *appOpt;
    const bool success = m_launcher.launch(app);
    if (!success) {
        QMessageBox::warning(
            this,
            QStringLiteral("Aplicativo não encontrado"),
            QStringLiteral("Não foi possível encontrar ou executar o arquivo '%1'. Certifique-se de que o aplicativo foi compilado.")
                .arg(app.executableName())
        );
    }
}

void MainWindow::onDownloadApp(const QString& appId) {
    HubLogger::instance().logInfo(
        QStringLiteral("MainWindow"),
        QStringLiteral("onDownloadApp"),
        QStringLiteral("Iniciando download visual do app"),
        appId
    );

    m_activeDownloadingAppId = appId;
    m_activeDownloadProgress = 0.0;
    m_catalog.updateAppStatus(appId, AppStatus::Downloading);

    m_downloadTimer->start(100);
}

void MainWindow::onCancelDownload(const QString& appId) {
    if (m_activeDownloadingAppId == appId) {
        m_downloadTimer->stop();
        m_activeDownloadingAppId.clear();
        m_activeDownloadProgress = 0.0;
        m_catalog.updateAppStatus(appId, AppStatus::NotInstalled);
    }
}

void MainWindow::simulateDownloadStep() {
    if (m_activeDownloadingAppId.isEmpty()) {
        m_downloadTimer->stop();
        return;
    }

    m_activeDownloadProgress += 4.0;
    if (m_activeDownloadProgress >= 100.0) {
        m_downloadTimer->stop();
        m_catalog.updateAppStatus(m_activeDownloadingAppId, AppStatus::Installed);
        auto appOpt = m_catalog.findApp(m_activeDownloadingAppId);
        if (appOpt) {
            m_catalog.updateAppVersion(m_activeDownloadingAppId, appOpt->latestVersion());
        }
        m_activeDownloadingAppId.clear();
        m_activeDownloadProgress = 0.0;
        scanInstalledApps();
    } else {
        const double mb = (m_activeDownloadProgress / 100.0) * 180.0;
        const QString text = QStringLiteral("%1 MB / 180 MB • 14.2 MB/s (%2%)")
            .arg(QString::number(mb, 'f', 1))
            .arg(static_cast<int>(m_activeDownloadProgress));

        if (m_appsPage) {
            m_appsPage->refreshCards();
        }
    }
}

} // namespace creative_suite::hub
