#include "canvas_size_dialog.h"

#include "image_document_store.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <array>

namespace image_editor {
namespace {

struct CanvasPreset {
    const char* name;
    QSize size;
};

constexpr std::array<CanvasPreset, 5> kPresets = {{
    {"Square (1080 x 1080 px)", QSize(1080, 1080)},
    {"Portrait (1080 x 1350 px)", QSize(1080, 1350)},
    {"Story / Reel (1080 x 1920 px)", QSize(1080, 1920)},
    {"Full HD Landscape (1920 x 1080 px)", QSize(1920, 1080)},
    {"A4 Portrait - 300 DPI (2480 x 3508 px)", QSize(2480, 3508)},
}};

constexpr int kCustomPresetIndex = static_cast<int>(kPresets.size());

constexpr std::array<CanvasAnchor, 9> kAnchors = {
    CanvasAnchor::TopLeft, CanvasAnchor::Top, CanvasAnchor::TopRight,
    CanvasAnchor::Left, CanvasAnchor::Center, CanvasAnchor::Right,
    CanvasAnchor::BottomLeft, CanvasAnchor::Bottom, CanvasAnchor::BottomRight,
};

constexpr std::array<const char*, 9> kAnchorNames = {
    "Top left", "Top", "Top right",
    "Left", "Center", "Right",
    "Bottom left", "Bottom", "Bottom right",
};

} // namespace

CanvasSizeDialog::CanvasSizeDialog(const QSize& current_size, QWidget* parent)
    : QDialog(parent) {
    setObjectName(QStringLiteral("canvasSizeDialog"));
    setWindowTitle(QStringLiteral("Canvas Size"));
    setModal(true);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    preset_combo_ = new QComboBox(this);
    preset_combo_->setObjectName(QStringLiteral("canvasSizePresetCombo"));
    for (const auto& preset : kPresets) preset_combo_->addItem(QString::fromLatin1(preset.name));
    preset_combo_->addItem(QStringLiteral("Custom"));
    form->addRow(QStringLiteral("Format:"), preset_combo_);

    width_spin_ = new QSpinBox(this);
    width_spin_->setObjectName(QStringLiteral("canvasSizeWidthSpin"));
    width_spin_->setRange(1, 32768);
    width_spin_->setSuffix(QStringLiteral(" px"));
    width_spin_->setValue(current_size.width());
    height_spin_ = new QSpinBox(this);
    height_spin_->setObjectName(QStringLiteral("canvasSizeHeightSpin"));
    height_spin_->setRange(1, 32768);
    height_spin_->setSuffix(QStringLiteral(" px"));
    height_spin_->setValue(current_size.height());
    form->addRow(QStringLiteral("Width:"), width_spin_);
    form->addRow(QStringLiteral("Height:"), height_spin_);
    layout->addLayout(form);

    auto* anchor_label = new QLabel(QStringLiteral("Anchor:"), this);
    layout->addWidget(anchor_label);
    auto* anchor_grid = new QGridLayout;
    anchor_grid->setSpacing(3);
    auto* anchor_group = new QButtonGroup(this);
    anchor_group->setExclusive(true);
    for (int i = 0; i < static_cast<int>(kAnchors.size()); ++i) {
        auto* button = new QRadioButton(QString::fromLatin1(kAnchorNames.at(i)), this);
        button->setObjectName(QStringLiteral("canvasSizeAnchor%1Button")
                                  .arg(QString::fromLatin1(kAnchorNames.at(i)).remove(QLatin1Char(' '))));
        anchor_group->addButton(button, i);
        anchor_grid->addWidget(button, i / 3, i % 3, Qt::AlignCenter);
        if (kAnchors.at(i) == CanvasAnchor::Center) {
            button->setObjectName(QStringLiteral("canvasSizeAnchorCenterButton"));
            button->setChecked(true);
        }
    }
    layout->addLayout(anchor_grid);

    auto* explanation = new QLabel(
        QStringLiteral("Canvas Size changes the document bounds without scaling its layers. New area uses the canvas background or transparency."), this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    validation_label_ = new QLabel(this);
    validation_label_->setObjectName(QStringLiteral("canvasSizeValidationLabel"));
    validation_label_->setWordWrap(true);
    layout->addWidget(validation_label_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("canvasSizeButtons"));
    connect(buttons, &QDialogButtonBox::accepted, this, &CanvasSizeDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    connect(preset_combo_, &QComboBox::currentIndexChanged,
            this, &CanvasSizeDialog::updateSizeControls);
    connect(width_spin_, &QSpinBox::valueChanged, this, &CanvasSizeDialog::updateValidity);
    connect(height_spin_, &QSpinBox::valueChanged, this, &CanvasSizeDialog::updateValidity);

    int matching_preset = kCustomPresetIndex;
    for (int i = 0; i < static_cast<int>(kPresets.size()); ++i) {
        if (kPresets.at(i).size == current_size) {
            matching_preset = i;
            break;
        }
    }
    preset_combo_->setCurrentIndex(matching_preset);
    if (matching_preset == kCustomPresetIndex) {
        width_spin_->setValue(current_size.width());
        height_spin_->setValue(current_size.height());
    }
    updateSizeControls(matching_preset);
    updateValidity();
    adjustSize();
}

QSize CanvasSizeDialog::canvasSize() const {
    return {width_spin_->value(), height_spin_->value()};
}

CanvasAnchor CanvasSizeDialog::anchor() const {
    const auto* group = findChild<QButtonGroup*>();
    if (group == nullptr || group->checkedId() < 0 || group->checkedId() >= static_cast<int>(kAnchors.size()))
        return CanvasAnchor::Center;
    return kAnchors.at(static_cast<std::size_t>(group->checkedId()));
}

void CanvasSizeDialog::accept() {
    if (!ImageDocumentStore::isValidCanvasSize(canvasSize())) {
        updateValidity();
        return;
    }
    QDialog::accept();
}

void CanvasSizeDialog::updateSizeControls(int preset_index) {
    const bool custom = preset_index == kCustomPresetIndex;
    width_spin_->setEnabled(custom);
    height_spin_->setEnabled(custom);
    if (preset_index >= 0 && preset_index < kCustomPresetIndex) {
        width_spin_->setValue(kPresets.at(static_cast<std::size_t>(preset_index)).size.width());
        height_spin_->setValue(kPresets.at(static_cast<std::size_t>(preset_index)).size.height());
    }
    updateValidity();
}

void CanvasSizeDialog::updateValidity() {
    const bool valid = ImageDocumentStore::isValidCanvasSize(canvasSize());
    auto* buttons = findChild<QDialogButtonBox*>(QStringLiteral("canvasSizeButtons"));
    if (buttons != nullptr) buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
    validation_label_->setText(valid ? QString{} : QStringLiteral(
        "Canvas dimensions must be positive and stay within 64 million pixels."));
}

} // namespace image_editor
