#include "ui/translation_helpers.h"
#include "audio_keyframe_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <array>

namespace motion::ui {
namespace {

using Property = creative_suite::animation::TransformProperty;

constexpr std::array<Property, 5> kProperties{{
    Property::PositionX,
    Property::PositionY,
    Property::Scale,
    Property::Rotation,
    Property::Opacity,
}};

QString propertyName(Property property)
{
    switch (property) {
    case Property::PositionX: return MotionUiTranslation::tr("Position X (canvas ratio)");
    case Property::PositionY: return MotionUiTranslation::tr("Position Y (canvas ratio)");
    case Property::Scale: return MotionUiTranslation::tr("Scale X and Y (%)");
    case Property::Rotation: return MotionUiTranslation::tr("Rotation (degrees)");
    case Property::Opacity: return MotionUiTranslation::tr("Opacity");
    }
    return MotionUiTranslation::tr("Transform");
}

} // namespace

AudioKeyframeDialog::AudioKeyframeDialog(
    const QString& layer_name,
    QWidget* parent, bool allow_opacity)
    : QDialog(parent)
{
    setObjectName(QStringLiteral("motion-audio-keyframe-dialog"));
    setWindowTitle(MotionUiTranslation::tr("Generate Keyframes from Audio"));
    setModal(true);
    resize(540, 250);

    auto* layout = new QVBoxLayout(this);
    auto* description = new QLabel(
        MotionUiTranslation::tr("Audio volume will create editable keyframes for the selected layer."),
        this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto* scale_note = new QLabel(MotionUiTranslation::tr("Scale replaces both axis curves with the same animation, regardless of the link."), this);
    scale_note->setObjectName(QStringLiteral("motion-audio-scale-note"));
    scale_note->setWordWrap(true);
    layout->addWidget(scale_note);

    auto* selected_layer = new QLabel(
        MotionUiTranslation::tr("Selected layer: %1").arg(layer_name), this);
    selected_layer->setObjectName(QStringLiteral("motion-audio-keyframe-layer"));
    layout->addWidget(selected_layer);

    auto* form = new QFormLayout;
    auto* source_row = new QWidget(this);
    auto* source_layout = new QHBoxLayout(source_row);
    source_layout->setContentsMargins(0, 0, 0, 0);
    audio_path_ = new QLineEdit(source_row);
    audio_path_->setObjectName(QStringLiteral("motion-audio-keyframe-path"));
    audio_path_->setPlaceholderText(MotionUiTranslation::tr("Choose an audio file"));
    auto* browse = new QPushButton(MotionUiTranslation::tr("Browse..."), source_row);
    browse->setObjectName(QStringLiteral("motion-audio-keyframe-browse"));
    source_layout->addWidget(audio_path_, 1);
    source_layout->addWidget(browse);
    form->addRow(MotionUiTranslation::tr("Audio file"), source_row);

    property_ = new QComboBox(this);
    property_->setObjectName(QStringLiteral("motion-audio-keyframe-property"));
    for (const auto property : kProperties) {
        if (!allow_opacity && property == Property::Opacity) continue;
        property_->addItem(propertyName(property), static_cast<int>(property));
    }
    property_->setCurrentIndex(2);
    form->addRow(MotionUiTranslation::tr("Property"), property_);

    minimum_value_ = new QDoubleSpinBox(this);
    minimum_value_->setObjectName(QStringLiteral("motion-audio-keyframe-minimum"));
    minimum_value_->setDecimals(6);
    minimum_value_->setKeyboardTracking(false);
    form->addRow(MotionUiTranslation::tr("Minimum value"), minimum_value_);

    maximum_value_ = new QDoubleSpinBox(this);
    maximum_value_->setObjectName(QStringLiteral("motion-audio-keyframe-maximum"));
    maximum_value_->setDecimals(6);
    maximum_value_->setKeyboardTracking(false);
    form->addRow(MotionUiTranslation::tr("Maximum value"), maximum_value_);
    layout->addLayout(form);

    auto* behavior = new QLabel(
        MotionUiTranslation::tr("Audio starts at the layer's first frame. If it ends early, the property returns to the minimum value."),
        this);
    behavior->setWordWrap(true);
    layout->addWidget(behavior);

    validation_message_ = new QLabel(this);
    validation_message_->setObjectName(QStringLiteral("motion-audio-keyframe-validation"));
    validation_message_->setWordWrap(true);
    layout->addWidget(validation_message_);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->setObjectName(QStringLiteral("motion-audio-keyframe-buttons"));
    buttons->button(QDialogButtonBox::Ok)->setObjectName(
        QStringLiteral("motion-audio-keyframe-accept"));
    buttons->button(QDialogButtonBox::Ok)->setText(MotionUiTranslation::tr("OK"));
    buttons->button(QDialogButtonBox::Cancel)->setText(MotionUiTranslation::tr("Cancel"));
    layout->addWidget(buttons);

    connect(browse, &QPushButton::clicked, this, [this] { browseForAudio(); });
    connect(audio_path_, &QLineEdit::textChanged, this, [this] { validateInputs(); });
    connect(property_, &QComboBox::currentIndexChanged, this,
            [this](int index) { setPropertyDefaults(index); });
    connect(minimum_value_, &QDoubleSpinBox::valueChanged,
            this, [this] { validateInputs(); });
    connect(maximum_value_, &QDoubleSpinBox::valueChanged,
            this, [this] { validateInputs(); });
    connect(buttons, &QDialogButtonBox::accepted, this, [this] { accept(); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    setPropertyDefaults(property_->currentIndex());
    validateInputs();
}

AudioKeyframeDialogSettings AudioKeyframeDialog::settings() const
{
    AudioKeyframeDialogSettings result;
    result.audio_path = audio_path_->text().trimmed();
    result.property = static_cast<Property>(property_->currentData().toInt());
    result.minimum_value = minimum_value_->value();
    result.maximum_value = maximum_value_->value();
    if(result.property==Property::Scale){result.minimum_value/=100.;result.maximum_value/=100.;}
    return result;
}

void AudioKeyframeDialog::accept()
{
    validateInputs();
    auto* buttons = findChild<QDialogButtonBox*>(
        QStringLiteral("motion-audio-keyframe-buttons"));
    auto* ok = buttons != nullptr ? buttons->button(QDialogButtonBox::Ok) : nullptr;
    if (ok == nullptr || !ok->isEnabled()) {
        return;
    }
    QDialog::accept();
}

void AudioKeyframeDialog::browseForAudio()
{
    const auto selected = QFileDialog::getOpenFileName(
        this, MotionUiTranslation::tr("Choose Audio File"), audio_path_->text(),
        MotionUiTranslation::tr("Audio files (*.wav *.mp3 *.flac *.aac *.m4a *.ogg *.opus *.wma);;All files (*)"));
    if (!selected.isEmpty()) audio_path_->setText(selected);
}

void AudioKeyframeDialog::setPropertyDefaults(int property_index)
{
    if (property_index < 0 || property_index >= static_cast<int>(kProperties.size())) return;
    const auto property = kProperties[static_cast<std::size_t>(property_index)];
    const QSignalBlocker minimum_blocker(minimum_value_);
    const QSignalBlocker maximum_blocker(maximum_value_);

    double minimum = 0.0;
    double maximum = 1.0;
    double allowed_minimum = -1'000'000'000.0;
    double allowed_maximum = 1'000'000'000.0;
    switch (property) {
    case Property::PositionX:
    case Property::PositionY:
        // Position is normalized to the composition canvas, not measured in pixels.
        minimum = 0.0;
        maximum = 1.0;
        break;
    case Property::Scale:
        minimum = 100.0;
        maximum = 200.0;
        allowed_minimum = 0.0001;
        allowed_maximum = 100'000'000.;
        break;
    case Property::Rotation:
        minimum = 0.0;
        maximum = 360.0;
        break;
    case Property::Opacity:
        minimum = 0.0;
        maximum = 1.0;
        allowed_minimum = 0.0;
        allowed_maximum = 1.0;
        break;
    }
    minimum_value_->setRange(allowed_minimum, allowed_maximum);
    maximum_value_->setRange(allowed_minimum, allowed_maximum);
    minimum_value_->setValue(minimum);
    maximum_value_->setValue(maximum);
    validateInputs();
}

void AudioKeyframeDialog::validateInputs()
{
    const auto path = audio_path_->text().trimmed();
    QString message;
    bool valid = !path.isEmpty();
    if (valid) {
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile() || !info.isReadable()) {
            valid = false;
            message = MotionUiTranslation::tr("Choose an existing, readable audio file.");
        }
    } else {
        message = MotionUiTranslation::tr("Choose an audio file to continue.");
    }

    if (minimum_value_->value() >= maximum_value_->value()) {
        valid = false;
        message = MotionUiTranslation::tr("The minimum value must be less than the maximum value.");
    }
    if (property_->currentData().toInt() == static_cast<int>(Property::Scale) &&
        minimum_value_->value() <= 0.0) {
        valid = false;
        message = MotionUiTranslation::tr("Scale values must be greater than zero.");
    }
    validation_message_->setText(message);
    if (auto* buttons = findChild<QDialogButtonBox*>(
            QStringLiteral("motion-audio-keyframe-buttons")); buttons != nullptr) {
        buttons->button(QDialogButtonBox::Ok)->setEnabled(valid);
    }
}

} // namespace motion::ui
