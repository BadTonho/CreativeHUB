#pragma once

#include <QObject>
#include <QPointer>

class QAction;
class QDialog;
class QEvent;
class QWidget;

namespace settings {
class ShortcutManager;
}

namespace ui {

class FunctionPalette final : public QObject {
public:
    FunctionPalette(QWidget* owner, settings::ShortcutManager& shortcut_manager);
    ~FunctionPalette() override;

    [[nodiscard]] QDialog* dialog() const noexcept;
    [[nodiscard]] QAction* toggleAction() const noexcept;

    void toggle();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createDialog();
    void removeDismissFilters();
    void closeDialog(bool restore_owner_focus);
    void restoreOwnerFocus();

    QWidget* owner_ = nullptr;
    QPointer<QDialog> dialog_;
    QAction* toggle_action_ = nullptr;
    bool suppress_owner_focus_restore_ = false;
};

}  // namespace ui
