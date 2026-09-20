#pragma once

#include <QListWidget>

class QMimeData;

class MediaBrowserListWidget final : public QListWidget {
public:
    explicit MediaBrowserListWidget(QWidget* parent = nullptr);

protected:
    [[nodiscard]] QMimeData* mimeData(
        const QList<QListWidgetItem*>& items) const override;
};
