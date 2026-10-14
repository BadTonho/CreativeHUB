#include "effects_favorites_widget.h"
#include "effects_catalog.h"
#include "ui/media_browser/media_drag_mime.h"
#include <QMenu>
#include <QMessageBox>

EffectsFavoritesWidget::EffectsFavoritesWidget(QWidget* parent) : QListWidget(parent) {
    setObjectName("effectsFavorites");
    setSelectionMode(QAbstractItemView::SingleSelection);
    setUniformItemSizes(true);
    setMinimumWidth(20);
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setDefaultDropAction(Qt::CopyAction);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this,&QWidget::customContextMenuRequested,this,[this](QPoint position) {
        auto* entry=itemAt(position);
        if (!entry) return;
        const auto id=entry->data(Qt::UserRole).toString();
        QMenu menu(this);auto* remove=menu.addAction("Remove from Favorites");
        if (menu.exec(viewport()->mapToGlobal(position))!=remove) return;
        if (effects::setFavorite(id,false)) { refresh();emit favoritesChanged(); }
        else QMessageBox::warning(this,"Favorites","The favorites could not be saved.");
    });
    refresh();
}
void EffectsFavoritesWidget::refresh() {
    clear();
    for (const auto& id : effects::favoriteIds()) for (const auto& d : effects::definitions()) if (d.id==id) {
        auto* entry=new QListWidgetItem(d.name,this);entry->setData(Qt::UserRole,id);break;
    }
}
QMimeData* EffectsFavoritesWidget::mimeData(const QList<QListWidgetItem*>& entries) const {
    if (entries.size()!=1 || !entries.front()) return nullptr;
    const auto id=entries.front()->data(Qt::UserRole).toString();
    return effects::isDraggable(id) ? ui::createEffectIdMimeData(id) : nullptr;
}
