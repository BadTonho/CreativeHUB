#pragma once

#include <QColor>
#include <QString>

namespace creative_suite::hub {

class HubPalette {
public:
    // Surface & background colors (refined deep dark mode)
    static inline const QColor backgroundDark{0x0f, 0x0f, 0x14};
    static inline const QColor sidebarBackground{0x14, 0x14, 0x1c};
    static inline const QColor headerBackground{0x17, 0x17, 0x22};
    static inline const QColor cardBackground{0x1c, 0x1c, 0x26};
    static inline const QColor cardHover{0x23, 0x23, 0x30};
    static inline const QColor cardBorder{0x2b, 0x2b, 0x3a};
    static inline const QColor cardBorderHover{0x4a, 0x4a, 0x66};

    // Text colors
    static inline const QColor textPrimary{0xf8, 0xf8, 0xfc};
    static inline const QColor textSecondary{0xa2, 0xa2, 0xb4};
    static inline const QColor textMuted{0x6c, 0x6c, 0x7e};

    // Primary Brand Accent (Electric Indigo / Violet)
    static inline const QColor accentPrimary{0x63, 0x66, 0xf1};       // Indigo-500
    static inline const QColor accentPrimaryHover{0x81, 0x8c, 0xf8};  // Indigo-400
    static inline const QColor accentPrimaryPressed{0x4f, 0x46, 0xe5};// Indigo-600

    // Signature colors per Creative Suite application
    static inline const QColor accentVideo{0xa8, 0x55, 0xf7};        // Purple
    static inline const QColor accentImage{0x0e, 0xa5, 0xe9};        // Cyan/Sky
    static inline const QColor accentMotion{0xec, 0x48, 0x99};       // Pink/Magenta
    static inline const QColor accentHub{0x63, 0x66, 0xf1};          // Indigo

    // Status colors
    static inline const QColor statusInstalled{0x10, 0xb9, 0x81};    // Emerald
    static inline const QColor statusUpdate{0x3b, 0x82, 0xf6};       // Blue
    static inline const QColor statusNotInstalled{0x71, 0x71, 0x7a}; // Slate
    static inline const QColor statusError{0xef, 0x44, 0x44};        // Red

    // String helpers for stylesheets
    static QString toRgbaString(const QColor& color, double alpha = 1.0);
    static QColor appAccentColor(const QString& appId);
};

} // namespace creative_suite::hub
