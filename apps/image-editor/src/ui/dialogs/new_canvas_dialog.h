#pragma once

#include <QColor>
#include <QDialog>
#include <QSize>

class QComboBox;
class QLabel;
class QSpinBox;

namespace image_editor {

class NewCanvasDialog final : public QDialog {
    Q_OBJECT

public:
    explicit NewCanvasDialog(QWidget* parent = nullptr);

    [[nodiscard]] QSize canvasSize() const;
    [[nodiscard]] QColor backgroundColor() const;

public slots:
    void accept() override;

private:
    void updateSizeControls(int preset_index);
    void updateValidity();
    void chooseCustomColor(int background_index);

    QComboBox* preset_combo_ = nullptr;
    QSpinBox* width_spin_ = nullptr;
    QSpinBox* height_spin_ = nullptr;
    QComboBox* background_combo_ = nullptr;
    QLabel* validation_label_ = nullptr;
    QColor custom_color_ = Qt::black;
    int last_background_index_ = 0;
};

} // namespace image_editor
