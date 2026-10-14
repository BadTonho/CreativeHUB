#pragma once

#include <QListWidget>
#include "ui/effects/effects_catalog.h"

class EffectsToolboxWidget final : public QListWidget {
    Q_OBJECT

public:
    explicit EffectsToolboxWidget(QWidget* parent = nullptr);
    void setWorkspace(effects::Workspace workspace);

    [[nodiscard]] QString currentCategoryId() const;

signals:
    void categoryChanged(const QString& category_id);
private:
    effects::Workspace workspace_ = effects::Workspace::None;
};
