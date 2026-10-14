#include "ui/functions/function_palette.h"

#include "settings/shortcut_manager.h"
#include "ui/media_browser/media_drag_mime.h"

#include <QAction>
#include <QAbstractItemView>
#include <QApplication>
#include <QHBoxLayout>
#include <QDialog>
#include <QDrag>
#include <QEvent>
#include <QKeySequence>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPoint>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <functional>
#include <utility>

namespace ui {
namespace {

class FunctionEffectListWidget final : public QListWidget {
public:
    using DragStateHandler = std::function<void(bool, bool)>;

    FunctionEffectListWidget(QWidget* parent, DragStateHandler drag_state_handler)
        : QListWidget(parent), drag_state_handler_(std::move(drag_state_handler)) {
        setSelectionMode(QAbstractItemView::SingleSelection);
        setSelectionBehavior(QAbstractItemView::SelectRows);
        setUniformItemSizes(true);
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
        setDefaultDropAction(Qt::CopyAction);
    }

    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override {
        if (items.size() != 1 || items.front() == nullptr || items.front()->isHidden()) return nullptr;
        const auto effect_id = items.front()->data(Qt::UserRole).toString();
        if (!canDrag(effect_id)) return nullptr;
        return createEffectIdMimeData(effect_id);
    }

    void setContext(effects::Workspace workspace, bool target_available) {
        workspace_ = workspace;
        target_available_ = target_available;
        for (int index = 0; index < count(); ++index) {
            auto* entry = item(index);
            entry->setFlags(canDrag(entry->data(Qt::UserRole).toString())
                ? entry->flags() | Qt::ItemIsDragEnabled
                : entry->flags() & ~Qt::ItemIsDragEnabled);
        }
    }

protected:
    void startDrag(Qt::DropActions supported_actions) override {
        auto* drag_mime = mimeData(selectedItems());
        if (drag_mime == nullptr) return;

        QDrag drag(this);
        drag.setMimeData(drag_mime);
        if (drag_state_handler_) drag_state_handler_(true, false);
        const auto action = drag.exec(
            supported_actions & Qt::CopyAction, Qt::CopyAction);
        if (drag_state_handler_) {
            drag_state_handler_(false, action == Qt::CopyAction);
        }
    }

private:
    bool canDrag(const QString& id) const {
        return effects::isDraggable(id, workspace_) &&
            (workspace_ != effects::Workspace::Fusion || target_available_);
    }
    DragStateHandler drag_state_handler_;
    effects::Workspace workspace_ = effects::Workspace::Edit;
    bool target_available_ = false;
};

}  // namespace

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
        toggle_action_,
        settings::ShortcutScope::Shared,
        QStringLiteral("Edit and Fusion workspaces"));

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
    if (workspace_ == effects::Workspace::None) return;
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
    setContext(workspace_, available);
}

void FunctionPalette::setContext(effects::Workspace workspace, bool target_available) {
    const bool changed = workspace_ != workspace;
    workspace_ = workspace;
    effect_target_available_ = target_available;
    if (effect_list_ != nullptr) {
        static_cast<FunctionEffectListWidget*>(effect_list_)->setContext(workspace, target_available);
        if (changed) filterEffects(search_ == nullptr ? QString{} : search_->text());
    }
    updateAddEnabled();
    if (workspace == effects::Workspace::None && dialog_ && dialog_->isVisible()) closeDialog(false);
}

void FunctionPalette::setAddHandler(std::function<bool(const QString&)> handler) {
    add_handler_ = std::move(handler);
    updateAddEnabled();
}

bool FunctionPalette::canAddCurrentEffect() const {
    if (adding_ || !add_handler_ || !effect_target_available_ || !effect_list_) return false;
    const auto* item = effect_list_->currentItem();
    if (item == nullptr || item->isHidden()) return false;
    const auto id = item->data(Qt::UserRole).toString();
    return workspace_ == effects::Workspace::Fusion
        ? effects::isDraggable(id, workspace_)
        : effects::actionFor(id, workspace_) == effects::Action::VisualEffect;
}

void FunctionPalette::updateAddEnabled() {
    if (add_button_ == nullptr || effect_list_ == nullptr) return;
    add_button_->setEnabled(canAddCurrentEffect());
    if (entry_hint_ == nullptr) return;
    QString hint;
    const auto* item = effect_list_->currentItem();
    if (item != nullptr && !item->isHidden()) {
        const auto id = item->data(Qt::UserRole).toString();
        if (!effects::isDraggable(id, workspace_)) hint = "This item is not available.";
        else if (workspace_ == effects::Workspace::Edit &&
                 effects::actionFor(id, workspace_) != effects::Action::VisualEffect)
            hint = "Drag this item to the timeline.";
        else if (!effect_target_available_) hint = "Select a compatible clip to add this item.";
    }
    entry_hint_->setText(hint);
    entry_hint_->setVisible(!hint.isEmpty());
}

void FunctionPalette::filterEffects(const QString& query) {
    if (effect_list_ == nullptr) return;
    QListWidgetItem* first_visible = nullptr;
    auto* previous = effect_list_->currentItem();
    for (int index = 0; index < effect_list_->count(); ++index) {
        auto* item = effect_list_->item(index);
        const bool visible = effects::isAvailable(item->data(Qt::UserRole).toString(), workspace_) &&
            item->text().contains(query, Qt::CaseInsensitive);
        item->setHidden(!visible);
        if (visible && first_visible == nullptr) first_visible = item;
    }
    if (previous != nullptr && !previous->isHidden()) effect_list_->setCurrentItem(previous);
    else if (first_visible != nullptr) effect_list_->setCurrentItem(first_visible);
    else effect_list_->setCurrentRow(-1);
    updateAddEnabled();
}

void FunctionPalette::addCurrentEffect() {
    if (!canAddCurrentEffect()) return;
    const auto effect_id = effect_list_->currentItem()->data(Qt::UserRole).toString();
    const QPointer<QDialog> active_dialog = dialog_;
    bool accepted = false;
    {
        const QScopedValueRollback<bool> guard(adding_, true);
        accepted = add_handler_(effect_id);
    }
    if (accepted && dialog_ == active_dialog) closeDialog(true);
    else updateAddEnabled();
}

bool FunctionPalette::eventFilter(QObject* watched, QEvent* event) {
    if (dialog_ == nullptr || !dialog_->isVisible()) {
        return QObject::eventFilter(watched, event);
    }
    if (effect_drag_in_progress_ || adding_) {
        return QObject::eventFilter(watched, event);
    }
    if (watched == effect_list_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            addCurrentEffect();
            return true;
        }
    }

    if (watched == dialog_ && event->type() == QEvent::WindowDeactivate) {
        const QPointer<QDialog> deactivated_dialog = dialog_;
        const auto drag_generation = drag_session_generation_;
        QTimer::singleShot(0, this, [this, deactivated_dialog,
                                     drag_generation]() {
            if (deactivated_dialog == nullptr ||
                dialog_ != deactivated_dialog ||
                !deactivated_dialog->isVisible()) {
                return;
            }
            if (effect_drag_in_progress_ ||
                drag_session_generation_ != drag_generation) {
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

void FunctionPalette::setEffectDragInProgress(bool in_progress) {
    effect_drag_in_progress_ = in_progress;
    ++drag_session_generation_;
}

void FunctionPalette::finishEffectDrag(bool drop_accepted) {
    setEffectDragInProgress(false);
    if (dialog_ == nullptr || !dialog_->isVisible()) return;
    if (drop_accepted) {
        closeDialog(true);
        return;
    }
    dialog_->raise();
    dialog_->activateWindow();
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
    search_->setPlaceholderText(QStringLiteral("Search effects, generators and nodes"));
    layout->addWidget(search_);
    effect_list_ = new FunctionEffectListWidget(dialog_,
        [this](bool in_progress, bool drop_accepted) {
            onEffectDragStateChanged(in_progress, drop_accepted);
        });
    effect_list_->setObjectName(QStringLiteral("functionsEffectList"));
    for (const auto& effect : effects::definitions()) {
        auto* item = new QListWidgetItem(effect.name, effect_list_);
        item->setData(Qt::UserRole, effect.id);
    }
    layout->addWidget(effect_list_, 1);
    entry_hint_ = new QLabel(dialog_);
    entry_hint_->setObjectName("functionsEntryHint");
    entry_hint_->setWordWrap(true);
    layout->addWidget(entry_hint_);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* cancel_button = new QPushButton(QStringLiteral("Cancel"), dialog_);
    cancel_button->setAutoDefault(false);
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
    static_cast<FunctionEffectListWidget*>(effect_list_)->setContext(workspace_, effect_target_available_);
    filterEffects(QString{});
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
            entry_hint_ = nullptr;
            if (!suppress_owner_focus_restore_) restoreOwnerFocus();
            suppress_owner_focus_restore_ = false;
        });
}

void FunctionPalette::onEffectDragStateChanged(
    bool in_progress, bool drop_accepted) {
    if (in_progress) setEffectDragInProgress(true);
    else finishEffectDrag(drop_accepted);
}

void FunctionPalette::restoreOwnerFocus() {
    if (owner_ == nullptr) return;
    owner_->raise();
    owner_->activateWindow();
}

}  // namespace ui
