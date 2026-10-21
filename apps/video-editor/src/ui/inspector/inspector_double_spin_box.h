#pragma once

#include <QDoubleSpinBox>

class QKeyEvent;

namespace ui {

class InspectorDoubleSpinBox final : public QDoubleSpinBox {
public:
    explicit InspectorDoubleSpinBox(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    bool handleArrowKey(QKeyEvent* event);
};

} // namespace ui
