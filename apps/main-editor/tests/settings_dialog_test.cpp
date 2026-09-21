#include "settings/settings_dialog.h"

#include <QApplication>
#include <QDialogButtonBox>
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
        SettingsDialog dialog;
        require(dialog.windowTitle() == "Settings",
                "Settings dialog title is incorrect.");
        require(dialog.isModal(), "Settings dialog must be modal.");

        const auto* tabs = dialog.findChild<QTabWidget*>();
        require(tabs != nullptr, "Settings dialog tabs are missing.");
        require(tabs->count() == 2,
                "Settings dialog must contain two tabs.");
        require(tabs->tabText(0) == "General",
                "General settings tab is missing.");
        require(tabs->tabText(1) == "Timeline",
                "Timeline settings tab is missing.");

        const auto* buttons = dialog.findChild<QDialogButtonBox*>();
        require(buttons != nullptr, "Settings dialog close button is missing.");
        require(buttons->button(QDialogButtonBox::Close) != nullptr,
                "Settings dialog Close button is missing.");

        dialog.close();
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
