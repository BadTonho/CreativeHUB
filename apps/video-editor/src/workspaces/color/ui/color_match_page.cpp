#include "color_match_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <array>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("ColorMatchPage", text);
}

constexpr int kPatchCount = 24;
constexpr int kColumns = 6;

// Approximate sRGB display values of a 24-patch reference chart in standard
// order (row 1 left to right, then rows 2 to 4).
constexpr std::array<unsigned, kPatchCount> kChartRgb{{
    0x735244, 0xC29682, 0x627A9D, 0x576C43, 0x8580B1, 0x67BDAA,
    0xD67E2C, 0x505BA6, 0xC15A63, 0x5E3C6C, 0x9DBC40, 0xE0A32E,
    0x383D96, 0x469449, 0xAF363C, 0xE7C71F, 0xBB5695, 0x0885A1,
    0xF3F3F2, 0xC8C8C8, 0xA0A0A0, 0x7A7A79, 0x555555, 0x343434,
}};

// Display position -> chart patch index, one table per preset. Presets differ
// only in how the patches are laid out on the page.
constexpr std::array<int, kPatchCount> kLegacyOrder{{
    0, 1, 2, 3, 4, 5,
    6, 7, 8, 9, 10, 11,
    12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23,
}};
constexpr std::array<int, kPatchCount> kMirroredOrder{{
    5, 4, 3, 2, 1, 0,
    6, 7, 8, 9, 10, 11,
    17, 16, 15, 14, 13, 12,
    18, 19, 20, 21, 22, 23,
}};

constexpr double kDefaultTemperature = 6500.0;
constexpr double kDefaultWhiteLevel = 0.9;

QComboBox* makeGammaCombo(QWidget* parent, const char* object_name) {
    auto* combo = new QComboBox(parent);
    combo->setObjectName(QLatin1String(object_name));
    combo->addItem(tr("Auto"));
    combo->addItem(tr("Gamma 2.2"));
    combo->addItem(tr("Gamma 2.4"));
    combo->addItem(tr("Gamma 2.6"));
    combo->addItem(tr("sRGB"));
    combo->addItem(tr("Rec.709"));
    combo->addItem(tr("Linear"));
    return combo;
}

}  // namespace

ColorMatchPage::ColorMatchPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorColorMatchPage");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Color Match"), this);
    title->setObjectName("colorColorMatchTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* preset = new QComboBox(this);
    preset->setObjectName("colorColorMatchPreset");
    preset->addItem(tr("24-patch chart"));
    preset->addItem(tr("24-patch chart - Legacy"));
    header->addWidget(preset);
    auto* reset = new QPushButton(QStringLiteral("↺"), this);
    reset->setObjectName("colorColorMatchResetButton");
    reset->setAccessibleName(tr("Reset Color Match"));
    reset->setFixedWidth(32);
    header->addWidget(reset);
    auto* more = new QPushButton(QStringLiteral("…"), this);
    more->setObjectName("colorColorMatchMoreButton");
    more->setAccessibleName(tr("More Color Match actions"));
    more->setFixedWidth(32);
    header->addWidget(more);
    root->addLayout(header);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    // Left: the 4 x 6 chart of reference patches.
    auto* chart = new QWidget(this);
    chart->setObjectName("colorColorMatchChart");
    auto* grid = new QGridLayout(chart);
    grid->setSpacing(10);
    grid->setContentsMargins(8, 8, 8, 8);
    for (int position = 0; position < kPatchCount; ++position) {
        auto* patch = new QFrame(chart);
        patch->setObjectName(QStringLiteral("colorColorMatchPatch%1").arg(position + 1));
        patch->setMinimumSize(32, 26);
        patch->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        patch->setMaximumSize(64, 52);
        grid->addWidget(patch, position / kColumns, position % kColumns);
    }
    body->addWidget(chart, 3);

    auto* separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    body->addWidget(separator);

    // Right: gamma, color space, temperature, white level and the action.
    auto* options = new QVBoxLayout;
    options->setSpacing(4);
    options->addWidget(new QLabel(tr("Source Gamma"), this));
    options->addWidget(makeGammaCombo(this, "colorColorMatchSourceGamma"));
    options->addWidget(new QLabel(tr("Target Gamma"), this));
    options->addWidget(makeGammaCombo(this, "colorColorMatchTargetGamma"));
    options->addWidget(new QLabel(tr("Target Color Space"), this));
    auto* space = new QComboBox(this);
    space->setObjectName("colorColorMatchTargetColorSpace");
    space->addItem(tr("Auto"));
    space->addItem(tr("Rec.709"));
    space->addItem(tr("Rec.2020"));
    space->addItem(tr("DCI-P3 D65"));
    space->addItem(tr("sRGB"));
    options->addWidget(space);

    auto* temperature_row = new QGridLayout;
    temperature_row->addWidget(new QLabel(tr("Color Temp"), this), 0, 0);
    auto* white_label = new QLabel(tr("White Level"), this);
    white_label->setObjectName("colorColorMatchWhiteLevelLabel");
    temperature_row->addWidget(white_label, 0, 2);
    auto* temperature = new QSpinBox(this);
    temperature->setObjectName("colorColorMatchTemperature");
    temperature->setRange(1000, 20000);
    temperature->setValue(static_cast<int>(kDefaultTemperature));
    temperature->setButtonSymbols(QAbstractSpinBox::NoButtons);
    temperature->setAlignment(Qt::AlignCenter);
    temperature->setFixedWidth(64);
    temperature_row->addWidget(temperature, 1, 0);
    temperature_row->addWidget(new QLabel(tr("K"), this), 1, 1);
    auto* white = new QDoubleSpinBox(this);
    white->setObjectName("colorColorMatchWhiteLevel");
    white->setRange(0.0, 1.0);
    white->setDecimals(1);
    white->setSingleStep(0.1);
    white->setValue(kDefaultWhiteLevel);
    white->setButtonSymbols(QAbstractSpinBox::NoButtons);
    white->setAlignment(Qt::AlignCenter);
    white->setFixedWidth(48);
    white->setEnabled(false);
    temperature_row->addWidget(white, 1, 2);
    auto* white_enabled = new QCheckBox(this);
    white_enabled->setObjectName("colorColorMatchWhiteLevelEnabled");
    white_enabled->setAccessibleName(tr("Use White Level"));
    temperature_row->addWidget(white_enabled, 1, 3);
    temperature_row->setColumnStretch(1, 1);
    options->addLayout(temperature_row);
    options->addStretch(1);
    auto* match = new QPushButton(tr("Match"), this);
    match->setObjectName("colorColorMatchMatchButton");
    options->addWidget(match);
    body->addLayout(options, 1);
    root->addLayout(body, 1);

    // Paint the patches for the active preset's layout.
    auto paintChart = [this, preset, chart] {
        const auto& order = preset->currentIndex() == 1 ? kLegacyOrder : kMirroredOrder;
        for (int position = 0; position < kPatchCount; ++position) {
            auto* patch = chart->findChild<QFrame*>(
                QStringLiteral("colorColorMatchPatch%1").arg(position + 1));
            if (patch == nullptr) continue;
            const unsigned rgb = kChartRgb[static_cast<std::size_t>(order[static_cast<std::size_t>(position)])];
            patch->setProperty("patchRgb", rgb);
            patch->setStyleSheet(QStringLiteral(
                "QFrame { background: #%1; border: 2px solid #0d0f12; border-radius: 6px; }")
                    .arg(rgb, 6, 16, QLatin1Char('0')));
        }
    };
    connect(preset, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [paintChart](int) { paintChart(); });
    connect(white_enabled, &QCheckBox::toggled, white, &QWidget::setEnabled);
    connect(reset, &QPushButton::clicked, this, [this] { restoreDefaults(); });
    paintChart();
}

void ColorMatchPage::restoreDefaults() {
    for (auto* combo : findChildren<QComboBox*>()) combo->setCurrentIndex(0);
    if (auto* temperature = findChild<QSpinBox*>("colorColorMatchTemperature"))
        temperature->setValue(static_cast<int>(kDefaultTemperature));
    if (auto* white = findChild<QDoubleSpinBox*>("colorColorMatchWhiteLevel"))
        white->setValue(kDefaultWhiteLevel);
    if (auto* enabled = findChild<QCheckBox*>("colorColorMatchWhiteLevelEnabled"))
        enabled->setChecked(false);
}

}  // namespace ui
