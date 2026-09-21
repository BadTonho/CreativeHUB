#pragma once

#include <QDialog>

class QWidget;

namespace settings {

class ShortcutManager;

class SettingsDialog final : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(
        QWidget* parent,
        ShortcutManager& shortcut_manager);

private:
    [[nodiscard]] QWidget* createGeneralPage();
    [[nodiscard]] QWidget* createTimelinePage();
    [[nodiscard]] QWidget* createShortcutsPage();

    ShortcutManager& shortcut_manager_;
};

} // namespace settings
