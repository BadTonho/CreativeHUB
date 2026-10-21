#include "ui/inspector/inspector_double_spin_box.h"

#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>

namespace ui {

InspectorDoubleSpinBox::InspectorDoubleSpinBox(QWidget* parent)
    : QDoubleSpinBox(parent) {
    setDecimals(3);
    setButtonSymbols(QAbstractSpinBox::NoButtons);
    lineEdit()->installEventFilter(this);
}

void InspectorDoubleSpinBox::setResetLabel(QLabel* label, double reset_value) {
    if (reset_label_ != nullptr) reset_label_->removeEventFilter(this);
    reset_label_ = label;
    reset_value_ = reset_value;
    if (reset_label_ == nullptr) return;
    reset_label_->installEventFilter(this);
    reset_label_->setCursor(Qt::PointingHandCursor);
    reset_label_->setToolTip(
        tr("Double-click the property name to reset its value."));
}

bool InspectorDoubleSpinBox::eventFilter(QObject* watched, QEvent* event) {
    if (watched == reset_label_ && event->type() == QEvent::MouseButtonDblClick) {
        auto* mouse_event = static_cast<QMouseEvent*>(event);
        if (mouse_event->button() == Qt::LeftButton && isEnabled()) {
            setValue(reset_value_);
            emit resetRequested(value());
            mouse_event->accept();
            return true;
        }
    }
    if (watched == lineEdit()) {
        if (event->type() == QEvent::KeyPress &&
            handleArrowKey(static_cast<QKeyEvent*>(event))) {
            return true;
        }
        if (event->type() == QEvent::KeyRelease) {
            auto* key_event = static_cast<QKeyEvent*>(event);
            if (!key_event->isAutoRepeat() &&
                (key_event->key() == Qt::Key_Up ||
                 key_event->key() == Qt::Key_Down)) {
                finishArrowAdjustment();
                key_event->accept();
                return true;
            }
        }
        if (event->type() == QEvent::FocusOut) {
            finishArrowAdjustment();
        }
    }
    return QDoubleSpinBox::eventFilter(watched, event);
}

void InspectorDoubleSpinBox::keyPressEvent(QKeyEvent* event) {
    if (handleArrowKey(event)) {
        return;
    }
    QDoubleSpinBox::keyPressEvent(event);
}

void InspectorDoubleSpinBox::keyReleaseEvent(QKeyEvent* event) {
    if (!event->isAutoRepeat() &&
        (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down)) {
        finishArrowAdjustment();
        event->accept();
        return;
    }
    QDoubleSpinBox::keyReleaseEvent(event);
}

bool InspectorDoubleSpinBox::handleArrowKey(QKeyEvent* event) {
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        auto step = singleStep();
        if (event->modifiers() == Qt::ShiftModifier) {
            step = 0.001;
        } else if (event->modifiers() == Qt::AltModifier) {
            step = 0.100;
        } else if (event->modifiers() == Qt::ControlModifier) {
            step = 1.000;
        }
        const auto previous_value = value();
        setValue(value() + (event->key() == Qt::Key_Up ? step : -step));
        if (value() != previous_value) {
            if (!arrow_adjustment_active_) {
                arrow_adjustment_active_ = true;
                emit arrowAdjustmentStarted();
            }
            emit arrowValueChanged(value());
        }
        event->accept();
        return true;
    }
    return false;
}

void InspectorDoubleSpinBox::finishArrowAdjustment() {
    if (!arrow_adjustment_active_) return;
    arrow_adjustment_active_ = false;
    emit arrowAdjustmentFinished();
}

} // namespace ui
