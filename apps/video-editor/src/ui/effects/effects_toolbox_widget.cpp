#include "ui/effects/effects_toolbox_widget.h"

#include "ui/effects/effects_catalog.h"

#include <QAbstractItemView>
#include <QListWidgetItem>
#include <QSignalBlocker>

EffectsToolboxWidget::EffectsToolboxWidget(QWidget* parent)
    : QListWidget(parent) {
    setObjectName(QStringLiteral("effectsToolbox"));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setUniformItemSizes(true);
    setMinimumWidth(20);

    connect(this, &QListWidget::currentRowChanged, this, [this](int) {
        emit categoryChanged(currentCategoryId());
    });

    setWorkspace(effects::Workspace::Edit);
}

void EffectsToolboxWidget::setWorkspace(effects::Workspace workspace) {
    if (workspace_ == workspace) return;
    workspace_ = workspace;
    const auto previous = currentCategoryId();
    {
        const QSignalBlocker blocker(this);
        clear();
        int selected = 0;
        for (const auto& category : effects::categories()) {
            if (!effects::categoryAvailable(category.id, workspace)) continue;
            auto* item = new QListWidgetItem(category.parent_id.isEmpty()
                ? category.name : QStringLiteral("    ") + category.name, this);
            item->setData(Qt::UserRole, category.id);
            if (category.id == previous) selected = count() - 1;
        }
        if (count() > 0) setCurrentRow(selected);
    }
    emit categoryChanged(currentCategoryId());
}

QString EffectsToolboxWidget::currentCategoryId() const {
    const auto* item = currentItem();
    return item == nullptr
        ? QStringLiteral("all")
        : item->data(Qt::UserRole).toString();
}
