#include "media_browser_list_widget.h"

#include "media_drag_mime.h"

#include <QMimeData>
#include <QVariant>

MediaBrowserListWidget::MediaBrowserListWidget(QWidget* parent)
    : QListWidget(parent) {
    setDragEnabled(true);
    setDragDropMode(QAbstractItemView::DragOnly);
    setDefaultDropAction(Qt::CopyAction);
}

QMimeData* MediaBrowserListWidget::mimeData(
    const QList<QListWidgetItem*>& items) const {
    auto* mime_data = new QMimeData;
    if (items.isEmpty()) return mime_data;

    const QString source_path = items.front()->data(Qt::UserRole).toString();
    if (!source_path.isEmpty()) {
        mime_data->setData(ui::kMediaPathMimeType, source_path.toUtf8());
    }
    return mime_data;
}
