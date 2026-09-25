#pragma once

#include <QListWidget>

class EffectsToolboxWidget final : public QListWidget {
    Q_OBJECT

public:
    explicit EffectsToolboxWidget(QWidget* parent = nullptr);

    [[nodiscard]] QString currentCategoryId() const;

signals:
    void categoryChanged(const QString& category_id);
};
