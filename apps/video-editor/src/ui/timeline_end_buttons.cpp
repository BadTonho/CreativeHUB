#include "ui/timeline_end_buttons.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QWidget>

namespace ui {

QWidget* createTimelineEndButtons(QWidget* parent) {
    auto* container = new QWidget(parent);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* edit_button = new QPushButton("Edit", container);
    edit_button->setObjectName("timelineEditButton");
    edit_button->setFixedHeight(28);
    edit_button->setEnabled(true);

    auto* unnamed_button = new QPushButton(container);
    unnamed_button->setObjectName("timelineUnnamedButton");
    unnamed_button->setAccessibleName("Unassigned timeline action");
    unnamed_button->setFixedSize(32, 28);
    unnamed_button->setEnabled(true);

    layout->addWidget(edit_button);
    layout->addWidget(unnamed_button);
    return container;
}

}  // namespace ui
