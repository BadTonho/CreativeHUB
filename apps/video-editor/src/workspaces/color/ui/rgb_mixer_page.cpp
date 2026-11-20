#include "rgb_mixer_page.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QVBoxLayout>

#include <cmath>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("RgbMixerPage", text);
}

constexpr double kSliderScale = 100.0;
constexpr double kMinimum = -2.0;
constexpr double kMaximum = 2.0;

// Holds the three bars of one output and paints the zero line behind them.
class BarsRow final : public QWidget {
public:
    explicit BarsRow(QWidget* parent) : QWidget(parent) {}

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setPen(QPen(QColor(QStringLiteral("#39424e")), 1.0));
        const int y = height() / 2;
        painter.drawLine(0, y, width(), y);
    }
};

// Pill button showing "source > destination" with two colored dots.
class RoutePill final : public QPushButton {
public:
    RoutePill(const QString& from, const QString& to, QWidget* parent)
        : QPushButton(parent), from_(from), to_(to) {
        setFixedSize(52, 22);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(QColor(isDown() ? QStringLiteral("#6aa3d8")
                                            : QStringLiteral("#526070")), 1.0));
        painter.setBrush(QColor(underMouse() ? QStringLiteral("#39516a")
                                             : QStringLiteral("#14181e")));
        painter.drawRoundedRect(QRectF(0.5, 0.5, width() - 1.0, height() - 1.0), 10.0, 10.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(from_));
        painter.drawEllipse(QPointF(14.0, height() / 2.0), 5.0, 5.0);
        painter.setBrush(QColor(to_));
        painter.drawEllipse(QPointF(width() - 14.0, height() / 2.0), 5.0, 5.0);
        painter.setPen(QColor(QStringLiteral("#d7e0ea")));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral(">"));
    }

private:
    QString from_;
    QString to_;
};

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
    const char* id;     // object-name suffix
    const char* color;  // hint-bar color
    const char* key;    // stylesheet property value
};

constexpr Channel kChannels[] = {
    {"R", "#d9423f", "r"},
    {"G", "#3fbf5a", "g"},
    {"B", "#4a5fd9", "b"},
};

}  // namespace

RgbMixerPage::RgbMixerPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorRgbMixerPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorRgbMixerPage QDoubleSpinBox { padding: 2px 2px; }
        QWidget#colorRgbMixerPage QSlider::groove:vertical {
            background: #14181e; border: 1px solid #39424e; border-radius: 6px; width: 16px;
        }
        QWidget#colorRgbMixerPage QSlider::handle:vertical {
            background: #e6ebf0; height: 8px; margin: 0 -3px; border-radius: 2px;
        }
        QWidget#colorRgbMixerPage QSlider[channel="r"]::handle:vertical { background: #d9423f; }
        QWidget#colorRgbMixerPage QSlider[channel="g"]::handle:vertical { background: #3fbf5a; }
        QWidget#colorRgbMixerPage QSlider[channel="b"]::handle:vertical { background: #4a5fd9; }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("RGB Mixer"), this);
    title->setObjectName("colorRgbMixerTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* reset_all = makeIconButton(this, QStringLiteral("↺"),
        QStringLiteral("colorRgbMixerResetButton"), tr("Reset RGB Mixer"));
    header->addWidget(reset_all);
    root->addLayout(header);

    // One column per output channel; each mixes the three input channels.
    auto* outputs = new QHBoxLayout;
    outputs->setSpacing(20);
    const struct { const char* name; const char* label; int identity; } output_specs[] = {
        {"Red", "Red Output", 0},
        {"Green", "Green Output", 1},
        {"Blue", "Blue Output", 2},
    };
    for (const auto& output : output_specs) {
        const QString output_name = QLatin1String(output.name);
        auto* column = new QVBoxLayout;
        column->setSpacing(6);

        auto* column_header = new QHBoxLayout;
        column_header->addWidget(makeIconButton(this, QStringLiteral("A"),
            QStringLiteral("colorRgbMixer%1AutoButton").arg(output_name),
            tr("Auto mix"), 24));
        column_header->addStretch();
        column_header->addWidget(new QLabel(tr(output.label), this));
        column_header->addStretch();
        auto* column_reset = makeIconButton(this, QStringLiteral("↺"),
            QStringLiteral("colorRgbMixer%1ResetButton").arg(output_name),
            tr("Reset output"), 24);
        column_header->addWidget(column_reset);
        column->addLayout(column_header);

        auto* bars = new BarsRow(this);
        auto* bars_layout = new QHBoxLayout(bars);
        bars_layout->setContentsMargins(0, 0, 0, 0);
        bars_layout->setSpacing(8);
        auto* values = new QHBoxLayout;
        values->setSpacing(8);
        for (int i = 0; i < 3; ++i) {
            const Channel& channel = kChannels[i];
            const QString suffix = output_name + QStringLiteral("Output") + QLatin1String(channel.id);
            const double default_value = i == output.identity ? 1.0 : 0.0;
            auto* slider = new QSlider(Qt::Vertical, bars);
            slider->setObjectName(QStringLiteral("colorRgbMixer%1Slider").arg(suffix));
            slider->setProperty("channel", QLatin1String(channel.key));
            slider->setRange(static_cast<int>(std::lround(kMinimum * kSliderScale)),
                static_cast<int>(std::lround(kMaximum * kSliderScale)));
            slider->setValue(static_cast<int>(std::lround(default_value * kSliderScale)));
            slider->setFixedHeight(120);
            slider->setFocusPolicy(Qt::NoFocus);
            bars_layout->addWidget(slider, 0, Qt::AlignHCenter);

            auto* cell = new QWidget(this);
            auto* cell_layout = new QVBoxLayout(cell);
            cell_layout->setContentsMargins(0, 0, 0, 0);
            cell_layout->setSpacing(1);
            auto* box = new QDoubleSpinBox(cell);
            box->setObjectName(QStringLiteral("colorRgbMixer%1Value").arg(suffix));
            box->setRange(kMinimum, kMaximum);
            box->setDecimals(2);
            box->setValue(default_value);
            box->setButtonSymbols(QAbstractSpinBox::NoButtons);
            box->setAlignment(Qt::AlignCenter);
            box->setFixedWidth(48);
            box->setKeyboardTracking(false);
            box->setProperty("defaultValue", default_value);
            cell_layout->addWidget(box);
            auto* hint = new QFrame(cell);
            hint->setFixedHeight(3);
            hint->setStyleSheet(QStringLiteral(
                "QFrame { border: none; border-radius: 1px; background: %1; }")
                    .arg(QLatin1String(channel.color)));
            cell_layout->addWidget(hint);
            values->addWidget(cell);

            // Bar and field mirror each other.
            connect(slider, &QSlider::valueChanged, box,
                [box](int value) { box->setValue(value / kSliderScale); });
            connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), slider,
                [slider](double value) {
                    slider->setValue(static_cast<int>(std::lround(value * kSliderScale)));
                });
        }
        column->addWidget(bars, 1);
        column->addLayout(values);
        outputs->addLayout(column, 1);
        connect(column_reset, &QPushButton::clicked, this, [this, output_name] {
            for (auto* box : findChildren<QDoubleSpinBox*>(QRegularExpression(
                     QStringLiteral("^colorRgbMixer%1Output[RGB]Value$").arg(output_name))))
                box->setValue(box->property("defaultValue").toDouble());
        });
    }
    root->addLayout(outputs, 1);

    // Footer: route shortcuts on the left, options on the right.
    auto* footer = new QHBoxLayout;
    footer->setSpacing(8);
    const struct { const char* object; const char* label; const char* from; const char* to; } routes[] = {
        {"colorRgbMixerRouteRedGreenButton", "Red to Green", "#d9423f", "#3fbf5a"},
        {"colorRgbMixerRouteGreenBlueButton", "Green to Blue", "#3fbf5a", "#4a5fd9"},
        {"colorRgbMixerRouteBlueRedButton", "Blue to Red", "#4a5fd9", "#d9423f"},
    };
    for (const auto& route : routes) {
        auto* pill = new RoutePill(QLatin1String(route.from), QLatin1String(route.to), this);
        pill->setObjectName(QLatin1String(route.object));
        pill->setAccessibleName(tr(route.label));
        pill->setToolTip(tr(route.label));
        footer->addWidget(pill);
    }
    footer->addStretch();
    auto* monochrome = new QCheckBox(tr("Monochrome"), this);
    monochrome->setObjectName("colorRgbMixerMonochrome");
    footer->addWidget(monochrome);
    auto* preserve = new QCheckBox(tr("Preserve Luminance"), this);
    preserve->setObjectName("colorRgbMixerPreserveLuminance");
    preserve->setChecked(true);
    footer->addWidget(preserve);
    root->addLayout(footer);

    connect(reset_all, &QPushButton::clicked, this, [this] { restoreDefaults(); });
}

void RgbMixerPage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    if (auto* monochrome = findChild<QCheckBox*>("colorRgbMixerMonochrome"))
        monochrome->setChecked(false);
    if (auto* preserve = findChild<QCheckBox*>("colorRgbMixerPreserveLuminance"))
        preserve->setChecked(true);
}

}  // namespace ui
