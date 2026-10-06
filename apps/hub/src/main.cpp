#include "ui/main_window.h"
#include "diagnostics/hub_logger.h"

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QCoreApplication::setOrganizationName(QStringLiteral("CreativeSuite"));
    QCoreApplication::setApplicationName(QStringLiteral("CreativeSuiteHub"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    creative_suite::hub::HubLogger::instance().logInfo(
        QStringLiteral("Application"),
        QStringLiteral("main"),
        QStringLiteral("Iniciando Creative Suite Hub v0.1.0")
    );

    creative_suite::hub::MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
