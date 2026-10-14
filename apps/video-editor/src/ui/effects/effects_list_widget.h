#pragma once

#include <QListWidget>
#include "ui/effects/effects_catalog.h"

class QMimeData;

class EffectsListWidget final : public QListWidget {
    Q_OBJECT

public:
    explicit EffectsListWidget(QWidget* parent = nullptr);

    void setCategory(const QString& category_id);
    void setWorkspace(effects::Workspace workspace);
    [[nodiscard]] QString categoryId() const;
    [[nodiscard]] int visibleEffectCount() const;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;

signals:
    void favoritesChanged();
    void creationRequested(const QString& id);

private:
    void updateVisibility();

    QString category_id_ = QStringLiteral("all");
    effects::Workspace workspace_ = effects::Workspace::Edit;
};
