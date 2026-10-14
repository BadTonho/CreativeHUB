#include "ui/effects/effects_catalog.h"

#include <creative_suite/effects/effects.h>
#include <creative_suite/diagnostics/logger.h>
#include <QSettings>
#include <algorithm>

namespace effects {

bool isDraggable(const QString& id) {
    const auto bytes = id.toUtf8();
    return creative_suite::effects::findDefinition(std::string_view(bytes.constData(),bytes.size())) != nullptr ||
        id == "video.background" || id == "video.fusion_composition" || id == "video.adjustment_clip" ||
        id == "text.text" || id == "transitions.cross_dissolve" || id == "transitions.fade_to_black";
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
                                                static_cast<qsizetype>(effect.category_id.size()))});
        }
        result.push_back({QStringLiteral("video.background"), QStringLiteral("Background"), QStringLiteral("video.generators")});
        result.push_back({QStringLiteral("video.fusion_composition"), QStringLiteral("Fusion Composition"), QStringLiteral("video")});
        result.push_back({QStringLiteral("video.adjustment_clip"), QStringLiteral("Adjustment Clip"), QStringLiteral("video")});
        result.push_back({QStringLiteral("audio.gain"), QStringLiteral("Gain"), QStringLiteral("audio")});
        result.push_back({QStringLiteral("transitions.cross_dissolve"), QStringLiteral("Cross Dissolve"), QStringLiteral("transitions")});
        result.push_back({QStringLiteral("transitions.fade_to_black"), QStringLiteral("Fade to Black"), QStringLiteral("transitions")});
        result.push_back({QStringLiteral("text.text"), QStringLiteral("Text"), QStringLiteral("text")});
        return result;
    }();
    return values;
}

}  // namespace effects
