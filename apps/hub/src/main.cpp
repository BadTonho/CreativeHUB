#include "ui/main_window.h"
#include "diagnostics/hub_logger.h"
#ifdef Q_OS_WIN
#include <creative_suite/updater/update_service.h>
#endif

#include <QApplication>
#include <QIcon>

#ifndef CREATIVE_SUITE_APP_VERSION
#define CREATIVE_SUITE_APP_VERSION "0.1.0"
#endif

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("CreativeSuite"));
    QCoreApplication::setApplicationName(QStringLiteral("CreativeSuiteHub"));
    QCoreApplication::setApplicationVersion(QStringLiteral(CREATIVE_SUITE_APP_VERSION));
    app.setWindowIcon(QIcon(QStringLiteral(":/app-icon/icon.png")));

    creative_suite::hub::HubLogger::instance().logInfo(
        QStringLiteral("Application"),
        QStringLiteral("main"),
        QStringLiteral("Iniciando Creative Suite Hub v%1").arg(QStringLiteral(CREATIVE_SUITE_APP_VERSION))
    );

    creative_suite::hub::MainWindow mainWindow;
#ifdef Q_OS_WIN
    creative_suite::updater::markApplicationStartupHealthy(QStringLiteral("hub"));
#endif
#ifdef Q_OS_WIN
    creative_suite::updater::UpdateCenter updater(
        &mainWindow,
        creative_suite::updater::defaultConfig(
            QStringLiteral("hub"), QStringLiteral("Creative Suite Hub"),
            QStringLiteral("creative-suite-hub.exe"),
            QStringLiteral(CREATIVE_SUITE_APP_VERSION)));
#endif
    mainWindow.show();

    return app.exec();
}
