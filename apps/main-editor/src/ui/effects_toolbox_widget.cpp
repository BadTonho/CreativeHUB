#include "ui/effects_toolbox_widget.h"

#include "ui/effects_catalog.h"

#include <QAbstractItemView>
#include <QListWidgetItem>

EffectsToolboxWidget::EffectsToolboxWidget(QWidget* parent)
    : QListWidget(parent) {
    setObjectName(QStringLiteral("effectsToolbox"));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setUniformItemSizes(true);
    setMinimumWidth(150);

    for (const auto& category : effects::categories()) {
        auto* item = new QListWidgetItem(category.name, this);
        item->setData(Qt::UserRole, category.id);
    }

    connect(this, &QListWidget::currentRowChanged, this, [this](int) {
        emit categoryChanged(currentCategoryId());
    });

    if (count() > 0) setCurrentRow(0);
}

QString EffectsToolboxWidget::currentCategoryId() const {
    const auto* item = currentItem();
    return item == nullptr
        ? QStringLiteral("all")
        : item->data(Qt::UserRole).toString();
}
