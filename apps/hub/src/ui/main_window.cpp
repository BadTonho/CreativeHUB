#include "main_window.h"
#include "dialogs/app_details_dialog.h"
#include "theme/hub_palette.h"
#include "theme/hub_style.h"
#include "../diagnostics/hub_logger.h"
#include "../model/activity_manager.h"
#include "../model/backup_manager.h"
#ifdef Q_OS_WIN
#include <creative_suite/updater/update_dialog.h>
#include <creative_suite/updater/update_service.h>
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QResizeEvent>
#include <QStatusBar>
#include <QTimer>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>

namespace creative_suite::hub {

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Creative Suite Hub"));
    setWindowIcon(QIcon(QStringLiteral(":/app-icon/icon.png")));
    resize(1080, 700);
    setMinimumSize(800, 520);

    setStyleSheet(HubStyle::globalStyleSheet());

    setupUi();
    scanInstalledApps();
    configureUpdateServices();
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (m_detailsModal) {
        m_detailsModal->setGeometry(rect());
    }
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
        if (index == 3 && m_storagePage) {
            m_storagePage->refreshStorageInfo();
        } else if (index == 4 && m_backupsPage) {
            m_backupsPage->refreshBackupsList();
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

    m_projectsPage = new ProjectsPage(&m_recentProjectsManager, &m_catalog, this);
    connect(m_projectsPage, &ProjectsPage::openProjectRequested, this, &MainWindow::onOpenProject);
    connect(m_projectsPage, &ProjectsPage::newProjectRequested, this, &MainWindow::onNewProject);
    m_pagesStack->addWidget(m_projectsPage);

    m_updatesPage = new UpdatesPage(&m_catalog, this);
    connect(m_updatesPage, &UpdatesPage::appDetailsRequested, this, &MainWindow::onShowAppDetails);
    connect(m_updatesPage, &UpdatesPage::checkUpdatesRequested, this, &MainWindow::onRefreshApps);
    connect(m_updatesPage, &UpdatesPage::updateAppRequested, this, &MainWindow::onDownloadApp);
    m_pagesStack->addWidget(m_updatesPage);

    m_storagePage = new StoragePage(this);
    m_pagesStack->addWidget(m_storagePage);

    m_backupsPage = new BackupsPage(this);
    m_pagesStack->addWidget(m_backupsPage);

    m_settingsPage = new SettingsPage(this);
    m_pagesStack->addWidget(m_settingsPage);

    bodyLayout->addWidget(m_pagesStack, 1);
    rootLayout->addLayout(bodyLayout, 1);

    // Animated In-Window Popup Modal
    m_detailsModal = new AppDetailsModal(this);
    m_detailsModal->setGeometry(rect());
    connect(m_detailsModal, &AppDetailsModal::actionRequested, this, [this](const QString& id) {
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

}

void MainWindow::scanInstalledApps() {
    int updatesCount = 0;

    for (const auto& app : m_catalog.apps()) {
#ifdef Q_OS_WIN
        const auto registration = creative_suite::updater::readInstalledApplication(app.id());
        if (registration) {
            m_launcher.addSearchPath(registration->install_path);
            const auto comparison = creative_suite::updater::compareVersions(
                app.latestVersion(), registration->version);
            m_catalog.updateAppStatus(app.id(),
                comparison && *comparison > 0 ? AppStatus::UpdateAvailable : AppStatus::Installed);
            m_catalog.updateAppVersion(app.id(), registration->version);
        } else if (m_launcher.isInstalled(app)) {
            m_catalog.updateAppStatus(app.id(), AppStatus::Installed);
        }
#else
        if (m_launcher.isInstalled(app)) {
            m_catalog.updateAppStatus(app.id(), AppStatus::Installed);
            m_catalog.updateAppVersion(app.id(), app.latestVersion());
        }
#endif
        const auto current = m_catalog.findApp(app.id());
        if (current && current->hasUpdate()) ++updatesCount;
    }

    m_sidebarWidget->setUpdatesCount(updatesCount);
    if (m_updatesPage) {
        m_updatesPage->refreshUpdates();
    }
}

void MainWindow::configureUpdateServices() {
#ifdef Q_OS_WIN
    m_updateRuntime = new creative_suite::updater::UpdateRuntime(QStringLiteral("hub"), this);
    m_updateRuntime->resumePendingInstalls({QStringLiteral("video-editor"),
                                            QStringLiteral("image-editor"),
                                            QStringLiteral("motion-editor")});
    for (const auto& app : m_catalog.apps()) {
        auto* service = new creative_suite::updater::UpdateService(
            creative_suite::updater::defaultConfig(
                app.id(), app.name(), app.executableName(), app.latestVersion()), this);
        m_updateServices.insert(app.id(), service);
        connect(service, &creative_suite::updater::UpdateService::updateAvailable,
                this, [this, service](const creative_suite::updater::ReleaseEntry& entry) {
            m_catalog.updateLatestVersion(entry.app_id, entry.version);
            if (service->config().installed) {
                m_catalog.updateAppStatus(entry.app_id, AppStatus::UpdateAvailable);
            }
            scanInstalledApps();
        });
        connect(service, &creative_suite::updater::UpdateService::stateChanged,
                this, [this, service, app_id = app.id()](creative_suite::updater::UpdateState state) {
            if (state == creative_suite::updater::UpdateState::Downloading) {
                m_catalog.updateAppStatus(app_id, AppStatus::Downloading);
            } else if (state == creative_suite::updater::UpdateState::UpdateAvailable) {
                m_catalog.updateAppStatus(app_id, service->config().installed
                    ? AppStatus::UpdateAvailable : AppStatus::NotInstalled);
            } else if (state == creative_suite::updater::UpdateState::UpToDate &&
                       service->config().installed) {
                m_catalog.updateAppStatus(app_id, AppStatus::Installed);
                m_catalog.updateAppVersion(app_id, service->config().current_version);
            } else if (state == creative_suite::updater::UpdateState::Failed) {
                const auto app = m_catalog.findApp(app_id);
                if (app && service->config().installed) {
                    const auto comparison = creative_suite::updater::compareVersions(
                        app->latestVersion(), service->config().current_version);
                    m_catalog.updateAppStatus(app_id,
                        comparison && *comparison > 0 ? AppStatus::UpdateAvailable : AppStatus::Installed);
                } else if (!service->config().installed) {
                    m_catalog.updateAppStatus(app_id, AppStatus::NotInstalled);
                }
            }
        });
        connect(service, &creative_suite::updater::UpdateService::operationFailed,
                this, [this](const QString& message) {
            statusBar()->showMessage(message, 10000);
        });
        connect(service, &creative_suite::updater::UpdateService::downloadReady,
                this, [this, service](const QString& installer,
                                      const creative_suite::updater::ReleaseEntry& entry) {
            if (!m_updateRuntime) return;
            const auto& config = service->config();
            if (!m_updateRuntime->installOrWait(entry.app_id, config.install_path,
                    config.executable_name, installer, entry.sha256_hex, config.installed)) {
                return;
            }
            m_catalog.updateAppStatus(entry.app_id, AppStatus::Installing);
            statusBar()->showMessage(QStringLiteral("O instalador de %1 foi iniciado.")
                                     .arg(config.app_name), 8000);
            QTimer::singleShot(5000, this, &MainWindow::scanInstalledApps);
        });
        connect(m_updateRuntime, &creative_suite::updater::UpdateRuntime::installationStateChanged,
                this, [this](const QString& app_id, creative_suite::updater::UpdateState state) {
            if (app_id != QStringLiteral("hub") &&
                (state == creative_suite::updater::UpdateState::WaitingForApplicationToClose ||
                 state == creative_suite::updater::UpdateState::InstallerStarted)) {
                m_catalog.updateAppStatus(app_id, AppStatus::Installing);
            }
        });
        connect(m_updateRuntime, &creative_suite::updater::UpdateRuntime::installationFailed,
                this, [this](const QString& app_id, const QString& message) {
            statusBar()->showMessage(message, 10000);
            const auto app = m_catalog.findApp(app_id);
            if (app && app->isInstalled()) m_catalog.updateAppStatus(app_id, AppStatus::Installed);
        });
        service->checkForUpdates();
    }
    QTimer::singleShot(0, this, [this] {
        for (const auto& app : m_catalog.apps()) {
            const auto registration = creative_suite::updater::readInstalledApplication(app.id());
            if (!registration || !registration->rollback_available ||
                !registration->needs_launch_check) continue;

            QMessageBox recovery(this);
            recovery.setIcon(QMessageBox::Warning);
            recovery.setWindowTitle(QStringLiteral("Verifique a atualização"));
            recovery.setText(QStringLiteral("O %1 foi atualizado recentemente, mas ainda não confirmou a inicialização. Ele está abrindo corretamente?")
                             .arg(app.name()));
            auto* restore = recovery.addButton(QStringLiteral("Restaurar versão anterior"),
                                               QMessageBox::AcceptRole);
            auto* working = recovery.addButton(QStringLiteral("Está funcionando"),
                                               QMessageBox::RejectRole);
            recovery.exec();
            if (recovery.clickedButton() == restore) {
                QString error;
                if (creative_suite::updater::restorePreviousVersion(app.id(), &error)) {
                    QMessageBox::information(this, QStringLiteral("Versão restaurada"),
                        QStringLiteral("A versão anterior do %1 foi restaurada.").arg(app.name()));
                } else {
                    QMessageBox::warning(this, QStringLiteral("Não foi possível restaurar"), error);
                }
            } else if (recovery.clickedButton() == working) {
                creative_suite::updater::markApplicationStartupHealthy(app.id());
            }
        }
    });
#endif
}

void MainWindow::onShowAppDetails(const QString& appId, const QRect& originRect) {
    auto appOpt = m_catalog.findApp(appId);
    if (!appOpt.has_value()) {
        return;
    }

    HubLogger::instance().logInfo(
        QStringLiteral("MainWindow"),
        QStringLiteral("onShowAppDetails"),
        QStringLiteral("Exibindo popup animado com os detalhes do aplicativo"),
        appId
    );

    if (m_detailsModal) {
        m_detailsModal->setGeometry(rect());
        m_detailsModal->showApp(*appOpt, originRect);
    }
}

void MainWindow::onRefreshApps() {
    HubLogger::instance().logInfo(QStringLiteral("MainWindow"), QStringLiteral("onRefreshApps"), QStringLiteral("Verificando aplicativos instalados"));
    scanInstalledApps();
#ifdef Q_OS_WIN
    for (auto* service : m_updateServices) {
        if (service && service->state() != creative_suite::updater::UpdateState::Downloading) {
            service->checkForUpdates();
        }
    }
#endif
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
    } else {
        ActivityManager::instance().addActivity(
            QStringLiteral("Aplicativo Aberto"),
            QStringLiteral("O %1 foi iniciado com sucesso.").arg(app.name()),
            QStringLiteral("system")
        );
    }
}

void MainWindow::onOpenProject(const QString& filePath, const QString& appId) {
    QString targetAppId = appId;
    if (targetAppId.isEmpty()) {
        targetAppId = RecentProjectsManager::detectAppForFile(filePath);
    }

    HubLogger::instance().logInfo(
        QStringLiteral("MainWindow"),
        QStringLiteral("onOpenProject"),
        QStringLiteral("Abrindo projeto"),
        QStringLiteral("%1 (%2)").arg(filePath, targetAppId)
    );

    auto appOpt = m_catalog.findApp(targetAppId);
    if (appOpt.has_value()) {
        const auto& app = *appOpt;
        const bool success = m_launcher.launch(app, {filePath});
        if (!success) {
            QMessageBox::warning(
                this,
                QStringLiteral("Aplicativo não encontrado"),
                QStringLiteral("Não foi possível encontrar ou executar o aplicativo '%1' para abrir o projeto '%2'. Certifique-se de que o aplicativo foi compilado.")
                    .arg(app.name(), filePath)
            );
        } else {
            m_recentProjectsManager.addOrUpdateProject(filePath, targetAppId);
            ActivityManager::instance().addActivity(
                QStringLiteral("Projeto Aberto"),
                QStringLiteral("Projeto '%1' aberto no %2.").arg(QFileInfo(filePath).fileName(), app.name()),
                QStringLiteral("project")
            );
            if (m_projectsPage) {
                m_projectsPage->refreshList();
            }
        }
    } else {
        // Fallback: try opening with default desktop tool
        const bool opened = QDesktopServices::openUrl(QUrl::fromLocalFile(filePath));
        if (opened) {
            m_recentProjectsManager.addOrUpdateProject(filePath, targetAppId);
            ActivityManager::instance().addActivity(
                QStringLiteral("Projeto Aberto"),
                QStringLiteral("Projeto '%1' aberto no sistema.").arg(QFileInfo(filePath).fileName()),
                QStringLiteral("project")
            );
            if (m_projectsPage) {
                m_projectsPage->refreshList();
            }
        } else {
            QMessageBox::warning(
                this,
                QStringLiteral("Não foi possível abrir o projeto"),
                QStringLiteral("Não há nenhum aplicativo associado para abrir o arquivo '%1'.").arg(filePath)
            );
        }
    }
}

void MainWindow::onNewProject(const QString& appId) {
    HubLogger::instance().logInfo(
        QStringLiteral("MainWindow"),
        QStringLiteral("onNewProject"),
        QStringLiteral("Criando novo projeto no aplicativo"),
        appId
    );
    onOpenApp(appId);
}

void MainWindow::onDownloadApp(const QString& appId) {
#ifdef Q_OS_WIN
    auto* service = m_updateServices.value(appId, nullptr);
    if (!service || !m_updateRuntime) {
        statusBar()->showMessage(QStringLiteral("O atualizador não está disponível para este aplicativo."), 8000);
        return;
    }
    creative_suite::updater::UpdateDialog dialog(service, m_updateRuntime, this);
    dialog.exec();
#else
    Q_UNUSED(appId);
    statusBar()->showMessage(QStringLiteral("A instalação pelo Hub estará disponível nesta plataforma em uma etapa futura."), 8000);
#endif
}

void MainWindow::onCancelDownload(const QString& appId) {
#ifdef Q_OS_WIN
    auto* service = m_updateServices.value(appId, nullptr);
    if (service) service->cancelDownload();
#else
    Q_UNUSED(appId);
#endif
}

} // namespace creative_suite::hub
