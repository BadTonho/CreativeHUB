#include "blur_page.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>
#include <memory>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("BlurPage", text);
}

constexpr double kSliderScale = 100.0;

// Holds the three bars of one group and paints the mid line behind them.
class BarsRow final : public QWidget {
public:
    explicit BarsRow(QWidget* parent) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setPen(QPen(QColor("#39424e"), 1.0));
        const int y = height() / 2;
        painter.drawLine(0, y, width(), y);
    }
};

QPushButton* makeButton(QWidget* parent, const QString& text, const QString& name,
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
    const char* id;
    const char* color;
    const char* key;
};

constexpr Channel kChannels[] = {
    {"R", "#d9423f", "r"},
    {"G", "#3fbf5a", "g"},
    {"B", "#4a5fd9", "b"},
};

struct GroupSpec {
    const char* name;      // object-name stem
    const char* label;
    double default_value;
    bool link_enabled;
};

constexpr GroupSpec kGroups[] = {
    {"Radius", "Radius", 0.5, true},
    {"HVRatio", "H/V Ratio", 0.5, true},
    {"Scaling", "Scaling", 0.25, false},
};

}  // namespace

BlurPage::BlurPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorBlurPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorBlurPage QDoubleSpinBox { padding: 2px 2px; }
        QWidget#colorBlurPage QPushButton:checked { background: #2f5f8f; }
        QWidget#colorBlurPage QSlider::groove:vertical {
            background: #14181e; border: 1px solid #39424e; border-radius: 6px; width: 16px;
        }
        QWidget#colorBlurPage QSlider::handle:vertical {
            background: #e6ebf0; height: 8px; margin: 0 -3px; border-radius: 2px;
        }
        QWidget#colorBlurPage QSlider[channel="r"]::handle:vertical { background: #d9423f; }
        QWidget#colorBlurPage QSlider[channel="g"]::handle:vertical { background: #3fbf5a; }
        QWidget#colorBlurPage QSlider[channel="b"]::handle:vertical { background: #4a5fd9; }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    // Header: title, effect-kind buttons and reset.
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Blur - Blur"), this);
    title->setObjectName("colorBlurTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* kinds = new QButtonGroup(this);
    kinds->setExclusive(true);
    const struct { const char* glyph; const char* object; const char* label; } kind_specs[] = {
        {"●", "colorBlurBlurModeButton", "Blur"},
        {"▲", "colorBlurSharpenModeButton", "Sharpen"},
        {"◌", "colorBlurMistModeButton", "Mist"},
    };
    for (const auto& spec : kind_specs) {
        auto* kind = makeButton(this, QString::fromUtf8(spec.glyph), QLatin1String(spec.object),
            tr(spec.label), 36);
        kind->setCheckable(true);
        kinds->addButton(kind);
        header->addWidget(kind);
    }
    kinds->buttons().first()->setChecked(true);
    auto* reset_all = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorBlurResetButton"), tr("Reset Blur"));
    header->addWidget(reset_all);
    root->addLayout(header);

    // Three groups of three channel bars.
    auto* groups = new QHBoxLayout;
    groups->setSpacing(20);
    for (const auto& group : kGroups) {
        const QString name = QLatin1String(group.name);
        const QString prefix = QStringLiteral("colorBlur") + name;
        auto* column = new QVBoxLayout;
        column->setSpacing(6);

        auto* column_header = new QHBoxLayout;
        auto* link = makeButton(this, QStringLiteral("⛓"), prefix + QStringLiteral("Link"),
            tr("Link channels"), 24);
        link->setCheckable(true);
        link->setChecked(group.link_enabled);
        link->setEnabled(group.link_enabled);
        column_header->addWidget(link);
        column_header->addStretch();
        column_header->addWidget(new QLabel(tr(group.label), this));
        column_header->addStretch();
        auto* reset = makeButton(this, QStringLiteral("↺"), prefix + QStringLiteral("ResetButton"),
            tr("Reset group"), 24);
        column_header->addWidget(reset);
        column->addLayout(column_header);

        auto* bars = new BarsRow(this);
        auto* bars_layout = new QHBoxLayout(bars);
        bars_layout->setContentsMargins(0, 0, 0, 0);
        bars_layout->setSpacing(8);
        auto* values = new QHBoxLayout;
        values->setSpacing(8);
        QDoubleSpinBox* boxes[3];
        for (int i = 0; i < 3; ++i) {
            const Channel& channel = kChannels[i];
            const QString suffix = QLatin1String(channel.id);
            auto* slider = new QSlider(Qt::Vertical, bars);
            slider->setObjectName(prefix + suffix + QStringLiteral("Slider"));
            slider->setProperty("channel", QLatin1String(channel.key));
            slider->setRange(0, static_cast<int>(kSliderScale));
            slider->setValue(static_cast<int>(std::lround(group.default_value * kSliderScale)));
            slider->setFixedHeight(120);
            slider->setFocusPolicy(Qt::NoFocus);
            bars_layout->addWidget(slider, 0, Qt::AlignHCenter);

            auto* cell = new QWidget(this);
            auto* cell_layout = new QVBoxLayout(cell);
            cell_layout->setContentsMargins(0, 0, 0, 0);
            cell_layout->setSpacing(1);
            auto* box = new QDoubleSpinBox(cell);
            box->setObjectName(prefix + suffix + QStringLiteral("Value"));
            box->setRange(0.0, 1.0);
            box->setDecimals(2);
            box->setValue(group.default_value);
            box->setButtonSymbols(QAbstractSpinBox::NoButtons);
            box->setAlignment(Qt::AlignCenter);
            box->setFixedWidth(48);
            box->setKeyboardTracking(false);
            box->setProperty("defaultValue", group.default_value);
            cell_layout->addWidget(box);
            auto* hint = new QFrame(cell);
            hint->setFixedHeight(3);
            hint->setStyleSheet(QStringLiteral(
                "QFrame { border: none; border-radius: 1px; background: %1; }")
                    .arg(QLatin1String(channel.color)));
            cell_layout->addWidget(hint);
            values->addWidget(cell);
            boxes[i] = box;

            // Bar and field mirror each other.
            connect(slider, &QSlider::valueChanged, box,
                [box](int value) { box->setValue(value / kSliderScale); });
            connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), slider,
                [slider](double value) {
                    slider->setValue(static_cast<int>(std::lround(value * kSliderScale)));
                });
        }
        // While linked, changing one channel sets the other two to the same value.
        auto syncing = std::make_shared<bool>(false);
        for (int i = 0; i < 3; ++i) {
            connect(boxes[i], qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                [link, syncing, boxes](double value) {
                    if (*syncing || !link->isChecked()) return;
                    *syncing = true;
                    for (auto* other : boxes) other->setValue(value);
                    *syncing = false;
                });
        }
        column->addWidget(bars, 1);
        column->addLayout(values);
        groups->addLayout(column, 1);
        connect(reset, &QPushButton::clicked, this, [this, prefix, link, group] {
            for (auto* box : findChildren<QDoubleSpinBox*>(
                     QRegularExpression(QStringLiteral("^%1[RGB]Value$").arg(prefix))))
                box->setValue(box->property("defaultValue").toDouble());
            link->setChecked(group.link_enabled);
        });
    }
    root->addLayout(groups, 1);

    // Footer: fields of the other effect kinds, disabled in the Blur kind.
    auto* footer = new QHBoxLayout;
    footer->setSpacing(10);
    footer->addStretch();
    const struct { const char* label; const char* name; double value; } footer_specs[] = {
        {"Coring Softness", "CoringSoftness", 0.0}, {"Level", "Level", 0.0}, {"Mix", "Mix", 100.0},
    };
    for (const auto& spec : footer_specs) {
        footer->addWidget(new QLabel(tr(spec.label), this));
        auto* box = new QDoubleSpinBox(this);
        box->setObjectName(QStringLiteral("colorBlur%1").arg(QLatin1String(spec.name)));
        box->setRange(0.0, 100.0);
        box->setDecimals(2);
        box->setValue(spec.value);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setAlignment(Qt::AlignCenter);
        box->setFixedWidth(60);
        box->setKeyboardTracking(false);
        box->setProperty("defaultValue", spec.value);
        box->setEnabled(false);
        footer->addWidget(box);
    }
    footer->addStretch();
    root->addLayout(footer);

    connect(kinds, &QButtonGroup::buttonClicked, this, [this, title](QAbstractButton* btn) {
        if (!btn) return;
        const QString obj = btn->objectName();
        if (obj == QLatin1String("colorBlurSharpenModeButton")) {
            mode_ = 1;
            title->setText(tr("Blur - Sharpen"));
        } else if (obj == QLatin1String("colorBlurMistModeButton")) {
            mode_ = 2;
            title->setText(tr("Blur - Mist"));
        } else {
            mode_ = 0;
            title->setText(tr("Blur - Blur"));
        }
        const bool footer_on = (mode_ != 0);
        for (const char* n : {"colorBlurCoringSoftness", "colorBlurLevel", "colorBlurMix"}) {
            if (auto* b = findChild<QDoubleSpinBox*>(QLatin1String(n))) b->setEnabled(footer_on);
        }
        emitEffect();
    });
    for (auto* box : findChildren<QDoubleSpinBox*>()) {
        connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { emitEffect(); });
    }
    connect(reset_all, &QPushButton::clicked, this, [this] {
        restoreDefaults();
        emitEffect();
    });
}

void BlurPage::setEffectChangedHandler(
    std::function<void(const creative_suite::effects::EffectInstance&)> on_changed) {
    on_changed_ = std::move(on_changed);
}

void BlurPage::setEffect(const creative_suite::effects::EffectInstance& effect) {
    updating_ = true;
    const auto val = [&effect](std::string_view id, double def) {
        for (const auto& p : effect.parameters) {
            if (p.id == id) return p.value;
        }
        return def;
    };
    mode_ = static_cast<int>(std::lround(val("mode", 0.0)));
    const char* mode_btn = (mode_ == 1) ? "colorBlurSharpenModeButton"
        : (mode_ == 2) ? "colorBlurMistModeButton" : "colorBlurBlurModeButton";
    if (auto* b = findChild<QPushButton*>(QLatin1String(mode_btn))) {
        const QSignalBlocker blocker(b);
        b->setChecked(true);
    }
    const bool footer_on = (mode_ != 0);
    for (const char* n : {"colorBlurCoringSoftness", "colorBlurLevel", "colorBlurMix"}) {
        if (auto* box = findChild<QDoubleSpinBox*>(QLatin1String(n))) box->setEnabled(footer_on);
    }
    const auto set_spin = [this](const char* obj, double v) {
        if (auto* box = findChild<QDoubleSpinBox*>(QLatin1String(obj))) {
            const QSignalBlocker blocker(box);
            box->setValue(v);
        }
    };
    set_spin("colorBlurRadiusRValue", val("radius_r", 0.5));
    set_spin("colorBlurRadiusGValue", val("radius_g", 0.5));
    set_spin("colorBlurRadiusBValue", val("radius_b", 0.5));
    set_spin("colorBlurRatioRValue", val("hv_ratio_r", 0.5));
    set_spin("colorBlurRatioGValue", val("hv_ratio_g", 0.5));
    set_spin("colorBlurRatioBValue", val("hv_ratio_b", 0.5));
    set_spin("colorBlurScalingRValue", val("scaling_r", 0.25));
    set_spin("colorBlurScalingGValue", val("scaling_g", 0.25));
    set_spin("colorBlurScalingBValue", val("scaling_b", 0.25));
    set_spin("colorBlurCoringSoftness", val("coring_softness", 0.0));
    set_spin("colorBlurLevel", val("level", 0.0));
    set_spin("colorBlurMix", val("mix", 100.0));
    updating_ = false;
}

void BlurPage::emitEffect() {
    if (updating_ || !on_changed_) return;
    auto eff = creative_suite::effects::makeDefaultInstance("video.color_blur");
    const auto spin = [this](const char* obj, double def) {
        auto* b = findChild<QDoubleSpinBox*>(QLatin1String(obj));
        return b ? b->value() : def;
    };
    (void)creative_suite::effects::setParameterValue(eff, "mode", static_cast<double>(mode_));
    (void)creative_suite::effects::setParameterValue(eff, "radius_r", spin("colorBlurRadiusRValue", 0.5));
    (void)creative_suite::effects::setParameterValue(eff, "radius_g", spin("colorBlurRadiusGValue", 0.5));
    (void)creative_suite::effects::setParameterValue(eff, "radius_b", spin("colorBlurRadiusBValue", 0.5));
    (void)creative_suite::effects::setParameterValue(eff, "hv_ratio_r", spin("colorBlurRatioRValue", 0.5));
    (void)creative_suite::effects::setParameterValue(eff, "hv_ratio_g", spin("colorBlurRatioGValue", 0.5));
    (void)creative_suite::effects::setParameterValue(eff, "hv_ratio_b", spin("colorBlurRatioBValue", 0.5));
    (void)creative_suite::effects::setParameterValue(eff, "scaling_r", spin("colorBlurScalingRValue", 0.25));
    (void)creative_suite::effects::setParameterValue(eff, "scaling_g", spin("colorBlurScalingGValue", 0.25));
    (void)creative_suite::effects::setParameterValue(eff, "scaling_b", spin("colorBlurScalingBValue", 0.25));
    (void)creative_suite::effects::setParameterValue(eff, "coring_softness", spin("colorBlurCoringSoftness", 0.0));
    (void)creative_suite::effects::setParameterValue(eff, "level", spin("colorBlurLevel", 0.0));
    (void)creative_suite::effects::setParameterValue(eff, "mix", spin("colorBlurMix", 100.0));
    on_changed_(eff);
}

void BlurPage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    for (const auto& group : kGroups) {
        if (auto* link = findChild<QPushButton*>(
                QStringLiteral("colorBlur%1Link").arg(QLatin1String(group.name))))
            link->setChecked(group.link_enabled);
    }
    if (auto* blur = findChild<QPushButton*>("colorBlurBlurModeButton")) blur->setChecked(true);
}

}  // namespace ui
