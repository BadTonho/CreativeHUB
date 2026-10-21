#pragma once

#include <QDoubleSpinBox>
#include <QPointer>

class QKeyEvent;
class QLabel;

namespace ui {

class InspectorDoubleSpinBox final : public QDoubleSpinBox {
    Q_OBJECT

public:
    explicit InspectorDoubleSpinBox(QWidget* parent = nullptr);
    void setResetLabel(QLabel* label, double value);
    void setResetValue(double value) noexcept { reset_value_ = value; }

signals:
    void arrowAdjustmentStarted();
    void arrowValueChanged(double value);
    void arrowAdjustmentFinished();
    void resetRequested(double value);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    bool handleArrowKey(QKeyEvent* event);
    void finishArrowAdjustment();

    QPointer<QLabel> reset_label_;
    double reset_value_ = 0.0;
    bool arrow_adjustment_active_ = false;
};

} // namespace ui
