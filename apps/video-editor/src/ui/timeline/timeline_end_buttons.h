#pragma once

class QWidget;
class QPushButton;

namespace ui {

struct TimelineEndButtons {
    QWidget* container = nullptr;
    QPushButton* edit = nullptr;
    QPushButton* fusion = nullptr;
};

[[nodiscard]] TimelineEndButtons createTimelineEndButtons(QWidget* parent);

}  // namespace ui
