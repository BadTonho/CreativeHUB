#pragma once

#include "../model/app_catalog.h"
#include "../application/app_launcher.h"
#include "header/header_bar.h"
#include "sidebar/sidebar_widget.h"
#include "../model/recent_projects_manager.h"
#include "pages/apps_page.h"
#include "pages/projects_page.h"
#include "pages/updates_page.h"
#include "pages/storage_page.h"
#include "pages/backups_page.h"
#include "pages/settings_page.h"
#include "dialogs/app_details_modal.h"

#include <QMainWindow>
#include <QHash>
#include <QStackedWidget>
#include <creative_suite/updater/update_service.h>
#include <creative_suite/translations/translation_update_service.h>

namespace creative_suite::updater {
class UpdateRuntime;
class UpdateService;
}

namespace creative_suite::hub {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    // Optional I/O adapters run on the GUI thread. Factories must parent their
    // objects to the supplied owner; empty callbacks use the production I/O.
    struct UpdateDependencies {
        std::function<std::optional<creative_suite::updater::InstalledApplication>(const QString&)> read_installation;
        std::function<creative_suite::updater::UpdateService*(creative_suite::updater::UpdateServiceConfig, QObject*)> create_service;
        std::function<creative_suite::updater::UpdateRuntime*(QObject*)> create_runtime;
    };
    explicit MainWindow(QWidget* parent = nullptr);
    explicit MainWindow(UpdateDependencies dependencies, QWidget* parent = nullptr);
    void startTranslationUpdateCheck();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onShowAppDetails(const QString& appId, const QRect& originRect = QRect());
    void onOpenApp(const QString& appId);
    void onOpenProject(const QString& filePath, const QString& appId);
    void onNewProject(const QString& appId);
    void onDownloadApp(const QString& appId);
    void onCancelDownload(const QString& appId);
    void onRefreshApps();
    void onCheckTranslationUpdates();

private:
    void setupUi();
    void scanInstalledApps();
    void configureUpdateServices();
    void refreshServiceInstallation(creative_suite::updater::UpdateService* service);

    AppCatalog m_catalog;
    UpdateDependencies m_updateDependencies;
    AppLauncher m_launcher;
    RecentProjectsManager m_recentProjectsManager;

    HeaderBar* m_headerBar{nullptr};
    SidebarWidget* m_sidebarWidget{nullptr};
    QStackedWidget* m_pagesStack{nullptr};

    AppsPage* m_appsPage{nullptr};
    ProjectsPage* m_projectsPage{nullptr};
    UpdatesPage* m_updatesPage{nullptr};
    StoragePage* m_storagePage{nullptr};
    BackupsPage* m_backupsPage{nullptr};
    SettingsPage* m_settingsPage{nullptr};
    AppDetailsModal* m_detailsModal{nullptr};

    creative_suite::updater::UpdateRuntime* m_updateRuntime{nullptr};
    creative_suite::translations::TranslationUpdateService* m_translationUpdateService{nullptr};
    QHash<QString, creative_suite::updater::UpdateService*> m_updateServices;
};

} // namespace creative_suite::hub
