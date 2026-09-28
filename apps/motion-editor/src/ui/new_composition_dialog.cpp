#include "new_composition_dialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QIntValidator>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <limits>

namespace motion::ui {

NewCompositionDialog::NewCompositionDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("motion-new-composition-dialog"));
    setWindowTitle(QStringLiteral("New Composition"));

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout();

    width_edit_ = new QLineEdit(this);
    width_edit_->setObjectName(QStringLiteral("motion-canvas-width"));
    width_edit_->setValidator(new QIntValidator(1, std::numeric_limits<int>::max(), width_edit_));
    width_edit_->setPlaceholderText(QStringLiteral("Enter width"));
    form->addRow(QStringLiteral("Width (px)"), width_edit_);

    height_edit_ = new QLineEdit(this);
    height_edit_->setObjectName(QStringLiteral("motion-canvas-height"));
    height_edit_->setValidator(new QIntValidator(1, std::numeric_limits<int>::max(), height_edit_));
    height_edit_->setPlaceholderText(QStringLiteral("Enter height"));
    form->addRow(QStringLiteral("Height (px)"), height_edit_);
    layout->addLayout(form);

    buttons_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        Qt::Horizontal,
        this);
    buttons_->setObjectName(QStringLiteral("motion-new-composition-buttons"));
    buttons_->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Create"));
    buttons_->button(QDialogButtonBox::Ok)->setObjectName(QStringLiteral("motion-create-composition"));
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(false);
    buttons_->button(QDialogButtonBox::Cancel)->setObjectName(QStringLiteral("motion-cancel-composition"));
    layout->addWidget(buttons_);

    connect(width_edit_, &QLineEdit::textChanged, this, [this] { updateCreateEnabled(); });
    connect(height_edit_, &QLineEdit::textChanged, this, [this] { updateCreateEnabled(); });
    connect(buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

std::optional<model::CanvasSize> NewCompositionDialog::canvasSize() const noexcept
{
    bool width_ok = false;
    bool height_ok = false;
    const int width = width_edit_->text().toInt(&width_ok);
    const int height = height_edit_->text().toInt(&height_ok);
    if (!width_ok || !height_ok || width <= 0 || height <= 0) {
        return std::nullopt;
    }
    return model::CanvasSize{width, height};
}

void NewCompositionDialog::updateCreateEnabled()
{
    buttons_->button(QDialogButtonBox::Ok)->setEnabled(canvasSize().has_value());
}

} // namespace motion::ui
