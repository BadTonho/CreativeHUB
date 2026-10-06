#include "hub_palette.h"

namespace creative_suite::hub {

QString HubPalette::toRgbaString(const QColor& color, double alpha) {
    const int a = static_cast<int>(alpha * 255.0);
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(a);
}

QColor HubPalette::appAccentColor(const QString& /*appId*/) {
    return cardBorderHover;
}

} // namespace creative_suite::hub
