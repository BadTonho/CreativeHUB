#include "media_browser_bin_tree_widget.h"

#include "media_drag_mime.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>

namespace {

bool hasBinPrefix(const QString& value, const QString& prefix) {
    return value.size() > prefix.size() &&
        value.startsWith(prefix) &&
        value.at(prefix.size()) == QLatin1Char('/');
}

} // namespace

MediaBrowserBinTreeWidget::MediaBrowserBinTreeWidget(QWidget* parent)
    : QTreeWidget(parent) {
    setDragEnabled(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(true);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setDragDropOverwriteMode(false);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setAutoExpandDelay(500);
}

QMimeData* MediaBrowserBinTreeWidget::mimeData(
    const QList<QTreeWidgetItem*>& items) const {
    auto* mime_data = new QMimeData;
    if (items.isEmpty()) return mime_data;

    const QString bin_path = items.front()->data(0, Qt::UserRole).toString();
    if (!bin_path.isEmpty()) {
        mime_data->setData(
            ui::kMediaBinPathMimeType,
            bin_path.toUtf8());
    }
    return mime_data;
}

void MediaBrowserBinTreeWidget::dragEnterEvent(QDragEnterEvent* event) {
    if (event != nullptr && event->mimeData() != nullptr &&
        (event->mimeData()->hasFormat(ui::kMediaPathMimeType) ||
         event->mimeData()->hasFormat(ui::kMediaBinPathMimeType))) {
        event->acceptProposedAction();
        return;
    }
    if (event != nullptr) event->ignore();
}

void MediaBrowserBinTreeWidget::dragMoveEvent(QDragMoveEvent* event) {
    if (event != nullptr && acceptsDrop(event->mimeData(), event->position().toPoint())) {
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }
    if (event != nullptr) event->ignore();
}

void MediaBrowserBinTreeWidget::dropEvent(QDropEvent* event) {
    if (event == nullptr) return;

    if (!acceptsDrop(event->mimeData(), event->position().toPoint())) {
        event->ignore();
        return;
    }

    const QString destination_bin = dropTargetPath(event->position().toPoint());
    const auto* drop_data = event->mimeData();
    if (drop_data->hasFormat(ui::kMediaPathMimeType)) {
        const QString source_path = QString::fromUtf8(
            drop_data->data(ui::kMediaPathMimeType));
        if (!source_path.isEmpty()) {
            emit mediaDropRequested(source_path, destination_bin);
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }
    }
    if (drop_data->hasFormat(ui::kMediaBinPathMimeType)) {
        const QString source_bin = QString::fromUtf8(
            drop_data->data(ui::kMediaBinPathMimeType));
        if (!source_bin.isEmpty()) {
            emit binDropRequested(source_bin, destination_bin);
            event->setDropAction(Qt::MoveAction);
            event->accept();
            return;
        }
    }

    event->ignore();
}

QString MediaBrowserBinTreeWidget::dropTargetPath(const QPoint& position) const {
    const auto* item = itemAt(position);
    if (item == nullptr) return {};
    return item->data(0, Qt::UserRole).toString();
}

bool MediaBrowserBinTreeWidget::isValidBinDrop(
    const QString& sourceBin,
    const QString& destinationBin) const {
    if (sourceBin.isEmpty() || destinationBin.isEmpty() ||
        sourceBin == QLatin1String("Unsorted") ||
        sourceBin == destinationBin ||
        hasBinPrefix(destinationBin, sourceBin)) {
        return false;
    }

    const int separator = sourceBin.lastIndexOf(QLatin1Char('/'));
    const QString leaf = sourceBin.mid(separator + 1);
    const QString newPath = destinationBin + QLatin1Char('/') + leaf;
    if (newPath == sourceBin || hasBinPrefix(newPath, sourceBin)) return false;

    for (int index = 0; index < topLevelItemCount(); ++index) {
        auto* root = topLevelItem(index);
        QList<QTreeWidgetItem*> pending{root};
        while (!pending.isEmpty()) {
            auto* item = pending.takeLast();
            if (item->data(0, Qt::UserRole).toString() == newPath) return false;
            for (int child = 0; child < item->childCount(); ++child) {
                pending.push_back(item->child(child));
            }
        }
    }
    return true;
}

bool MediaBrowserBinTreeWidget::acceptsDrop(
    const QMimeData* mimeData,
    const QPoint& position) const {
    if (mimeData == nullptr || dropTargetPath(position).isEmpty()) return false;
    if (mimeData->hasFormat(ui::kMediaPathMimeType)) return true;
    if (!mimeData->hasFormat(ui::kMediaBinPathMimeType)) return false;

    const QString source_bin = QString::fromUtf8(
        mimeData->data(ui::kMediaBinPathMimeType));
    return isValidBinDrop(source_bin, dropTargetPath(position));
}
