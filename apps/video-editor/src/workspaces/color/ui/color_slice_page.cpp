#include "color_slice_page.h"

#include <QCheckBox>
#include <QConicalGradient>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRadialGradient>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>
#include <iterator>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("ColorSlicePage", text);
}

constexpr double kSliderScale = 100.0;

// Small hue disc with the column's slice drawn as a wedge outline.
class SliceWheel final : public QWidget {
public:
    SliceWheel(double hue_degrees, QWidget* parent) : QWidget(parent), hue_(hue_degrees) {
        setFixedSize(56, 56);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPointF center(width() / 2.0, height() / 2.0);
        const double radius = width() / 2.0 - 2.0;
        QConicalGradient hues(center, 0.0);
        for (int step = 0; step <= 6; ++step)
            hues.setColorAt(step / 6.0, QColor::fromHsv((step % 6) * 60, 200, 215));
        painter.setPen(Qt::NoPen);
        painter.setBrush(hues);
        painter.drawEllipse(center, radius, radius);
        QRadialGradient fade(center, radius);
        fade.setColorAt(0.0, QColor(235, 235, 235, 230));
        fade.setColorAt(1.0, QColor(235, 235, 235, 0));
        painter.setBrush(fade);
        painter.drawEllipse(center, radius, radius);

        // Wedge for the slice (hue measured counter-clockwise from 3 o'clock).
        painter.setPen(QPen(QColor(255, 255, 255, 235), 1.2));
        constexpr double kHalfWidth = 16.0;
        constexpr double kPi = 3.14159265358979323846;
        for (const double edge : {hue_ - kHalfWidth, hue_ + kHalfWidth}) {
            const double radians = edge * kPi / 180.0;
            painter.drawLine(center,
                QPointF(center.x() + radius * std::cos(radians),
                    center.y() - radius * std::sin(radians)));
        }
        painter.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(center, radius, radius);
    }

private:
    double hue_;
};

QDoubleSpinBox* makeSpin(QWidget* parent, const QString& name, double minimum,
    double maximum, double default_value, int width = 52) {
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(minimum, maximum);
    box->setDecimals(2);
    box->setValue(default_value);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setAlignment(Qt::AlignCenter);
    box->setFixedWidth(width);
    box->setKeyboardTracking(false);
    box->setProperty("defaultValue", default_value);
    return box;
}

QPushButton* makeIconButton(QWidget* parent, const QString& text, const QString& name,
    const QString& accessible_name, int width = 24) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setAccessibleName(accessible_name);
    button->setToolTip(accessible_name);
    button->setFixedSize(width, 24);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

// Thin horizontal slider with a decorative colored groove, mirroring a field.
QSlider* makeThinSlider(QWidget* parent, const QString& name, QDoubleSpinBox* spin,
    const QString& groove) {
    auto* slider = new QSlider(Qt::Horizontal, parent);
    slider->setObjectName(name);
    slider->setRange(static_cast<int>(std::lround(spin->minimum() * kSliderScale)),
        static_cast<int>(std::lround(spin->maximum() * kSliderScale)));
    slider->setValue(static_cast<int>(std::lround(spin->value() * kSliderScale)));
    slider->setFocusPolicy(Qt::NoFocus);
    slider->setStyleSheet(QStringLiteral(
        "QSlider::groove:horizontal { height: 3px; border-radius: 1px; background: %1; }"
        "QSlider::handle:horizontal { background: #e6ebf0; width: 4px; height: 9px;"
        " margin: -4px 0; border-radius: 1px; }").arg(groove));
    QObject::connect(slider, &QSlider::valueChanged, spin,
        [spin](int value) { spin->setValue(value / kSliderScale); });
    QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), slider,
        [slider](double value) {
            slider->setValue(static_cast<int>(std::lround(value * kSliderScale)));
        });
    return slider;
}

struct SliceSpec {
    const char* name;
    const char* label;
    double hue;          // wedge position on the small wheel, degrees
    const char* color;   // fill color for the vertical slider handle
    const char* groove;  // decorative Hue groove
};

constexpr SliceSpec kSlices[] = {
    {"Red", "Red", 0.0, "#d9423f",
        "qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #c040a0,stop:0.5 #d9423f,stop:1 #e0903a)"},
    {"Skin", "Skin", 25.0, "#d8b0a0",
        "qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #d9423f,stop:0.5 #d8b0a0,stop:1 #e0c040)"},
    {"Yellow", "Yellow", 60.0, "#e8e040",
        "qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #e0903a,stop:0.5 #e8e040,stop:1 #8ac040)"},
    {"Green", "Green", 120.0, "#3fbf5a",
        "qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #b0c040,stop:0.5 #3fbf5a,stop:1 #30b0a0)"},
    {"Cyan", "Cyan", 180.0, "#5fd8e0",
        "qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #30b070,stop:0.5 #5fd8e0,stop:1 #4a80d9)"},
    {"Blue", "Blue", 240.0, "#4a5fd9",
        "qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #30a0d0,stop:0.5 #4a5fd9,stop:1 #8a50c0)"},
    {"Magenta", "Magenta", 300.0, "#d94fa0",
        "qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #8a50c0,stop:0.5 #d94fa0,stop:1 #d9423f)"},
};

}  // namespace

ColorSlicePage::ColorSlicePage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorSlicePage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorSlicePage QDoubleSpinBox { padding: 2px 2px; }
        QWidget#colorSlicePage QSlider::groove:vertical {
            background: #14181e; border: 1px solid #39424e; border-radius: 5px; width: 12px;
        }
        QWidget#colorSlicePage QSlider::handle:vertical {
            background: #6b7684; height: 8px; margin: 0 -2px; border-radius: 2px;
        }
        QWidget#colorSlicePage QPushButton:checked { background: #2f5f8f; }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Color Slice"), this);
    title->setObjectName("colorSliceTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* reset_all = makeIconButton(this, QStringLiteral("↺"),
        QStringLiteral("colorSliceResetButton"), tr("Reset Color Slice"), 28);
    header->addWidget(reset_all);
    root->addLayout(header);

    // Global row.
    auto* global = new QHBoxLayout;
    global->setSpacing(8);
    const struct { const char* label; const char* name; double minimum; double maximum; double value; } global_specs[] = {
        {"Den", "Den", -1.0, 1.0, 0.0},
        {"Den.Depth", "DenDepth", -1.0, 1.0, 0.0},
        {"Sat", "Sat", 0.0, 2.0, 1.0},
        {"Sat.Balance", "SatBalance", -1.0, 1.0, 0.0},
        {"Sat.Depth", "SatDepth", -1.0, 1.0, 0.0},
        {"Hue", "Hue", -1.0, 1.0, 0.0},
    };
    for (const auto& spec : global_specs) {
        global->addWidget(new QLabel(tr(spec.label), this));
        global->addWidget(makeSpin(this,
            QStringLiteral("colorSlice%1").arg(QLatin1String(spec.name)),
            spec.minimum, spec.maximum, spec.value, 56));
    }
    root->addLayout(global);

    // Seven slice columns.
    auto* columns = new QHBoxLayout;
    columns->setSpacing(6);
    for (const auto& slice : kSlices) {
        const QString name = QLatin1String(slice.name);
        const QString prefix = QStringLiteral("colorSlice") + name;
        auto* column = new QVBoxLayout;
        column->setSpacing(4);

        auto* top = new QHBoxLayout;
        auto* toggle = makeIconButton(this, QStringLiteral("◑"),
            prefix + QStringLiteral("ToggleButton"), tr("Enable slice"));
        toggle->setCheckable(true);
        toggle->setChecked(true);
        top->addWidget(toggle);
        top->addStretch();
        top->addWidget(new QLabel(tr(slice.label), this));
        top->addStretch();
        auto* reset = makeIconButton(this, QStringLiteral("↺"),
            prefix + QStringLiteral("ResetButton"), tr("Reset slice"));
        top->addWidget(reset);
        column->addLayout(top);

        auto* wheel = new SliceWheel(slice.hue, this);
        wheel->setObjectName(prefix + QStringLiteral("Wheel"));
        column->addWidget(wheel, 0, Qt::AlignHCenter);

        auto* center_row = new QGridLayout;
        center_row->setSpacing(1);
        center_row->addWidget(new QLabel(tr("Center"), this), 0, 0);
        auto* center = makeSpin(this, prefix + QStringLiteral("Center"), -1.0, 1.0, 0.0);
        center_row->addWidget(center, 0, 1);
        center_row->addWidget(makeThinSlider(this, prefix + QStringLiteral("CenterSlider"),
                                  center, QStringLiteral("#6b7684")), 1, 0, 1, 2);
        column->addLayout(center_row);

        auto* hue_row = new QGridLayout;
        hue_row->setSpacing(1);
        hue_row->addWidget(new QLabel(tr("Hue"), this), 0, 0);
        auto* hue = makeSpin(this, prefix + QStringLiteral("Hue"), -1.0, 1.0, 0.0);
        hue_row->addWidget(hue, 0, 1);
        hue_row->addWidget(makeThinSlider(this, prefix + QStringLiteral("HueSlider"), hue,
                               QLatin1String(slice.groove)), 1, 0, 1, 2);
        column->addLayout(hue_row);

        // Two vertical bars with their values: A (default 0.00) and B (default 1.00).
        auto* bars = new QHBoxLayout;
        bars->setSpacing(10);
        const struct { const char* id; const char* glyph; double minimum; double maximum; double value; } bar_specs[] = {
            {"A", "∷", -1.0, 1.0, 0.0},
            {"B", "◖", 0.0, 2.0, 1.0},
        };
        auto* values = new QHBoxLayout;
        values->setSpacing(4);
        for (const auto& bar : bar_specs) {
            auto* cell = new QVBoxLayout;
            cell->setSpacing(2);
            auto* glyph = new QLabel(QString::fromUtf8(bar.glyph), this);
            glyph->setAlignment(Qt::AlignCenter);
            cell->addWidget(glyph);
            auto* slider = new QSlider(Qt::Vertical, this);
            slider->setObjectName(prefix + QStringLiteral("Slider") + QLatin1String(bar.id));
            slider->setRange(static_cast<int>(std::lround(bar.minimum * kSliderScale)),
                static_cast<int>(std::lround(bar.maximum * kSliderScale)));
            slider->setValue(static_cast<int>(std::lround(bar.value * kSliderScale)));
            slider->setFixedHeight(84);
            slider->setFocusPolicy(Qt::NoFocus);
            if (QLatin1String(bar.id) == QLatin1String("A")) {
                slider->setStyleSheet(QStringLiteral(
                    "QSlider::handle:vertical { background: %1; height: 8px; margin: 0 -2px;"
                    " border-radius: 2px; }").arg(QLatin1String(slice.color)));
            }
            cell->addWidget(slider, 1, Qt::AlignHCenter);
            bars->addLayout(cell);
            auto* box = makeSpin(this, prefix + QStringLiteral("Value") + QLatin1String(bar.id),
                bar.minimum, bar.maximum, bar.value, 46);
            values->addWidget(box);
            connect(slider, &QSlider::valueChanged, box,
                [box](int value) { box->setValue(value / kSliderScale); });
            connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), slider,
                [slider](double value) {
                    slider->setValue(static_cast<int>(std::lround(value * kSliderScale)));
                });
        }
        column->addLayout(bars, 1);
        column->addLayout(values);
        columns->addLayout(column, 1);

        connect(reset, &QPushButton::clicked, this, [this, prefix, toggle] {
            for (auto* box : findChildren<QDoubleSpinBox*>(QRegularExpression(
                     QStringLiteral("^%1(Center|Hue|ValueA|ValueB)$").arg(prefix))))
                box->setValue(box->property("defaultValue").toDouble());
            toggle->setChecked(true);
        });
        if (&slice != &kSlices[std::size(kSlices) - 1]) {
            auto* line = new QFrame(this);
            line->setFrameShape(QFrame::VLine);
            line->setFrameShadow(QFrame::Plain);
            columns->addWidget(line);
        }
    }
    root->addLayout(columns, 1);

    for (auto* box : findChildren<QDoubleSpinBox*>()) {
        connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { emitEffect(); });
    }
    for (auto* button : findChildren<QPushButton*>(QRegularExpression(QStringLiteral("ToggleButton$")))) {
        connect(button, &QPushButton::toggled, this, [this](bool) { emitEffect(); });
    }
    connect(reset_all, &QPushButton::clicked, this, [this] {
        restoreDefaults();
        emitEffect();
    });
}

void ColorSlicePage::setEffectChangedHandler(
    std::function<void(const creative_suite::effects::EffectInstance&)> on_changed) {
    on_changed_ = std::move(on_changed);
}

void ColorSlicePage::setEffect(const creative_suite::effects::EffectInstance& effect) {
    updating_ = true;
    const auto val = [&effect](std::string_view id, double def) {
        for (const auto& p : effect.parameters) {
            if (p.id == id) return p.value;
        }
        return def;
    };
    const auto set_spin = [this](const QString& obj, double v) {
        if (auto* b = findChild<QDoubleSpinBox*>(obj)) {
            const QSignalBlocker blocker(b);
            b->setValue(v);
        }
    };
    const auto set_tog = [this](const QString& obj, bool v) {
        if (auto* b = findChild<QPushButton*>(obj)) {
            const QSignalBlocker blocker(b);
            b->setChecked(v);
        }
    };
    set_spin(QStringLiteral("colorSliceGlobalDen"), val("global_den", 0.0));
    set_spin(QStringLiteral("colorSliceGlobalDenDepth"), val("global_den_depth", 0.0));
    set_spin(QStringLiteral("colorSliceGlobalSat"), val("global_sat", 1.0));
    set_spin(QStringLiteral("colorSliceGlobalSatBalance"), val("global_sat_balance", 0.0));
    set_spin(QStringLiteral("colorSliceGlobalSatDepth"), val("global_sat_depth", 0.0));
    set_spin(QStringLiteral("colorSliceGlobalHue"), val("global_hue", 0.0));
    const struct { const char* ui; const char* id; } slices[] = {
        {"Red", "red"}, {"Skin", "skin"}, {"Yellow", "yellow"}, {"Green", "green"},
        {"Cyan", "cyan"}, {"Blue", "blue"}, {"Magenta", "magenta"}};
    for (const auto& s : slices) {
        const QString p = QStringLiteral("colorSlice") + QLatin1String(s.ui);
        const std::string id(s.id);
        set_tog(p + QStringLiteral("ToggleButton"), val(id + "_enabled", 1.0) >= 0.5);
        set_spin(p + QStringLiteral("Center"), val(id + "_center", 0.0));
        set_spin(p + QStringLiteral("Hue"), val(id + "_hue", 0.0));
        set_spin(p + QStringLiteral("ValueA"), val(id + "_density", 0.0));
        set_spin(p + QStringLiteral("ValueB"), val(id + "_sat", 1.0));
    }
    updating_ = false;
}

void ColorSlicePage::emitEffect() {
    if (updating_ || !on_changed_) return;
    auto eff = creative_suite::effects::makeDefaultInstance("video.color_slice");
    const auto spin = [this](const QString& obj, double def) {
        auto* b = findChild<QDoubleSpinBox*>(obj);
        return b ? b->value() : def;
    };
    const auto tog = [this](const QString& obj) {
        auto* b = findChild<QPushButton*>(obj);
        return (b && b->isChecked()) ? 1.0 : 0.0;
    };
    (void)creative_suite::effects::setParameterValue(eff, "global_den", spin(QStringLiteral("colorSliceGlobalDen"), 0.0));
    (void)creative_suite::effects::setParameterValue(eff, "global_den_depth", spin(QStringLiteral("colorSliceGlobalDenDepth"), 0.0));
    (void)creative_suite::effects::setParameterValue(eff, "global_sat", spin(QStringLiteral("colorSliceGlobalSat"), 1.0));
    (void)creative_suite::effects::setParameterValue(eff, "global_sat_balance", spin(QStringLiteral("colorSliceGlobalSatBalance"), 0.0));
    (void)creative_suite::effects::setParameterValue(eff, "global_sat_depth", spin(QStringLiteral("colorSliceGlobalSatDepth"), 0.0));
    (void)creative_suite::effects::setParameterValue(eff, "global_hue", spin(QStringLiteral("colorSliceGlobalHue"), 0.0));
    const struct { const char* ui; const char* id; } slices[] = {
        {"Red", "red"}, {"Skin", "skin"}, {"Yellow", "yellow"}, {"Green", "green"},
        {"Cyan", "cyan"}, {"Blue", "blue"}, {"Magenta", "magenta"}};
    for (const auto& s : slices) {
        const QString p = QStringLiteral("colorSlice") + QLatin1String(s.ui);
        const std::string id(s.id);
        (void)creative_suite::effects::setParameterValue(eff, id + "_enabled", tog(p + QStringLiteral("ToggleButton")));
        (void)creative_suite::effects::setParameterValue(eff, id + "_center", spin(p + QStringLiteral("Center"), 0.0));
        (void)creative_suite::effects::setParameterValue(eff, id + "_hue", spin(p + QStringLiteral("Hue"), 0.0));
        (void)creative_suite::effects::setParameterValue(eff, id + "_density", spin(p + QStringLiteral("ValueA"), 0.0));
        (void)creative_suite::effects::setParameterValue(eff, id + "_sat", spin(p + QStringLiteral("ValueB"), 1.0));
    }
    on_changed_(eff);
}

void ColorSlicePage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    for (auto* button : findChildren<QPushButton*>(QRegularExpression(QStringLiteral("ToggleButton$"))))
        button->setChecked(true);
}

}  // namespace ui
