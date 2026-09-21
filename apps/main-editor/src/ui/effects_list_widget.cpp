#include "ui/effects_list_widget.h"

#include "ui/effects_catalog.h"

#include <QAbstractItemView>
#include <QListWidgetItem>

#include <algorithm>

EffectsListWidget::EffectsListWidget(QWidget* parent)
    : QListWidget(parent) {
    setObjectName(QStringLiteral("effectsList"));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setUniformItemSizes(true);
    setMinimumWidth(30);
    setAlternatingRowColors(true);

    for (const auto& effect : effects::definitions()) {
        auto* item = new QListWidgetItem(effect.name, this);
        item->setData(Qt::UserRole, effect.id);
        item->setData(Qt::UserRole + 1, effect.category_id);
    }

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

void EffectsListWidget::updateVisibility() {
    for (int index = 0; index < count(); ++index) {
        auto* item = this->item(index);
        const bool visible = category_id_ == QStringLiteral("all") ||
            item->data(Qt::UserRole + 1).toString() == category_id_;
        item->setHidden(!visible);
    }
    clearSelection();
}
