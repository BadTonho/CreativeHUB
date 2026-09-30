#include "ui/main_window.h"

#include <creative_suite/diagnostics/logger.h>

#include <QApplication>
#include <QIcon>

int main(int argc, char* argv[])
{
    static_cast<void>(creative_suite::diagnostics::Logger::instance()
                         .initialize_default("motion-studio"));
    QApplication application(argc, argv);
    application.setWindowIcon(QIcon(QStringLiteral(":/app-icon/icon.png")));
    QCoreApplication::setOrganizationName(QStringLiteral("Creative Suite"));
    QCoreApplication::setApplicationName(QStringLiteral("Motion Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral("Beta 0.1.0"));
    motion::ui::MainWindow window;
    window.show();
    return application.exec();
}
