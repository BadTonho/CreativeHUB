#include "camera_raw_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("CameraRawPage", text);
}

// Every numeric control: object name suffix, label, range, decimals, default.
struct NumberSpec {
    const char* name;
    const char* label;
    double minimum;
    double maximum;
    int decimals;
    double default_value;
};

QDoubleSpinBox* makeNumber(QWidget* parent, const NumberSpec& spec) {
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(QStringLiteral("colorCameraRaw%1").arg(QLatin1String(spec.name)));
    box->setRange(spec.minimum, spec.maximum);
    box->setDecimals(spec.decimals);
    box->setValue(spec.default_value);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setAlignment(Qt::AlignCenter);
    box->setFixedWidth(64);
    box->setKeyboardTracking(false);
    box->setProperty("defaultValue", spec.default_value);
    return box;
}

QLabel* makeLabel(QWidget* parent, const char* text) {
    auto* label = new QLabel(tr(text), parent);
    label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return label;
}

}  // namespace

CameraRawPage::CameraRawPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorCameraRawPage");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Camera Raw"), this);
    title->setObjectName("colorCameraRawTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    camera_format_ = new QLabel(this);
    camera_format_->setObjectName("colorCameraRawFormat");
    header->addWidget(camera_format_);
    auto* more = new QPushButton(QStringLiteral("…"), this);
    more->setObjectName("colorCameraRawMoreButton");
    more->setAccessibleName(tr("More Camera Raw actions"));
    more->setFixedWidth(32);
    header->addWidget(more);
    root->addLayout(header);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(16);

    // Column 1: decode settings.
    auto* decode_form = new QFormLayout;
    decode_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto* quality = new QComboBox(this);
    quality->setObjectName("colorCameraRawDecodeQuality");
    quality->addItem(tr("Use project setting"));
    quality->addItem(tr("Full"));
    quality->addItem(tr("Half"));
    quality->addItem(tr("Quarter"));
    quality->addItem(tr("Eighth"));
    decode_form->addRow(tr("Decode Quality"), quality);
    auto* decode_using = new QComboBox(this);
    decode_using->setObjectName("colorCameraRawDecodeUsing");
    decode_using->addItem(tr("Camera default"));
    decode_using->addItem(tr("Clip"));
    decode_form->addRow(tr("Decode Using"), decode_using);
    const NumberSpec decode_numbers[] = {
        {"Lift", "Lift", -100.0, 100.0, 2, 0.0},
        {"Gain", "Gain", -100.0, 100.0, 2, 0.0},
        {"Contrast", "Contrast", -100.0, 100.0, 2, 0.0},
    };
    for (const auto& spec : decode_numbers)
        decode_form->addRow(tr(spec.label), makeNumber(this, spec));
    auto* monochrome = new QCheckBox(tr("Decode as monochrome"), this);
    monochrome->setObjectName("colorCameraRawMonochrome");
    auto* log_profile = new QCheckBox(tr("Decode as log profile"), this);
    log_profile->setObjectName("colorCameraRawLogProfile");
    decode_form->addRow(QString(), monochrome);
    decode_form->addRow(QString(), log_profile);
    columns->addLayout(decode_form, 1);

    auto addSeparator = [this, columns] {
        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Plain);
        columns->addWidget(line);
    };
    addSeparator();

    // Column 2: develop settings.
    auto* develop_form = new QFormLayout;
    develop_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    const NumberSpec develop_numbers[] = {
        {"ColorTemp", "Color Temp", 1000.0, 20000.0, 0, 2000.0},
        {"Tint", "Tint", -100.0, 100.0, 2, 0.0},
        {"Exposure", "Exposure", 0.0, 10000.0, 0, 160.0},
        {"Sharpness", "Sharpness", 0.0, 100.0, 2, 10.0},
        {"Highlights", "Highlights", -100.0, 100.0, 2, 0.0},
        {"FinetuneRed", "Finetune Red", 0.0, 200.0, 2, 100.0},
        {"FinetuneGreen", "Finetune Green", 0.0, 200.0, 2, 100.0},
        {"FinetuneBlue", "Finetune Blue", 0.0, 200.0, 2, 50.0},
    };
    for (const auto& spec : develop_numbers)
        develop_form->addRow(tr(spec.label), makeNumber(this, spec));
    columns->addLayout(develop_form, 1);
    addSeparator();

    // Column 3: color settings.
    auto* color_form = new QFormLayout;
    color_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    const NumberSpec color_numbers[] = {
        {"Shadows", "Shadows", -100.0, 100.0, 2, 0.0},
        {"ColorBoost", "Color Boost", -100.0, 100.0, 2, 0.0},
        {"Saturation", "Saturation", -100.0, 100.0, 2, 0.0},
        {"MidtoneDetail", "Midtone Detail", -100.0, 100.0, 2, 0.0},
    };
    for (const auto& spec : color_numbers)
        color_form->addRow(tr(spec.label), makeNumber(this, spec));
    columns->addLayout(color_form, 1);
    root->addLayout(columns, 1);

    auto* footer_line = new QFrame(this);
    footer_line->setFrameShape(QFrame::HLine);
    footer_line->setFrameShadow(QFrame::Plain);
    root->addWidget(footer_line);
    auto* footer = new QHBoxLayout;
    footer->addStretch();
    auto* use_changes = new QPushButton(tr("Use Changes"), this);
    use_changes->setObjectName("colorCameraRawUseChangesButton");
    auto* use_settings = new QPushButton(tr("Use Settings"), this);
    use_settings->setObjectName("colorCameraRawUseSettingsButton");
    footer->addWidget(use_changes);
    footer->addWidget(use_settings);
    root->addLayout(footer);

    connect(decode_using, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](int) { updateDecodeControlsEnabled(); });
    // Visual stage: Use Settings only restores the local control defaults.
    connect(use_settings, &QPushButton::clicked, this, [this] { restoreDefaults(); });
    updateDecodeControlsEnabled();
}

void CameraRawPage::setCameraFormat(const QString& text) {
    camera_format_->setText(text);
}

void CameraRawPage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    for (auto* combo : findChildren<QComboBox*>()) combo->setCurrentIndex(0);
    for (auto* check : findChildren<QCheckBox*>()) check->setChecked(false);
}

void CameraRawPage::updateDecodeControlsEnabled() {
    // Per-clip decode overrides apply only when decoding with the clip's own
    // settings; the camera default leaves them untouched.
    const auto* using_box = findChild<QComboBox*>("colorCameraRawDecodeUsing");
    const bool clip_settings = using_box != nullptr && using_box->currentIndex() == 1;
    for (const char* name : {"colorCameraRawLift", "colorCameraRawGain",
             "colorCameraRawContrast", "colorCameraRawMonochrome",
             "colorCameraRawLogProfile"}) {
        if (auto* control = findChild<QWidget*>(QLatin1String(name)))
            control->setEnabled(clip_settings);
    }
}

}  // namespace ui
