#pragma once

#include <QListWidget>

class EffectsListWidget final : public QListWidget {
    Q_OBJECT

public:
    explicit EffectsListWidget(QWidget* parent = nullptr);

    void setCategory(const QString& category_id);
    [[nodiscard]] QString categoryId() const;
    [[nodiscard]] int visibleEffectCount() const;

private:
    void updateVisibility();

    QString category_id_ = QStringLiteral("all");
};
