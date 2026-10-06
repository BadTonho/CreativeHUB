#pragma once

#include <QColor>
#include <QString>

namespace creative_suite::hub {

class HubPalette {
public:
    // Surface & background colors (pure monochrome dark mode: black, dark gray, charcoal)
    static inline const QColor backgroundDark{0x0e, 0x0e, 0x0e};
    static inline const QColor sidebarBackground{0x14, 0x14, 0x14};
    static inline const QColor headerBackground{0x14, 0x14, 0x14};
    static inline const QColor cardBackground{0x18, 0x18, 0x18};
    static inline const QColor cardHover{0x20, 0x20, 0x20};
    static inline const QColor cardBorder{0x29, 0x29, 0x29};
    static inline const QColor cardBorderHover{0x4a, 0x4a, 0x4a};

    // Text colors (crisp white and neutral grays)
    static inline const QColor textPrimary{0xff, 0xff, 0xff};
    static inline const QColor textSecondary{0x9e, 0x9e, 0x9e};
    static inline const QColor textMuted{0x66, 0x66, 0x66};

    // Primary Brand Accent (Clean high-contrast White)
    static inline const QColor accentPrimary{0xff, 0xff, 0xff};
    static inline const QColor accentPrimaryHover{0xe4, 0xe4, 0xe4};
    static inline const QColor accentPrimaryPressed{0xcc, 0xcc, 0xcc};

    // Signature colors in monochrome (neutral clean grays)
    static inline const QColor accentVideo{0x55, 0x55, 0x55};
    static inline const QColor accentImage{0x55, 0x55, 0x55};
    static inline const QColor accentMotion{0x55, 0x55, 0x55};
    static inline const QColor accentHub{0x55, 0x55, 0x55};

    // Status colors (neutral dark mode)
    static inline const QColor statusInstalled{0xff, 0xff, 0xff};
    static inline const QColor statusUpdate{0xee, 0xee, 0xee};
    static inline const QColor statusNotInstalled{0x88, 0x88, 0x88};
    static inline const QColor statusError{0xdc, 0x26, 0x26};

    // String helpers for stylesheets
    static QString toRgbaString(const QColor& color, double alpha = 1.0);
    static QColor appAccentColor(const QString& appId);
};

} // namespace creative_suite::hub
