#pragma once

#include <QString>
#include <QVector>
#include <QStringList>

namespace effects {

struct Category {
    QString id;
    QString name;
    QString parent_id;
};

struct Definition {
    QString id;
    QString name;
    QString category_id;
};

const QVector<Category>& categories();
const QVector<Definition>& definitions();
[[nodiscard]] bool isDraggable(const QString& id);
[[nodiscard]] QStringList favoriteIds();
[[nodiscard]] bool setFavorite(const QString& id, bool enabled);
// Includes descendants, so Video also lists its Generators subcategory.
[[nodiscard]] bool categoryContains(const QString& parent, const QString& child);

}  // namespace effects
