#include "settings/settings_dialog.h"
#include "settings/shortcut_manager.h"
#include "settings/user_preferences.h"

#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTest>

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
    QCoreApplication::setApplicationName("SettingsDialogTest");
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QTemporaryDir settings_directory;
    if (!settings_directory.isValid()) return 1;
    QSettings::setPath(
        QSettings::IniFormat, QSettings::UserScope, settings_directory.path());

    try {
        QSettings settings;
        settings.clear();
        settings.sync();

        require(settings::workspacePageTransitionsEnabled() &&
                    settings::workspacePageTransitionStyle() ==
                        settings::WorkspacePageTransitionStyle::WorkspaceContent &&
                    settings::workspacePageTransitionDurationMs() ==
                        settings::kDefaultWorkspacePageTransitionDurationMs,
                "Workspace content transitions must be the default at 250 ms.");
        settings::setWorkspacePageTransitionStyle(
            settings::WorkspacePageTransitionStyle::EntireApplicationWindow);
        settings.sync();
        require(settings::workspacePageTransitionStyle() ==
                    settings::WorkspacePageTransitionStyle::EntireApplicationWindow,
                "The whole-window transition style was not persisted.");
        settings.setValue(settings::kWorkspacePageTransitionStyleKey, 99);
        require(settings::workspacePageTransitionStyle() ==
                    settings::kDefaultWorkspacePageTransitionStyle,
                "An invalid workspace transition style did not use its default.");
        settings.remove(settings::kWorkspacePageTransitionStyleKey);
        settings::setWorkspacePageTransitionsEnabled(false);
        require(!settings::workspacePageTransitionsEnabled(),
                "Disabling workspace transitions was not persisted.");
        settings::setWorkspacePageTransitionsEnabled(true);
        settings::setWorkspacePageTransitionDurationMs(137);
        require(settings::workspacePageTransitionDurationMs() == 125,
                "Workspace transition duration must snap to 25 ms steps.");
        settings::setWorkspacePageTransitionDurationMs(1);
        require(settings::workspacePageTransitionDurationMs() ==
                    settings::kMinimumWorkspacePageTransitionDurationMs,
                "Workspace transition duration did not enforce its lower bound.");
        settings::setWorkspacePageTransitionDurationMs(999);
        require(settings::workspacePageTransitionDurationMs() ==
                    settings::kMaximumWorkspacePageTransitionDurationMs,
                "Workspace transition duration did not enforce its upper bound.");
        settings.setValue(settings::kWorkspacePageTransitionDurationMsKey,
                          QStringLiteral("invalid"));
        require(settings::workspacePageTransitionDurationMs() ==
                    settings::kDefaultWorkspacePageTransitionDurationMs,
                "Invalid workspace transition duration did not fall back to 250 ms.");
        settings.setValue(settings::kWorkspacePageTransitionsEnabledKey,
                          QStringLiteral("invalid"));
        require(settings::workspacePageTransitionsEnabled(),
                "Invalid workspace transition toggle did not use its default.");
        settings.remove(settings::kWorkspacePageTransitionDurationMsKey);
        settings.remove(settings::kWorkspacePageTransitionsEnabledKey);

        require(settings::timelineTrackGroupSplitRatio() == 0.5,
                "Timeline track group split ratio must default to 50/50.");
        settings::setTimelineTrackGroupSplitRatio(0.68);
        settings.sync();
        require(settings::timelineTrackGroupSplitRatio() == 0.68 &&
                    settings.value(
                        settings::kTimelineTrackGroupSplitRatioKey).toDouble() == 0.68,
                "Timeline track group split ratio was not persisted locally.");
        settings::setTimelineTrackGroupSplitRatio(1.0);
        require(settings::timelineTrackGroupSplitRatio() ==
                    settings::kMaximumTimelineTrackGroupSplitRatio,
                "Timeline track group split ratio did not enforce its upper bound.");
        settings.setValue(settings::kTimelineTrackGroupSplitRatioKey, "invalid");
        require(settings::timelineTrackGroupSplitRatio() == 0.5,
                "Invalid Timeline split ratio did not fall back to the default.");

        require(settings::timelineTrackRowHeightAdjustmentMode() ==
                    timeline::TrackRowHeightAdjustmentMode::Together,
                "Track row-height adjustment must default to Together.");
        settings::setTimelineTrackRowHeightAdjustmentMode(
            timeline::TrackRowHeightAdjustmentMode::IndependentlyByGroup);
        settings.sync();
        require(settings::timelineTrackRowHeightAdjustmentMode() ==
                    timeline::TrackRowHeightAdjustmentMode::IndependentlyByGroup,
                "Independent track row-height adjustment was not persisted locally.");
        settings.setValue(
            settings::kTimelineTrackRowHeightAdjustmentModeKey, 99);
        require(settings::timelineTrackRowHeightAdjustmentMode() ==
                    timeline::TrackRowHeightAdjustmentMode::Together,
                "An invalid track row-height mode did not fall back to Together.");
        settings::setTimelineTrackRowHeightAdjustmentMode(
            timeline::TrackRowHeightAdjustmentMode::Together);
        settings.clear();

        require(settings::monitorVolumePercent() ==
                    settings::kDefaultMonitorVolumePercent,
                "Monitor volume default is incorrect.");
        settings.setValue(settings::kMonitorVolumePercentKey, "invalid");
        require(settings::monitorVolumePercent() ==
                    settings::kDefaultMonitorVolumePercent,
                "Invalid monitor volume preference was not normalized.");
        settings.remove(settings::kMonitorVolumePercentKey);
        settings::setMonitorVolumePercent(150);
        require(settings::monitorVolumePercent() == 150 &&
                    settings.value(settings::kMonitorVolumePercentKey).toInt() == 150,
                "Monitor volume preference was not persisted.");
        settings::setMonitorVolumePercent(999);
        require(settings::monitorVolumePercent() ==
                    settings::kMaximumMonitorVolumePercent,
                "Monitor volume upper bound was not applied.");
        settings::setMonitorVolumePercent(-10);
        require(settings::monitorVolumePercent() ==
                    settings::kMinimumMonitorVolumePercent,
                "Monitor volume lower bound was not applied.");
        settings.clear();

        QAction app_shortcut_action;
        app_shortcut_action.setShortcut(QKeySequence("Ctrl+N"));
        QAction edit_shortcut_action;
        edit_shortcut_action.setShortcut(QKeySequence("Ctrl+K"));
        QAction switch_edit_action;
        switch_edit_action.setShortcut(QKeySequence("Alt+1"));
        QAction switch_fusion_action;
        switch_fusion_action.setShortcut(QKeySequence("Alt+2"));
        QAction switch_render_action;
        switch_render_action.setShortcut(QKeySequence("Alt+3"));
        settings::ShortcutManager shortcut_manager;
        shortcut_manager.registerAction(
            QStringLiteral("settings.test.new"), QStringLiteral("New Project"),
            &app_shortcut_action, settings::ShortcutScope::Application,
            QStringLiteral("All workspaces"));
        shortcut_manager.registerAction(
            QStringLiteral("workspace.switch_edit"),
            QStringLiteral("Switch to Edit workspace"),
            &switch_edit_action, settings::ShortcutScope::Application,
            QStringLiteral("All workspaces"));
        shortcut_manager.registerAction(
            QStringLiteral("workspace.switch_fusion"),
            QStringLiteral("Switch to Fusion workspace"),
            &switch_fusion_action, settings::ShortcutScope::Application,
            QStringLiteral("All workspaces"));
        shortcut_manager.registerAction(
            QStringLiteral("workspace.switch_render"),
            QStringLiteral("Switch to Render workspace"),
            &switch_render_action, settings::ShortcutScope::Application,
            QStringLiteral("All workspaces"));
        shortcut_manager.registerAction(
            QStringLiteral("settings.test.split"), QStringLiteral("Split Clip"),
            &edit_shortcut_action, settings::ShortcutScope::Edit,
            QStringLiteral("Edit workspace"));
        QString settings_shortcut_load_error;
        const bool settings_shortcuts_loaded =
            shortcut_manager.load(&settings_shortcut_load_error);
        if (!settings_shortcuts_loaded) {
            std::fprintf(
                stderr, "%s\n", settings_shortcut_load_error.toUtf8().constData());
        }
        require(settings_shortcuts_loaded,
                "Shortcut preferences could not be loaded for Settings.");
        require(shortcut_manager.setShortcut(
                    QStringLiteral("settings.test.new"), QKeySequence("Ctrl+P")),
                "Settings shortcut fixture could not be customized.");
        settings::SettingsDialog dialog(nullptr, shortcut_manager);
        require(dialog.windowTitle() == "Settings",
                "Settings dialog title is incorrect.");
        require(dialog.isModal(), "Settings dialog must be modal.");
        require(dialog.width() >= 900 && dialog.height() >= 680,
                "Settings dialog default size is too small.");

        auto* tabs = dialog.findChild<QTabWidget*>();
        require(tabs != nullptr, "Settings dialog tabs are missing.");
        require(tabs->count() == 4,
                "Settings dialog must contain four tabs.");
        require(tabs->tabText(0) == "General",
                "General settings tab is missing.");
        require(tabs->tabText(1) == "Autosave",
                "Autosave settings tab is missing.");
        require(tabs->tabText(2) == "Timeline",
                "Timeline settings tab is missing.");
        require(tabs->tabText(3) == "Shortcuts",
                "Shortcuts settings tab is missing.");

        auto* workspace_transitions = dialog.findChild<QCheckBox*>(
            "workspacePageTransitionsCheckBox");
        auto* workspace_transition_duration = dialog.findChild<QSlider*>(
            "workspacePageTransitionDurationSlider");
        auto* workspace_transition_style = dialog.findChild<QComboBox*>(
            "workspacePageTransitionStyleComboBox");
        auto* workspace_transition_duration_label = dialog.findChild<QLabel*>(
            "workspacePageTransitionDurationLabel");
        require(workspace_transitions != nullptr &&
                    workspace_transition_duration != nullptr &&
                    workspace_transition_style != nullptr &&
                    workspace_transition_duration_label != nullptr &&
                    workspace_transitions->isChecked() &&
                    workspace_transition_style->isEnabled() &&
                    workspace_transition_style->currentData().toInt() ==
                        static_cast<int>(
                            settings::WorkspacePageTransitionStyle::WorkspaceContent) &&
                    workspace_transition_duration->isEnabled() &&
                    workspace_transition_duration->minimum() == 100 &&
                    workspace_transition_duration->maximum() == 600 &&
                    workspace_transition_duration->singleStep() == 25 &&
                    workspace_transition_duration->value() == 250 &&
                    workspace_transition_duration_label->text() == "250 ms",
                "Workspace transition settings controls have incorrect defaults.");
        const auto whole_window_style_index =
            workspace_transition_style->findData(static_cast<int>(
                settings::WorkspacePageTransitionStyle::EntireApplicationWindow));
        require(whole_window_style_index >= 0,
                "The whole-window transition option is missing.");
        workspace_transition_style->setCurrentIndex(whole_window_style_index);
        require(settings::workspacePageTransitionStyle() ==
                    settings::WorkspacePageTransitionStyle::EntireApplicationWindow,
                "Changing the workspace transition style must persist immediately.");
        workspace_transition_duration->setValue(400);
        require(settings::workspacePageTransitionDurationMs() == 400 &&
                    workspace_transition_duration_label->text() == "400 ms",
                "Changing the workspace duration must apply and persist immediately.");
        workspace_transitions->setChecked(false);
        require(!settings::workspacePageTransitionsEnabled() &&
                    !workspace_transition_duration->isEnabled() &&
                    !workspace_transition_style->isEnabled(),
                "Disabling workspace animation must persist and disable its controls.");
        {
            settings::SettingsDialog reopened(nullptr, shortcut_manager);
            auto* reopened_toggle = reopened.findChild<QCheckBox*>(
                "workspacePageTransitionsCheckBox");
            auto* reopened_duration = reopened.findChild<QSlider*>(
                "workspacePageTransitionDurationSlider");
            auto* reopened_style = reopened.findChild<QComboBox*>(
                "workspacePageTransitionStyleComboBox");
            require(reopened_toggle != nullptr && reopened_duration != nullptr &&
                        reopened_style != nullptr &&
                        !reopened_toggle->isChecked() &&
                        !reopened_duration->isEnabled() &&
                        !reopened_style->isEnabled() &&
                        reopened_style->currentData().toInt() ==
                            static_cast<int>(
                                settings::WorkspacePageTransitionStyle::EntireApplicationWindow) &&
                        reopened_duration->value() == 400,
                    "Reopened Settings did not restore workspace transition preferences.");
        }
        workspace_transitions->setChecked(true);
        workspace_transition_style->setCurrentIndex(
            workspace_transition_style->findData(static_cast<int>(
                settings::WorkspacePageTransitionStyle::WorkspaceContent)));
        require(settings::workspacePageTransitionStyle() ==
                    settings::WorkspacePageTransitionStyle::WorkspaceContent,
                "The workspace-content style could not be selected again.");
        workspace_transition_duration->setValue(100);
        require(settings::workspacePageTransitionsEnabled() &&
                    settings::workspacePageTransitionDurationMs() == 100,
                "The minimum workspace transition duration was not applied.");
        workspace_transition_duration->setValue(600);
        require(settings::workspacePageTransitionDurationMs() == 600,
                "The maximum workspace transition duration was not applied.");
        workspace_transition_duration->setValue(250);

        require(dialog.findChild<QLabel*>("shortcutScope_application") != nullptr &&
                    dialog.findChild<QLabel*>("shortcutScope_edit") != nullptr &&
                    dialog.findChild<QLabel*>(
                        "shortcutScopeDescription_application") != nullptr &&
                    dialog.findChild<QLabel*>(
                        "shortcutScopeDescription_edit") != nullptr,
                "Shortcut Settings must group commands and explain availability.");
        require(dialog.findChild<QLabel*>("shortcutScope_shared") == nullptr &&
                    dialog.findChild<QLabel*>("shortcutScope_fusion") == nullptr &&
                    dialog.findChild<QLabel*>("shortcutScope_render") == nullptr,
                "Shortcut Settings must omit scopes without registered commands.");
        auto* app_sequence_editor = dialog.findChild<QKeySequenceEdit*>(
            "shortcutEditor_settings.test.new");
        auto* app_reset_button = dialog.findChild<QPushButton*>(
            "shortcutReset_settings.test.new");
        auto* app_clear_button = dialog.findChild<QPushButton*>(
            "shortcutClear_settings.test.new");
        auto* switch_edit_editor = dialog.findChild<QKeySequenceEdit*>(
            "shortcutEditor_workspace.switch_edit");
        auto* switch_fusion_editor = dialog.findChild<QKeySequenceEdit*>(
            "shortcutEditor_workspace.switch_fusion");
        auto* switch_render_editor = dialog.findChild<QKeySequenceEdit*>(
            "shortcutEditor_workspace.switch_render");
        auto* row_height_mode = dialog.findChild<QComboBox*>(
            "timelineTrackHeightAdjustmentModeComboBox");
        require(app_sequence_editor != nullptr && app_reset_button != nullptr &&
                    app_clear_button != nullptr &&
                    app_sequence_editor->keySequence() == QKeySequence("Ctrl+P") &&
                    switch_edit_editor != nullptr &&
                    switch_edit_editor->keySequence() == QKeySequence("Alt+1") &&
                    switch_fusion_editor != nullptr &&
                    switch_fusion_editor->keySequence() == QKeySequence("Alt+2") &&
                    switch_render_editor != nullptr &&
                    switch_render_editor->keySequence() == QKeySequence("Alt+3") &&
                    row_height_mode != nullptr &&
                    row_height_mode->currentData().toInt() ==
                        static_cast<int>(timeline::TrackRowHeightAdjustmentMode::Together),
                "Settings did not show the configured shortcut or the three workspace navigation defaults.");
        row_height_mode->setCurrentIndex(1);
        require(settings::timelineTrackRowHeightAdjustmentMode() ==
                    timeline::TrackRowHeightAdjustmentMode::IndependentlyByGroup,
                "Changing the Timeline height mode did not apply immediately.");
        row_height_mode->setCurrentIndex(0);
        require(settings::timelineTrackRowHeightAdjustmentMode() ==
                    timeline::TrackRowHeightAdjustmentMode::Together,
                "Restoring the Timeline height mode did not apply immediately.");
        dialog.show();
        tabs->setCurrentWidget(tabs->widget(3));
        QApplication::processEvents();
        app_sequence_editor->setFocus();
        QApplication::processEvents();
        QTest::keyClick(
            app_sequence_editor, Qt::Key_F6,
            Qt::ControlModifier | Qt::AltModifier);
        QTest::qWait(1100);
        require(shortcut_manager.shortcut(QStringLiteral("settings.test.new")) ==
                    QKeySequence("Ctrl+Alt+F6") &&
                    app_shortcut_action.shortcut() == QKeySequence("Ctrl+Alt+F6"),
                "Capturing a key sequence in Settings must apply it immediately.");
        require(shortcut_manager.setShortcut(
                    QStringLiteral("settings.test.split"),
                    QKeySequence("Ctrl+Alt+F8")),
                "Settings conflict fixture could not be customized.");
        app_sequence_editor->setFocus();
        QTest::keyClick(
            app_sequence_editor, Qt::Key_F8,
            Qt::ControlModifier | Qt::AltModifier);
        QTest::qWait(1100);
        auto* shortcut_feedback = dialog.findChild<QLabel*>("shortcutFeedback");
        const bool conflict_was_reported = shortcut_feedback != nullptr &&
            shortcut_feedback->text().contains("Split Clip") &&
            shortcut_feedback->text().contains("Edit workspace") &&
            app_sequence_editor->keySequence() == QKeySequence("Ctrl+Alt+F6");
        if (!conflict_was_reported) {
            std::fprintf(
                stderr, "feedback='%s', editor='%s', configured='%s'\n",
                shortcut_feedback == nullptr
                    ? "<missing>" : shortcut_feedback->text().toUtf8().constData(),
                app_sequence_editor->keySequence().toString().toUtf8().constData(),
                shortcut_manager.shortcut(QStringLiteral("settings.test.new"))
                    .toString().toUtf8().constData());
        }
        require(conflict_was_reported,
                "Settings must explain a conflicting binding and keep its previous value.");
        require(shortcut_manager.resetShortcut(
                    QStringLiteral("settings.test.split")),
                "Settings conflict fixture did not reset its edited scope shortcut.");
        app_clear_button->click();
        require(shortcut_manager.shortcut(QStringLiteral("settings.test.new")).isEmpty() &&
                    app_sequence_editor->keySequence().isEmpty() &&
                    app_shortcut_action.shortcut().isEmpty(),
                "Settings Clear must disable the shortcut immediately.");
        app_reset_button->click();
        require(app_sequence_editor->keySequence() == QKeySequence("Ctrl+N") &&
                    app_shortcut_action.shortcut() == QKeySequence("Ctrl+N"),
                "Settings individual Reset did not update the shortcut immediately.");

        require(settings::audioWaveformDisplayMode() ==
                    settings::AudioWaveformDisplayMode::Mono,
                "Audio waveform display must default to Mono.");
        settings.setValue(settings::kAudioWaveformDisplayModeKey, 99);
        require(settings::audioWaveformDisplayMode() ==
                    settings::AudioWaveformDisplayMode::Mono,
                "Invalid audio waveform preference did not fall back to Mono.");
        settings.remove(settings::kAudioWaveformDisplayModeKey);
        auto* waveform_mode = dialog.findChild<QComboBox*>(
            "audioWaveformDisplayModeComboBox");
        require(waveform_mode != nullptr && waveform_mode->count() == 2 &&
                    waveform_mode->itemText(0) == "Mono" &&
                    waveform_mode->itemText(1) == "Stereo" &&
                    waveform_mode->currentIndex() == 0,
                "Timeline waveform mode controls or defaults are incorrect.");
        int waveform_mode_changes = 0;
        QObject::connect(
            &dialog, &settings::SettingsDialog::audioWaveformStereoModeChanged,
            [&](bool stereo) {
                ++waveform_mode_changes;
                require(settings::audioWaveformDisplayMode() ==
                            (stereo
                                ? settings::AudioWaveformDisplayMode::Stereo
                                : settings::AudioWaveformDisplayMode::Mono),
                        "Waveform mode signal preceded global preference storage.");
            });
        waveform_mode->setCurrentIndex(1);
        settings.sync();
        require(waveform_mode_changes == 1 &&
                    settings::audioWaveformDisplayMode() ==
                        settings::AudioWaveformDisplayMode::Stereo &&
                    settings.value(settings::kAudioWaveformDisplayModeKey).toInt() == 1,
                "Stereo waveform preference was not applied and persisted immediately.");
        {
            settings::SettingsDialog reopened(nullptr, shortcut_manager);
            const auto* reopened_mode = reopened.findChild<QComboBox*>(
                "audioWaveformDisplayModeComboBox");
            require(reopened_mode != nullptr && reopened_mode->currentIndex() == 1,
                    "Reopened Settings lost the stereo waveform preference.");
        }
        waveform_mode->setCurrentIndex(0);
        require(waveform_mode_changes == 2 &&
                    settings::audioWaveformDisplayMode() ==
                        settings::AudioWaveformDisplayMode::Mono,
                "Mono waveform preference did not apply immediately.");

        const auto* buttons = dialog.findChild<QDialogButtonBox*>();
        require(buttons != nullptr, "Settings dialog close button is missing.");
        require(buttons->button(QDialogButtonBox::Close) != nullptr,
                "Settings dialog Close button is missing.");

        auto* metrics_check = dialog.findChild<QCheckBox*>(
            "previewMetricsCheckBox");
        require(metrics_check != nullptr,
                "Preview metrics checkbox is missing.");
        require(metrics_check->isChecked(),
                "Preview metrics must be enabled by default.");

        auto* gpu_check = dialog.findChild<QCheckBox*>("gpuCompositionCheckBox");
        require(gpu_check && !gpu_check->isChecked() && !settings::gpuCompositionEnabled(),
                "GPU composition must be disabled by default.");
        require(gpu_check->text() == "Use GPU for timeline preview (Experimental)" &&
                !gpu_check->accessibleDescription().isEmpty(), "GPU setting label/accessibility missing.");
        int gpu_changes = 0;
        QObject::connect(&dialog, &settings::SettingsDialog::gpuCompositionEnabledChanged,
            [&](bool enabled) {
                ++gpu_changes;
                require(settings::gpuCompositionEnabled() == enabled,
                        "GPU signal preceded persistence.");
            });
        gpu_check->setChecked(true);
        settings.sync();
        require(gpu_changes == 1 && settings.value(settings::kGpuCompositionEnabledKey).toBool(),
                "GPU preference was not applied/persisted immediately.");
        {
            settings::SettingsDialog reopened(nullptr, shortcut_manager);
            require(reopened.findChild<QCheckBox*>("gpuCompositionCheckBox")->isChecked(),
                    "Reopened Settings lost the GPU preference.");
        }
        gpu_check->setChecked(false);
        require(gpu_changes == 2 && !settings::gpuCompositionEnabled(), "GPU preference did not disable.");

        auto* autosave_check = dialog.findChild<QCheckBox*>(
            "projectAutosaveCheckBox");
        require(autosave_check != nullptr,
                "Project autosave checkbox is missing.");
        require(autosave_check->isChecked(),
                "Project autosave must be enabled by default.");
        auto* interval_spin = dialog.findChild<QSpinBox*>(
            "projectAutosaveIntervalSpinBox");
        auto* retention_spin = dialog.findChild<QSpinBox*>(
            "projectAutosaveRetentionSpinBox");
        require(interval_spin != nullptr && retention_spin != nullptr,
                "Project autosave controls are missing.");
        require(interval_spin->value() == 30 && retention_spin->value() == 5,
                "Project autosave defaults are incorrect.");
        require(interval_spin->minimumWidth() >= 96 &&
                        retention_spin->minimumWidth() >= 96,
                "Project autosave numeric controls are too narrow to read.");

        auto* snapshot_table = dialog.findChild<QTableWidget*>(
            "autosaveSnapshotsTable");
        auto* refresh_button = dialog.findChild<QPushButton*>(
            "autosaveRefreshButton");
        auto* restore_button = dialog.findChild<QPushButton*>(
            "autosaveRestoreButton");
        auto* delete_button = dialog.findChild<QPushButton*>(
            "autosaveDeleteButton");
        auto* open_folder_button = dialog.findChild<QPushButton*>(
            "autosaveOpenFolderButton");
        require(snapshot_table != nullptr && refresh_button != nullptr &&
                    restore_button != nullptr && delete_button != nullptr &&
                    open_folder_button != nullptr,
                "Autosave management controls are missing.");
        require(snapshot_table->rowCount() == 0 &&
                    !restore_button->isEnabled() &&
                    !delete_button->isEnabled() &&
                    !open_folder_button->isEnabled(),
                "Autosave controls must start disabled for an empty list.");

        std::vector<settings::AutosaveSnapshotItem> autosave_rows{
            {"project.csp", "Saved project", "Today 12:00",
             "snapshot-new.csp", "C:/recovery/snapshot-new.csp",
             "C:/project.csp", "C:/recovery"},
            {"Unsaved project", "Unsaved project", "Today 11:00",
             "snapshot-old.csp", "C:/unsaved/snapshot-old.csp", "",
             "C:/unsaved"}};
        dialog.setAutosaveSnapshots(autosave_rows);
        require(snapshot_table->rowCount() == 2 &&
                    snapshot_table->currentRow() == 0,
                "Autosave list did not select the newest snapshot.");
        require(snapshot_table->item(0, 0)->text() == "project.csp" &&
                    snapshot_table->item(0, 1)->text() == "Saved project" &&
                    snapshot_table->item(0, 3)->text() == "snapshot-new.csp",
                "Autosave snapshot row contents are incorrect.");
        require(snapshot_table->item(0, 0)->data(Qt::UserRole).toString() ==
                        "C:/recovery/snapshot-new.csp" &&
                    snapshot_table->item(0, 0)->data(Qt::UserRole + 1).toString() ==
                        "C:/project.csp" &&
                    snapshot_table->item(0, 0)->data(Qt::UserRole + 2).toString() ==
                        "C:/recovery",
                "Autosave snapshot paths were not preserved in item data.");
        require(restore_button->isEnabled() && delete_button->isEnabled() &&
                    open_folder_button->isEnabled(),
                "Autosave actions must enable for a selected snapshot.");

        bool refresh_requested = false;
        QString restore_path;
        QString restore_project_path;
        QString delete_path;
        QString open_folder_path;
        QObject::connect(
            &dialog, &settings::SettingsDialog::autosaveRefreshRequested,
            [&refresh_requested]() { refresh_requested = true; });
        QObject::connect(
            &dialog, &settings::SettingsDialog::autosaveRestoreRequested,
            [&restore_path, &restore_project_path](
                const QString& snapshot_path, const QString& project_path) {
                restore_path = snapshot_path;
                restore_project_path = project_path;
            });
        QObject::connect(
            &dialog, &settings::SettingsDialog::autosaveDeleteRequested,
            [&delete_path](const QString& snapshot_path) {
                delete_path = snapshot_path;
            });
        QObject::connect(
            &dialog, &settings::SettingsDialog::autosaveOpenFolderRequested,
            [&open_folder_path](const QString& folder_path) {
                open_folder_path = folder_path;
            });
        refresh_button->click();
        restore_button->click();
        delete_button->click();
        open_folder_button->click();
        require(refresh_requested &&
                    restore_path == "C:/recovery/snapshot-new.csp" &&
                    restore_project_path == "C:/project.csp" &&
                    delete_path == "C:/recovery/snapshot-new.csp" &&
                    open_folder_path == "C:/recovery",
                "Autosave management actions did not emit their requests.");

        dialog.setAutosaveSnapshots({});
        require(snapshot_table->rowCount() == 0 &&
                    !restore_button->isEnabled() &&
                    !delete_button->isEnabled() &&
                    !open_folder_button->isEnabled(),
                "Autosave actions did not disable after clearing the list.");

        bool signal_emitted = false;
        bool signal_value = false;
        QObject::connect(
            &dialog,
            &settings::SettingsDialog::previewPerformanceMetricsEnabledChanged,
            [&signal_emitted, &signal_value](bool enabled) {
                signal_emitted = true;
                signal_value = enabled;
            });
        metrics_check->setChecked(false);
        require(signal_emitted && !signal_value,
                "Disabling preview metrics did not emit its signal.");
        require(
            !settings.value(settings::kPreviewMetricsEnabledKey).toBool(),
            "Disabling preview metrics was not persisted.");

        signal_emitted = false;
        metrics_check->setChecked(true);
        require(signal_emitted && signal_value,
                "Enabling preview metrics did not emit its signal.");
        require(
            settings.value(settings::kPreviewMetricsEnabledKey).toBool(),
            "Enabling preview metrics was not persisted.");

        autosave_check->setChecked(false);
        interval_spin->setValue(60);
        retention_spin->setValue(10);
        require(
            !settings.value(settings::kProjectAutosaveEnabledKey).toBool() &&
                settings.value(settings::kProjectAutosaveIntervalSecondsKey).toInt() == 60 &&
                settings.value(settings::kProjectAutosaveRetentionKey).toInt() == 10,
            "Project autosave settings were not persisted.");

        settings::setProjectAutosaveIntervalSeconds(1);
        settings::setProjectAutosaveRetention(100);
        require(
            settings::projectAutosaveIntervalSeconds() ==
                    settings::kMinimumProjectAutosaveIntervalSeconds &&
                settings::projectAutosaveRetention() ==
                    settings::kMaximumProjectAutosaveRetention,
            "Project autosave settings did not clamp invalid values.");

        dialog.close();
        settings.clear();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
