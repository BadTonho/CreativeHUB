#pragma once

#include "../model/app_catalog.h"
#include "../application/app_launcher.h"
#include "header/header_bar.h"
#include "sidebar/sidebar_widget.h"
#include "../model/recent_projects_manager.h"
#include "pages/apps_page.h"
#include "pages/projects_page.h"
#include "pages/updates_page.h"
#include "pages/settings_page.h"
#include "dialogs/app_details_modal.h"

#include <QMainWindow>
#include <QHash>
#include <QStackedWidget>

namespace creative_suite::updater {
class UpdateRuntime;
class UpdateService;
}

namespace creative_suite::hub {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

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

private:
    void setupUi();
    void scanInstalledApps();
    void configureUpdateServices();

    AppCatalog m_catalog;
    AppLauncher m_launcher;
    RecentProjectsManager m_recentProjectsManager;

    HeaderBar* m_headerBar{nullptr};
    SidebarWidget* m_sidebarWidget{nullptr};
    QStackedWidget* m_pagesStack{nullptr};

    AppsPage* m_appsPage{nullptr};
    ProjectsPage* m_projectsPage{nullptr};
    UpdatesPage* m_updatesPage{nullptr};
    SettingsPage* m_settingsPage{nullptr};
    AppDetailsModal* m_detailsModal{nullptr};

    creative_suite::updater::UpdateRuntime* m_updateRuntime{nullptr};
    QHash<QString, creative_suite::updater::UpdateService*> m_updateServices;
};

} // namespace creative_suite::hub
