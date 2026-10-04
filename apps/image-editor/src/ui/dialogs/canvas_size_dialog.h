#pragma once

#include "../../core/document/image_document_session.h"

#include <QDialog>
#include <QSize>

class QComboBox;
class QLabel;
class QSpinBox;

namespace image_editor {

class CanvasSizeDialog final : public QDialog {
    Q_OBJECT

public:
    explicit CanvasSizeDialog(const QSize& current_size, QWidget* parent = nullptr);

    [[nodiscard]] QSize canvasSize() const;
    [[nodiscard]] CanvasAnchor anchor() const;

public slots:
    void accept() override;

private:
    void updateSizeControls(int preset_index);
    void updateValidity();

    QComboBox* preset_combo_ = nullptr;
    QSpinBox* width_spin_ = nullptr;
    QSpinBox* height_spin_ = nullptr;
    QLabel* validation_label_ = nullptr;
};

} // namespace image_editor
