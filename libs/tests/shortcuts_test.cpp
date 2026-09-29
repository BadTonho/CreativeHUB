#include <creative_suite/shortcuts/shortcut_manager.h>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>

namespace {

void require(bool condition, const char* message)
{
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

using creative_suite::shortcuts::ShortcutAssignment;
using creative_suite::shortcuts::ShortcutManager;

} // namespace

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QCoreApplication::setOrganizationName(QStringLiteral("CreativeSuiteShortcutTests"));
    QCoreApplication::setApplicationName(QStringLiteral("SharedManager"));
    QTemporaryDir settings_directory;
    require(settings_directory.isValid(), "temporary settings storage is available");
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                       settings_directory.path());

    QAction create_action;
    create_action.setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    QAction import_action;
    import_action.setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    ShortcutManager manager(QStringLiteral("MotionStudio/KeyboardShortcuts"));
    manager.registerAction(QStringLiteral("file.new"), QStringLiteral("New"), &create_action);
    manager.registerAction(QStringLiteral("media.import"), QStringLiteral("Import"), &import_action);
    require(manager.entries().size() == 2, "registered action catalog is retained");
    require(manager.load(), "defaults load when no preferences exist");
    require(create_action.shortcut() == QKeySequence(Qt::CTRL | Qt::Key_N),
            "registered shortcut is retained as the default");

    QString error;
    require(manager.setShortcut(QStringLiteral("media.import"), QKeySequence{}),
            "an empty shortcut can be assigned");
    require(import_action.shortcut().isEmpty(), "empty shortcut disables the action binding");
    QSettings persisted;
    persisted.beginGroup(QStringLiteral("MotionStudio/KeyboardShortcuts"));
    require(persisted.value(QStringLiteral("media.import")).toString() ==
                QStringLiteral("<disabled>"),
            "empty assignments have an explicit persisted representation");
    persisted.endGroup();

    require(!manager.setShortcut(QStringLiteral("media.import"), create_action.shortcut(), &error),
            "duplicate assignments are rejected");
    require(error.contains(QStringLiteral("New")), "conflicts identify the assigned command");

    const auto before_create = create_action.shortcut();
    const auto before_import = import_action.shortcut();
    const std::vector<ShortcutAssignment> conflicting_batch{
        {QStringLiteral("file.new"), QKeySequence(Qt::CTRL | Qt::Key_P)},
        {QStringLiteral("media.import"), QKeySequence(Qt::CTRL | Qt::Key_P)},
    };
    require(!manager.applyShortcuts(conflicting_batch, &error),
            "a conflicting batch is rejected");
    require(create_action.shortcut() == before_create && import_action.shortcut() == before_import,
            "a rejected batch does not partially change actions");
    QSettings after_conflict;
    after_conflict.beginGroup(QStringLiteral("MotionStudio/KeyboardShortcuts"));
    require(!after_conflict.contains(QStringLiteral("file.new")) &&
                after_conflict.value(QStringLiteral("media.import")).toString() ==
                    QStringLiteral("<disabled>"),
            "a rejected batch does not partially change persisted preferences");
    after_conflict.endGroup();
    require(!manager.applyShortcuts({
                {QStringLiteral("file.new"), QKeySequence(Qt::CTRL | Qt::Key_P)},
                {QStringLiteral("unknown.command"), QKeySequence(Qt::CTRL | Qt::Key_U)}}, &error),
            "unknown commands reject a batch");
    require(create_action.shortcut() == before_create,
            "unknown command rejection does not change earlier assignments");

    const std::vector<ShortcutAssignment> accepted_batch{
        {QStringLiteral("file.new"), QKeySequence(Qt::CTRL | Qt::Key_P)},
        {QStringLiteral("media.import"), QKeySequence{}},
    };
    require(manager.applyShortcuts(accepted_batch, &error), "a valid batch is persisted");
    require(create_action.shortcut() == QKeySequence(Qt::CTRL | Qt::Key_P) &&
                import_action.shortcut().isEmpty(),
            "a valid batch is applied together");

    QAction loaded_create;
    loaded_create.setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    QAction loaded_import;
    loaded_import.setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    ShortcutManager loaded(QStringLiteral("MotionStudio/KeyboardShortcuts"));
    loaded.registerAction(QStringLiteral("file.new"), QStringLiteral("New"), &loaded_create);
    loaded.registerAction(QStringLiteral("media.import"), QStringLiteral("Import"), &loaded_import);
    require(loaded.load(), "saved preferences load in another manager instance");
    require(loaded_create.shortcut() == QKeySequence(Qt::CTRL | Qt::Key_P) &&
                loaded_import.shortcut().isEmpty(),
            "custom and cleared shortcuts persist across instances");

    QSettings separate_preferences;
    separate_preferences.beginGroup(QStringLiteral("ImageEditor/KeyboardShortcuts"));
    separate_preferences.setValue(QStringLiteral("file.new"), QStringLiteral("Ctrl+Q"));
    separate_preferences.endGroup();
    separate_preferences.sync();
    QAction other_editor_action;
    other_editor_action.setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
    ShortcutManager other_editor(QStringLiteral("ImageEditor/KeyboardShortcuts"));
    other_editor.registerAction(
        QStringLiteral("file.new"), QStringLiteral("New"), &other_editor_action);
    require(other_editor.load() &&
                other_editor_action.shortcut() == QKeySequence(Qt::CTRL | Qt::Key_Q) &&
                loaded_create.shortcut() == QKeySequence(Qt::CTRL | Qt::Key_P),
            "separate application groups load independent assignments for the same command ID");

    require(loaded.resetShortcut(QStringLiteral("file.new")), "individual reset succeeds");
    require(loaded_create.shortcut() == QKeySequence(Qt::CTRL | Qt::Key_N),
            "individual reset restores its registered default");
    QSettings unrelated_preference;
    unrelated_preference.beginGroup(QStringLiteral("MotionStudio/KeyboardShortcuts"));
    unrelated_preference.setValue(QStringLiteral("unregistered.preference"), 42);
    unrelated_preference.endGroup();
    unrelated_preference.sync();
    loaded.resetAll();
    require(loaded_create.shortcut() == QKeySequence(Qt::CTRL | Qt::Key_N) &&
                loaded_import.shortcut() == QKeySequence(Qt::CTRL | Qt::Key_I),
            "Reset All restores all registered defaults");
    QSettings after_reset_all;
    after_reset_all.beginGroup(QStringLiteral("MotionStudio/KeyboardShortcuts"));
    require(after_reset_all.value(QStringLiteral("unregistered.preference")).toInt() == 42,
            "Reset All preserves unrelated values in the shortcut settings group");
    after_reset_all.endGroup();

    QSettings conflict_settings;
    conflict_settings.beginGroup(QStringLiteral("MotionStudio/KeyboardShortcuts"));
    conflict_settings.setValue(QStringLiteral("file.new"), QStringLiteral("Ctrl+P"));
    conflict_settings.setValue(QStringLiteral("media.import"), QStringLiteral("Ctrl+P"));
    conflict_settings.endGroup();
    conflict_settings.sync();
    require(loaded.load(), "conflicting persisted settings recover using defaults");
    require(loaded_create.shortcut() == QKeySequence(Qt::CTRL | Qt::Key_P) &&
                loaded_import.shortcut() == QKeySequence(Qt::CTRL | Qt::Key_I),
            "load repairs a conflict without leaving duplicate shortcuts active");
    QSettings recovered_unrelated_preference;
    recovered_unrelated_preference.beginGroup(
        QStringLiteral("MotionStudio/KeyboardShortcuts"));
    require(recovered_unrelated_preference
                .value(QStringLiteral("unregistered.preference")).toInt() == 42,
            "preference recovery preserves unrelated settings in the same group");
    recovered_unrelated_preference.endGroup();

    return EXIT_SUCCESS;
}
