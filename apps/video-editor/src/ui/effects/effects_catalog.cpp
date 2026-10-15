#include "ui/effects/effects_catalog.h"

#include <creative_suite/effects/effects.h>
#include <creative_suite/diagnostics/logger.h>
#include <QSettings>
#include <algorithm>

namespace effects {

bool isDraggable(const QString& id) {
    return isDraggable(id, Workspace::Edit) || isDraggable(id, Workspace::Fusion);
}

Action actionFor(const QString& id, Workspace workspace) {
    if (workspace == Workspace::None) return Action::Unavailable;
    for (const auto& definition : definitions()) {
        if (definition.id == id)
            return workspace == Workspace::Fusion
                ? definition.fusion_action : definition.edit_action;
    }
    return Action::Unavailable;
}

bool isAvailable(const QString& id, Workspace workspace) {
    return actionFor(id, workspace) != Action::Unavailable;
}

bool isDraggable(const QString& id, Workspace workspace) {
    const auto action = actionFor(id, workspace);
    return action != Action::Unavailable && action != Action::AudioEffect;
}

bool categoryAvailable(const QString& id, Workspace workspace) {
    return std::any_of(definitions().cbegin(), definitions().cend(),
        [&](const Definition& definition) {
            return isAvailable(definition.id, workspace) &&
                categoryContains(id, definition.category_id);
        });
}
QStringList favoriteIds() {
    const auto saved = QSettings().value("effects/favorites").toStringList();
    QStringList result;
    for (const auto& id : saved) {
        const bool known = std::any_of(definitions().cbegin(), definitions().cend(),[&](const auto& d){return d.id==id;});
        if (known && isDraggable(id) && !result.contains(id)) result.push_back(id);
    }
    return result;
}
bool setFavorite(const QString& id, bool enabled) {
    if (!isDraggable(id)) return false;
    auto saved = favoriteIds();
    if (enabled && !saved.contains(id)) saved.push_back(id);
    if (!enabled) saved.removeAll(id);
    QSettings settings;
    settings.setValue("effects/favorites",saved);
    settings.sync();
    if (settings.status()!=QSettings::NoError) {
        creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,
            "effects","save-favorites","Effect favorites could not be saved.",
            {{"error_code",std::to_string(int(settings.status()))},{"effect_id",id.toStdString()},
             {"settings_file",settings.fileName().toStdString()}});
        return false;
    }
    return true;
}

const QVector<Category>& categories() {
    static const QVector<Category> values{
        {QStringLiteral("all"), QStringLiteral("All")},
        {QStringLiteral("video"), QStringLiteral("Video")},
        {QStringLiteral("video.generators"), QStringLiteral("Generators"), QStringLiteral("video")},
        {QStringLiteral("audio"), QStringLiteral("Audio")},
        {QStringLiteral("transitions"), QStringLiteral("Transitions")},
        {QStringLiteral("text"), QStringLiteral("Text")},
        {QStringLiteral("fusion"), QStringLiteral("Fusion")},
        {QStringLiteral("fusion.masks"), QStringLiteral("Masks"), QStringLiteral("fusion")},
    };
    return values;
}

bool categoryContains(const QString& parent, const QString& child) {
    if (parent == QStringLiteral("all")) return true;
    auto current = child;
    // Bound traversal even if a future catalog accidentally contains a cycle.
    for (qsizetype depth = 0; depth <= categories().size() && !current.isEmpty(); ++depth) {
        if (current == parent) return true;
        QString ancestor;
        for (const auto& category : categories())
            if (category.id == current) { ancestor = category.parent_id; break; }
        current = ancestor;
    }
    return false;
}

const QVector<Definition>& definitions() {
    static const QVector<Definition> values = [] {
        QVector<Definition> result;
        for (const auto& effect : creative_suite::effects::builtInEffects()) {
            result.push_back({QString::fromUtf8(effect.id.data(),
                                                static_cast<qsizetype>(effect.id.size())),
                              QString::fromUtf8(effect.name.data(),
                                                static_cast<qsizetype>(effect.name.size())),
                              QString::fromUtf8(effect.category_id.data(),
                                                static_cast<qsizetype>(effect.category_id.size())),
                              Action::VisualEffect, Action::VisualEffect});
        }
        result.push_back({"video.gaussian_blur", "Gaussian Blur", "video", Action::Unavailable, Action::GaussianBlurNode});
        result.push_back({"video.background", "Background", "video.generators", Action::BackgroundClip, Action::BackgroundNode});
        result.push_back({"video.fusion_composition", "Fusion Composition", "video", Action::CompositionClip});
        result.push_back({"video.adjustment_clip", "Adjustment Clip", "video", Action::AdjustmentClip});
        result.push_back({"audio.gain", "Gain", "audio", Action::AudioEffect});
        result.push_back({"transitions.cross_dissolve", "Cross Dissolve", "transitions", Action::Transition});
        result.push_back({"transitions.fade_to_black", "Fade to Black", "transitions", Action::Transition});
        result.push_back({"text.text", "Text", "text", Action::TextClip, Action::TextNode});
        result.push_back({"fusion.input", "Input", "fusion", Action::Unavailable, Action::InputNode});
        result.push_back({"fusion.transform", "Transform", "fusion", Action::Unavailable, Action::TransformNode});
        result.push_back({"fusion.color", "Color", "fusion", Action::Unavailable, Action::ColorNode});
        result.push_back({"fusion.merge", "Merge", "fusion", Action::Unavailable, Action::MergeNode});
        result.push_back({"fusion.rectangle_mask", "Rectangle Mask", "fusion.masks", Action::Unavailable, Action::RectangleMaskNode});
        result.push_back({"fusion.ellipse_mask", "Ellipse Mask", "fusion.masks", Action::Unavailable, Action::EllipseMaskNode});
        return result;
    }();
    return values;
}

}  // namespace effects
