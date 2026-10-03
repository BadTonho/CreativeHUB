#include "ui/effects/effects_catalog.h"

#include <creative_suite/effects/effects.h>

namespace effects {

const QVector<Category>& categories() {
    static const QVector<Category> values{
        {QStringLiteral("all"), QStringLiteral("All")},
        {QStringLiteral("video"), QStringLiteral("Video")},
        {QStringLiteral("audio"), QStringLiteral("Audio")},
        {QStringLiteral("transitions"), QStringLiteral("Transitions")},
        {QStringLiteral("text"), QStringLiteral("Text")},
    };
    return values;
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
        result.push_back({QStringLiteral("audio.gain"), QStringLiteral("Gain"), QStringLiteral("audio")});
        result.push_back({QStringLiteral("transitions.cross_dissolve"), QStringLiteral("Cross Dissolve"), QStringLiteral("transitions")});
        result.push_back({QStringLiteral("transitions.fade_to_black"), QStringLiteral("Fade to Black"), QStringLiteral("transitions")});
        result.push_back({QStringLiteral("text.text"), QStringLiteral("Text"), QStringLiteral("text")});
        return result;
    }();
    return values;
}

}  // namespace effects
