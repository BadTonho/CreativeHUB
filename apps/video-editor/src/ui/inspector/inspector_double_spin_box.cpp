#include "ui/inspector/inspector_double_spin_box.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLineEdit>

namespace ui {

InspectorDoubleSpinBox::InspectorDoubleSpinBox(QWidget* parent)
    : QDoubleSpinBox(parent) {
    setDecimals(3);
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    lineEdit()->installEventFilter(this);
}

bool InspectorDoubleSpinBox::eventFilter(QObject* watched, QEvent* event) {
    if (watched == lineEdit() && event->type() == QEvent::KeyPress &&
        handleArrowKey(static_cast<QKeyEvent*>(event))) {
        return true;
    }
    return QDoubleSpinBox::eventFilter(watched, event);
}

void InspectorDoubleSpinBox::keyPressEvent(QKeyEvent* event) {
    if (handleArrowKey(event)) {
        return;
    }
    QDoubleSpinBox::keyPressEvent(event);
}

bool InspectorDoubleSpinBox::handleArrowKey(QKeyEvent* event) {
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        auto step = singleStep();
        if (event->modifiers() == Qt::ShiftModifier) {
            step = 0.001;
        } else if (event->modifiers() == Qt::AltModifier) {
            step = 0.100;
        }
        setValue(value() + (event->key() == Qt::Key_Up ? step : -step));
        event->accept();
        return true;
    }
    return false;
}

} // namespace ui
