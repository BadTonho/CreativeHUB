#include "ui/main_window.h"

#include <QApplication>
#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QTemporaryDir recovery_directory;
    if (!recovery_directory.isValid()) return EXIT_FAILURE;
    const auto encoded_recovery = recovery_directory.path().toUtf8();
    const auto* recovery_data = reinterpret_cast<const char8_t*>(encoded_recovery.constData());
    motion::ui::MainWindow window(
        nullptr,
        std::filesystem::path(std::u8string(
            recovery_data, recovery_data + encoded_recovery.size())),
        "startup-test-session");
    window.show();
    application.processEvents();

    const auto* empty_state = window.findChild<QLabel*>(QStringLiteral("motion-empty-state"));
    const auto* new_composition_button = window.findChild<QPushButton*>(
        QStringLiteral("motion-empty-new-composition-button"));
    if (!window.isVisible()
        || !window.isMaximized()
        || window.windowTitle() != QStringLiteral("Motion Studio")
        || empty_state == nullptr
        || empty_state->text() != QStringLiteral("No composition open")
        || new_composition_button == nullptr
        || new_composition_button->text() != QStringLiteral("New Composition...")
        || !new_composition_button->isVisible()
        || window.findChild<QWidget*>(QStringLiteral("motion-media-pool")) != nullptr
        || !window.findChildren<QDockWidget*>().empty()
        || window.findChild<QAction*>(
               QStringLiteral("motion-reset-panel-layout-action")) == nullptr
        || window.findChild<QAction*>(
               QStringLiteral("motion-reset-panel-layout-action"))->isEnabled()) {
        std::cerr << "Motion Studio did not start maximized in its empty state.\n";
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
