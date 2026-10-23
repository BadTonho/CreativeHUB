#include "ui/timeline/timeline_end_buttons.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QWidget>
#include <QCoreApplication>

namespace ui {

TimelineEndButtons createTimelineEndButtons(QWidget* parent) {
    auto* container = new QWidget(parent);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    auto* edit_button = new QPushButton(QCoreApplication::translate("TimelineEndButtons", "Edit"), container);
    edit_button->setObjectName("timelineEditButton");
    edit_button->setFixedSize(68, 22);
    edit_button->setCheckable(true);
    edit_button->setAutoExclusive(true);
    edit_button->setToolTip(QCoreApplication::translate("TimelineEndButtons", "Switch to the Edit workspace"));
    edit_button->setAccessibleName(QCoreApplication::translate("TimelineEndButtons", "Edit workspace"));
    edit_button->setChecked(true);

    auto* unnamed_button = new QPushButton(container);
    unnamed_button->setObjectName("timelineUnnamedButton");
    unnamed_button->setAccessibleName(QCoreApplication::translate("TimelineEndButtons", "Fusion"));
    unnamed_button->setToolTip(QCoreApplication::translate("TimelineEndButtons", "Switch to the Fusion workspace"));
    unnamed_button->setFixedSize(32, 22);
    unnamed_button->setCheckable(true);
    unnamed_button->setAutoExclusive(true);

    auto* color_button = new QPushButton(QCoreApplication::translate("TimelineEndButtons", "Color"), container);
    color_button->setObjectName("timelineColorButton");
    color_button->setFixedSize(68, 22);
    color_button->setCheckable(true);
    color_button->setAutoExclusive(true);
    color_button->setToolTip(QCoreApplication::translate("TimelineEndButtons", "Switch to the Color workspace"));
    color_button->setAccessibleName(QCoreApplication::translate("TimelineEndButtons", "Color workspace"));

    auto* audio_button = new QPushButton(QCoreApplication::translate("TimelineEndButtons", "Audio"), container);
    audio_button->setObjectName("timelineAudioButton");
    audio_button->setFixedSize(68, 22);
    audio_button->setCheckable(true);
    audio_button->setAutoExclusive(true);
    audio_button->setToolTip(QCoreApplication::translate("TimelineEndButtons", "Switch to the Audio workspace"));
    audio_button->setAccessibleName(QCoreApplication::translate("TimelineEndButtons", "Audio workspace"));

    auto* render_button = new QPushButton(QCoreApplication::translate("TimelineEndButtons", "Render"), container);
    render_button->setObjectName("timelineRenderButton");
    render_button->setFixedSize(68, 22);
    render_button->setCheckable(true);
    render_button->setAutoExclusive(true);
    render_button->setToolTip(QCoreApplication::translate("TimelineEndButtons", "Switch to the Render workspace"));
    render_button->setAccessibleName(QCoreApplication::translate("TimelineEndButtons", "Render workspace"));

    container->setStyleSheet(
        "QPushButton:checked {"
        " background-color: #1680bd;"
        " border: 1px solid #2c9bd8;"
        " color: #ffffff;"
        "}");

    layout->addWidget(edit_button);
    layout->addWidget(unnamed_button);
    layout->addWidget(color_button);
    layout->addWidget(audio_button);
    layout->addWidget(render_button);
    return {container, edit_button, unnamed_button, color_button, audio_button, render_button};
}

}  // namespace ui
