#include "update_center.h"
#include "update_dialog.h"

#include <QAction>
#include <QCoreApplication>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>

namespace creative_suite::updater {

UpdateCenter::UpdateCenter(QMainWindow* window, UpdateServiceConfig config, QObject* parent)
    : QObject(parent ? parent : window), m_window(window), m_config(std::move(config)) {
    if (!m_window) return;
    m_runtime = new UpdateRuntime(m_config.app_id, this);
    m_service = new UpdateService(m_config, this);
    m_status_button = new QToolButton(m_window);
    m_status_button->setText(QStringLiteral("Update available"));
    m_status_button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_status_button->setAutoRaise(true);
    m_status_button->setVisible(false);
    m_status_button->setObjectName(QStringLiteral("updateCenterStatusButton"));
    m_window->statusBar()->addPermanentWidget(m_status_button);
    connect(m_status_button, &QToolButton::clicked, this, &UpdateCenter::openUpdateDialog);

    auto* menu_bar = m_window->menuBar();
    QMenu* help_menu = nullptr;
    for (auto* action : menu_bar->actions()) {
        if (action->menu() && action->menu()->title().contains(QStringLiteral("Help"), Qt::CaseInsensitive)) {
            help_menu = action->menu(); break;
        }
    }
    if (!help_menu) help_menu = menu_bar->addMenu(QStringLiteral("&Help"));
    auto* check_action = help_menu->addAction(QStringLiteral("Check for Updates..."));
    check_action->setObjectName(QStringLiteral("checkForUpdatesAction"));
    connect(check_action, &QAction::triggered, this, [this] {
        if (m_service && m_service->state() == UpdateState::Idle) m_service->checkForUpdates();
        openUpdateDialog();
    });
    connect(m_service, &UpdateService::updateAvailable, this, [this](const ReleaseEntry& entry) {
        if (!m_status_button) return;
        m_status_button->setText(QStringLiteral("Update available · %1").arg(entry.version));
        m_status_button->setVisible(true);
    });
    connect(m_service, &UpdateService::downloadReady, this,
            [this](const QString& installer, const ReleaseEntry& entry) {
        if (!m_runtime) return;
        const bool accepted = m_runtime->installOrWait(entry.app_id, m_config.install_path,
            m_config.executable_name, installer, entry.sha256_hex, m_config.installed);
        if (accepted && m_status_button) {
            m_status_button->setText(QStringLiteral("Update ready · %1").arg(entry.version));
            m_status_button->setVisible(true);
        }
    });
    if (m_config.installed) {
        QTimer::singleShot(0, m_service, &UpdateService::checkForUpdates);
        m_runtime->resumePendingInstalls({m_config.app_id});
    }
}

void UpdateCenter::openUpdateDialog() {
    if (!m_window || !m_service || !m_runtime) return;
    UpdateDialog dialog(m_service, m_runtime, m_window);
    dialog.exec();
}

} // namespace creative_suite::updater
