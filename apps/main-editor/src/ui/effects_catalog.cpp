#include "ui/effects_catalog.h"

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
    static const QVector<Definition> values{
        {QStringLiteral("video.grayscale"), QStringLiteral("Grayscale"), QStringLiteral("video")},
        {QStringLiteral("audio.gain"), QStringLiteral("Gain"), QStringLiteral("audio")},
        {QStringLiteral("transitions.cross_dissolve"), QStringLiteral("Cross Dissolve"), QStringLiteral("transitions")},
        {QStringLiteral("transitions.fade_to_black"), QStringLiteral("Fade to Black"), QStringLiteral("transitions")},
        {QStringLiteral("text.text"), QStringLiteral("Text"), QStringLiteral("text")},
    };
    return values;
}

}  // namespace effects
