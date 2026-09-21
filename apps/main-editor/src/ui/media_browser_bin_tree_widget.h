#pragma once

#include <QTreeWidget>

class QDragMoveEvent;
class QDragEnterEvent;
class QDropEvent;
class QMimeData;
class QModelIndex;
class QPainter;

class MediaBrowserBinTreeWidget final : public QTreeWidget {
    Q_OBJECT

public:
    explicit MediaBrowserBinTreeWidget(QWidget* parent = nullptr);

signals:
    void mediaDropRequested(
        const QString& sourcePath,
        const QString& destinationBin);
    void binDropRequested(
        const QString& sourceBin,
        const QString& destinationBin);

protected:
    [[nodiscard]] QMimeData* mimeData(
        const QList<QTreeWidgetItem*>& items) const override;
    void drawBranches(
        QPainter* painter,
        const QRect& rect,
        const QModelIndex& index) const override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    [[nodiscard]] QString dropTargetPath(const QPoint& position) const;
    [[nodiscard]] bool isValidBinDrop(
        const QString& sourceBin,
        const QString& destinationBin) const;
    [[nodiscard]] bool acceptsDrop(
        const QMimeData* mimeData,
        const QPoint& position) const;
};
