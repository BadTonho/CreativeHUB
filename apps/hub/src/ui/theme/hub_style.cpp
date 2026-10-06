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
            background: #2e2e3e;
            min-height: 28px;
            border-radius: 4px;
        }
        QScrollBar::handle:vertical:hover {
            background: #46465c;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }

        QToolTip {
            background-color: #1e1e28;
            color: #f4f4fa;
            border: 1px solid #363648;
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
            background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 #4f46e5, stop:1 #6366f1);
            color: #ffffff;
            font-weight: 700;
            font-size: 12px;
            padding: 7px 18px;
            border-radius: 8px;
            border: 1px solid rgba(255, 255, 255, 0.12);
        }
        QPushButton:hover {
            background: qlineargradient(spread:pad, x1:0, y1:0, x2:1, y2:0, stop:0 #5b51ef, stop:1 #7073f8);
            border-color: rgba(255, 255, 255, 0.22);
        }
        QPushButton:pressed {
            background: #4338ca;
        }
        QPushButton:disabled {
            background: #252532;
            color: #5d5d6e;
            border: 1px solid #323242;
        }
    )");
}

QString HubStyle::secondaryButtonStyle() {
    return QString(R"(
        QPushButton {
            background-color: #20202c;
            color: #e4e4ee;
            font-weight: 600;
            font-size: 12px;
            padding: 7px 16px;
            border-radius: 8px;
            border: 1px solid #343446;
        }
        QPushButton:hover {
            background-color: #2b2b3b;
            border-color: #4c4c64;
            color: #ffffff;
        }
        QPushButton:pressed {
            background-color: #1c1c26;
        }
        QPushButton:disabled {
            background-color: #181822;
            color: #555566;
            border-color: #252534;
        }
    )");
}

QString HubStyle::searchInputStyle() {
    return QString(R"(
        QLineEdit {
            background-color: #161620;
            color: #ffffff;
            border: 1px solid #2e2e40;
            border-radius: 8px;
            padding: 7px 14px;
            font-size: 12px;
        }
        QLineEdit:focus {
            border: 1px solid #6366f1;
            background-color: #1b1b26;
        }
    )");
}

} // namespace creative_suite::hub
