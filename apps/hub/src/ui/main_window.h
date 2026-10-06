#pragma once

#include "../model/app_catalog.h"
#include "../application/app_launcher.h"
#include "header/header_bar.h"
#include "sidebar/sidebar_widget.h"
#include "pages/apps_page.h"
#include "pages/updates_page.h"
#include "pages/settings_page.h"

#include <QMainWindow>
#include <QStackedWidget>
#include <QTimer>

namespace creative_suite::hub {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private slots:
    void onShowAppDetails(const QString& appId);
    void onOpenApp(const QString& appId);
    void onDownloadApp(const QString& appId);
    void onCancelDownload(const QString& appId);
    void onRefreshApps();
    void simulateDownloadStep();

private:
    void setupUi();
    void scanInstalledApps();

    AppCatalog m_catalog;
    AppLauncher m_launcher;

    HeaderBar* m_headerBar{nullptr};
    SidebarWidget* m_sidebarWidget{nullptr};
    QStackedWidget* m_pagesStack{nullptr};

    AppsPage* m_appsPage{nullptr};
    UpdatesPage* m_updatesPage{nullptr};
    SettingsPage* m_settingsPage{nullptr};

    // Download simulation timer for visual feedback
    QTimer* m_downloadTimer{nullptr};
    QString m_activeDownloadingAppId;
    double m_activeDownloadProgress{0.0};
};

} // namespace creative_suite::hub
