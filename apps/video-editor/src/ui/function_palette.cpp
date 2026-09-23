#include "ui/function_palette.h"

#include "settings/shortcut_manager.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QEvent>
#include <QKeySequence>
#include <QMouseEvent>
#include <QPoint>
#include <QTimer>
#include <QWidget>

namespace ui {

FunctionPalette::FunctionPalette(
    QWidget* owner,
    settings::ShortcutManager& shortcut_manager)
    : QObject(owner), owner_(owner) {
    toggle_action_ = new QAction(
        QStringLiteral("Open Functions Window"), owner_);
    toggle_action_->setObjectName(
        QStringLiteral("toggleFunctionsWindowAction"));
    toggle_action_->setShortcut(QKeySequence(QStringLiteral("Shift+Space")));
    toggle_action_->setShortcutContext(Qt::WindowShortcut);
    owner_->addAction(toggle_action_);
    shortcut_manager.registerAction(
        QStringLiteral("workspace.functions_window"),
        QStringLiteral("Open Functions Window"),
        toggle_action_);

    connect(
        toggle_action_,
        &QAction::triggered,
        this,
        [this]() { toggle(); });

    createDialog();
}

FunctionPalette::~FunctionPalette() {
    removeDismissFilters();
}

QDialog* FunctionPalette::dialog() const noexcept {
    return dialog_.data();
}

QAction* FunctionPalette::toggleAction() const noexcept {
    return toggle_action_;
}

void FunctionPalette::toggle() {
    if (dialog_ != nullptr && dialog_->isVisible()) {
        closeDialog(true);
        return;
    }

    if (dialog_ == nullptr) createDialog();
    const QPoint position = owner_->frameGeometry().center() -
        QPoint(dialog_->width() / 2, dialog_->height() / 2);
    dialog_->move(position);
    dialog_->installEventFilter(this);
    if (qApp != nullptr) qApp->installEventFilter(this);
    dialog_->show();
    dialog_->raise();
    dialog_->activateWindow();
}

bool FunctionPalette::eventFilter(QObject* watched, QEvent* event) {
    if (dialog_ == nullptr || !dialog_->isVisible()) {
        return QObject::eventFilter(watched, event);
    }

    if (watched == dialog_ && event->type() == QEvent::WindowDeactivate) {
        const QPointer<QDialog> deactivated_dialog = dialog_;
        QTimer::singleShot(0, this, [this, deactivated_dialog]() {
            if (deactivated_dialog == nullptr ||
                dialog_ != deactivated_dialog ||
                !deactivated_dialog->isVisible()) {
                return;
            }
            closeDialog(QApplication::activeWindow() == owner_);
        });
    } else if (event->type() == QEvent::MouseButtonPress) {
        const auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (!dialog_->frameGeometry().contains(
                mouse_event->globalPosition().toPoint())) {
            auto* clicked_widget = qobject_cast<QWidget*>(watched);
            const bool restore_owner_focus = clicked_widget != nullptr &&
                (clicked_widget == owner_ || owner_->isAncestorOf(clicked_widget));
            closeDialog(restore_owner_focus);
        }
    }
    return QObject::eventFilter(watched, event);
}

void FunctionPalette::removeDismissFilters() {
    if (dialog_ != nullptr) dialog_->removeEventFilter(this);
    if (qApp != nullptr) qApp->removeEventFilter(this);
}

void FunctionPalette::closeDialog(bool restore_owner_focus) {
    if (dialog_ == nullptr) return;
    removeDismissFilters();
    suppress_owner_focus_restore_ = !restore_owner_focus;
    dialog_->close();
}

void FunctionPalette::createDialog() {
    dialog_ = new QDialog(owner_);
    dialog_->setObjectName(QStringLiteral("functionsWindow"));
    dialog_->setWindowTitle(QStringLiteral("Functions"));
    dialog_->setWindowFlag(Qt::Tool, true);
    dialog_->setWindowModality(Qt::NonModal);
    dialog_->setModal(false);
    dialog_->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog_->resize(420, 320);
    dialog_->addAction(toggle_action_);
    connect(
        dialog_,
        &QDialog::rejected,
        this,
        [this]() {
            removeDismissFilters();
            dialog_.clear();
            if (!suppress_owner_focus_restore_) restoreOwnerFocus();
            suppress_owner_focus_restore_ = false;
        });
}

void FunctionPalette::restoreOwnerFocus() {
    if (owner_ == nullptr) return;
    owner_->raise();
    owner_->activateWindow();
}

}  // namespace ui
