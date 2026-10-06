#include "hub_style.h"
#include "hub_palette.h"

namespace creative_suite::hub {

QString HubStyle::globalStyleSheet() {
    return QString(R"(
        QWidget {
            background-color: %1;
            color: %2;
            font-family: 'Inter', 'Segoe UI', -apple-system, Roboto, sans-serif;
            font-size: 13px;
        }

        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background: #2a2a2a;
            min-height: 28px;
            border-radius: 4px;
        }
        QScrollBar::handle:vertical:hover {
            background: #3e3e3e;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }

        QToolTip {
            background-color: #1a1a1a;
            color: #ffffff;
            border: 1px solid #333333;
            padding: 6px 12px;
            border-radius: 8px;
            font-size: 12px;
        }
    )")
    .arg(HubPalette::backgroundDark.name())
    .arg(HubPalette::textPrimary.name());
}

QString HubStyle::primaryButtonStyle() {
    return QString(R"(
        QPushButton {
            background-color: #ffffff;
            color: #000000;
            font-weight: 700;
            font-size: 12px;
            padding: 7px 18px;
            border-radius: 8px;
            border: none;
        }
        QPushButton:hover {
            background-color: #e4e4e4;
        }
        QPushButton:pressed {
            background-color: #cccccc;
        }
        QPushButton:disabled {
            background-color: #202020;
            color: #555555;
            border: 1px solid #282828;
        }
    )");
}

QString HubStyle::secondaryButtonStyle() {
    return QString(R"(
        QPushButton {
            background-color: #1a1a1a;
            color: #ffffff;
            font-weight: 600;
            font-size: 12px;
            padding: 7px 16px;
            border-radius: 8px;
            border: 1px solid #2e2e2e;
        }
        QPushButton:hover {
            background-color: #262626;
            border-color: #444444;
            color: #ffffff;
        }
        QPushButton:pressed {
            background-color: #141414;
        }
        QPushButton:disabled {
            background-color: #141414;
            color: #444444;
            border-color: #202020;
        }
    )");
}

QString HubStyle::searchInputStyle() {
    return QString(R"(
        QLineEdit {
            background-color: #141414;
            color: #ffffff;
            border: 1px solid #282828;
            border-radius: 8px;
            padding: 7px 14px;
            font-size: 12px;
        }
        QLineEdit:focus {
            border: 1px solid #555555;
            background-color: #1c1c1c;
        }
    )");
}

} // namespace creative_suite::hub
