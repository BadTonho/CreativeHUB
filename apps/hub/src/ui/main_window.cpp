#include "main_window.h"
#include <QCoreApplication>
#include "dialogs/app_details_dialog.h"
#include "theme/hub_palette.h"
#include "theme/hub_style.h"
#include "../diagnostics/hub_logger.h"
#include "../model/activity_manager.h"
#include "../model/backup_manager.h"
#ifdef Q_OS_WIN
#include "../update_ui/update_dialog.h"
#include <creative_suite/updater/update_service.h>
#endif

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QResizeEvent>
#include <QStatusBar>
#include <QTimer>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QLocale>
#include <algorithm>
#include <creative_suite/translations/language_preference.h>

namespace creative_suite::hub {

MainWindow::MainWindow(QWidget* parent)
    : MainWindow(UpdateDependencies{}, parent)
{}

MainWindow::MainWindow(UpdateDependencies dependencies, QWidget* parent)
    : QMainWindow(parent)
    , m_updateDependencies(std::move(dependencies))
{
#ifdef Q_OS_WIN
    if (!m_updateDependencies.read_installation) {
        m_updateDependencies.read_installation = creative_suite::updater::readInstalledApplication;
    }
    if (!m_updateDependencies.create_service) {
        m_updateDependencies.create_service = [](auto config, QObject* owner) {
            return new creative_suite::updater::UpdateService(std::move(config), owner);
        };
    }
    if (!m_updateDependencies.create_runtime) {
        m_updateDependencies.create_runtime = [](QObject* owner) {
            return new creative_suite::updater::UpdateRuntime(QStringLiteral("hub"), owner);
        };
    }
#endif
    setWindowTitle(QStringLiteral("Creative Suite Hub"));
    setWindowIcon(QIcon(QStringLiteral(":/app-icon/icon.png")));
    resize(1080, 700);
    setMinimumSize(800, 520);

    setStyleSheet(HubStyle::globalStyleSheet());

    m_translationUpdateService = new creative_suite::translations::TranslationUpdateService(
        creative_suite::translations::TranslationUpdateService::defaultConfig(), this);
    connect(m_translationUpdateService,
            &creative_suite::translations::TranslationUpdateService::operationFailed,
            this, [](const QString& operation, const QString& application_id, const QString& cause) {
        HubLogger::instance().logWarning(
            QStringLiteral("Translations"), operation, cause, application_id);
    });
    connect(m_translationUpdateService,
            &creative_suite::translations::TranslationUpdateService::stateChanged,
            this, [this](creative_suite::translations::TranslationUpdateState state) {
        if (!m_updatesPage) return;
        if (state == creative_suite::translations::TranslationUpdateState::Checking ||
            state == creative_suite::translations::TranslationUpdateState::Downloading) {
            m_updatesPage->setTranslationStatus(tr("Checking translation files…"), true);
        }
    });
    connect(m_translationUpdateService,
            &creative_suite::translations::TranslationUpdateService::finished,
            this, [this](int installed_count, bool had_errors) {
        if (had_errors) {
            m_updatesPage->setTranslationStatus(
                tr("Could not check all translation files. See the Hub log."));
        } else if (installed_count > 0) {
            m_updatesPage->setTranslationStatus(
                tr("Translation files downloaded. Restart the affected app to use them."));
        } else {
            m_updatesPage->setTranslationStatus(tr("Translation files are up to date."));
        }
    });

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
    connect(m_updatesPage, &UpdatesPage::checkTranslationUpdatesRequested,
            this, &MainWindow::onCheckTranslationUpdates);
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
        const auto registration = m_updateDependencies.read_installation(app.id());
        if (auto* service = m_updateServices.value(app.id(), nullptr)) {
            service->setInstallationState(registration.has_value(),
                registration ? registration->version : QString{},
                registration ? registration->install_path : QString{},
                registration && !registration->executable_name.isEmpty()
                    ? registration->executable_name : app.executableName());
        }
        if (registration) {
            m_launcher.addSearchPath(registration->install_path);
            const auto comparison = creative_suite::updater::compareVersions(
                app.latestVersion(), registration->version);
            m_catalog.updateAppStatus(app.id(),
                comparison && *comparison > 0 ? AppStatus::UpdateAvailable : AppStatus::Installed);
            m_catalog.updateAppVersion(app.id(), registration->version);
        } else if (m_launcher.isInstalled(app)) {
            m_catalog.updateAppStatus(app.id(), AppStatus::Installed);
            m_catalog.updateAppVersion(app.id(), QString{});
        } else {
            m_catalog.updateAppStatus(app.id(), AppStatus::NotInstalled);
            m_catalog.updateAppVersion(app.id(), QString{});
        }
        if (auto* service = m_updateServices.value(app.id(), nullptr);
            service && service->state() == creative_suite::updater::UpdateState::Downloading) {
            m_catalog.updateAppStatus(app.id(), AppStatus::Downloading);
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
    if (m_appsPage) m_appsPage->refreshCards();
    if (m_updatesPage) {
        m_updatesPage->refreshUpdates();
    }
}

void MainWindow::refreshServiceInstallation(creative_suite::updater::UpdateService* service) {
#ifdef Q_OS_WIN
    const auto app = m_catalog.findApp(service->config().app_id);
    if (!app) return;
    const auto registration = m_updateDependencies.read_installation(app->id());
    service->setInstallationState(registration.has_value(),
        registration ? registration->version : QString{},
        registration ? registration->install_path : QString{},
        registration && !registration->executable_name.isEmpty()
            ? registration->executable_name : app->executableName());
#else
    Q_UNUSED(service);
#endif
}

void MainWindow::configureUpdateServices() {
#ifdef Q_OS_WIN
    m_updateRuntime = m_updateDependencies.create_runtime(this);
    m_updateRuntime->resumePendingInstalls({QStringLiteral("video-editor"),
                                            QStringLiteral("image-editor"),
                                            QStringLiteral("motion-editor")});
    for (const auto& app : m_catalog.apps()) {
        auto* service = m_updateDependencies.create_service(
            creative_suite::updater::defaultConfig(
                app.id(), app.name(), app.executableName(), app.latestVersion()), this);
        m_updateServices.insert(app.id(), service);
        refreshServiceInstallation(service);
        connect(service, &creative_suite::updater::UpdateService::updateAvailable,
                this, [this, service](const creative_suite::updater::ReleaseEntry& entry) {
            m_catalog.updateLatestVersion(entry.app_id, entry.version);
            if (service->config().installed) {
                m_catalog.updateAppStatus(entry.app_id, AppStatus::UpdateAvailable);
            }
            QSettings preferences;
            if (preferences.value(QStringLiteral("hub/notify_updates"), true).toBool()) {
                const auto app = m_catalog.findApp(entry.app_id);
                statusBar()->showMessage(
                    QCoreApplication::translate("MainWindow", "%1 update %2 is available.")
                        .arg(app ? app->name() : entry.app_id, entry.version), 10000);
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
            if (m_appsPage) m_appsPage->refreshCards();
        });
        connect(service, &creative_suite::updater::UpdateService::downloadProgress,
                this, [this, app_id = app.id()](qint64 received, qint64 total) {
            if (!m_appsPage) return;
            const double percentage = total > 0
                ? std::clamp(100.0 * static_cast<double>(received) / static_cast<double>(total), 0.0, 100.0)
                : 0.0;
            m_appsPage->setDownloadingProgress(app_id, percentage,
                QCoreApplication::translate("MainWindow", "Downloading update…"));
        });
        connect(service, &creative_suite::updater::UpdateService::operationFailed,
                this, [this](const QString& message) {
            statusBar()->showMessage(message, 10000);
        });
        connect(service, &creative_suite::updater::UpdateService::downloadReady,
                this, [this, service](const QString& installer,
                                      const creative_suite::updater::ReleaseEntry& entry) {
            if (!m_updateRuntime) return;
            refreshServiceInstallation(service);
            const auto& config = service->config();
            if (config.installed) {
                const auto comparison = creative_suite::updater::compareVersions(
                    entry.version, config.current_version);
                if (!comparison) {
                    HubLogger::instance().logError(QStringLiteral("MainWindow"),
                        QStringLiteral("install_update"),
                        QStringLiteral("The registered application version is invalid."),
                        config.app_id + QStringLiteral(": ") + config.current_version);
                    statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Could not verify the installed version."), 8000);
                    return;
                }
                if (*comparison <= 0) {
                    scanInstalledApps();
                    // Keep the emitted release reference valid until signal delivery ends.
                    QTimer::singleShot(0, service, &creative_suite::updater::UpdateService::checkForUpdates);
                    return;
                }
            }
            if (!m_updateRuntime->installOrWait(entry.app_id, config.install_path,
                    config.executable_name, installer, entry.sha256_hex, config.installed)) {
                return;
            }
            m_catalog.updateAppStatus(entry.app_id, AppStatus::Installing);
            statusBar()->showMessage(QCoreApplication::translate("MainWindow", "The %1 installer has started.")
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
            const auto registration = m_updateDependencies.read_installation(app.id());
            if (!registration || !registration->rollback_available ||
                !registration->needs_launch_check) continue;

            QMessageBox recovery(this);
            recovery.setIcon(QMessageBox::Warning);
            recovery.setWindowTitle(QCoreApplication::translate("MainWindow", "Confirm the update"));
            recovery.setText(QCoreApplication::translate("MainWindow", "%1 was recently updated but has not confirmed startup yet. Is it opening correctly?")
                             .arg(app.name()));
            auto* restore = recovery.addButton(QCoreApplication::translate("MainWindow", "Restore Previous Version"),
                                               QMessageBox::AcceptRole);
            auto* working = recovery.addButton(QCoreApplication::translate("MainWindow", "It works"),
                                               QMessageBox::RejectRole);
            recovery.exec();
            if (recovery.clickedButton() == restore) {
                QString error;
                if (creative_suite::updater::restorePreviousVersion(app.id(), &error)) {
                    onRefreshApps();
                    QMessageBox::information(this, QCoreApplication::translate("MainWindow", "Version Restored"),
                        QCoreApplication::translate("MainWindow", "The previous version of %1 was restored.").arg(app.name()));
                } else {
                    QMessageBox::warning(this, QCoreApplication::translate("MainWindow", "Could not restore"), error);
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

void MainWindow::startTranslationUpdateCheck() {
    QTimer::singleShot(0, this, &MainWindow::onCheckTranslationUpdates);
}

void MainWindow::onCheckTranslationUpdates() {
    const QString locale = QStringLiteral("pt_BR");
    if (!creative_suite::translations::anyApplicationUsesTranslationLocale(
            locale, QLocale::system())) {
        m_updatesPage->setTranslationStatus(
            tr("None of your app language settings currently use a translated language."));
        return;
    }
    if (!m_translationUpdateService->checkForUpdates(locale)) {
        m_updatesPage->setTranslationStatus(tr("A translation check is already running."));
    }
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
            QCoreApplication::translate("MainWindow", "Application not found"),
            QCoreApplication::translate("MainWindow", "Could not find or launch the file '%1'. Make sure the application has been built.")
                .arg(app.executableName())
        );
    } else {
        ActivityManager::instance().addActivity(
            QCoreApplication::translate("MainWindow", "Application Opened"),
            QCoreApplication::translate("MainWindow", "%1 started successfully.").arg(app.name()),
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
        QCoreApplication::translate("MainWindow", "Opening project"),
        QCoreApplication::translate("MainWindow", "%1 (%2)").arg(filePath, targetAppId)
    );

    auto appOpt = m_catalog.findApp(targetAppId);
    if (appOpt.has_value()) {
        const auto& app = *appOpt;
        const bool success = m_launcher.launch(app, {filePath});
        if (!success) {
            QMessageBox::warning(
                this,
                QCoreApplication::translate("MainWindow", "Application not found"),
                QCoreApplication::translate("MainWindow", "Could not find or launch '%1' to open project '%2'. Make sure the application has been built.")
                    .arg(app.name(), filePath)
            );
        } else {
            m_recentProjectsManager.addOrUpdateProject(filePath, targetAppId);
            ActivityManager::instance().addActivity(
                QCoreApplication::translate("MainWindow", "Project Opened"),
                QCoreApplication::translate("MainWindow", "Project '%1' opened in %2.").arg(QFileInfo(filePath).fileName(), app.name()),
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
                QCoreApplication::translate("MainWindow", "Project Opened"),
                QCoreApplication::translate("MainWindow", "Project '%1' opened on this system.").arg(QFileInfo(filePath).fileName()),
                QStringLiteral("project")
            );
            if (m_projectsPage) {
                m_projectsPage->refreshList();
            }
        } else {
            QMessageBox::warning(
                this,
                QCoreApplication::translate("MainWindow", "Could not open project"),
                QCoreApplication::translate("MainWindow", "No application is associated with file '%1'.").arg(filePath)
            );
        }
    }
}

void MainWindow::onNewProject(const QString& appId) {
    HubLogger::instance().logInfo(
        QStringLiteral("MainWindow"),
        QStringLiteral("onNewProject"),
        QCoreApplication::translate("MainWindow", "Creating a new project in the application"),
        appId
    );
    onOpenApp(appId);
}

void MainWindow::onDownloadApp(const QString& appId) {
#ifdef Q_OS_WIN
    auto* service = m_updateServices.value(appId, nullptr);
    if (!service || !m_updateRuntime) {
        statusBar()->showMessage(QCoreApplication::translate("MainWindow", "The updater is not available for this application."), 8000);
        return;
    }
    scanInstalledApps();
    if (service->state() != creative_suite::updater::UpdateState::Downloading &&
        service->state() != creative_suite::updater::UpdateState::Checking) {
        service->checkForUpdates();
    }
    creative_suite::updater::UpdateDialog dialog(service, m_updateRuntime, this);
    dialog.exec();
#else
    Q_UNUSED(appId);
    statusBar()->showMessage(QCoreApplication::translate("MainWindow", "Hub-based installation will be available on this platform in a future update."), 8000);
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
