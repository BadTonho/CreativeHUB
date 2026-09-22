#include "settings/settings_dialog.h"
#include "settings/shortcut_manager.h"
#include "settings/user_preferences.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTabWidget>

#include <cstdio>
#include <stdexcept>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);

    try {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings settings;
        settings.clear();
        settings.sync();

        settings::ShortcutManager shortcut_manager;
        settings::SettingsDialog dialog(nullptr, shortcut_manager);
        require(dialog.windowTitle() == "Settings",
                "Settings dialog title is incorrect.");
        require(dialog.isModal(), "Settings dialog must be modal.");
        require(dialog.width() >= 900 && dialog.height() >= 680,
                "Settings dialog default size is too small.");

        const auto* tabs = dialog.findChild<QTabWidget*>();
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
