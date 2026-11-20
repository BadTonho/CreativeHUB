#include "primaries_bars_page.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("PrimariesBarsPage", text);
}

constexpr double kSliderScale = 100.0;

QDoubleSpinBox* makeSpin(QWidget* parent, const QString& name, double minimum,
    double maximum, int decimals, double default_value, int width = 56) {
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(minimum, maximum);
    box->setDecimals(decimals);
    box->setValue(default_value);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setAlignment(Qt::AlignCenter);
    box->setFixedWidth(width);
    box->setKeyboardTracking(false);
    box->setProperty("defaultValue", default_value);
    return box;
}

// Thin decorative bar under a field hinting at what the control shifts.
QFrame* makeHintBar(QWidget* parent, const QString& from, const QString& to) {
    auto* bar = new QFrame(parent);
    bar->setFixedHeight(3);
    bar->setStyleSheet(QStringLiteral(
        "QFrame { border: none; border-radius: 1px;"
        " background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %1, stop:1 %2); }")
            .arg(from, to));
    return bar;
}

// A field with its hint bar stacked underneath.
QWidget* makeFieldColumn(QWidget* parent, QDoubleSpinBox* box, const QString& from,
    const QString& to) {
    auto* column = new QWidget(parent);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);
    box->setParent(column);
    layout->addWidget(box);
    layout->addWidget(makeHintBar(column, from, to));
    return column;
}

QPushButton* makeIconButton(QWidget* parent, const QString& text, const QString& name,
    const QString& accessible_name, int width = 28) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setAccessibleName(accessible_name);
    button->setToolTip(accessible_name);
    button->setFixedSize(width, 26);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

struct Channel {
    const char* id;      // object-name suffix
    const char* color;   // handle and hint-bar color
    const char* key;     // stylesheet property value
};

constexpr Channel kMaster{"Y", "#e6ebf0", "y"};
constexpr Channel kRed{"R", "#d9423f", "r"};
constexpr Channel kGreen{"G", "#3fbf5a", "g"};
constexpr Channel kBlue{"B", "#4a5fd9", "b"};

struct GroupSpec {
    const char* name;
    double minimum;
    double maximum;
    double default_value;
    bool has_master;
    bool has_sampler;
};

}  // namespace

PrimariesBarsPage::PrimariesBarsPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorPrimariesBarsPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorPrimariesBarsPage QDoubleSpinBox { padding: 2px 2px; }
        QWidget#colorPrimariesBarsPage QSlider::groove:vertical {
            background: #14181e; border: 1px solid #39424e; border-radius: 5px; width: 14px;
        }
        QWidget#colorPrimariesBarsPage QSlider::handle:vertical {
            background: #e6ebf0; height: 8px; margin: 0 -3px; border-radius: 2px;
        }
        QWidget#colorPrimariesBarsPage QSlider[channel="r"]::handle:vertical { background: #d9423f; }
        QWidget#colorPrimariesBarsPage QSlider[channel="g"]::handle:vertical { background: #3fbf5a; }
        QWidget#colorPrimariesBarsPage QSlider[channel="b"]::handle:vertical { background: #4a5fd9; }
        QWidget#colorPrimariesBarsPage QSlider::groove:horizontal {
            background: #14181e; border: 1px solid #39424e; border-radius: 5px; height: 12px;
        }
        QWidget#colorPrimariesBarsPage QSlider::handle:horizontal {
            background: #6b7684; width: 14px; margin: -2px 0; border-radius: 4px;
        }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    // Header: title, mode switch (Wheels / Bars / Log) and Reset.
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Primaries - Color Bars"), this);
    title->setObjectName("colorPrimariesBarsTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* modes = new QButtonGroup(this);
    modes->setExclusive(true);
    const struct { const char* glyph; const char* object; const char* label; } mode_specs[] = {
        {"◎", "colorPrimariesBarsWheelsModeButton", "Wheels"},
        {"∣∣", "colorPrimariesBarsBarsModeButton", "Bars"},
        {"LOG", "colorPrimariesBarsLogModeButton", "Log"},
    };
    int mode_id = 0;
    for (const auto& spec : mode_specs) {
        auto* mode = makeIconButton(this, QString::fromUtf8(spec.glyph),
            QLatin1String(spec.object), tr(spec.label), 36);
        mode->setCheckable(true);
        modes->addButton(mode, mode_id++);
        header->addWidget(mode);
    }
    modes->button(1)->setChecked(true);
    auto* reset_all = makeIconButton(this, QStringLiteral("↺"),
        QStringLiteral("colorPrimariesBarsResetButton"), tr("Reset Primaries"));
    header->addWidget(reset_all);
    root->addLayout(header);

    // Top row: auto balance, picker and the global controls.
    auto* top = new QHBoxLayout;
    top->setSpacing(10);
    top->addWidget(makeIconButton(this, QStringLiteral("A"),
        QStringLiteral("colorPrimariesBarsAutoButton"), tr("Auto balance")));
    top->addWidget(makeIconButton(this, QStringLiteral("⌖"),
        QStringLiteral("colorPrimariesBarsPickButton"), tr("Pick white point")));
    top->addStretch();
    const struct {
        const char* label; const char* name; double minimum; double maximum;
        int decimals; double default_value; const char* from; const char* to;
    } top_specs[] = {
        {"Temp", "Temp", -4000.0, 4000.0, 1, 0.0, "#4a7fd9", "#e09a3a"},
        {"Tint", "Tint", -100.0, 100.0, 2, 0.0, "#3fbf5a", "#c24fb0"},
        {"Contrast", "Contrast", 0.0, 2.0, 3, 1.0, "#000000", "#ffffff"},
        {"Pivot", "Pivot", 0.0, 1.0, 3, 0.435, "#000000", "#ffffff"},
        {"Mid/Detail", "MidDetail", -100.0, 100.0, 2, 0.0, "#3a424e", "#9aa4b0"},
    };
    for (const auto& spec : top_specs) {
        top->addWidget(new QLabel(tr(spec.label), this));
        auto* box = makeSpin(this,
            QStringLiteral("colorPrimariesBars%1").arg(QLatin1String(spec.name)),
            spec.minimum, spec.maximum, spec.decimals, spec.default_value, 60);
        top->addWidget(makeFieldColumn(this, box, QLatin1String(spec.from),
            QLatin1String(spec.to)));
    }
    root->addLayout(top);

    // Middle: Lift / Gamma / Gain / Offset bar groups.
    auto* groups = new QHBoxLayout;
    groups->setSpacing(10);
    const GroupSpec group_specs[] = {
        {"Lift", -1.0, 1.0, 0.0, true, true},
        {"Gamma", -1.0, 1.0, 0.0, true, false},
        {"Gain", 0.0, 4.0, 1.0, true, true},
        {"Offset", 0.0, 50.0, 25.0, false, false},
    };
    for (const auto& group : group_specs) {
        auto* column = new QVBoxLayout;
        column->setSpacing(4);
        const QString group_name = QLatin1String(group.name);
        auto* group_header = new QHBoxLayout;
        if (group.has_sampler) {
            group_header->addWidget(makeIconButton(this, QStringLiteral("⌖"),
                QStringLiteral("colorPrimariesBars%1SampleButton").arg(group_name),
                tr("Sample"), 24));
        } else {
            group_header->addSpacing(24);
        }
        group_header->addStretch();
        auto* group_title = new QLabel(tr(group.name), this);
        group_header->addWidget(group_title);
        group_header->addStretch();
        auto* group_reset = makeIconButton(this, QStringLiteral("↺"),
            QStringLiteral("colorPrimariesBars%1ResetButton").arg(group_name),
            tr("Reset group"), 24);
        group_header->addWidget(group_reset);
        column->addLayout(group_header);

        const Channel channels_with_master[] = {kMaster, kRed, kGreen, kBlue};
        const Channel channels_rgb[] = {kRed, kGreen, kBlue};
        const Channel* channels = group.has_master ? channels_with_master : channels_rgb;
        const int channel_count = group.has_master ? 4 : 3;
        auto* sliders = new QHBoxLayout;
        sliders->setSpacing(6);
        auto* values = new QHBoxLayout;
        values->setSpacing(6);
        for (int i = 0; i < channel_count; ++i) {
            const Channel& channel = channels[i];
            const QString suffix = group_name + QLatin1String(channel.id);
            auto* slider = new QSlider(Qt::Vertical, this);
            slider->setObjectName(QStringLiteral("colorPrimariesBars%1Slider").arg(suffix));
            slider->setProperty("channel", QLatin1String(channel.key));
            slider->setRange(static_cast<int>(std::lround(group.minimum * kSliderScale)),
                static_cast<int>(std::lround(group.maximum * kSliderScale)));
            slider->setValue(static_cast<int>(std::lround(group.default_value * kSliderScale)));
            slider->setFixedHeight(96);
            slider->setFocusPolicy(Qt::NoFocus);
            sliders->addWidget(slider, 0, Qt::AlignHCenter);
            auto* box = makeSpin(this,
                QStringLiteral("colorPrimariesBars%1Value").arg(suffix),
                group.minimum, group.maximum, 2, group.default_value, 48);
            values->addWidget(makeFieldColumn(this, box, QLatin1String(channel.color),
                QLatin1String(channel.color)));
            // Slider and field mirror each other; the field is authoritative.
            connect(slider, &QSlider::valueChanged, box,
                [box](int value) { box->setValue(value / kSliderScale); });
            connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), slider,
                [slider](double value) {
                    slider->setValue(static_cast<int>(std::lround(value * kSliderScale)));
                });
            slider->setProperty("defaultValue", group.default_value);
        }
        column->addLayout(sliders);
        column->addLayout(values);
        // Master strip (visual stand-in for the rotary master control).
        auto* master = new QSlider(Qt::Horizontal, this);
        master->setObjectName(QStringLiteral("colorPrimariesBars%1MasterStrip").arg(group_name));
        master->setRange(-100, 100);
        master->setFocusPolicy(Qt::NoFocus);
        master->setProperty("defaultValue", 0.0);
        column->addWidget(master);
        groups->addLayout(column, 1);
        connect(group_reset, &QPushButton::clicked, this, [this, group_name] {
            for (auto* box : findChildren<QDoubleSpinBox*>(
                     QRegularExpression(QStringLiteral("^colorPrimariesBars%1[YRGB]Value$")
                                            .arg(group_name))))
                box->setValue(box->property("defaultValue").toDouble());
        });
    }
    root->addLayout(groups, 1);

    // Bottom row: secondary global controls.
    auto* bottom = new QHBoxLayout;
    bottom->setSpacing(8);
    const struct {
        const char* label; const char* name; double minimum; double maximum;
        double default_value; const char* from; const char* to;
    } bottom_specs[] = {
        {"Col Boost", "ColBoost", 0.0, 100.0, 0.0, "#3a424e", "#d9423f"},
        {"Shad", "Shad", -100.0, 100.0, 0.0, "#000000", "#6b7684"},
        {"Hi/Light", "HiLight", -100.0, 100.0, 0.0, "#6b7684", "#ffffff"},
        {"Sat", "Sat", 0.0, 100.0, 50.0, "#6b7684", "#d9423f"},
        {"Hue", "Hue", 0.0, 100.0, 50.0, "#d9423f", "#4a5fd9"},
        {"L. Mix", "LMix", 0.0, 100.0, 100.0, "#3a424e", "#e6ebf0"},
    };
    for (const auto& spec : bottom_specs) {
        bottom->addWidget(new QLabel(tr(spec.label), this));
        auto* box = makeSpin(this,
            QStringLiteral("colorPrimariesBars%1").arg(QLatin1String(spec.name)),
            spec.minimum, spec.maximum, 2, spec.default_value, 56);
        bottom->addWidget(makeFieldColumn(this, box, QLatin1String(spec.from),
            QLatin1String(spec.to)));
    }
    root->addLayout(bottom);

    connect(reset_all, &QPushButton::clicked, this, [this] { restoreDefaults(); });
}

void PrimariesBarsPage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    for (auto* strip : findChildren<QSlider*>(QRegularExpression(QStringLiteral("MasterStrip$"))))
        strip->setValue(0);
}

}  // namespace ui
