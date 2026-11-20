#include "motion_effects_page.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("MotionEffectsPage", text);
}

QComboBox* makeCombo(QWidget* parent, const QString& name, const QStringList& items) {
    auto* combo = new QComboBox(parent);
    combo->setObjectName(name);
    combo->addItems(items);
    return combo;
}

QDoubleSpinBox* makeNumber(QWidget* parent, const QString& name, double maximum,
    double default_value) {
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(0.0, maximum);
    box->setDecimals(1);
    box->setValue(default_value);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setAlignment(Qt::AlignCenter);
    box->setFixedWidth(56);
    box->setKeyboardTracking(false);
    box->setProperty("defaultValue", default_value);
    return box;
}

QPushButton* makeResetButton(QWidget* parent, const QString& name) {
    auto* button = new QPushButton(QStringLiteral("↺"), parent);
    button->setObjectName(name);
    button->setAccessibleName(tr("Reset section"));
    button->setToolTip(tr("Reset section"));
    button->setFixedSize(24, 24);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

// Section title with its reset button; returns the reset button.
QPushButton* addSectionHeader(QVBoxLayout* column, QWidget* parent, const char* title,
    const QString& reset_name) {
    auto* row = new QHBoxLayout;
    auto* label = new QLabel(tr(title), parent);
    QFont font = label->font();
    font.setBold(true);
    label->setFont(font);
    row->addWidget(label);
    row->addStretch();
    auto* reset = makeResetButton(parent, reset_name);
    row->addWidget(reset);
    column->addLayout(row);
    return reset;
}

QFormLayout* makeForm() {
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return form;
}

QFrame* makeLine(QWidget* parent, QFrame::Shape shape) {
    auto* line = new QFrame(parent);
    line->setFrameShape(shape);
    line->setFrameShadow(QFrame::Plain);
    return line;
}

const QStringList kEstimationTypes{QStringLiteral("Faster"), QStringLiteral("Better")};
const QStringList kRanges{QStringLiteral("Small"), QStringLiteral("Medium"),
    QStringLiteral("Large")};

// Luma and Chroma fields joined by a link toggle; while linked, editing one
// mirrors the other.
void addLinkedPair(QVBoxLayout* column, QWidget* parent, const QString& prefix,
    QDoubleSpinBox*& luma, QDoubleSpinBox*& chroma) {
    auto* row = new QHBoxLayout;
    auto* link_column = new QVBoxLayout;
    link_column->setSpacing(0);
    link_column->addWidget(new QLabel(QStringLiteral("┌"), parent));
    auto* link = new QPushButton(QStringLiteral("⛓"), parent);
    link->setObjectName(prefix + QStringLiteral("Link"));
    link->setAccessibleName(tr("Link Luma and Chroma"));
    link->setToolTip(tr("Link Luma and Chroma"));
    link->setCheckable(true);
    link->setChecked(true);
    link->setFixedSize(24, 22);
    link->setFocusPolicy(Qt::NoFocus);
    link_column->addWidget(link);
    link_column->addWidget(new QLabel(QStringLiteral("└"), parent));
    row->addLayout(link_column);
    auto* form = makeForm();
    luma = makeNumber(parent, prefix + QStringLiteral("Luma"), 100.0, 0.0);
    chroma = makeNumber(parent, prefix + QStringLiteral("Chroma"), 100.0, 0.0);
    form->addRow(tr("Luma"), luma);
    form->addRow(tr("Chroma"), chroma);
    row->addLayout(form, 1);
    column->addLayout(row);
    auto* luma_ptr = luma;
    auto* chroma_ptr = chroma;
    QObject::connect(luma, qOverload<double>(&QDoubleSpinBox::valueChanged), parent,
        [link, luma_ptr, chroma_ptr](double value) {
            if (!link->isChecked()) return;
            const QSignalBlocker blocker(chroma_ptr);
            chroma_ptr->setValue(value);
        });
    QObject::connect(chroma, qOverload<double>(&QDoubleSpinBox::valueChanged), parent,
        [link, luma_ptr, chroma_ptr](double value) {
            if (!link->isChecked()) return;
            const QSignalBlocker blocker(luma_ptr);
            luma_ptr->setValue(value);
        });
}

}  // namespace

MotionEffectsPage::MotionEffectsPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorMotionEffectsPage");
    setStyleSheet(QStringLiteral(
        "QWidget#colorMotionEffectsPage QDoubleSpinBox { padding: 2px 2px; }"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Motion Effects"), this);
    title->setObjectName("colorMotionEffectsTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* reset_all = makeResetButton(this, QStringLiteral("colorMotionEffectsResetButton"));
    reset_all->setAccessibleName(tr("Reset Motion Effects"));
    reset_all->setToolTip(tr("Reset Motion Effects"));
    header->addWidget(reset_all);
    root->addLayout(header);

    auto* columns = new QHBoxLayout;
    columns->setSpacing(12);

    // Column 1: Temporal NR and Temporal Threshold.
    auto* temporal = new QVBoxLayout;
    auto* temporal_reset = addSectionHeader(temporal, this, "Temporal NR",
        QStringLiteral("colorMotionEffectsTemporalNRResetButton"));
    auto* temporal_form = makeForm();
    temporal_form->addRow(tr("Frames"), makeCombo(this,
        QStringLiteral("colorMotionEffectsTemporalFrames"),
        {QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3")}));
    temporal_form->addRow(tr("Mo. Est. Type"), makeCombo(this,
        QStringLiteral("colorMotionEffectsTemporalEstType"), kEstimationTypes));
    auto* temporal_range = makeCombo(this,
        QStringLiteral("colorMotionEffectsTemporalRange"), kRanges);
    temporal_range->setCurrentIndex(1);
    temporal_range->setProperty("defaultIndex", 1);
    temporal_form->addRow(tr("Motion Range"), temporal_range);
    temporal->addLayout(temporal_form);
    temporal->addWidget(makeLine(this, QFrame::HLine));
    auto* threshold_reset = addSectionHeader(temporal, this, "Temporal Threshold",
        QStringLiteral("colorMotionEffectsTemporalThresholdResetButton"));
    QDoubleSpinBox* temporal_luma = nullptr;
    QDoubleSpinBox* temporal_chroma = nullptr;
    addLinkedPair(temporal, this, QStringLiteral("colorMotionEffectsTemporal"),
        temporal_luma, temporal_chroma);
    auto* motion_form = makeForm();
    motion_form->addRow(tr("Motion"), makeNumber(this,
        QStringLiteral("colorMotionEffectsTemporalMotion"), 100.0, 50.0));
    temporal->addLayout(motion_form);
    temporal->addWidget(makeLine(this, QFrame::HLine));
    auto* blend_form = makeForm();
    blend_form->addRow(tr("Blend"), makeNumber(this,
        QStringLiteral("colorMotionEffectsTemporalBlend"), 1.0, 0.0));
    temporal->addLayout(blend_form);
    temporal->addStretch(1);
    columns->addLayout(temporal, 1);
    columns->addWidget(makeLine(this, QFrame::VLine));

    // Column 2: Spatial NR and Spatial Threshold.
    auto* spatial = new QVBoxLayout;
    auto* spatial_reset = addSectionHeader(spatial, this, "Spatial NR",
        QStringLiteral("colorMotionEffectsSpatialNRResetButton"));
    auto* spatial_form = makeForm();
    spatial_form->addRow(tr("Mode"), makeCombo(this,
        QStringLiteral("colorMotionEffectsSpatialMode"), kEstimationTypes));
    spatial_form->addRow(tr("Radius"), makeCombo(this,
        QStringLiteral("colorMotionEffectsSpatialRadius"), kRanges));
    spatial->addLayout(spatial_form);
    spatial->addWidget(makeLine(this, QFrame::HLine));
    auto* spatial_threshold_reset = addSectionHeader(spatial, this, "Spatial Threshold",
        QStringLiteral("colorMotionEffectsSpatialThresholdResetButton"));
    QDoubleSpinBox* spatial_luma = nullptr;
    QDoubleSpinBox* spatial_chroma = nullptr;
    addLinkedPair(spatial, this, QStringLiteral("colorMotionEffectsSpatial"),
        spatial_luma, spatial_chroma);
    spatial->addWidget(makeLine(this, QFrame::HLine));
    auto* spatial_blend_form = makeForm();
    spatial_blend_form->addRow(tr("Blend"), makeNumber(this,
        QStringLiteral("colorMotionEffectsSpatialBlend"), 1.0, 0.0));
    spatial->addLayout(spatial_blend_form);
    spatial->addStretch(1);
    columns->addLayout(spatial, 1);
    columns->addWidget(makeLine(this, QFrame::VLine));

    // Column 3: Motion Blur.
    auto* blur = new QVBoxLayout;
    auto* blur_reset = addSectionHeader(blur, this, "Motion Blur",
        QStringLiteral("colorMotionEffectsMotionBlurResetButton"));
    auto* blur_form = makeForm();
    blur_form->addRow(tr("Mo. Est. Type"), makeCombo(this,
        QStringLiteral("colorMotionEffectsBlurEstType"), kEstimationTypes));
    auto* blur_range = makeCombo(this, QStringLiteral("colorMotionEffectsBlurRange"), kRanges);
    blur_range->setCurrentIndex(1);
    blur_range->setProperty("defaultIndex", 1);
    blur_form->addRow(tr("Motion Range"), blur_range);
    blur_form->addRow(tr("Motion Blur"), makeNumber(this,
        QStringLiteral("colorMotionEffectsBlurAmount"), 100.0, 0.0));
    blur->addLayout(blur_form);
    blur->addStretch(1);
    columns->addLayout(blur, 1);
    root->addLayout(columns, 1);

    // Resets restore only the controls whose names match a section's pattern.
    const auto resetMatching = [this](const QString& pattern) {
        for (auto* box : findChildren<QDoubleSpinBox*>(QRegularExpression(pattern)))
            box->setValue(box->property("defaultValue").toDouble());
        for (auto* combo : findChildren<QComboBox*>(QRegularExpression(pattern)))
            combo->setCurrentIndex(combo->property("defaultIndex").toInt());
    };
    connect(temporal_reset, &QPushButton::clicked, this, [resetMatching] {
        resetMatching(QStringLiteral("^colorMotionEffectsTemporal(Frames|EstType|Range)$"));
    });
    connect(threshold_reset, &QPushButton::clicked, this, [resetMatching] {
        resetMatching(QStringLiteral("^colorMotionEffectsTemporal(Luma|Chroma|Motion|Blend)$"));
    });
    connect(spatial_reset, &QPushButton::clicked, this, [resetMatching] {
        resetMatching(QStringLiteral("^colorMotionEffectsSpatial(Mode|Radius)$"));
    });
    connect(spatial_threshold_reset, &QPushButton::clicked, this, [resetMatching] {
        resetMatching(QStringLiteral("^colorMotionEffectsSpatial(Luma|Chroma|Blend)$"));
    });
    connect(blur_reset, &QPushButton::clicked, this, [resetMatching] {
        resetMatching(QStringLiteral("^colorMotionEffects(BlurEstType|BlurRange|BlurAmount)$"));
    });
    connect(reset_all, &QPushButton::clicked, this, [this] { restoreDefaults(); });
    connect(findChild<QComboBox*>("colorMotionEffectsTemporalFrames"),
        qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](int) { updateTemporalControlsEnabled(); });
    updateTemporalControlsEnabled();
}

void MotionEffectsPage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    for (auto* combo : findChildren<QComboBox*>())
        combo->setCurrentIndex(combo->property("defaultIndex").toInt());
    for (auto* button : findChildren<QPushButton*>(QRegularExpression(QStringLiteral("Link$"))))
        button->setChecked(true);
}

void MotionEffectsPage::updateTemporalControlsEnabled() {
    // Temporal denoising needs neighboring frames: with Frames at 0 every
    // other temporal control is disabled.
    const auto* frames = findChild<QComboBox*>("colorMotionEffectsTemporalFrames");
    const bool active = frames != nullptr && frames->currentIndex() > 0;
    for (const char* name : {"colorMotionEffectsTemporalEstType",
             "colorMotionEffectsTemporalRange", "colorMotionEffectsTemporalLuma",
             "colorMotionEffectsTemporalChroma", "colorMotionEffectsTemporalMotion",
             "colorMotionEffectsTemporalBlend", "colorMotionEffectsTemporalLink"}) {
        if (auto* control = findChild<QWidget*>(QLatin1String(name)))
            control->setEnabled(active);
    }
}

}  // namespace ui
