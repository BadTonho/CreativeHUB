#include "settings/shortcut_manager.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QKeySequence>
#include <QSettings>

#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    QCoreApplication::setOrganizationName("CreativeSuiteTests");
    QCoreApplication::setApplicationName("ShortcutManagerTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);

    QSettings settings;
    settings.clear();

    try {
        QAction save_action;
        save_action.setShortcut(QKeySequence("Ctrl+S"));
        QAction open_action;
        open_action.setShortcut(QKeySequence("Ctrl+O"));

        settings::ShortcutManager manager;
        manager.registerAction(
            QStringLiteral("file.save"), QStringLiteral("Save"),
            &save_action);
        manager.registerAction(
            QStringLiteral("file.open"), QStringLiteral("Open"),
            &open_action);
        manager.load();

        require(manager.entries().size() == 2,
                "Shortcut actions were not registered.");
        require(manager.setShortcut(
                    QStringLiteral("file.open"), QKeySequence("Ctrl+P")),
                "A unique shortcut should be accepted.");
        require(open_action.shortcut() == QKeySequence("Ctrl+P"),
                "Accepted shortcut was not applied.");

        QString conflict_message;
        require(!manager.setShortcut(
                    QStringLiteral("file.open"), QKeySequence("Ctrl+S"),
                    &conflict_message),
                "Duplicate shortcut should be rejected.");
        require(conflict_message.contains("Save"),
                "Shortcut conflict did not identify the existing command.");
        require(open_action.shortcut() == QKeySequence("Ctrl+P"),
                "Rejected shortcut changed the existing assignment.");

        require(manager.setShortcut(
                    QStringLiteral("file.save"), QKeySequence()),
                "An empty shortcut should be accepted.");
        require(save_action.shortcut().isEmpty(),
                "Empty shortcut was not applied.");

        QAction loaded_save_action;
        loaded_save_action.setShortcut(QKeySequence("Ctrl+S"));
        QAction loaded_open_action;
        loaded_open_action.setShortcut(QKeySequence("Ctrl+O"));
        settings::ShortcutManager loaded_manager;
        loaded_manager.registerAction(
            QStringLiteral("file.save"), QStringLiteral("Save"),
            &loaded_save_action);
        loaded_manager.registerAction(
            QStringLiteral("file.open"), QStringLiteral("Open"),
            &loaded_open_action);
        loaded_manager.load();
        require(loaded_save_action.shortcut().isEmpty(),
                "Empty shortcut was not persisted.");
        require(loaded_open_action.shortcut() == QKeySequence("Ctrl+P"),
                "Shortcut was not persisted in portable settings.");

        require(loaded_manager.resetShortcut(QStringLiteral("file.open")),
                "Individual shortcut reset failed.");
        require(loaded_open_action.shortcut() == QKeySequence("Ctrl+O"),
                "Individual reset did not restore the default.");

        loaded_manager.resetAll();
        require(loaded_save_action.shortcut() == QKeySequence("Ctrl+S"),
                "Reset All did not restore the Save default.");
        require(loaded_open_action.shortcut() == QKeySequence("Ctrl+O"),
                "Reset All did not restore the Open default.");

        settings.clear();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        settings.clear();
        return 1;
    }
}
