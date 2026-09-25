#include "ui/timeline/timeline_end_buttons.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QWidget>

namespace ui {

TimelineEndButtons createTimelineEndButtons(QWidget* parent) {
    auto* container = new QWidget(parent);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* edit_button = new QPushButton("Edit", container);
    edit_button->setObjectName("timelineEditButton");
    edit_button->setFixedSize(68, 28);
    edit_button->setCheckable(true);
    edit_button->setAutoExclusive(true);
    edit_button->setToolTip("Switch to the Edit workspace");
    edit_button->setAccessibleName("Edit workspace");
    edit_button->setChecked(true);

    auto* unnamed_button = new QPushButton(container);
    unnamed_button->setObjectName("timelineUnnamedButton");
    unnamed_button->setAccessibleName("Fusion");
    unnamed_button->setToolTip("Switch to the Fusion workspace");
    unnamed_button->setFixedSize(32, 28);
    unnamed_button->setCheckable(true);
    unnamed_button->setAutoExclusive(true);

    auto* render_button = new QPushButton("Render", container);
    render_button->setObjectName("timelineRenderButton");
    render_button->setFixedSize(68, 28);
    render_button->setCheckable(true);
    render_button->setAutoExclusive(true);
    render_button->setToolTip("Switch to the Render workspace");
    render_button->setAccessibleName("Render workspace");

    container->setStyleSheet(
        "QPushButton:checked {"
        " background-color: #1680bd;"
        " border: 1px solid #2c9bd8;"
        " color: #ffffff;"
        "}");

    layout->addWidget(edit_button);
    layout->addWidget(unnamed_button);
    layout->addWidget(render_button);
    return {container, edit_button, unnamed_button, render_button};
}

}  // namespace ui
