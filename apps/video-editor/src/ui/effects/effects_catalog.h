#pragma once

#include <QString>
#include <QVector>
#include <QStringList>

namespace effects {

enum class Workspace { Edit, Fusion, None };

// UI actions route creation to the workspace that owns the corresponding model.
enum class Action {
    Unavailable, VisualEffect, AudioEffect, BackgroundClip, CompositionClip,
    AdjustmentClip, TextClip, Transition,
    InputNode, TransformNode, ColorNode, MergeNode, BackgroundNode, TextNode,
    RectangleMaskNode, EllipseMaskNode, GaussianBlurNode,
};

struct Category {
    QString id;
    QString name;
    QString parent_id;
};

struct Definition {
    QString id;
    QString name;
    QString category_id;
    Action edit_action = Action::Unavailable;
    Action fusion_action = Action::Unavailable;
};

const QVector<Category>& categories();
const QVector<Definition>& definitions();
[[nodiscard]] QString translateCatalogLabel(const QString& source_text);
[[nodiscard]] bool isDraggable(const QString& id);
[[nodiscard]] Action actionFor(const QString& id, Workspace workspace);
[[nodiscard]] bool isAvailable(const QString& id, Workspace workspace);
[[nodiscard]] bool isDraggable(const QString& id, Workspace workspace);
[[nodiscard]] bool categoryAvailable(const QString& id, Workspace workspace);
[[nodiscard]] QStringList favoriteIds();
[[nodiscard]] bool setFavorite(const QString& id, bool enabled);
// Includes descendants, so Video also lists its Generators subcategory.
[[nodiscard]] bool categoryContains(const QString& parent, const QString& child);

}  // namespace effects
