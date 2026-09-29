#include "ui/main_window.h"

#include <creative_suite/diagnostics/logger.h>

#include <QApplication>

int main(int argc, char* argv[])
{
    static_cast<void>(creative_suite::diagnostics::Logger::instance()
                         .initialize_default("motion-studio"));
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("Creative Suite"));
    QCoreApplication::setApplicationName(QStringLiteral("Motion Studio"));
    motion::ui::MainWindow window;
    window.show();
    return application.exec();
}
