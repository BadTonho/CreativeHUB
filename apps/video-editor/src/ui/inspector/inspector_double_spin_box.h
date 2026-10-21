#pragma once

#include <QDoubleSpinBox>

class QKeyEvent;

namespace ui {

class InspectorDoubleSpinBox final : public QDoubleSpinBox {
    Q_OBJECT

public:
    explicit InspectorDoubleSpinBox(QWidget* parent = nullptr);

signals:
    void arrowAdjustmentStarted();
    void arrowValueChanged(double value);
    void arrowAdjustmentFinished();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    bool handleArrowKey(QKeyEvent* event);
    void finishArrowAdjustment();

    bool arrow_adjustment_active_ = false;
};

} // namespace ui
