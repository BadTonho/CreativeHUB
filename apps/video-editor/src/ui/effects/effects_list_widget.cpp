#include "ui/effects/effects_list_widget.h"

#include "ui/effects/effects_catalog.h"
#include "ui/media_browser/media_drag_mime.h"

#include <creative_suite/effects/effects.h>

#include <QAbstractItemView>
#include <QMimeData>
#include <QListWidgetItem>

#include <algorithm>
#include <QMenu>
#include <QMessageBox>

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
    connect(this,&QWidget::customContextMenuRequested,this,[this](QPoint position) {
        auto* entry = itemAt(position);
        if (!entry) return;
        const auto id=entry->data(Qt::UserRole).toString();
        if (!effects::isDraggable(id)) return;
        const bool saved=effects::favoriteIds().contains(id);
        QMenu menu(this);
        auto* toggle=menu.addAction(saved ? "Remove from Favorites" : "Add to Favorites");
        if (menu.exec(viewport()->mapToGlobal(position))!=toggle) return;
        if (effects::setFavorite(id,!saved)) emit favoritesChanged();
        else QMessageBox::warning(this,"Favorites","The favorites could not be saved.");
    });
    updateVisibility();
}

void EffectsListWidget::setCategory(const QString& category_id) {
    if (category_id_ == category_id) {
        updateVisibility();
        return;
    }

    const auto known_category = std::any_of(
        effects::categories().cbegin(),
        effects::categories().cend(),
        [&category_id](const effects::Category& category) {
            return category.id == category_id;
        });
    category_id_ = known_category ? category_id : QStringLiteral("all");
    updateVisibility();
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
    if (!effects::isDraggable(effect_id)) return nullptr;

    return ui::createEffectIdMimeData(effect_id);
}

void EffectsListWidget::updateVisibility() {
    for (int index = 0; index < count(); ++index) {
        auto* item = this->item(index);
        const bool visible = effects::categoryContains(category_id_, item->data(Qt::UserRole + 1).toString());
        item->setHidden(!visible);
    }
    clearSelection();
}
