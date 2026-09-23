#include "settings/shortcut_manager.h"
#include "ui/function_palette.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QKeySequence>
#include <QMainWindow>
#include <QSettings>
#include <QTest>
#include <QWidget>

#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void sendKey(
    QWidget* target,
    int key,
    Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QTest::keyClick(target, static_cast<Qt::Key>(key), modifiers);
    QApplication::processEvents();
}

void verifyFunctionPalette(QSettings& settings) {
    settings.clear();

    QMainWindow main_window;
    main_window.resize(640, 480);
    auto* editor = new QWidget;
    editor->setFocusPolicy(Qt::StrongFocus);
    main_window.setCentralWidget(editor);

    int playback_triggers = 0;
    QAction playback_action(&main_window);
    playback_action.setShortcut(QKeySequence(QStringLiteral("Space")));
    playback_action.setShortcutContext(Qt::WindowShortcut);
    QObject::connect(
        &playback_action,
        &QAction::triggered,
        &main_window,
        [&playback_triggers]() { ++playback_triggers; });
    main_window.addAction(&playback_action);

    settings::ShortcutManager shortcut_manager;
    ui::FunctionPalette palette(&main_window, shortcut_manager);
    shortcut_manager.registerAction(
        QStringLiteral("playback.play_pause"),
        QStringLiteral("Play or Pause"),
        &playback_action);
    shortcut_manager.load();

    require(palette.dialog()->objectName() ==
                QStringLiteral("functionsWindow"),
            "Functions window object name is incorrect.");
    require(palette.dialog()->windowTitle() == QStringLiteral("Functions"),
            "Functions window title is incorrect.");
    require(!palette.dialog()->isVisible(),
            "Functions window must start hidden.");
    require(!palette.dialog()->isModal() &&
                palette.dialog()->windowModality() == Qt::NonModal,
            "Functions window must be non-modal.");
    require(palette.dialog()->windowFlags().testFlag(Qt::Tool),
            "Functions window must use a floating tool-window flag.");
    require(palette.dialog()->size() == QSize(420, 320),
            "Functions window initial size is incorrect.");
    require(palette.dialog()->minimumSize() !=
                palette.dialog()->maximumSize(),
            "Functions window must remain resizable.");
    require(palette.dialog()->layout() == nullptr &&
                palette.dialog()->findChildren<QWidget*>(
                    QString(), Qt::FindDirectChildrenOnly).isEmpty(),
            "Functions window body must remain empty.");
    require(palette.toggleAction()->shortcut() ==
                QKeySequence(QStringLiteral("Shift+Space")),
            "Functions window shortcut default is incorrect.");
    require(palette.toggleAction()->shortcutContext() == Qt::WindowShortcut,
            "Functions window shortcut must use window context.");
    require(shortcut_manager.entries().size() == 2,
            "Functions window shortcut was not registered.");
    require(shortcut_manager.setShortcut(
                QStringLiteral("workspace.functions_window"),
                QKeySequence(QStringLiteral("Ctrl+Shift+F"))),
            "Functions window shortcut should be customizable.");
    require(shortcut_manager.resetShortcut(
                QStringLiteral("workspace.functions_window")),
            "Functions window shortcut should be individually resettable.");
    require(palette.toggleAction()->shortcut() ==
                QKeySequence(QStringLiteral("Shift+Space")),
            "Resetting the Functions shortcut must restore Shift+Space.");

    main_window.show();
    main_window.raise();
    main_window.activateWindow();
    require(QTest::qWaitForWindowActive(&main_window),
            "Test Main Window did not become active.");
    QApplication::processEvents();
    editor->setFocus();
    QApplication::processEvents();

    auto* const original_dialog = palette.dialog();
    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    require(palette.dialog()->isVisible(),
            "Shift+Space must open the Functions window from a child widget.");
    require(palette.dialog()->isActiveWindow(),
            "Opening Functions must activate its window.");
    require((palette.dialog()->frameGeometry().center() -
                main_window.frameGeometry().center()).manhattanLength() <= 8,
            "Functions window must open centered over the Main Editor.");
    require(playback_triggers == 0,
            "Shift+Space must not trigger the regular Space action.");

    sendKey(palette.dialog(), Qt::Key_Space, Qt::ShiftModifier);
    require(!palette.dialog()->isVisible(),
            "Shift+Space must hide the Functions window when it is focused.");

    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    require(palette.dialog()->isVisible(),
            "Shift+Space must reopen the same Functions window.");
    sendKey(palette.dialog(), Qt::Key_Escape);
    require(!palette.dialog()->isVisible(),
            "Escape must hide the Functions window.");

    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    require(palette.dialog() == original_dialog &&
                palette.dialog()->isVisible(),
            "Reopening Functions must reuse the existing window.");
    palette.dialog()->close();
    QApplication::processEvents();
    require(!palette.dialog()->isVisible(),
            "Closing the Functions window must hide it.");
    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    require(palette.dialog() == original_dialog &&
                palette.dialog()->isVisible(),
            "Closing and reopening must not create duplicate windows.");

    palette.dialog()->hide();
    main_window.activateWindow();
    main_window.setFocus();
    QApplication::processEvents();
    sendKey(&main_window, Qt::Key_Space);
    require(playback_triggers == 1,
            "Space must continue to trigger the regular playback action.");

    settings.clear();
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

        verifyFunctionPalette(settings);
        settings.clear();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        settings.clear();
        return 1;
    }
}
