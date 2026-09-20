#include "main_window.h"

#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QApplication::setApplicationName("Main Editor");
    QApplication::setApplicationVersion("0.1.0");

    MainWindow window;
    window.show();

    return application.exec();
}
