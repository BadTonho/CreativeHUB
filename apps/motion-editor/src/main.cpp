#include "ui/main_window/main_window.h"
#ifdef Q_OS_WIN
#include <creative_suite/updater/update_service.h>
#endif

#include <creative_suite/diagnostics/logger.h>

#include <QApplication>
#include <QIcon>

#ifndef CREATIVE_SUITE_APP_VERSION
#define CREATIVE_SUITE_APP_VERSION "0.1.0"
#endif

int main(int argc, char* argv[])
{
    static_cast<void>(creative_suite::diagnostics::Logger::instance()
                         .initialize_default("motion-studio"));
    QApplication application(argc, argv);
    application.setWindowIcon(QIcon(QStringLiteral(":/app-icon/icon.png")));
    QCoreApplication::setOrganizationName(QStringLiteral("Creative Suite"));
    QCoreApplication::setApplicationName(QStringLiteral("Motion Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral(CREATIVE_SUITE_APP_VERSION));
    motion::ui::MainWindow window;
#ifdef Q_OS_WIN
    creative_suite::updater::UpdateCenter updater(
        &window,
        creative_suite::updater::defaultConfig(
            QStringLiteral("motion-editor"), QStringLiteral("Motion Studio"),
            QStringLiteral("creative-suite-motion-editor.exe"),
            QStringLiteral(CREATIVE_SUITE_APP_VERSION)));
#endif
    window.show();
#ifdef Q_OS_WIN
    creative_suite::updater::markApplicationStartupHealthy(QStringLiteral("motion-editor"));
#endif
    return application.exec();
}
