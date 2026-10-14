#pragma once

#include <QListWidget>
#include "ui/effects/effects_catalog.h"

class EffectsFavoritesWidget final : public QListWidget {
    Q_OBJECT
public:
    explicit EffectsFavoritesWidget(QWidget* parent = nullptr);
    void refresh();
    void setWorkspace(effects::Workspace workspace);
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
signals:
    void favoritesChanged();
    void creationRequested(const QString& id);
private:
    effects::Workspace workspace_ = effects::Workspace::Edit;
};
