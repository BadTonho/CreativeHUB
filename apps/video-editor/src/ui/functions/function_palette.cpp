#include "ui/functions/function_palette.h"

#include "settings/shortcut_manager.h"

#include <creative_suite/effects/effects.h>

#include <QAction>
#include <QApplication>
#include <QHBoxLayout>
#include <QDialog>
#include <QEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPoint>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
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
    if (search_ != nullptr) {
        search_->setFocus(Qt::ShortcutFocusReason);
        search_->selectAll();
    }
}

void FunctionPalette::setEffectTargetAvailable(bool available) {
    if (effect_target_available_ == available) return;
    effect_target_available_ = available;
    updateAddEnabled();
}

void FunctionPalette::updateAddEnabled() {
    if (add_button_ == nullptr || effect_list_ == nullptr) return;
    add_button_->setEnabled(effect_target_available_ &&
        effect_list_->currentItem() != nullptr &&
        !effect_list_->currentItem()->isHidden());
}

void FunctionPalette::filterEffects(const QString& query) {
    if (effect_list_ == nullptr) return;
    QListWidgetItem* first_visible = nullptr;
    for (int index = 0; index < effect_list_->count(); ++index) {
        auto* item = effect_list_->item(index);
        const bool visible = item->text().contains(query, Qt::CaseInsensitive);
        item->setHidden(!visible);
        if (visible && first_visible == nullptr) first_visible = item;
    }
    if (first_visible != nullptr) effect_list_->setCurrentItem(first_visible);
    else effect_list_->setCurrentRow(-1);
    updateAddEnabled();
}

void FunctionPalette::addCurrentEffect() {
    if (!effect_target_available_ || effect_list_ == nullptr ||
        effect_list_->currentItem() == nullptr ||
        effect_list_->currentItem()->isHidden()) return;
    const auto effect_id = effect_list_->currentItem()->data(Qt::UserRole).toString();
    if (effect_id.isEmpty()) return;
    emit effectAddRequested(effect_id);
    closeDialog(true);
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
    dialog_->resize(420, 360);
    dialog_->addAction(toggle_action_);
    auto* layout = new QVBoxLayout(dialog_);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(7);
    search_ = new QLineEdit(dialog_);
    search_->setObjectName(QStringLiteral("functionsEffectSearch"));
    search_->setPlaceholderText(QStringLiteral("Search effects"));
    layout->addWidget(search_);
    effect_list_ = new QListWidget(dialog_);
    effect_list_->setObjectName(QStringLiteral("functionsEffectList"));
    for (const auto& effect : creative_suite::effects::builtInEffects()) {
        auto* item = new QListWidgetItem(
            QString::fromUtf8(effect.name.data(),
                              static_cast<qsizetype>(effect.name.size())),
            effect_list_);
        item->setData(Qt::UserRole,
            QString::fromUtf8(effect.id.data(),
                              static_cast<qsizetype>(effect.id.size())));
    }
    layout->addWidget(effect_list_, 1);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel_button = new QPushButton(QStringLiteral("Cancel"), dialog_);
    cancel_button->setObjectName(QStringLiteral("functionsCancelButton"));
    add_button_ = new QPushButton(QStringLiteral("Add"), dialog_);
    add_button_->setObjectName(QStringLiteral("functionsAddButton"));
    buttons->addWidget(cancel_button);
    buttons->addWidget(add_button_);
    layout->addLayout(buttons);
    connect(search_, &QLineEdit::textChanged,
            this, &FunctionPalette::filterEffects);
    connect(search_, &QLineEdit::returnPressed,
            this, &FunctionPalette::addCurrentEffect);
    connect(effect_list_, &QListWidget::currentRowChanged,
            this, [this](int) { updateAddEnabled(); });
    connect(effect_list_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) { addCurrentEffect(); });
    connect(add_button_, &QPushButton::clicked,
            this, &FunctionPalette::addCurrentEffect);
    connect(cancel_button, &QPushButton::clicked,
            this, [this]() { closeDialog(true); });
    if (effect_list_->count() > 0) effect_list_->setCurrentRow(0);
    updateAddEnabled();
    connect(
        dialog_,
        &QDialog::rejected,
        this,
        [this]() {
            removeDismissFilters();
            dialog_.clear();
            search_ = nullptr;
            effect_list_ = nullptr;
            add_button_ = nullptr;
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
