#include "ui/effects/effects_list_widget.h"

#include "ui/effects/effects_catalog.h"
#include "ui/media_browser/media_drag_mime.h"

#include <QAbstractItemView>
#include <QMimeData>
#include <QListWidgetItem>

#include <QMenu>
#include <QMessageBox>
#include <QCoreApplication>

EffectsListWidget::EffectsListWidget(QWidget* parent)
    : QListWidget(parent) {
    setObjectName(QStringLiteral("effectsList"));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setUniformItemSizes(true);
    setMinimumWidth(30);
    setAlternatingRowColors(true);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setDefaultDropAction(Qt::CopyAction);

    for (const auto& effect : effects::definitions()) {
        auto* item = new QListWidgetItem(effect.name, this);
        item->setData(Qt::UserRole, effect.id);
        item->setData(Qt::UserRole + 1, effect.category_id);
        if (!effects::isDraggable(effect.id)) {
            item->setFlags(item->flags() & ~Qt::ItemIsDragEnabled);
        }
    }

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const auto id = item->data(Qt::UserRole).toString();
        if (isEnabled() && !item->isHidden() && workspace_ == effects::Workspace::Fusion &&
            effects::isDraggable(id, workspace_)) emit creationRequested(id);
    });
    connect(this,&QWidget::customContextMenuRequested,this,[this](QPoint position) {
        auto* entry = itemAt(position);
        if (!entry) return;
        const auto id=entry->data(Qt::UserRole).toString();
        if (!effects::isDraggable(id)) return;
        const bool saved=effects::favoriteIds().contains(id);
        QMenu menu(this);
        auto* toggle=menu.addAction(saved ? QCoreApplication::translate("EffectsListWidget", "Remove from Favorites") : QCoreApplication::translate("EffectsListWidget", "Add to Favorites"));
        if (menu.exec(viewport()->mapToGlobal(position))!=toggle) return;
        if (effects::setFavorite(id,!saved)) emit favoritesChanged();
        else QMessageBox::warning(this,QCoreApplication::translate("EffectsListWidget", "Favorites"),QCoreApplication::translate("EffectsListWidget", "The favorites could not be saved."));
    });
    updateVisibility();
}

void EffectsListWidget::setCategory(const QString& category_id) {
    const auto known_category = effects::categoryAvailable(category_id, workspace_);
    category_id_ = known_category ? category_id : QStringLiteral("all");
    updateVisibility();
}

void EffectsListWidget::setWorkspace(effects::Workspace workspace) {
    if (workspace_ == workspace) return;
    workspace_ = workspace;
    setCategory(category_id_);
}

QString EffectsListWidget::categoryId() const {
    return category_id_;
}

int EffectsListWidget::visibleEffectCount() const {
    int visible_count = 0;
    for (int index = 0; index < count(); ++index) {
        if (!item(index)->isHidden()) ++visible_count;
    }
    return visible_count;
}

QMimeData* EffectsListWidget::mimeData(
    const QList<QListWidgetItem*>& items) const {
    if (items.size() != 1 || items.front() == nullptr) {
        return nullptr;
    }
    const auto effect_id = items.front()->data(Qt::UserRole).toString();
    if (!isEnabled() || items.front()->isHidden() ||
        !effects::isDraggable(effect_id, workspace_)) return nullptr;

    return ui::createEffectIdMimeData(effect_id);
}

void EffectsListWidget::updateVisibility() {
    for (int index = 0; index < count(); ++index) {
        auto* item = this->item(index);
        const auto id = item->data(Qt::UserRole).toString();
        const bool visible = effects::isAvailable(id, workspace_) &&
            effects::categoryContains(category_id_, item->data(Qt::UserRole + 1).toString());
        item->setHidden(!visible);
        item->setFlags(effects::isDraggable(id, workspace_)
            ? item->flags() | Qt::ItemIsDragEnabled
            : item->flags() & ~Qt::ItemIsDragEnabled);
    }
    clearSelection();
}
