#include "settings/shortcut_manager.h"
#include "ui/functions/function_palette.h"
#include "ui/media_browser/media_drag_mime.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QDialog>
#include <QEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QMimeData>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QStringList>
#include <QTest>
#include <QTemporaryDir>
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

void processDeferredDeletes() {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
}

void verifyFunctionPalette(QSettings& settings) {
    settings.clear();

    QMainWindow main_window;
    main_window.resize(640, 480);
    auto* editor = new QWidget;
    editor->setFocusPolicy(Qt::StrongFocus);
    auto* outside_button = new QPushButton(QStringLiteral("Outside"), editor);
    outside_button->setGeometry(10, 10, 80, 30);
    int outside_button_clicks = 0;
    QObject::connect(
        outside_button,
        &QPushButton::clicked,
        &main_window,
        [&outside_button_clicks]() { ++outside_button_clicks; });
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
        &playback_action, settings::ShortcutScope::Application,
        QStringLiteral("All workspaces"));
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
    require(palette.dialog()->size() == QSize(420, 360),
            "Functions window initial size is incorrect.");
    require(palette.dialog()->minimumSize() !=
                palette.dialog()->maximumSize(),
            "Functions window must remain resizable.");
    auto* functions_search = palette.dialog()->findChild<QLineEdit*>(
        QStringLiteral("functionsEffectSearch"));
    auto* functions_list = palette.dialog()->findChild<QListWidget*>(
        QStringLiteral("functionsEffectList"));
    auto* functions_add = palette.dialog()->findChild<QPushButton*>(
        QStringLiteral("functionsAddButton"));
    require(palette.dialog()->layout() != nullptr && functions_search != nullptr &&
                functions_list != nullptr && functions_list->count() == 4 &&
                functions_add != nullptr,
            "Functions must provide effect search, list, and Add controls.");
    require(functions_list->dragEnabled() &&
                functions_list->dragDropMode() == QAbstractItemView::DragOnly,
            "Functions effects must support copy-only dragging.");
    const QStringList fusion_effect_ids{
        QStringLiteral("video.grayscale"),
        QStringLiteral("video.brightness"),
        QStringLiteral("video.contrast"),
        QStringLiteral("video.saturation")};
    for (int index = 0; index < fusion_effect_ids.size(); ++index) {
        auto* item = functions_list->item(index);
        require(item->data(Qt::UserRole).toString() == fusion_effect_ids[index] &&
                    item->flags().testFlag(Qt::ItemIsDragEnabled),
                "Functions must expose the four draggable Fusion visual effects.");
        auto* mime = ui::createEffectIdMimeData(fusion_effect_ids[index]);
        require(mime->hasFormat(ui::kEffectIdMimeType) &&
                    mime->data(ui::kEffectIdMimeType) ==
                        fusion_effect_ids[index].toUtf8(),
                "Functions effect drag must use the shared Effects MIME payload.");
        delete mime;
    }
    require(!functions_add->isEnabled(),
            "Add must be disabled without a compatible selected clip.");
    functions_search->setText(QStringLiteral("contrast"));
    require(functions_list->count() == 4 &&
                functions_list->item(0)->isHidden() &&
                functions_list->item(1)->isHidden() &&
                !functions_list->item(2)->isHidden() &&
                functions_list->item(3)->isHidden(),
            "Functions search did not filter the visual filter list.");
    require(!functions_add->isEnabled(),
            "Filtering must remain available while Add is disabled.");
    functions_search->clear();
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

    QString added_effect_id;
    int added_effect_requests = 0;
    QObject::connect(
        &palette,
        &ui::FunctionPalette::effectAddRequested,
        [&added_effect_id, &added_effect_requests](const QString& effect_id) {
            added_effect_id = effect_id;
            ++added_effect_requests;
        });

    main_window.show();
    main_window.raise();
    main_window.activateWindow();
    require(QTest::qWaitForWindowActive(&main_window),
            "Test Main Window did not become active.");
    QApplication::processEvents();
    editor->setFocus();
    QApplication::processEvents();

    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    require(palette.dialog()->isVisible(),
            "Shift+Space must open the Functions window from a child widget.");
    require(palette.dialog()->isActiveWindow(),
            "Opening Functions must activate its window.");
    require((palette.dialog()->frameGeometry().center() -
                main_window.frameGeometry().center()).manhattanLength() <= 8,
            "Functions window must open centered over the Video Editor.");
    require(playback_triggers == 0,
            "Shift+Space must not trigger the regular Space action.");
    QPointer<QDialog> first_dialog = palette.dialog();

    QTest::mouseClick(
        palette.dialog(),
        Qt::LeftButton,
        Qt::NoModifier,
        palette.dialog()->rect().center());
    QApplication::processEvents();
    require(palette.dialog()->isVisible(),
            "Clicking inside Functions must keep the window open.");

    const QPoint outside_click = outside_button->mapToGlobal(
        outside_button->rect().center());
    require(!palette.dialog()->frameGeometry().contains(outside_click),
            "Test click must land outside the Functions window.");
    QTest::mouseClick(
        outside_button,
        Qt::LeftButton,
        Qt::NoModifier,
        outside_button->rect().center());
    processDeferredDeletes();
    require(first_dialog.isNull() && palette.dialog() == nullptr,
            "Clicking outside Functions must close and destroy the window.");
    require(outside_button_clicks == 1,
            "The outside click must still reach the clicked Video Editor control.");

    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    require(palette.dialog() != nullptr && palette.dialog()->isVisible(),
            "Shift+Space must create a new Functions window after closing.");
    QPointer<QDialog> second_dialog = palette.dialog();

    sendKey(palette.dialog(), Qt::Key_Space, Qt::ShiftModifier);
    processDeferredDeletes();
    require(second_dialog.isNull() && palette.dialog() == nullptr,
            "Shift+Space must close the Functions window when it is focused.");

    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    QPointer<QDialog> third_dialog = palette.dialog();
    require(third_dialog != nullptr && third_dialog->isVisible(),
            "Shift+Space must create a Functions window after toggling closed.");
    sendKey(third_dialog, Qt::Key_Escape);
    processDeferredDeletes();
    require(third_dialog.isNull() && palette.dialog() == nullptr,
            "Escape must close and destroy the Functions window.");

    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    QPointer<QDialog> fourth_dialog = palette.dialog();
    require(fourth_dialog != nullptr && fourth_dialog->isVisible(),
            "Shift+Space must open Functions after Escape closes it.");
    palette.dialog()->close();
    processDeferredDeletes();
    require(fourth_dialog.isNull() && palette.dialog() == nullptr,
            "The title-bar close action must destroy the Functions window.");

    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    QPointer<QDialog> fifth_dialog = palette.dialog();
    require(fifth_dialog != nullptr && fifth_dialog->isVisible(),
            "Functions must reopen after the title-bar close action.");
    require(main_window.findChildren<QDialog*>(
                QStringLiteral("functionsWindow")).size() == 1,
            "Only one Functions window may exist after reopening.");

    sendKey(fifth_dialog, Qt::Key_Space, Qt::ShiftModifier);
    processDeferredDeletes();
    require(fifth_dialog.isNull() && palette.dialog() == nullptr,
            "Shift+Space must close the final Functions window.");

    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    QPointer<QDialog> deactivated_dialog = palette.dialog();
    QEvent deactivation(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(deactivated_dialog, &deactivation);
    QTest::qWait(1);
    processDeferredDeletes();
    require(deactivated_dialog.isNull() && palette.dialog() == nullptr,
            "Losing window activation must close and destroy Functions.");

    main_window.activateWindow();
    editor->setFocus();
    QApplication::processEvents();
    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    QPointer<QDialog> drag_dialog = palette.dialog();
    require(drag_dialog != nullptr && drag_dialog->isVisible(),
            "Functions did not reopen for drag-dismissal checks.");
    require(QMetaObject::invokeMethod(
                &palette, "onEffectDragStateChanged", Qt::DirectConnection,
                Q_ARG(bool, true), Q_ARG(bool, false)),
            "The Functions effect drag did not enter its protected state.");
    QEvent drag_deactivation(QEvent::WindowDeactivate);
    QCoreApplication::sendEvent(drag_dialog, &drag_deactivation);
    QTest::qWait(1);
    require(drag_dialog->isVisible(),
            "Window deactivation during a drag must not close Functions.");
    require(QMetaObject::invokeMethod(
                &palette, "onEffectDragStateChanged", Qt::DirectConnection,
                Q_ARG(bool, false), Q_ARG(bool, false)),
            "The canceled Functions drag did not finish.");
    require(drag_dialog->isVisible(),
            "A canceled or rejected drag must leave Functions open.");
    require(QMetaObject::invokeMethod(
                &palette, "onEffectDragStateChanged", Qt::DirectConnection,
                Q_ARG(bool, true), Q_ARG(bool, false)) &&
                QMetaObject::invokeMethod(
                    &palette, "onEffectDragStateChanged", Qt::DirectConnection,
                    Q_ARG(bool, false), Q_ARG(bool, true)),
            "The accepted Functions drag did not finish.");
    processDeferredDeletes();
    require(drag_dialog.isNull() && palette.dialog() == nullptr,
            "A successful drop must close and destroy Functions.");

    palette.setEffectTargetAvailable(true);
    main_window.activateWindow();
    editor->setFocus();
    QApplication::processEvents();
    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    require(palette.dialog() != nullptr && palette.dialog()->isVisible(),
            "Functions did not reopen with an available effect target.");
    auto* active_search = palette.dialog()->findChild<QLineEdit*>(
        QStringLiteral("functionsEffectSearch"));
    auto* active_add = palette.dialog()->findChild<QPushButton*>(
        QStringLiteral("functionsAddButton"));
    auto* active_cancel = palette.dialog()->findChild<QPushButton*>(
        QStringLiteral("functionsCancelButton"));
    require(active_search != nullptr && active_add != nullptr && active_add->isEnabled(),
            "Add must be enabled for a compatible selected clip.");
    require(active_cancel != nullptr,
            "Functions must provide a Cancel control.");
    QTest::mouseClick(active_cancel, Qt::LeftButton);
    processDeferredDeletes();
    require(palette.dialog() == nullptr && added_effect_id.isEmpty(),
            "Cancel must close Functions without applying a filter.");

    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    active_search = palette.dialog()->findChild<QLineEdit*>(
        QStringLiteral("functionsEffectSearch"));
    active_add = palette.dialog()->findChild<QPushButton*>(
        QStringLiteral("functionsAddButton"));
    require(active_search != nullptr && active_add != nullptr && active_add->isEnabled(),
            "Functions did not restore its controls after Cancel.");
    active_search->setText(QStringLiteral("Brightness"));
    QTest::mouseClick(active_add, Qt::LeftButton);
    processDeferredDeletes();
    require(added_effect_id == QStringLiteral("video.brightness") &&
                added_effect_requests == 1 &&
                palette.dialog() == nullptr,
            "Add must apply the selected filter and close Functions.");

    added_effect_id.clear();
    sendKey(editor, Qt::Key_Space, Qt::ShiftModifier);
    active_search = palette.dialog()->findChild<QLineEdit*>(
        QStringLiteral("functionsEffectSearch"));
    require(active_search != nullptr,
            "Functions did not restore search after Add.");
    active_search->setText(QStringLiteral("Saturation"));
    active_search->setFocus();
    sendKey(active_search, Qt::Key_Return);
    processDeferredDeletes();
    require(added_effect_id == QStringLiteral("video.saturation") &&
                added_effect_requests == 2 &&
                palette.dialog() == nullptr,
            "Enter must add the searched effect to the selected clip and close Functions.");
    palette.setEffectTargetAvailable(false);

    main_window.activateWindow();
    editor->setFocus();
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
    QTemporaryDir settings_directory;
    if (!settings_directory.isValid()) return 1;
    QSettings::setPath(
        QSettings::IniFormat, QSettings::UserScope, settings_directory.path());

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
            &save_action, settings::ShortcutScope::Application,
            QStringLiteral("All workspaces"));
        manager.registerAction(
            QStringLiteral("file.open"), QStringLiteral("Open"),
            &open_action, settings::ShortcutScope::Application,
            QStringLiteral("All workspaces"));
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
            &loaded_save_action, settings::ShortcutScope::Application,
            QStringLiteral("All workspaces"));
        loaded_manager.registerAction(
            QStringLiteral("file.open"), QStringLiteral("Open"),
            &loaded_open_action, settings::ShortcutScope::Application,
            QStringLiteral("All workspaces"));
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

        QAction app_action;
        app_action.setShortcut(QKeySequence("Ctrl+N"));
        QAction shared_action;
        shared_action.setShortcut(QKeySequence("Ctrl+Shift+P"));
        QAction edit_action;
        edit_action.setShortcut(QKeySequence("Ctrl+K"));
        QAction fusion_action;
        fusion_action.setShortcut(QKeySequence("Ctrl+F"));
        QAction render_action;
        render_action.setShortcut(QKeySequence("Ctrl+R"));
        settings::ShortcutManager scoped_manager;
        scoped_manager.registerAction(
            QStringLiteral("scoped.application"), QStringLiteral("Application"),
            &app_action, settings::ShortcutScope::Application,
            QStringLiteral("All workspaces"));
        scoped_manager.registerAction(
            QStringLiteral("scoped.shared"), QStringLiteral("Shared"),
            &shared_action, settings::ShortcutScope::Shared,
            QStringLiteral("Edit and Fusion"));
        scoped_manager.registerAction(
            QStringLiteral("scoped.edit"), QStringLiteral("Edit command"),
            &edit_action, settings::ShortcutScope::Edit,
            QStringLiteral("Edit"));
        scoped_manager.registerAction(
            QStringLiteral("scoped.fusion"), QStringLiteral("Fusion command"),
            &fusion_action, settings::ShortcutScope::Fusion,
            QStringLiteral("Fusion"));
        scoped_manager.registerAction(
            QStringLiteral("scoped.render"), QStringLiteral("Render command"),
            &render_action, settings::ShortcutScope::Render,
            QStringLiteral("Render"));
        QString scoped_load_error;
        const bool scoped_load_succeeded =
            scoped_manager.load(&scoped_load_error);
        if (!scoped_load_succeeded) {
            std::fprintf(stderr, "%s\n", scoped_load_error.toUtf8().constData());
        }
        require(scoped_load_succeeded,
                "Scoped shortcut preferences could not be loaded.");

        require(scoped_manager.setShortcut(
                    QStringLiteral("scoped.edit"), QKeySequence("Ctrl+P")) &&
                    scoped_manager.setShortcut(
                        QStringLiteral("scoped.fusion"), QKeySequence("Ctrl+P")) &&
                    scoped_manager.setShortcut(
                        QStringLiteral("scoped.render"), QKeySequence("Ctrl+P")),
                "Edit, Fusion, and Render must be able to reuse a key sequence.");
        require(!scoped_manager.setShortcut(
                    QStringLiteral("scoped.shared"), QKeySequence("Ctrl+P"),
                    &conflict_message) && conflict_message.contains("Edit command"),
                "Shared must reject a sequence that overlaps Edit or Fusion.");
        require(scoped_manager.setShortcut(
                    QStringLiteral("scoped.edit"), QKeySequence("Ctrl+M")) &&
                    !scoped_manager.setShortcut(
                        QStringLiteral("scoped.shared"), QKeySequence("Ctrl+P"),
                        &conflict_message) &&
                    conflict_message.contains("Fusion command") &&
                    scoped_manager.setShortcut(
                        QStringLiteral("scoped.edit"), QKeySequence("Ctrl+P")),
                "Shared must also reject a sequence that overlaps Fusion.");
        require(scoped_manager.setShortcut(
                    QStringLiteral("scoped.shared"), QKeySequence("Ctrl+R")),
                "Shared must be allowed to reuse a Render-only sequence.");
        require(!scoped_manager.setShortcut(
                    QStringLiteral("scoped.application"), QKeySequence("Ctrl+R"),
                    &conflict_message) && conflict_message.contains("Shared") &&
                    !scoped_manager.setShortcut(
                        QStringLiteral("scoped.application"), QKeySequence("Ctrl+P"),
                        &conflict_message) && conflict_message.contains("Edit command"),
                "Application must conflict with Shared and Edit sequences.");
        require(scoped_manager.setShortcut(
                    QStringLiteral("scoped.edit"), QKeySequence("Ctrl+M")) &&
                    !scoped_manager.setShortcut(
                        QStringLiteral("scoped.application"), QKeySequence("Ctrl+P"),
                        &conflict_message) && conflict_message.contains("Fusion command") &&
                    scoped_manager.setShortcut(
                        QStringLiteral("scoped.fusion"), QKeySequence("Ctrl+G")) &&
                    !scoped_manager.setShortcut(
                        QStringLiteral("scoped.application"), QKeySequence("Ctrl+P"),
                        &conflict_message) && conflict_message.contains("Render command") &&
                    scoped_manager.setShortcut(
                        QStringLiteral("scoped.fusion"), QKeySequence("Ctrl+P")) &&
                    scoped_manager.setShortcut(
                        QStringLiteral("scoped.edit"), QKeySequence("Ctrl+P")),
                "Application must conflict with Fusion and Render sequences too.");

        scoped_manager.setWorkspace(ui::WorkspacePageId::Edit);
        require(app_action.shortcut() == QKeySequence("Ctrl+N") &&
                    shared_action.shortcut() == QKeySequence("Ctrl+R") &&
                    edit_action.shortcut() == QKeySequence("Ctrl+P") &&
                    fusion_action.shortcut().isEmpty() &&
                    render_action.shortcut().isEmpty(),
                "Edit must activate only Application, Shared, and Edit bindings.");
        scoped_manager.setWorkspace(ui::WorkspacePageId::Fusion);
        require(app_action.shortcut() == QKeySequence("Ctrl+N") &&
                    shared_action.shortcut() == QKeySequence("Ctrl+R") &&
                    edit_action.shortcut().isEmpty() &&
                    fusion_action.shortcut() == QKeySequence("Ctrl+P") &&
                    render_action.shortcut().isEmpty(),
                "Fusion must activate only Application, Shared, and Fusion bindings.");
        scoped_manager.setWorkspace(ui::WorkspacePageId::Render);
        require(app_action.shortcut() == QKeySequence("Ctrl+N") &&
                    shared_action.shortcut().isEmpty() &&
                    edit_action.shortcut().isEmpty() &&
                    fusion_action.shortcut().isEmpty() &&
                    render_action.shortcut() == QKeySequence("Ctrl+P"),
                "Render must activate only Application and Render bindings.");
        require(scoped_manager.shortcut(QStringLiteral("scoped.shared")) ==
                    QKeySequence("Ctrl+R"),
                "Inactive shortcut configuration must remain queryable.");

        QAction loaded_app_action;
        loaded_app_action.setShortcut(QKeySequence("Ctrl+N"));
        QAction loaded_shared_action;
        loaded_shared_action.setShortcut(QKeySequence("Ctrl+Shift+P"));
        QAction loaded_edit_action;
        loaded_edit_action.setShortcut(QKeySequence("Ctrl+K"));
        QAction loaded_fusion_action;
        loaded_fusion_action.setShortcut(QKeySequence("Ctrl+F"));
        QAction loaded_render_action;
        loaded_render_action.setShortcut(QKeySequence("Ctrl+R"));
        settings::ShortcutManager reopened_scoped_manager;
        reopened_scoped_manager.registerAction(
            QStringLiteral("scoped.application"), QStringLiteral("Application"),
            &loaded_app_action, settings::ShortcutScope::Application,
            QStringLiteral("All workspaces"));
        reopened_scoped_manager.registerAction(
            QStringLiteral("scoped.shared"), QStringLiteral("Shared"),
            &loaded_shared_action, settings::ShortcutScope::Shared,
            QStringLiteral("Edit and Fusion"));
        reopened_scoped_manager.registerAction(
            QStringLiteral("scoped.edit"), QStringLiteral("Edit command"),
            &loaded_edit_action, settings::ShortcutScope::Edit,
            QStringLiteral("Edit"));
        reopened_scoped_manager.registerAction(
            QStringLiteral("scoped.fusion"), QStringLiteral("Fusion command"),
            &loaded_fusion_action, settings::ShortcutScope::Fusion,
            QStringLiteral("Fusion"));
        reopened_scoped_manager.registerAction(
            QStringLiteral("scoped.render"), QStringLiteral("Render command"),
            &loaded_render_action, settings::ShortcutScope::Render,
            QStringLiteral("Render"));
        require(reopened_scoped_manager.load() &&
                    reopened_scoped_manager.shortcut(
                        QStringLiteral("scoped.edit")) == QKeySequence("Ctrl+P") &&
                    reopened_scoped_manager.shortcut(
                        QStringLiteral("scoped.shared")) == QKeySequence("Ctrl+R") &&
                    reopened_scoped_manager.shortcut(
                        QStringLiteral("scoped.render")) == QKeySequence("Ctrl+P") &&
                    loaded_render_action.shortcut().isEmpty(),
                "Scoped shortcuts did not persist across inactive workspaces.");
        reopened_scoped_manager.setWorkspace(ui::WorkspacePageId::Render);
        require(loaded_render_action.shortcut() == QKeySequence("Ctrl+P"),
                "Reopened Render binding was not activated from its saved configuration.");
        reopened_scoped_manager.setWorkspace(ui::WorkspacePageId::Edit);
        require(reopened_scoped_manager.resetShortcut(
                    QStringLiteral("scoped.edit")) &&
                    loaded_edit_action.shortcut() == QKeySequence("Ctrl+K"),
                "Scoped shortcut individual reset failed.");
        reopened_scoped_manager.resetAll();
        require(reopened_scoped_manager.shortcut(
                    QStringLiteral("scoped.shared")) ==
                        QKeySequence("Ctrl+Shift+P") &&
                    reopened_scoped_manager.shortcut(
                        QStringLiteral("scoped.render")) == QKeySequence("Ctrl+R"),
                "Scoped Reset All did not restore defaults.");

        settings.beginGroup(QStringLiteral("shortcuts"));
        settings.setValue(QStringLiteral("repair.application"),
                          QStringLiteral("Ctrl+P"));
        settings.setValue(QStringLiteral("repair.edit"), QStringLiteral("Ctrl+P"));
        settings.endGroup();
        settings.sync();
        QAction repair_application_action;
        repair_application_action.setShortcut(QKeySequence("Ctrl+N"));
        QAction repair_edit_action;
        repair_edit_action.setShortcut(QKeySequence("Ctrl+K"));
        settings::ShortcutManager repair_manager;
        repair_manager.registerAction(
            QStringLiteral("repair.application"), QStringLiteral("Repair Application"),
            &repair_application_action, settings::ShortcutScope::Application,
            QStringLiteral("All workspaces"));
        repair_manager.registerAction(
            QStringLiteral("repair.edit"), QStringLiteral("Repair Edit"),
            &repair_edit_action, settings::ShortcutScope::Edit,
            QStringLiteral("Edit workspace"));
        const bool repair_preferences_loaded = repair_manager.load();
        settings.sync();
        require(repair_preferences_loaded &&
                    repair_manager.shortcut(QStringLiteral("repair.application")) ==
                        QKeySequence("Ctrl+P") &&
                    repair_manager.shortcut(QStringLiteral("repair.edit")) ==
                        QKeySequence("Ctrl+K") &&
                    settings.value(QStringLiteral("shortcuts/repair.edit")).toString() ==
                        QStringLiteral("Ctrl+K"),
                "Conflicting stored preferences must restore a valid default and persist the repair.");

        verifyFunctionPalette(settings);
        settings.clear();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        settings.clear();
        return 1;
    }
}
