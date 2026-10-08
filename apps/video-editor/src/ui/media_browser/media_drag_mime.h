#pragma once

#include <QMimeData>
#include <QString>

namespace ui {

inline constexpr char kMediaPathMimeType[] =
    "application/x-creative-suite-media-path";
inline constexpr char kMediaFrameCountMimeType[] =
    "application/x-creative-suite-media-frame-count";
inline constexpr char kMediaFrameRateMimeType[] =
    "application/x-creative-suite-media-frame-rate";
inline constexpr char kMediaDurationSecondsMimeType[] =
    "application/x-creative-suite-media-duration-seconds";
inline constexpr char kMediaDisplayNameMimeType[] =
    "application/x-creative-suite-media-display-name";
inline constexpr char kMediaKindMimeType[] =
    "application/x-creative-suite-media-kind";
inline constexpr char kMediaBinPathMimeType[] =
    "application/x-creative-suite-media-bin-path";
inline constexpr char kEffectIdMimeType[] =
    "application/x-creative-suite-effect-id";

inline QMimeData* createEffectIdMimeData(const QString& effect_id) {
    auto* mime_data = new QMimeData;
    mime_data->setData(kEffectIdMimeType, effect_id.toUtf8());
    return mime_data;
}

} // namespace ui
