#include "ui/main_window.h"

#include <QApplication>
#include <QLabel>

#include <cstdlib>
#include <iostream>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    motion::ui::MainWindow window;
    window.show();
    application.processEvents();

    const auto* empty_state = window.findChild<QLabel*>(QStringLiteral("motion-empty-state"));
    if (!window.isVisible()
        || window.windowTitle() != QStringLiteral("Motion Studio")
        || empty_state == nullptr
        || empty_state->text() != QStringLiteral("No composition open")) {
        std::cerr << "Motion Studio did not start in its empty state.\n";
        return EXIT_FAILURE;
    }

    window.close();
    application.processEvents();
    if (window.isVisible()) {
        std::cerr << "Motion Studio main window did not close.\n";
        return EXIT_FAILURE;
    }

    std::cout << "Motion Studio offscreen startup test passed.\n";
    return EXIT_SUCCESS;
}
