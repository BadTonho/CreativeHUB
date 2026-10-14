#pragma once

#include <QListWidget>

class EffectsFavoritesWidget final : public QListWidget {
    Q_OBJECT
public:
    explicit EffectsFavoritesWidget(QWidget* parent = nullptr);
    void refresh();
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
signals:
    void favoritesChanged();
};
