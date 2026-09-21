#include "ui/effects_catalog.h"

namespace effects {

const QVector<Category>& categories() {
    static const QVector<Category> values{
        {QStringLiteral("all"), QStringLiteral("All")},
        {QStringLiteral("video"), QStringLiteral("Video")},
        {QStringLiteral("audio"), QStringLiteral("Audio")},
        {QStringLiteral("transitions"), QStringLiteral("Transitions")},
        {QStringLiteral("generators"), QStringLiteral("Generators")},
        {QStringLiteral("text"), QStringLiteral("Text")},
    };
    return values;
}

const QVector<Definition>& definitions() {
    static const QVector<Definition> values{
        {QStringLiteral("video.blur"), QStringLiteral("Blur"), QStringLiteral("video")},
        {QStringLiteral("video.sharpen"), QStringLiteral("Sharpen"), QStringLiteral("video")},
        {QStringLiteral("video.brightness_contrast"), QStringLiteral("Brightness / Contrast"), QStringLiteral("video")},
        {QStringLiteral("video.saturation"), QStringLiteral("Saturation"), QStringLiteral("video")},
        {QStringLiteral("video.grayscale"), QStringLiteral("Grayscale"), QStringLiteral("video")},
        {QStringLiteral("audio.gain"), QStringLiteral("Gain"), QStringLiteral("audio")},
        {QStringLiteral("audio.fade_in"), QStringLiteral("Fade In"), QStringLiteral("audio")},
        {QStringLiteral("audio.fade_out"), QStringLiteral("Fade Out"), QStringLiteral("audio")},
        {QStringLiteral("transitions.cross_dissolve"), QStringLiteral("Cross Dissolve"), QStringLiteral("transitions")},
        {QStringLiteral("transitions.fade_to_black"), QStringLiteral("Fade to Black"), QStringLiteral("transitions")},
        {QStringLiteral("transitions.wipe"), QStringLiteral("Wipe"), QStringLiteral("transitions")},
        {QStringLiteral("generators.color_matte"), QStringLiteral("Color Matte"), QStringLiteral("generators")},
        {QStringLiteral("generators.solid_color"), QStringLiteral("Solid Color"), QStringLiteral("generators")},
        {QStringLiteral("generators.noise"), QStringLiteral("Noise"), QStringLiteral("generators")},
        {QStringLiteral("text.title"), QStringLiteral("Title"), QStringLiteral("text")},
        {QStringLiteral("text.lower_third"), QStringLiteral("Lower Third"), QStringLiteral("text")},
        {QStringLiteral("text.caption"), QStringLiteral("Caption"), QStringLiteral("text")},
    };
    return values;
}

}  // namespace effects
