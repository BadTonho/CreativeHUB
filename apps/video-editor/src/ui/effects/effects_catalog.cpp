#include "ui/effects/effects_catalog.h"

#include <creative_suite/effects/effects.h>
#include <creative_suite/diagnostics/logger.h>
#include <QCoreApplication>
#include <QSettings>
#include <algorithm>

namespace effects {

QString translateCatalogLabel(const QString& source_text) {
    if (source_text == QStringLiteral("All")) return QCoreApplication::translate("EffectsCatalog", "All");
    if (source_text == QStringLiteral("Video")) return QCoreApplication::translate("EffectsCatalog", "Video");
    if (source_text == QStringLiteral("Generators")) return QCoreApplication::translate("EffectsCatalog", "Generators");
    if (source_text == QStringLiteral("Audio")) return QCoreApplication::translate("EffectsCatalog", "Audio");
    if (source_text == QStringLiteral("Transitions")) return QCoreApplication::translate("EffectsCatalog", "Transitions");
    if (source_text == QStringLiteral("Text")) return QCoreApplication::translate("EffectsCatalog", "Text");
    if (source_text == QStringLiteral("Fusion")) return QCoreApplication::translate("EffectsCatalog", "Fusion");
    if (source_text == QStringLiteral("Masks")) return QCoreApplication::translate("EffectsCatalog", "Masks");
    if (source_text == QStringLiteral("Grayscale")) return QCoreApplication::translate("EffectsCatalog", "Grayscale");
    if (source_text == QStringLiteral("Brightness")) return QCoreApplication::translate("EffectsCatalog", "Brightness");
    if (source_text == QStringLiteral("Contrast")) return QCoreApplication::translate("EffectsCatalog", "Contrast");
    if (source_text == QStringLiteral("Saturation")) return QCoreApplication::translate("EffectsCatalog", "Saturation");
    if (source_text == QStringLiteral("Amount")) return QCoreApplication::translate("EffectsCatalog", "Amount");
    if (source_text == QStringLiteral("Gaussian Blur")) return QCoreApplication::translate("EffectsCatalog", "Gaussian Blur");
    if (source_text == QStringLiteral("Background")) return QCoreApplication::translate("EffectsCatalog", "Background");
    if (source_text == QStringLiteral("Fusion Composition")) return QCoreApplication::translate("EffectsCatalog", "Fusion Composition");
    if (source_text == QStringLiteral("Adjustment Clip")) return QCoreApplication::translate("EffectsCatalog", "Adjustment Clip");
    if (source_text == QStringLiteral("Gain")) return QCoreApplication::translate("EffectsCatalog", "Gain");
    if (source_text == QStringLiteral("Cross Dissolve")) return QCoreApplication::translate("EffectsCatalog", "Cross Dissolve");
    if (source_text == QStringLiteral("Fade to Black")) return QCoreApplication::translate("EffectsCatalog", "Fade to Black");
    if (source_text == QStringLiteral("Input")) return QCoreApplication::translate("EffectsCatalog", "Input");
    if (source_text == QStringLiteral("Transform")) return QCoreApplication::translate("EffectsCatalog", "Transform");
    if (source_text == QStringLiteral("Color")) return QCoreApplication::translate("EffectsCatalog", "Color");
    if (source_text == QStringLiteral("Merge")) return QCoreApplication::translate("EffectsCatalog", "Merge");
    if (source_text == QStringLiteral("Rectangle Mask")) return QCoreApplication::translate("EffectsCatalog", "Rectangle Mask");
    if (source_text == QStringLiteral("Ellipse Mask")) return QCoreApplication::translate("EffectsCatalog", "Ellipse Mask");
    if (source_text == QStringLiteral("Effect")) return QCoreApplication::translate("EffectsCatalog", "Effect");
    if (source_text == QStringLiteral("Node")) return QCoreApplication::translate("EffectsCatalog", "Node");
    return source_text;
}

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
        {QStringLiteral("all"), translateCatalogLabel(QStringLiteral("All"))},
        {QStringLiteral("video"), translateCatalogLabel(QStringLiteral("Video"))},
        {QStringLiteral("video.generators"), translateCatalogLabel(QStringLiteral("Generators")), QStringLiteral("video")},
        {QStringLiteral("audio"), translateCatalogLabel(QStringLiteral("Audio"))},
        {QStringLiteral("transitions"), translateCatalogLabel(QStringLiteral("Transitions"))},
        {QStringLiteral("text"), translateCatalogLabel(QStringLiteral("Text"))},
        {QStringLiteral("fusion"), translateCatalogLabel(QStringLiteral("Fusion"))},
        {QStringLiteral("fusion.masks"), translateCatalogLabel(QStringLiteral("Masks")), QStringLiteral("fusion")},
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
                              translateCatalogLabel(QString::fromUtf8(effect.name.data(),
                                                static_cast<qsizetype>(effect.name.size()))),
                              QString::fromUtf8(effect.category_id.data(),
                                                static_cast<qsizetype>(effect.category_id.size())),
                              Action::VisualEffect, Action::VisualEffect});
        }
        result.push_back({"video.gaussian_blur", translateCatalogLabel(QStringLiteral("Gaussian Blur")), "video", Action::Unavailable, Action::GaussianBlurNode});
        result.push_back({"video.background", translateCatalogLabel(QStringLiteral("Background")), "video.generators", Action::BackgroundClip, Action::BackgroundNode});
        result.push_back({"video.fusion_composition", translateCatalogLabel(QStringLiteral("Fusion Composition")), "video", Action::CompositionClip});
        result.push_back({"video.adjustment_clip", translateCatalogLabel(QStringLiteral("Adjustment Clip")), "video", Action::AdjustmentClip});
        result.push_back({"audio.gain", translateCatalogLabel(QStringLiteral("Gain")), "audio", Action::AudioEffect});
        result.push_back({"transitions.cross_dissolve", translateCatalogLabel(QStringLiteral("Cross Dissolve")), "transitions", Action::Transition});
        result.push_back({"transitions.fade_to_black", translateCatalogLabel(QStringLiteral("Fade to Black")), "transitions", Action::Transition});
        result.push_back({"text.text", translateCatalogLabel(QStringLiteral("Text")), "text", Action::TextClip, Action::TextNode});
        result.push_back({"fusion.input", translateCatalogLabel(QStringLiteral("Input")), "fusion", Action::Unavailable, Action::InputNode});
        result.push_back({"fusion.transform", translateCatalogLabel(QStringLiteral("Transform")), "fusion", Action::Unavailable, Action::TransformNode});
        result.push_back({"fusion.color", translateCatalogLabel(QStringLiteral("Color")), "fusion", Action::Unavailable, Action::ColorNode});
        result.push_back({"fusion.merge", translateCatalogLabel(QStringLiteral("Merge")), "fusion", Action::Unavailable, Action::MergeNode});
        result.push_back({"fusion.rectangle_mask", translateCatalogLabel(QStringLiteral("Rectangle Mask")), "fusion.masks", Action::Unavailable, Action::RectangleMaskNode});
        result.push_back({"fusion.ellipse_mask", translateCatalogLabel(QStringLiteral("Ellipse Mask")), "fusion.masks", Action::Unavailable, Action::EllipseMaskNode});
        return result;
    }();
    return values;
}

}  // namespace effects
