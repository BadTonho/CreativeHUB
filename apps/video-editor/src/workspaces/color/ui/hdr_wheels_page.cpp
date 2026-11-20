#include "hdr_wheels_page.h"

#include <QButtonGroup>
#include <QConicalGradient>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("HdrWheelsPage", text);
}

constexpr double kSliderScale = 100.0;

// Color wheel: hue ring, dark disc with a draggable puck, and two decorative
// side arcs. The puck position is a point in the unit disc (x right, y up).
class WheelWidget final : public QWidget {
public:
    explicit WheelWidget(QWidget* parent) : QWidget(parent) {
        setFixedSize(136, 136);
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::CrossCursor);
    }

    void setPosition(double x, double y) {
        x_ = std::clamp(x, -1.0, 1.0);
        y_ = std::clamp(y, -1.0, 1.0);
        update();
    }
    [[nodiscard]] double positionX() const noexcept { return x_; }
    [[nodiscard]] double positionY() const noexcept { return y_; }
    void setChangedHandler(std::function<void(double, double)> handler) {
        changed_ = std::move(handler);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPointF center(width() / 2.0, height() / 2.0);
        const double half = std::min(width(), height()) / 2.0;
        const double arc_radius = half - 6.0;
        const double ring_outer = half - 16.0;
        const double inner = ring_outer * 0.78;

        // Side arcs with their handles.
        QPen arc_pen(QColor(QStringLiteral("#6b7684")), 3.0, Qt::SolidLine, Qt::RoundCap);
        painter.setPen(arc_pen);
        painter.setBrush(Qt::NoBrush);
        const QRectF arc_rect(center.x() - arc_radius, center.y() - arc_radius,
            arc_radius * 2.0, arc_radius * 2.0);
        painter.drawArc(arc_rect, 130 * 16, 100 * 16);
        painter.drawArc(arc_rect, -50 * 16, 100 * 16);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#e6ebf0")));
        painter.drawEllipse(QPointF(center.x() - arc_radius, center.y()), 3.0, 3.0);
        painter.setBrush(QColor(QStringLiteral("#d9423f")));
        painter.drawEllipse(QPointF(center.x() + arc_radius * std::cos(0.8),
                                center.y() + arc_radius * std::sin(0.8)),
            3.0, 3.0);

        // Hue ring.
        QConicalGradient hues(center, 0.0);
        for (int step = 0; step <= 6; ++step) {
            hues.setColorAt(step / 6.0,
                QColor::fromHsv(static_cast<int>((6 - step) % 6) * 60, 220, 235));
        }
        painter.setBrush(hues);
        painter.drawEllipse(center, ring_outer, ring_outer);
        painter.setBrush(QColor(QStringLiteral("#161a20")));
        painter.drawEllipse(center, inner, inner);

        // Crosshair.
        painter.setPen(QPen(QColor(QStringLiteral("#39424e")), 1.0));
        painter.drawLine(QPointF(center.x() - inner, center.y()),
            QPointF(center.x() + inner, center.y()));
        painter.drawLine(QPointF(center.x(), center.y() - inner),
            QPointF(center.x(), center.y() + inner));

        // Puck.
        painter.setPen(QPen(QColor(QStringLiteral("#0d0f12")), 1.0));
        painter.setBrush(QColor(QStringLiteral("#f2f5f8")));
        painter.drawEllipse(QPointF(center.x() + x_ * inner, center.y() - y_ * inner), 3.5, 3.5);
    }

    void mousePressEvent(QMouseEvent* event) override { moveTo(event->position()); }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton) moveTo(event->position());
    }
    void mouseDoubleClickEvent(QMouseEvent*) override { apply(0.0, 0.0); }

private:
    [[nodiscard]] double innerRadius() const {
        return (std::min(width(), height()) / 2.0 - 16.0) * 0.78;
    }
    void moveTo(const QPointF& position) {
        const double radius = innerRadius();
        double x = (position.x() - width() / 2.0) / radius;
        double y = -(position.y() - height() / 2.0) / radius;
        const double length = std::hypot(x, y);
        if (length > 1.0) {
            x /= length;
            y /= length;
        }
        apply(x, y);
    }
    void apply(double x, double y) {
        setPosition(x, y);
        if (changed_) changed_(x_, y_);
    }

    double x_ = 0.0;
    double y_ = 0.0;
    std::function<void(double, double)> changed_;
};

QDoubleSpinBox* makeSpin(QWidget* parent, const QString& name, double minimum,
    double maximum, int decimals, double default_value, int width) {
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

QFrame* makeHintBar(QWidget* parent, const QString& from, const QString& to) {
    auto* bar = new QFrame(parent);
    bar->setFixedHeight(3);
    bar->setStyleSheet(QStringLiteral(
        "QFrame { border: none; border-radius: 1px;"
        " background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 %1, stop:1 %2); }")
            .arg(from, to));
    return bar;
}

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

// A label, a value field and a thin slider underneath, mirroring each other.
QWidget* makeSliderField(QWidget* parent, const QString& label, const QString& name,
    double minimum, double maximum, double default_value) {
    auto* box = new QWidget(parent);
    auto* grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(1);
    grid->addWidget(new QLabel(label, box), 0, 0, Qt::AlignLeft | Qt::AlignBottom);
    auto* spin = makeSpin(box, name, minimum, maximum, 2, default_value, 52);
    grid->addWidget(spin, 0, 1, Qt::AlignRight);
    auto* slider = new QSlider(Qt::Horizontal, box);
    slider->setObjectName(name + QStringLiteral("Slider"));
    slider->setProperty("thin", true);
    slider->setRange(static_cast<int>(std::lround(minimum * kSliderScale)),
        static_cast<int>(std::lround(maximum * kSliderScale)));
    slider->setValue(static_cast<int>(std::lround(default_value * kSliderScale)));
    slider->setFocusPolicy(Qt::NoFocus);
    grid->addWidget(slider, 1, 0, 1, 2);
    grid->setColumnStretch(0, 1);
    QObject::connect(slider, &QSlider::valueChanged, spin,
        [spin](int value) { spin->setValue(value / kSliderScale); });
    QObject::connect(spin, qOverload<double>(&QDoubleSpinBox::valueChanged), slider,
        [slider](double value) {
            slider->setValue(static_cast<int>(std::lround(value * kSliderScale)));
        });
    return box;
}

struct WheelSpec {
    const char* name;
    bool has_zone;        // zone button + zone value + third field
    double zone_value;
    double range_value;
};

}  // namespace

HdrWheelsPage::HdrWheelsPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorHdrWheelsPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorHdrWheelsPage QDoubleSpinBox { padding: 2px 2px; }
        QWidget#colorHdrWheelsPage QSlider[thin="true"]::groove:horizontal {
            background: #6b7684; height: 2px;
        }
        QWidget#colorHdrWheelsPage QSlider[thin="true"]::handle:horizontal {
            background: #e6ebf0; width: 4px; height: 8px; margin: -4px 0; border-radius: 1px;
        }
        QWidget#colorHdrWheelsPage QPushButton[dot="true"] {
            border-radius: 6px; padding: 0; background: #20242a;
        }
        QWidget#colorHdrWheelsPage QPushButton[dot="true"]:checked {
            background: #6aa3d8; border-color: #9cc4ea;
        }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    // Header: title, panel, mode buttons and reset.
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("High Dynamic Range - Color Wheels"), this);
    title->setObjectName("colorHdrWheelsTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    header->addWidget(makeIconButton(this, QStringLiteral("⇥"),
        QStringLiteral("colorHdrWheelsPanelButton"), tr("Panel"), 32));
    auto* modes = new QButtonGroup(this);
    modes->setExclusive(true);
    auto* wheels_mode = makeIconButton(this, QStringLiteral("◎"),
        QStringLiteral("colorHdrWheelsWheelsModeButton"), tr("Wheels"), 32);
    auto* graph_mode = makeIconButton(this, QStringLiteral("∿"),
        QStringLiteral("colorHdrWheelsGraphModeButton"), tr("Graph"), 32);
    for (auto* mode : {wheels_mode, graph_mode}) {
        mode->setCheckable(true);
        modes->addButton(mode);
        header->addWidget(mode);
    }
    wheels_mode->setChecked(true);
    auto* reset_all = makeIconButton(this, QStringLiteral("↺"),
        QStringLiteral("colorHdrWheelsResetButton"), tr("Reset High Dynamic Range"));
    header->addWidget(reset_all);
    header->addWidget(makeIconButton(this, QStringLiteral("…"),
        QStringLiteral("colorHdrWheelsMoreButton"), tr("More actions")));
    root->addLayout(header);

    // Zone pager: previous, six zone-set dots, next.
    auto* pager = new QHBoxLayout;
    pager->addStretch();
    auto* previous = makeIconButton(this, QStringLiteral("‹"),
        QStringLiteral("colorHdrWheelsPreviousZoneButton"), tr("Previous zone set"), 24);
    pager->addWidget(previous);
    auto* dots = new QButtonGroup(this);
    dots->setExclusive(true);
    for (int i = 0; i < 6; ++i) {
        auto* dot = new QPushButton(this);
        dot->setObjectName(QStringLiteral("colorHdrWheelsZoneDot%1").arg(i + 1));
        dot->setAccessibleName(tr("Zone set") + QStringLiteral(" %1").arg(i + 1));
        dot->setProperty("dot", true);
        dot->setCheckable(true);
        dot->setFixedSize(12, 12);
        dot->setFocusPolicy(Qt::NoFocus);
        dots->addButton(dot, i);
        pager->addWidget(dot);
    }
    dots->button(0)->setChecked(true);
    auto* next = makeIconButton(this, QStringLiteral("›"),
        QStringLiteral("colorHdrWheelsNextZoneButton"), tr("Next zone set"), 24);
    pager->addWidget(next);
    pager->addStretch();
    root->addLayout(pager);
    connect(previous, &QPushButton::clicked, this, [dots] {
        const int id = std::max(0, dots->checkedId() - 1);
        dots->button(id)->setChecked(true);
    });
    connect(next, &QPushButton::clicked, this, [dots] {
        const int id = std::min(5, dots->checkedId() + 1);
        dots->button(id)->setChecked(true);
    });

    // The four wheels.
    auto* wheels = new QHBoxLayout;
    wheels->setSpacing(12);
    const WheelSpec specs[] = {
        {"Dark", true, -1.5, 0.20},
        {"Shadow", true, 1.0, 0.22},
        {"Light", true, -1.0, 0.22},
        {"Global", false, 0.0, 0.0},
    };
    for (const auto& spec : specs) {
        const QString wheel_name = QLatin1String(spec.name);
        const QString prefix = QStringLiteral("colorHdrWheels") + wheel_name;
        auto* column = new QVBoxLayout;
        column->setSpacing(4);

        auto* top = new QHBoxLayout;
        auto* zone_column = new QVBoxLayout;
        zone_column->setSpacing(0);
        if (spec.has_zone) {
            zone_column->addWidget(makeIconButton(this, QStringLiteral("◐"),
                                       prefix + QStringLiteral("ZoneButton"), tr("Zone"), 24),
                0, Qt::AlignHCenter);
            auto* zone_value = makeSpin(this, prefix + QStringLiteral("ZoneValue"), -5.0, 5.0, 2,
                spec.zone_value, 44);
            zone_value->setFrame(false);
            zone_column->addWidget(zone_value, 0, Qt::AlignHCenter);
        } else {
            zone_column->addSpacing(26);
        }
        top->addLayout(zone_column);
        top->addStretch();
        top->addWidget(new QLabel(tr(spec.name), this));
        top->addStretch();
        auto* wheel_reset = makeIconButton(this, QStringLiteral("↺"),
            prefix + QStringLiteral("ResetButton"), tr("Reset wheel"), 24);
        top->addWidget(wheel_reset, 0, Qt::AlignTop);
        column->addLayout(top);

        auto* wheel = new WheelWidget(this);
        wheel->setObjectName(prefix + QStringLiteral("Wheel"));
        column->addWidget(wheel, 0, Qt::AlignHCenter);

        auto* tone = new QHBoxLayout;
        tone->setSpacing(10);
        tone->addWidget(makeSliderField(this, tr("Exp"), prefix + QStringLiteral("Exp"),
            -4.0, 4.0, 0.0));
        tone->addWidget(makeSliderField(this, tr("Sat"), prefix + QStringLiteral("Sat"),
            0.0, 3.0, 1.0));
        column->addLayout(tone);

        auto* position = new QHBoxLayout;
        position->setSpacing(4);
        auto* x_box = makeSpin(this, prefix + QStringLiteral("X"), -1.0, 1.0, 2, 0.0, 46);
        auto* y_box = makeSpin(this, prefix + QStringLiteral("Y"), -1.0, 1.0, 2, 0.0, 46);
        position->addWidget(new QLabel(QStringLiteral("X"), this));
        position->addWidget(x_box);
        position->addWidget(new QLabel(QStringLiteral("Y"), this));
        position->addWidget(y_box);
        if (spec.has_zone) {
            position->addWidget(new QLabel(QStringLiteral("∣"), this));
            position->addWidget(makeSpin(this, prefix + QStringLiteral("Range"), 0.0, 1.0, 2,
                spec.range_value, 46));
        }
        position->addStretch();
        column->addLayout(position);
        wheels->addLayout(column, 1);

        // Puck and X/Y fields mirror each other.
        wheel->setChangedHandler([x_box, y_box](double x, double y) {
            x_box->setValue(x);
            y_box->setValue(y);
        });
        const auto syncWheel = [wheel, x_box, y_box](double) {
            wheel->setPosition(x_box->value(), y_box->value());
        };
        connect(x_box, qOverload<double>(&QDoubleSpinBox::valueChanged), wheel, syncWheel);
        connect(y_box, qOverload<double>(&QDoubleSpinBox::valueChanged), wheel, syncWheel);
        connect(wheel_reset, &QPushButton::clicked, this, [this, prefix] {
            for (auto* box : findChildren<QDoubleSpinBox*>(
                     QRegularExpression(QStringLiteral("^%1(Exp|Sat|X|Y|Range|ZoneValue)$")
                                            .arg(prefix))))
                box->setValue(box->property("defaultValue").toDouble());
        });
    }
    root->addLayout(wheels, 1);

    // Bottom row: global controls.
    auto* bottom = new QHBoxLayout;
    bottom->setSpacing(8);
    const struct {
        const char* label; const char* name; double minimum; double maximum;
        int decimals; double default_value; const char* from; const char* to;
    } bottom_specs[] = {
        {"Temp", "Temp", -100.0, 100.0, 2, 0.0, "#4a7fd9", "#e09a3a"},
        {"Tint", "Tint", -100.0, 100.0, 2, 0.0, "#3fbf5a", "#c24fb0"},
        {"Hue", "Hue", -100.0, 100.0, 2, 0.0, "#d9423f", "#4a5fd9"},
        {"Cont", "Cont", 0.0, 2.0, 3, 1.0, "#000000", "#ffffff"},
        {"Pivot", "Pivot", 0.0, 1.0, 3, 0.0, "#000000", "#ffffff"},
        {"MD", "MD", -100.0, 100.0, 2, 0.0, "#3a424e", "#9aa4b0"},
        {"B/Ofs", "BOfs", -1.0, 1.0, 3, 0.0, "#000000", "#6b7684"},
    };
    for (const auto& spec : bottom_specs) {
        bottom->addWidget(new QLabel(tr(spec.label), this));
        auto* box = makeSpin(this,
            QStringLiteral("colorHdrWheels%1").arg(QLatin1String(spec.name)),
            spec.minimum, spec.maximum, spec.decimals, spec.default_value, 56);
        bottom->addWidget(makeFieldColumn(this, box, QLatin1String(spec.from),
            QLatin1String(spec.to)));
    }
    root->addLayout(bottom);

    connect(reset_all, &QPushButton::clicked, this, [this] { restoreDefaults(); });
}

void HdrWheelsPage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    // WheelWidget has no Q_OBJECT, so match it with dynamic_cast.
    for (auto* child : findChildren<QWidget*>()) {
        if (auto* wheel = dynamic_cast<WheelWidget*>(child)) wheel->setPosition(0.0, 0.0);
    }
    if (auto* first = findChild<QPushButton*>("colorHdrWheelsZoneDot1")) first->setChecked(true);
}

}  // namespace ui
