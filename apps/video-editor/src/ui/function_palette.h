#pragma once

#include <QObject>

class QAction;
class QDialog;
class QWidget;

namespace settings {
class ShortcutManager;
}

namespace ui {

class FunctionPalette final : public QObject {
public:
    FunctionPalette(QWidget* owner, settings::ShortcutManager& shortcut_manager);

    [[nodiscard]] QDialog* dialog() const noexcept;
    [[nodiscard]] QAction* toggleAction() const noexcept;

    void toggle();

private:
    void restoreOwnerFocus();

    QWidget* owner_ = nullptr;
    QDialog* dialog_ = nullptr;
    QAction* toggle_action_ = nullptr;
};

}  // namespace ui
