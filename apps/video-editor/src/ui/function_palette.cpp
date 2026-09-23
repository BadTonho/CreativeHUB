#include "ui/function_palette.h"

#include "settings/shortcut_manager.h"

#include <QAction>
#include <QDialog>
#include <QKeySequence>
#include <QPoint>
#include <QWidget>

namespace ui {

FunctionPalette::FunctionPalette(
    QWidget* owner,
    settings::ShortcutManager& shortcut_manager)
    : QObject(owner), owner_(owner), dialog_(new QDialog(owner)) {
    dialog_->setObjectName(QStringLiteral("functionsWindow"));
    dialog_->setWindowTitle(QStringLiteral("Functions"));
    dialog_->setWindowFlag(Qt::Tool, true);
    dialog_->setWindowModality(Qt::NonModal);
    dialog_->setModal(false);
    dialog_->setAttribute(Qt::WA_DeleteOnClose, false);
    dialog_->resize(420, 320);
    connect(
        dialog_,
        &QDialog::rejected,
        this,
        [this]() { restoreOwnerFocus(); });

    toggle_action_ = new QAction(
        QStringLiteral("Open Functions Window"), owner_);
    toggle_action_->setObjectName(
        QStringLiteral("toggleFunctionsWindowAction"));
    toggle_action_->setShortcut(QKeySequence(QStringLiteral("Shift+Space")));
    toggle_action_->setShortcutContext(Qt::WindowShortcut);
    owner_->addAction(toggle_action_);
    dialog_->addAction(toggle_action_);
    shortcut_manager.registerAction(
        QStringLiteral("workspace.functions_window"),
        QStringLiteral("Open Functions Window"),
        toggle_action_);

    connect(
        toggle_action_,
        &QAction::triggered,
        this,
        [this]() { toggle(); });
}

QDialog* FunctionPalette::dialog() const noexcept {
    return dialog_;
}

QAction* FunctionPalette::toggleAction() const noexcept {
    return toggle_action_;
}

void FunctionPalette::toggle() {
    if (dialog_->isVisible()) {
        dialog_->hide();
        restoreOwnerFocus();
        return;
    }

    const QPoint position = owner_->frameGeometry().center() -
        QPoint(dialog_->width() / 2, dialog_->height() / 2);
    dialog_->move(position);
    dialog_->show();
    dialog_->raise();
    dialog_->activateWindow();
}

void FunctionPalette::restoreOwnerFocus() {
    if (owner_ == nullptr) return;
    owner_->raise();
    owner_->activateWindow();
}

}  // namespace ui
