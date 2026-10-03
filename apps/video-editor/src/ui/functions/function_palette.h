#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include "settings/shortcut_manager.h"

class QAction;
class QDialog;
class QEvent;
class QWidget;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace ui {

class FunctionPalette final : public QObject {
    Q_OBJECT
public:
    FunctionPalette(QWidget* owner, settings::ShortcutManager& shortcut_manager);
    ~FunctionPalette() override;

    [[nodiscard]] QDialog* dialog() const noexcept;
    [[nodiscard]] QAction* toggleAction() const noexcept;

    void toggle();
    void setEffectTargetAvailable(bool available);

signals:
    void effectAddRequested(const QString& effect_id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void createDialog();
    void removeDismissFilters();
    void closeDialog(bool restore_owner_focus);
    void restoreOwnerFocus();
    void updateAddEnabled();
    void addCurrentEffect();
    void filterEffects(const QString& query);

    QWidget* owner_ = nullptr;
    QPointer<QDialog> dialog_;
    QAction* toggle_action_ = nullptr;
    QLineEdit* search_ = nullptr;
    QListWidget* effect_list_ = nullptr;
    QPushButton* add_button_ = nullptr;
    bool effect_target_available_ = false;
    bool suppress_owner_focus_restore_ = false;
};

}  // namespace ui
