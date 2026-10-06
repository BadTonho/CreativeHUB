#include "hub_style.h"
#include "hub_palette.h"

namespace creative_suite::hub {

QString HubStyle::globalStyleSheet() {
    return QString(R"(
        QWidget {
            background-color: %1;
            color: %2;
            font-family: 'Segoe UI', 'SF Pro Text', -apple-system, sans-serif;
            font-size: 13px;
        }

        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background: #40404a;
            min-height: 24px;
            border-radius: 4px;
        }
        QScrollBar::handle:vertical:hover {
            background: #50505c;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }

        QToolTip {
            background-color: #2b2b32;
            color: #f0f0f5;
            border: 1px solid #444450;
            padding: 6px 10px;
            border-radius: 6px;
        }
    )")
    .arg(HubPalette::backgroundDark.name())
    .arg(HubPalette::textPrimary.name());
}

QString HubStyle::primaryButtonStyle() {
    return QString(R"(
        QPushButton {
            background-color: %1;
            color: #ffffff;
            font-weight: 600;
            font-size: 12px;
            padding: 7px 16px;
            border-radius: 6px;
            border: none;
        }
        QPushButton:hover {
            background-color: %2;
        }
        QPushButton:pressed {
            background-color: %3;
        }
        QPushButton:disabled {
            background-color: #383842;
            color: #70707c;
        }
    )")
    .arg(HubPalette::accentPrimary.name())
    .arg(HubPalette::accentPrimaryHover.name())
    .arg(HubPalette::accentPrimaryPressed.name());
}

QString HubStyle::secondaryButtonStyle() {
    return QString(R"(
        QPushButton {
            background-color: transparent;
            color: %1;
            font-weight: 500;
            font-size: 12px;
            padding: 6px 14px;
            border-radius: 6px;
            border: 1px solid #484854;
        }
        QPushButton:hover {
            background-color: #32323a;
            border-color: #606070;
            color: #ffffff;
        }
        QPushButton:pressed {
            background-color: #282830;
        }
    )")
    .arg(HubPalette::textPrimary.name());
}

QString HubStyle::searchInputStyle() {
    return QString(R"(
        QLineEdit {
            background-color: #222228;
            color: #ffffff;
            border: 1px solid #383842;
            border-radius: 6px;
            padding: 6px 12px;
            font-size: 12px;
        }
        QLineEdit:focus {
            border: 1px solid %1;
            background-color: #26262e;
        }
    )")
    .arg(HubPalette::accentPrimary.name());
}

} // namespace creative_suite::hub
