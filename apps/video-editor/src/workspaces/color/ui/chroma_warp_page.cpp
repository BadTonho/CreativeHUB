#include "chroma_warp_page.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("ChromaWarpPage", text);
}

constexpr double kSliderScale = 1000.0;

// Color plane with pins. Pins live in the unit square (x right, y down).
// Tool 0 adds pins on click, tools 0 and 3 select and drag them.
class WarpGraph final : public QWidget {
public:
    explicit WarpGraph(QWidget* parent) : QWidget(parent) {
        setMinimumSize(240, 240);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setCursor(Qt::CrossCursor);
        resetPins();
    }

    void resetPins() {
        pins_ = {QPointF(0.5, 0.5)};
        selected_ = -1;
        grid_visible_ = true;
        publish();
        update();
    }
    void setTool(int tool) { tool_ = tool; }
    void setGridVisible(bool visible) {
        grid_visible_ = visible;
        update();
    }
    void setSelectionChangedHandler(std::function<void(int)> handler) {
        selection_changed_ = std::move(handler);
    }
    // Removes the selected pin, if any.
    void removeSelected() {
        if (selected_ < 0) return;
        pins_.erase(pins_.begin() + selected_);
        selected_ = -1;
        publish();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor(QStringLiteral("#1c2028")));
        const QRectF area = planeRect();
        if (cache_.size() != area.size().toSize()) cache_ = renderPlane(area.size().toSize());
        painter.save();
        painter.setClipPath(gamutPath(area));
        painter.drawImage(area.topLeft(), cache_);
        if (grid_visible_) {
            painter.setPen(QPen(QColor(0, 0, 0, 90), 1.0));
            constexpr int kDivisions = 10;
            for (int i = 1; i < kDivisions; ++i) {
                const double fraction = static_cast<double>(i) / kDivisions;
                painter.drawLine(QPointF(area.left() + area.width() * fraction, area.top()),
                    QPointF(area.left() + area.width() * fraction, area.bottom()));
                painter.drawLine(QPointF(area.left(), area.top() + area.height() * fraction),
                    QPointF(area.right(), area.top() + area.height() * fraction));
            }
        }
        painter.restore();
        painter.setPen(QPen(QColor(QStringLiteral("#39424e")), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(gamutPath(area));

        for (int i = 0; i < static_cast<int>(pins_.size()); ++i) {
            const QPointF center = toPixel(pins_[static_cast<std::size_t>(i)]);
            const bool selected = i == selected_;
            painter.setPen(QPen(selected ? QColor(QStringLiteral("#ffd24a"))
                                         : QColor(QStringLiteral("#f2f5f8")), 1.6));
            painter.setBrush(QColor(0, 0, 0, selected ? 90 : 40));
            painter.drawEllipse(center, 8.0, 8.0);
            if (selected) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(QStringLiteral("#ffd24a")));
                painter.drawEllipse(center, 2.0, 2.0);
            }
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        int index = hitTest(event->position());
        if (index < 0 && tool_ == 0) {
            const QPointF value = toUnit(event->position());
            if (!gamutPath(QRectF(0, 0, 1, 1)).contains(value)) return;
            pins_.push_back(value);
            index = static_cast<int>(pins_.size()) - 1;
            publish();
        }
        if (index >= 0 && (tool_ == 0 || tool_ == 3)) dragging_ = index;
        select(index);
        update();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_ < 0 || !(event->buttons() & Qt::LeftButton)) return;
        pins_[static_cast<std::size_t>(dragging_)] = toUnit(event->position());
        update();
    }
    void mouseReleaseEvent(QMouseEvent*) override { dragging_ = -1; }

private:
    [[nodiscard]] QRectF planeRect() const {
        const double side = std::min(width(), height()) - 12.0;
        return QRectF((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    }
    // Rounded triangle approximating a display gamut, in the given rect.
    [[nodiscard]] static QPainterPath gamutPath(const QRectF& area) {
        const auto p = [&area](double x, double y) {
            return QPointF(area.left() + x * area.width(), area.top() + y * area.height());
        };
        QPainterPath path;
        path.moveTo(p(0.22, 0.02));
        path.quadTo(p(0.62, 0.22), p(0.94, 0.56));
        path.quadTo(p(0.55, 0.92), p(0.10, 0.96));
        path.quadTo(p(0.00, 0.40), p(0.22, 0.02));
        path.closeSubpath();
        return path;
    }
    // Inverse-distance blend of three primaries with a pale center.
    [[nodiscard]] static QImage renderPlane(const QSize& size) {
        QImage image(size, QImage::Format_ARGB32_Premultiplied);
        const QPointF vertices[3] = {{0.22, 0.02}, {0.94, 0.56}, {0.10, 0.96}};
        const QColor colors[3] = {QColor(205, 30, 80), QColor(20, 30, 220), QColor(40, 190, 50)};
        for (int y = 0; y < size.height(); ++y) {
            for (int x = 0; x < size.width(); ++x) {
                const double u = (x + 0.5) / size.width();
                const double v = (y + 0.5) / size.height();
                double weights[3];
                double total = 0.0;
                for (int i = 0; i < 3; ++i) {
                    const double distance = std::hypot(u - vertices[i].x(), v - vertices[i].y());
                    weights[i] = 1.0 / (0.02 + distance * distance * distance);
                    total += weights[i];
                }
                double red = 0.0, green = 0.0, blue = 0.0;
                for (int i = 0; i < 3; ++i) {
                    red += colors[i].red() * weights[i] / total;
                    green += colors[i].green() * weights[i] / total;
                    blue += colors[i].blue() * weights[i] / total;
                }
                const double glow = std::max(0.0, 1.0 - std::hypot(u - 0.45, v - 0.5) * 2.4);
                red += (245.0 - red) * glow * 0.8;
                green += (245.0 - green) * glow * 0.8;
                blue += (245.0 - blue) * glow * 0.8;
                image.setPixelColor(x, y, QColor(static_cast<int>(red), static_cast<int>(green),
                                              static_cast<int>(blue)));
            }
        }
        return image;
    }
    [[nodiscard]] QPointF toPixel(const QPointF& value) const {
        const QRectF area = planeRect();
        return QPointF(area.left() + value.x() * area.width(),
            area.top() + value.y() * area.height());
    }
    [[nodiscard]] QPointF toUnit(const QPointF& pixel) const {
        const QRectF area = planeRect();
        return QPointF(std::clamp((pixel.x() - area.left()) / area.width(), 0.0, 1.0),
            std::clamp((pixel.y() - area.top()) / area.height(), 0.0, 1.0));
    }
    [[nodiscard]] int hitTest(const QPointF& pixel) const {
        for (int i = static_cast<int>(pins_.size()) - 1; i >= 0; --i) {
            const QPointF delta = toPixel(pins_[static_cast<std::size_t>(i)]) - pixel;
            if (std::hypot(delta.x(), delta.y()) <= 10.0) return i;
        }
        return -1;
    }
    void select(int index) {
        if (selected_ == index) return;
        selected_ = index;
        publish();
    }
    // Counts and selection are exposed as properties for tests.
    void publish() {
        setProperty("pinCount", static_cast<int>(pins_.size()));
        setProperty("selectedPin", selected_);
        if (selection_changed_) selection_changed_(selected_);
    }

    std::vector<QPointF> pins_;
    int selected_ = -1;
    int dragging_ = -1;
    int tool_ = 0;
    bool grid_visible_ = true;
    QImage cache_;
    std::function<void(int)> selection_changed_;
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

}  // namespace

ChromaWarpPage::ChromaWarpPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorChromaWarpPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorChromaWarpPage QPushButton:checked { background: #2f5f8f; }
        QWidget#colorChromaWarpPage QSlider::groove:horizontal { background: #6b7684; height: 2px; }
        QWidget#colorChromaWarpPage QSlider::handle:horizontal {
            background: #e6ebf0; width: 4px; height: 9px; margin: -4px 0; border-radius: 1px;
        }
        QWidget#colorChromaWarpPage QSlider::handle:horizontal:disabled { background: #4a5260; }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    // Header: title, mode buttons, grid toggle, expand, reset and more.
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Color Warp - Chroma Warp"), this);
    title->setObjectName("colorChromaWarpTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* modes = new QButtonGroup(this);
    modes->setExclusive(true);
    auto* chroma_mode = makeButton(this, QStringLiteral("⬡"),
        QStringLiteral("colorChromaWarpChromaModeButton"), tr("Chroma Warp"), 32);
    auto* hue_mode = makeButton(this, QStringLiteral("◎"),
        QStringLiteral("colorChromaWarpHueSatModeButton"), tr("Hue-Saturation Warp"), 32);
    for (auto* mode : {chroma_mode, hue_mode}) {
        mode->setCheckable(true);
        modes->addButton(mode);
        header->addWidget(mode);
    }
    chroma_mode->setChecked(true);
    auto* grid_button = makeButton(this, QStringLiteral("▦"),
        QStringLiteral("colorChromaWarpGridButton"), tr("Show grid"), 32);
    grid_button->setCheckable(true);
    grid_button->setChecked(true);
    header->addWidget(grid_button);
    header->addWidget(makeButton(this, QStringLiteral("⤢"),
        QStringLiteral("colorChromaWarpExpandButton"), tr("Expand")));
    auto* reset_all = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorChromaWarpResetButton"), tr("Reset Chroma Warp"));
    header->addWidget(reset_all);
    header->addWidget(makeButton(this, QStringLiteral("…"),
        QStringLiteral("colorChromaWarpMoreButton"), tr("More actions")));
    root->addLayout(header);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    auto* graph = new WarpGraph(this);
    graph->setObjectName("colorChromaWarpGraph");
    body->addWidget(graph, 3);

    // Side panel: Tools and the selected pin's parameters.
    auto* side = new QVBoxLayout;
    side->setSpacing(6);
    side->addWidget(new QLabel(tr("Tools"), this));
    auto* tools = new QHBoxLayout;
    tools->setSpacing(2);
    auto* tool_group = new QButtonGroup(this);
    tool_group->setExclusive(true);
    const struct { const char* glyph; const char* label; } tool_specs[] = {
        {"⌖", "Add pin"}, {"⊕", "Add pin pair"}, {"⊙", "Pick pin"},
        {"↖", "Select"},
    };
    int tool_index = 0;
    for (const auto& spec : tool_specs) {
        auto* tool = makeButton(this, QString::fromUtf8(spec.glyph),
            QStringLiteral("colorChromaWarpTool%1").arg(++tool_index), tr(spec.label), 40);
        tool->setCheckable(true);
        tool_group->addButton(tool, tool_index - 1);
        tools->addWidget(tool);
    }
    tool_group->button(0)->setChecked(true);
    auto* delete_pin = makeButton(this, QStringLiteral("✕"),
        QStringLiteral("colorChromaWarpDeleteButton"), tr("Delete selected pin"), 40);
    delete_pin->setEnabled(false);
    tools->addWidget(delete_pin);
    side->addLayout(tools);

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Plain);
    side->addWidget(divider);

    auto* pin_header = new QHBoxLayout;
    pin_header->addWidget(new QLabel(tr("Pin"), this));
    pin_header->addStretch();
    auto* pin_reset = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorChromaWarpPinResetButton"), tr("Reset pin"), 24);
    pin_header->addWidget(pin_reset);
    side->addLayout(pin_header);

    // Pin parameters: label, value field and a thin slider, off until a pin
    // is selected.
    const struct { const char* label; const char* name; double minimum; double maximum; double value; } pin_specs[] = {
        {"Chroma Range", "ChromaRange", 0.0, 0.2, 0.04},
        {"Exposure", "Exposure", -1.0, 1.0, 0.0},
        {"Tonal Range Low", "TonalLow", 0.0, 1.0, 1.0},
        {"Tonal Range High", "TonalHigh", 0.0, 1.0, 1.0},
        {"Tonal Range Pivot", "TonalPivot", 0.0, 1.0, 0.5},
    };
    for (const auto& spec : pin_specs) {
        auto* row = new QHBoxLayout;
        row->addWidget(new QLabel(tr(spec.label), this));
        row->addStretch();
        auto* box = new QDoubleSpinBox(this);
        box->setObjectName(QStringLiteral("colorChromaWarp%1").arg(QLatin1String(spec.name)));
        box->setRange(spec.minimum, spec.maximum);
        box->setDecimals(3);
        box->setValue(spec.value);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setFrame(false);
        box->setAlignment(Qt::AlignRight);
        box->setFixedWidth(56);
        box->setKeyboardTracking(false);
        box->setProperty("defaultValue", spec.value);
        row->addWidget(box);
        side->addLayout(row);
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setObjectName(QStringLiteral("colorChromaWarp%1Slider").arg(QLatin1String(spec.name)));
        slider->setRange(static_cast<int>(std::lround(spec.minimum * kSliderScale)),
            static_cast<int>(std::lround(spec.maximum * kSliderScale)));
        slider->setValue(static_cast<int>(std::lround(spec.value * kSliderScale)));
        slider->setFocusPolicy(Qt::NoFocus);
        side->addWidget(slider);
        connect(slider, &QSlider::valueChanged, box,
            [box](int value) { box->setValue(value / kSliderScale); });
        connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), slider,
            [slider](double value) {
                slider->setValue(static_cast<int>(std::lround(value * kSliderScale)));
            });
    }
    auto* link_row = new QHBoxLayout;
    for (const char* name : {"colorChromaWarpLinkButton", "colorChromaWarpUnlinkButton"}) {
        auto* link = makeButton(this, QStringLiteral("⛓"), QLatin1String(name),
            tr("Link pins"), 52);
        link->setEnabled(false);
        link_row->addWidget(link);
    }
    link_row->addStretch();
    side->addLayout(link_row);
    side->addStretch(1);
    auto* side_host = new QWidget(this);
    side_host->setLayout(side);
    side_host->setMinimumWidth(240);
    side_host->setMaximumWidth(320);
    body->addWidget(side_host, 2);
    root->addLayout(body, 1);

    // Pin parameters apply only while a pin is selected.
    const auto setPinControlsEnabled = [this, delete_pin](int selected) {
        const bool enabled = selected >= 0;
        for (auto* control : findChildren<QDoubleSpinBox*>()) control->setEnabled(enabled);
        for (auto* control : findChildren<QSlider*>()) control->setEnabled(enabled);
        delete_pin->setEnabled(enabled);
    };
    graph->setSelectionChangedHandler(setPinControlsEnabled);
    setPinControlsEnabled(-1);
    connect(tool_group, &QButtonGroup::idClicked, this,
        [graph](int id) { graph->setTool(id); });
    connect(delete_pin, &QPushButton::clicked, this, [graph] { graph->removeSelected(); });
    connect(grid_button, &QPushButton::toggled, this,
        [graph](bool visible) { graph->setGridVisible(visible); });
    connect(pin_reset, &QPushButton::clicked, this, [this] {
        for (auto* box : findChildren<QDoubleSpinBox*>())
            box->setValue(box->property("defaultValue").toDouble());
    });
    connect(reset_all, &QPushButton::clicked, this, [this] { restoreDefaults(); });
}

void ChromaWarpPage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    if (auto* chroma = findChild<QPushButton*>("colorChromaWarpChromaModeButton"))
        chroma->setChecked(true);
    if (auto* tool = findChild<QPushButton*>("colorChromaWarpTool1")) tool->setChecked(true);
    if (auto* grid = findChild<QPushButton*>("colorChromaWarpGridButton")) grid->setChecked(true);
    // WarpGraph has no Q_OBJECT, so match it with dynamic_cast.
    if (auto* graph = dynamic_cast<WarpGraph*>(findChild<QWidget*>("colorChromaWarpGraph"))) {
        graph->setTool(0);
        graph->setGridVisible(true);
        graph->resetPins();
    }
}

}  // namespace ui
