#pragma once

#include <QColor>
#include <QString>

namespace creative_suite::hub {

class HubPalette {
public:
    // Surface & background colors
    static inline const QColor backgroundDark{0x18, 0x18, 0x18};
    static inline const QColor sidebarBackground{0x1f, 0x1f, 0x22};
    static inline const QColor headerBackground{0x22, 0x22, 0x26};
    static inline const QColor cardBackground{0x26, 0x26, 0x2b};
    static inline const QColor cardHover{0x2f, 0x2f, 0x35};
    static inline const QColor cardBorder{0x38, 0x38, 0x40};

    // Text colors
    static inline const QColor textPrimary{0xf5, 0xf5, 0xf7};
    static inline const QColor textSecondary{0xa0, 0xa0, 0xa8};
    static inline const QColor textMuted{0x70, 0x70, 0x78};

    // Accent colors (Creative Suite branding)
    static inline const QColor accentPrimary{0x00, 0x7a, 0xff};       // Vibrant Blue
    static inline const QColor accentPrimaryHover{0x20, 0x8a, 0xff};
    static inline const QColor accentPrimaryPressed{0x00, 0x66, 0xd6};

    // Status colors
    static inline const QColor statusInstalled{0x28, 0xa7, 0x45};     // Green
    static inline const QColor statusUpdate{0x00, 0x7a, 0xff};        // Blue
    static inline const QColor statusNotInstalled{0x80, 0x80, 0x88};  // Gray
    static inline const QColor statusError{0xdc, 0x35, 0x45};         // Red

    // String helpers for stylesheets
    static QString toRgbaString(const QColor& color, double alpha = 1.0);
};

} // namespace creative_suite::hub
