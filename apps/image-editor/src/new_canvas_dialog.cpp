#include "new_canvas_dialog.h"

#include "image_document_store.h"

#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include <iterator>

namespace image_editor {
namespace {

struct CanvasPreset {
    const char* name;
    QSize size;
};

constexpr CanvasPreset kPresets[] = {
    {"Square (1080 x 1080 px)", QSize(1080, 1080)},
    {"Portrait (1080 x 1350 px)", QSize(1080, 1350)},
    {"Story / Reel (1080 x 1920 px)", QSize(1080, 1920)},
    {"Full HD Landscape (1920 x 1080 px)", QSize(1920, 1080)},
    {"A4 Portrait - 300 DPI (2480 x 3508 px)", QSize(2480, 3508)},
};

constexpr int kCustomPresetIndex = 1 + static_cast<int>(std::size(kPresets));

} // namespace

NewCanvasDialog::NewCanvasDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("newCanvasDialog"));
    setWindowTitle(QStringLiteral("Create New Canvas"));
    setModal(true);

    auto* outer_layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    preset_combo_ = new QComboBox(this);
    preset_combo_->setObjectName(QStringLiteral("newCanvasPresetCombo"));
    preset_combo_->addItem(QStringLiteral("Choose a canvas size..."));
    for (const auto& preset : kPresets) {
        preset_combo_->addItem(QString::fromLatin1(preset.name));
    }
    preset_combo_->addItem(QStringLiteral("Custom"));
    form->addRow(QStringLiteral("Format:"), preset_combo_);

    width_spin_ = new QSpinBox(this);
    width_spin_->setObjectName(QStringLiteral("canvasWidthSpin"));
    width_spin_->setRange(1, 32768);
    width_spin_->setValue(1920);
    width_spin_->setSuffix(QStringLiteral(" px"));
    height_spin_ = new QSpinBox(this);
    height_spin_->setObjectName(QStringLiteral("canvasHeightSpin"));
    height_spin_->setRange(1, 32768);
    height_spin_->setValue(1080);
    height_spin_->setSuffix(QStringLiteral(" px"));
    form->addRow(QStringLiteral("Width:"), width_spin_);
    form->addRow(QStringLiteral("Height:"), height_spin_);

    background_combo_ = new QComboBox(this);
    background_combo_->setObjectName(QStringLiteral("newCanvasBackgroundCombo"));
    background_combo_->addItem(QStringLiteral("Choose a background..."));
    background_combo_->addItem(QStringLiteral("Transparent"));
    background_combo_->addItem(QStringLiteral("White"));
    background_combo_->addItem(QStringLiteral("Custom color..."));
    form->addRow(QStringLiteral("Background:"), background_combo_);

    outer_layout->addLayout(form);
    validation_label_ = new QLabel(this);
    validation_label_->setObjectName(QStringLiteral("newCanvasValidationLabel"));
    validation_label_->setWordWrap(true);
    outer_layout->addWidget(validation_label_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("newCanvasButtons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &NewCanvasDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer_layout->addWidget(buttons);

    connect(preset_combo_, &QComboBox::currentIndexChanged,
            this, &NewCanvasDialog::updateSizeControls);
    connect(background_combo_, &QComboBox::currentIndexChanged,
            this, &NewCanvasDialog::chooseCustomColor);
    connect(width_spin_, &QSpinBox::valueChanged, this, &NewCanvasDialog::updateValidity);
    connect(height_spin_, &QSpinBox::valueChanged, this, &NewCanvasDialog::updateValidity);

    updateSizeControls(preset_combo_->currentIndex());
    updateValidity();
    adjustSize();
}

QSize NewCanvasDialog::canvasSize() const {
    return {width_spin_->value(), height_spin_->value()};
}

QColor NewCanvasDialog::backgroundColor() const {
    switch (background_combo_->currentIndex()) {
    case 1: return QColor(0, 0, 0, 0);
    case 2: return Qt::white;
    case 3: return custom_color_;
    default: return {};
    }
}

void NewCanvasDialog::accept() {
    if (preset_combo_->currentIndex() <= 0 ||
        background_combo_->currentIndex() <= 0 ||
        !ImageDocumentStore::isValidCanvasSize(canvasSize())) {
        updateValidity();
        return;
    }
    QDialog::accept();
}

void NewCanvasDialog::updateSizeControls(int preset_index) {
    const bool custom = preset_index == kCustomPresetIndex;
    width_spin_->setEnabled(custom);
    height_spin_->setEnabled(custom);
    if (preset_index > 0 && preset_index < kCustomPresetIndex) {
        const auto& preset = kPresets[preset_index - 1];
        width_spin_->setValue(preset.size.width());
        height_spin_->setValue(preset.size.height());
    }
    updateValidity();
}

void NewCanvasDialog::updateValidity() {
    auto* buttons = findChild<QDialogButtonBox*>(QStringLiteral("newCanvasButtons"));
    const bool valid_size = ImageDocumentStore::isValidCanvasSize(canvasSize());
    const bool has_preset = preset_combo_->currentIndex() > 0;
    const bool has_background = background_combo_->currentIndex() > 0;
    if (buttons != nullptr) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(
            valid_size && has_preset && has_background);
    }
    if (!has_preset) {
        validation_label_->setText(QStringLiteral("Choose a canvas format or custom dimensions."));
    } else if (!valid_size) {
        validation_label_->setText(QStringLiteral(
            "Canvas dimensions must be positive and stay within 64 million pixels."));
    } else if (!has_background) {
        validation_label_->setText(QStringLiteral("Choose a background for the new canvas."));
    } else {
        validation_label_->clear();
    }
}

void NewCanvasDialog::chooseCustomColor(int background_index) {
    if (background_index == 3) {
        const QColor selected = QColorDialog::getColor(
            custom_color_, this, QStringLiteral("Choose Canvas Background"),
            QColorDialog::ShowAlphaChannel);
        if (selected.isValid()) {
            custom_color_ = selected;
            last_background_index_ = 3;
        } else {
            const QSignalBlocker blocker(background_combo_);
            background_combo_->setCurrentIndex(last_background_index_);
        }
    } else if (background_index > 0) {
        last_background_index_ = background_index;
    }
    updateValidity();
}

} // namespace image_editor
