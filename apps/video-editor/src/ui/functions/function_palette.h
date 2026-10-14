#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <cstdint>
#include <functional>

#include "settings/shortcut_manager.h"
#include "ui/effects/effects_catalog.h"

class QAction;
class QDialog;
class QEvent;
class QWidget;
class QLineEdit;
class QListWidget;
class QPushButton;
class QLabel;

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
    void setContext(effects::Workspace workspace, bool target_available);
    // The UI closes only when the synchronous application edit is accepted.
    void setAddHandler(std::function<bool(const QString&)> handler);

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
    [[nodiscard]] bool canAddCurrentEffect() const;
    void setEffectDragInProgress(bool in_progress);
    void finishEffectDrag(bool drop_accepted);

private slots:
    void onEffectDragStateChanged(bool in_progress, bool drop_accepted);

private:
    QWidget* owner_ = nullptr;
    QPointer<QDialog> dialog_;
    QAction* toggle_action_ = nullptr;
    QLineEdit* search_ = nullptr;
    QListWidget* effect_list_ = nullptr;
    QPushButton* add_button_ = nullptr;
    QLabel* entry_hint_ = nullptr;
    effects::Workspace workspace_ = effects::Workspace::Edit;
    std::function<bool(const QString&)> add_handler_;
    bool adding_ = false;
    bool effect_target_available_ = false;
    bool effect_drag_in_progress_ = false;
    std::uint64_t drag_session_generation_ = 0;
    bool suppress_owner_focus_restore_ = false;
};

}  // namespace ui
