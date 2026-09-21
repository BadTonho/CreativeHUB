#pragma once

#include <QListWidget>

class QMimeData;

class EffectsListWidget final : public QListWidget {
    Q_OBJECT

public:
    explicit EffectsListWidget(QWidget* parent = nullptr);

    void setCategory(const QString& category_id);
    [[nodiscard]] QString categoryId() const;
    [[nodiscard]] int visibleEffectCount() const;
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;

private:
    void updateVisibility();

    QString category_id_ = QStringLiteral("all");
};
