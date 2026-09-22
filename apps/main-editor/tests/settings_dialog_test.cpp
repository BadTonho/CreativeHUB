#include "settings/settings_dialog.h"
#include "settings/shortcut_manager.h"
#include "settings/user_preferences.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QSettings>
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
        require(tabs->count() == 3,
                "Settings dialog must contain three tabs.");
        require(tabs->tabText(0) == "General",
                "General settings tab is missing.");
        require(tabs->tabText(1) == "Timeline",
                "Timeline settings tab is missing.");
        require(tabs->tabText(2) == "Shortcuts",
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

        dialog.close();
        settings.clear();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
