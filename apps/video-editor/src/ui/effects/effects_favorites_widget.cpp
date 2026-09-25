#include "ui/effects/effects_favorites_widget.h"

#include <QAbstractItemView>

EffectsFavoritesWidget::EffectsFavoritesWidget(QWidget* parent)
    : QListWidget(parent) {
    setObjectName(QStringLiteral("effectsFavorites"));
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setUniformItemSizes(true);
    setMinimumWidth(20);
}
