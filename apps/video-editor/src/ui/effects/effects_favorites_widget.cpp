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
    connect(this, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        const auto id = item->data(Qt::UserRole).toString();
        if (isEnabled() && workspace_ == effects::Workspace::Fusion &&
            effects::isDraggable(id, workspace_)) emit creationRequested(id);
    });
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
    for (const auto& id : effects::favoriteIds()) for (const auto& d : effects::definitions()) if (d.id==id && effects::isAvailable(id, workspace_)) {
        auto* entry=new QListWidgetItem(d.name,this);entry->setData(Qt::UserRole,id);break;
    }
}
void EffectsFavoritesWidget::setWorkspace(effects::Workspace workspace) {
    if (workspace_ == workspace) return;
    workspace_ = workspace;
    refresh();
}
QMimeData* EffectsFavoritesWidget::mimeData(const QList<QListWidgetItem*>& entries) const {
    if (entries.size()!=1 || !entries.front()) return nullptr;
    const auto id=entries.front()->data(Qt::UserRole).toString();
    return isEnabled() && effects::isDraggable(id, workspace_) ? ui::createEffectIdMimeData(id) : nullptr;
}
