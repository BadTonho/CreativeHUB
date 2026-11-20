#include "qualifier_page.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("QualifierPage", text);
}

constexpr double kSliderScale = 10.0;

// Gradient bar with two draggable range handles. Positions are percentages
// (0 to 100); the low handle never passes the high handle.
class RangeBar final : public QWidget {
public:
    enum class Kind { Hue, Saturation, Luminance };

    RangeBar(Kind kind, QWidget* parent) : QWidget(parent), kind_(kind) {
        setFixedHeight(26);
        setMinimumWidth(220);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setCursor(Qt::SizeHorCursor);
        publish();
    }

    void setRange(double low, double high) {
        low_ = std::clamp(low, 0.0, 100.0);
        high_ = std::clamp(high, low_, 100.0);
        publish();
        update();
    }
    [[nodiscard]] double low() const noexcept { return low_; }
    [[nodiscard]] double high() const noexcept { return high_; }
    // Called while a handle is dragged: (low, high, whether low was moved).
    void setDraggedHandler(std::function<void(double, double, bool)> handler) {
        dragged_ = std::move(handler);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        const QRectF bar = barRect();
        QLinearGradient gradient(bar.left(), 0, bar.right(), 0);
        switch (kind_) {
        case Kind::Hue: {
            const QColor stops[] = {QColor("#8a2be2"), QColor("#d62fb0"), QColor("#e03030"),
                QColor("#e0a020"), QColor("#d8e020"), QColor("#30c030"), QColor("#20c8c8"),
                QColor("#3050e0"), QColor("#6a2be2")};
            for (int i = 0; i < 9; ++i) gradient.setColorAt(i / 8.0, stops[i]);
            break;
        }
        case Kind::Saturation:
            gradient.setColorAt(0.0, QColor("#7e8280"));
            gradient.setColorAt(1.0, QColor("#58d010"));
            break;
        case Kind::Luminance:
            gradient.setColorAt(0.0, QColor("#000000"));
            gradient.setColorAt(1.0, QColor("#9aa098"));
            break;
        }
        painter.fillRect(bar, gradient);
        // Outside the selected range the bar is dimmed.
        painter.fillRect(QRectF(bar.left(), bar.top(), toPixel(low_) - bar.left(), bar.height()),
            QColor(0, 0, 0, 120));
        painter.fillRect(QRectF(toPixel(high_), bar.top(), bar.right() - toPixel(high_), bar.height()),
            QColor(0, 0, 0, 120));
        painter.setPen(QPen(QColor("#0d0f12"), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(bar);
        // Handles: small white brackets.
        painter.setPen(QPen(QColor("#f2f5f8"), 2.0));
        for (const double value : {low_, high_}) {
            const double x = toPixel(value);
            painter.drawLine(QPointF(x, 1.0), QPointF(x, height() - 1.0));
        }
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        const double x = event->position().x();
        // Nearest handle wins; ties go to the high handle so both can be reached at 0 and 100.
        const bool low_is_nearer = std::abs(x - toPixel(low_)) < std::abs(x - toPixel(high_));
        dragging_low_ = low_is_nearer;
        dragging_ = true;
        moveHandle(x);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_ && (event->buttons() & Qt::LeftButton)) moveHandle(event->position().x());
    }
    void mouseReleaseEvent(QMouseEvent*) override { dragging_ = false; }

private:
    [[nodiscard]] QRectF barRect() const { return QRectF(rect()).adjusted(3.0, 3.0, -3.0, -3.0); }
    [[nodiscard]] double toPixel(double percent) const {
        const QRectF bar = barRect();
        return bar.left() + bar.width() * percent / 100.0;
    }
    [[nodiscard]] double toPercent(double x) const {
        const QRectF bar = barRect();
        return std::clamp((x - bar.left()) / bar.width() * 100.0, 0.0, 100.0);
    }
    void moveHandle(double x) {
        const double value = toPercent(x);
        if (dragging_low_) low_ = std::min(value, high_);
        else high_ = std::max(value, low_);
        publish();
        update();
        if (dragged_) dragged_(low_, high_, dragging_low_);
    }
    // The range is exposed as properties for tests.
    void publish() {
        setProperty("rangeLow", low_);
        setProperty("rangeHigh", high_);
    }

    Kind kind_;
    double low_ = 0.0;
    double high_ = 100.0;
    bool dragging_ = false;
    bool dragging_low_ = false;
    std::function<void(double, double, bool)> dragged_;
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

QDoubleSpinBox* makeSpin(QWidget* parent, const QString& name, double minimum, double maximum,
    double value, int width = 56) {
    auto* box = new QDoubleSpinBox(parent);
    box->setObjectName(name);
    box->setRange(minimum, maximum);
    box->setDecimals(1);
    box->setValue(value);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setAlignment(Qt::AlignCenter);
    box->setFixedWidth(width);
    box->setKeyboardTracking(false);
    box->setProperty("defaultValue", value);
    return box;
}

}  // namespace

QualifierPage::QualifierPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorQualifierPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorQualifierPage QDoubleSpinBox { padding: 2px 2px; }
        QWidget#colorQualifierPage QPushButton:checked { background: #2f5f8f; }
        QWidget#colorQualifierPage QPushButton[switch="true"] {
            border-radius: 7px; background: #14181e; padding: 0;
        }
        QWidget#colorQualifierPage QPushButton[switch="true"]:checked {
            background: #d9423f; border-color: #d9423f;
        }
        QWidget#colorQualifierPage QSlider::groove:horizontal { background: #6b7684; height: 2px; }
        QWidget#colorQualifierPage QSlider::handle:horizontal {
            background: #e6ebf0; width: 4px; height: 8px; margin: -3px 0; border-radius: 1px;
        }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    // Header: title, selection-mode buttons, reset and more.
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Qualifier - HSL"), this);
    title->setObjectName("colorQualifierTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* modes = new QButtonGroup(this);
    modes->setExclusive(true);
    const struct { const char* glyph; const char* object; const char* label; } mode_specs[] = {
        {"◐", "colorQualifierHslModeButton", "HSL"},
        {"RGB", "colorQualifierRgbModeButton", "RGB"},
        {"◑", "colorQualifierLumModeButton", "Luminance"},
        {"3D", "colorQualifierThreeDModeButton", "3D"},
    };
    for (const auto& spec : mode_specs) {
        auto* mode = makeButton(this, QString::fromUtf8(spec.glyph), QLatin1String(spec.object),
            tr(spec.label), 36);
        mode->setCheckable(true);
        modes->addButton(mode);
        header->addWidget(mode);
    }
    modes->buttons().first()->setChecked(true);
    auto* reset_all = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorQualifierResetButton"), tr("Reset Qualifier"));
    header->addWidget(reset_all);
    header->addWidget(makeButton(this, QStringLiteral("…"),
        QStringLiteral("colorQualifierMoreButton"), tr("More actions")));
    root->addLayout(header);

    // Selection tools: pickers, softening brushes and invert.
    auto* tools = new QHBoxLayout;
    tools->setSpacing(4);
    auto* tool_group = new QButtonGroup(this);
    tool_group->setExclusive(true);
    const struct { const char* glyph; const char* label; } tool_specs[] = {
        {"⌖", "Pick"}, {"⌖+", "Add to selection"}, {"⌖−", "Subtract from selection"},
        {"✎−", "Soften less"}, {"✎+", "Soften more"},
    };
    int tool_index = 0;
    for (const auto& spec : tool_specs) {
        auto* tool = makeButton(this, QString::fromUtf8(spec.glyph),
            QStringLiteral("colorQualifierTool%1").arg(++tool_index), tr(spec.label), 34);
        tool->setCheckable(true);
        tool_group->addButton(tool, tool_index - 1);
        tools->addWidget(tool);
    }
    tool_group->button(0)->setChecked(true);
    auto* invert = makeButton(this, QStringLiteral("◌"),
        QStringLiteral("colorQualifierInvertButton"), tr("Invert selection"), 34);
    invert->setCheckable(true);
    tools->addWidget(invert);
    tools->addStretch();
    root->addLayout(tools);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    // Left: the three range sections.
    auto* sections = new QVBoxLayout;
    sections->setSpacing(4);
    const struct {
        const char* name; const char* label; RangeBar::Kind kind;
        const char* labels[4]; const char* fields[4]; double values[4];
    } section_specs[] = {
        {"Hue", "Hue", RangeBar::Kind::Hue, {"Center", "Width", "Soft", "Sym"},
            {"Center", "Width", "Soft", "Sym"}, {50.0, 100.0, 0.0, 50.0}},
        {"Saturation", "Saturation", RangeBar::Kind::Saturation, {"Low", "High", "L. Soft", "H. Soft"},
            {"Low", "High", "LSoft", "HSoft"}, {0.0, 100.0, 0.0, 0.0}},
        {"Luminance", "Luminance", RangeBar::Kind::Luminance, {"Low", "High", "L. Soft", "H. Soft"},
            {"Low", "High", "LSoft", "HSoft"}, {0.0, 100.0, 0.0, 0.0}},
    };
    for (const auto& spec : section_specs) {
        const QString name = QLatin1String(spec.name);
        const QString prefix = QStringLiteral("colorQualifier") + name;
        auto* head = new QHBoxLayout;
        auto* toggle = new QPushButton(this);
        toggle->setObjectName(prefix + QStringLiteral("Toggle"));
        toggle->setAccessibleName(tr("Enable range"));
        toggle->setProperty("switch", true);
        toggle->setCheckable(true);
        toggle->setChecked(true);
        toggle->setFixedSize(30, 14);
        toggle->setFocusPolicy(Qt::NoFocus);
        head->addWidget(toggle);
        head->addWidget(new QLabel(tr(spec.label), this));
        head->addStretch();
        auto* reset = makeButton(this, QStringLiteral("↺"), prefix + QStringLiteral("ResetButton"),
            tr("Reset range"), 24);
        head->addWidget(reset);
        sections->addLayout(head);

        auto* bar = new RangeBar(spec.kind, this);
        bar->setObjectName(prefix + QStringLiteral("Bar"));
        sections->addWidget(bar);

        auto* fields = new QHBoxLayout;
        fields->setSpacing(6);
        QDoubleSpinBox* boxes[4];
        for (int i = 0; i < 4; ++i) {
            fields->addStretch();
            fields->addWidget(new QLabel(tr(spec.labels[i]), this));
            boxes[i] = makeSpin(this, prefix + QLatin1String(spec.fields[i]), 0.0, 100.0, spec.values[i]);
            fields->addWidget(boxes[i]);
        }
        sections->addLayout(fields);

        // Bars and fields mirror each other. Hue shows center +/- width / 2;
        // Saturation and Luminance show Low to High.
        if (spec.kind == RangeBar::Kind::Hue) {
            auto* center = boxes[0];
            auto* width = boxes[1];
            const auto syncBar = [bar, center, width](double) {
                bar->setRange(center->value() - width->value() / 2.0,
                    center->value() + width->value() / 2.0);
            };
            connect(center, qOverload<double>(&QDoubleSpinBox::valueChanged), bar, syncBar);
            connect(width, qOverload<double>(&QDoubleSpinBox::valueChanged), bar, syncBar);
            bar->setDraggedHandler([center, width](double low, double high, bool low_moved) {
                // Dragging a handle resizes the range symmetrically about Center.
                const double edge = low_moved ? low : high;
                width->setValue(std::min(100.0, 2.0 * std::abs(center->value() - edge)));
            });
        } else {
            auto* low = boxes[0];
            auto* high = boxes[1];
            const auto syncBar = [bar, low, high](double) { bar->setRange(low->value(), high->value()); };
            connect(low, qOverload<double>(&QDoubleSpinBox::valueChanged), bar, syncBar);
            connect(high, qOverload<double>(&QDoubleSpinBox::valueChanged), bar, syncBar);
            bar->setDraggedHandler([low, high](double new_low, double new_high, bool) {
                const QSignalBlocker low_blocker(low);
                const QSignalBlocker high_blocker(high);
                low->setValue(new_low);
                high->setValue(new_high);
            });
        }
        connect(reset, &QPushButton::clicked, this, [this, prefix, toggle] {
            for (auto* box : findChildren<QDoubleSpinBox*>(QRegularExpression(
                     QStringLiteral("^%1(Center|Width|Soft|Sym|Low|High|LSoft|HSoft)$").arg(prefix))))
                box->setValue(box->property("defaultValue").toDouble());
            toggle->setChecked(true);
        });
        auto* divider = new QFrame(this);
        divider->setFrameShape(QFrame::NoFrame);
        divider->setFixedHeight(6);
        sections->addWidget(divider);
    }
    sections->addStretch(1);
    body->addLayout(sections, 3);

    // Right: Matte Finesse.
    auto* matte = new QVBoxLayout;
    matte->setSpacing(4);
    auto* matte_head = new QHBoxLayout;
    matte_head->addWidget(new QLabel(tr("Matte Finesse"), this));
    matte_head->addStretch();
    auto* tabs = new QButtonGroup(this);
    tabs->setExclusive(true);
    for (int i = 1; i <= 2; ++i) {
        auto* tab = makeButton(this, QString::number(i),
            QStringLiteral("colorQualifierMatteTab%1").arg(i), tr("Matte Finesse set") +
                QStringLiteral(" %1").arg(i), 44);
        tab->setCheckable(true);
        tabs->addButton(tab, i - 1);
        matte_head->addWidget(tab);
    }
    tabs->button(0)->setChecked(true);
    matte->addLayout(matte_head);
    const struct { const char* label; const char* name; double minimum; double maximum; double value; } matte_specs[] = {
        {"Pre-Filter", "PreFilter", 0.0, 100.0, 0.0},
        {"Clean Black", "CleanBlack", 0.0, 100.0, 0.0},
        {"Clean White", "CleanWhite", 0.0, 100.0, 0.0},
        {"Black Clip", "BlackClip", 0.0, 100.0, 0.0},
        {"White Clip", "WhiteClip", 0.0, 100.0, 100.0},
        {"Blur Radius", "BlurRadius", 0.0, 100.0, 0.0},
        {"In/Out Ratio", "InOutRatio", -100.0, 100.0, 0.0},
    };
    for (const auto& spec : matte_specs) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(tr(spec.label), this));
        row->addStretch();
        auto* box = makeSpin(this, QStringLiteral("colorQualifier%1").arg(QLatin1String(spec.name)),
            spec.minimum, spec.maximum, spec.value, 52);
        box->setFrame(false);
        box->setAlignment(Qt::AlignRight);
        row->addWidget(box);
        matte->addLayout(row);
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setObjectName(QStringLiteral("colorQualifier%1Slider").arg(QLatin1String(spec.name)));
        slider->setRange(static_cast<int>(std::lround(spec.minimum * kSliderScale)),
            static_cast<int>(std::lround(spec.maximum * kSliderScale)));
        slider->setValue(static_cast<int>(std::lround(spec.value * kSliderScale)));
        slider->setFocusPolicy(Qt::NoFocus);
        matte->addWidget(slider);
        connect(slider, &QSlider::valueChanged, box,
            [box](int value) { box->setValue(value / kSliderScale); });
        connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), slider,
            [slider](double value) { slider->setValue(static_cast<int>(std::lround(value * kSliderScale))); });
    }
    matte->addStretch(1);
    auto* matte_host = new QWidget(this);
    matte_host->setLayout(matte);
    matte_host->setMinimumWidth(190);
    matte_host->setMaximumWidth(260);
    body->addWidget(matte_host, 1);
    root->addLayout(body, 1);

    connect(reset_all, &QPushButton::clicked, this, [this] { restoreDefaults(); });
}

void QualifierPage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    for (auto* button : findChildren<QPushButton*>(QRegularExpression(QStringLiteral("Toggle$"))))
        button->setChecked(true);
    if (auto* tool = findChild<QPushButton*>("colorQualifierTool1")) tool->setChecked(true);
    if (auto* invert = findChild<QPushButton*>("colorQualifierInvertButton")) invert->setChecked(false);
    if (auto* hsl = findChild<QPushButton*>("colorQualifierHslModeButton")) hsl->setChecked(true);
    if (auto* tab = findChild<QPushButton*>("colorQualifierMatteTab1")) tab->setChecked(true);
}

}  // namespace ui
