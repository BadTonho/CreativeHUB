#pragma once

#include "model/composition_document.h"

#include <QDialog>

#include <optional>

class QLineEdit;
class QDialogButtonBox;

namespace motion::ui {

class NewCompositionDialog final : public QDialog {
public:
    explicit NewCompositionDialog(QWidget* parent = nullptr);

    [[nodiscard]] std::optional<model::CanvasSize> canvasSize() const noexcept;

private:
    void updateCreateEnabled();

    QLineEdit* width_edit_ = nullptr;
    QLineEdit* height_edit_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
};

} // namespace motion::ui
