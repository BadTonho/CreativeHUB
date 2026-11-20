#include "curves_page.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <vector>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("CurvesPage", text);
}

// Curve graph: grid, straight segments between draggable points, and a
// decorative marker at the top-left. Points live in the unit square (x right,
// y up). Clicking empty space adds a point, double-clicking an interior point
// removes it, and the two end points only move vertically.
class CurveGraph final : public QWidget {
public:
    explicit CurveGraph(QWidget* parent) : QWidget(parent) {
        setMinimumSize(300, 200);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        setCursor(Qt::CrossCursor);
        resetPoints();
    }

    void resetPoints() {
        points_ = {QPointF(0.0, 0.0), QPointF(1.0, 1.0)};
        dragging_ = -1;
        publishPointCount();
        update();
    }
    [[nodiscard]] int pointCount() const noexcept { return static_cast<int>(points_.size()); }
    [[nodiscard]] QPointF pointAt(int index) const { return points_.at(static_cast<std::size_t>(index)); }
    // Pixel position of a point, for hit testing and for tests.
    [[nodiscard]] QPointF pixelAt(int index) const { return toPixel(pointAt(index)); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF area = graphRect();
        painter.fillRect(area, QColor(QStringLiteral("#14181e")));
        painter.setPen(QPen(QColor(QStringLiteral("#272e38")), 1.0));
        constexpr int kDivisions = 8;
        for (int i = 1; i < kDivisions; ++i) {
            const double fraction = static_cast<double>(i) / kDivisions;
            painter.drawLine(QPointF(area.left() + area.width() * fraction, area.top()),
                QPointF(area.left() + area.width() * fraction, area.bottom()));
            painter.drawLine(QPointF(area.left(), area.top() + area.height() * fraction),
                QPointF(area.right(), area.top() + area.height() * fraction));
        }
        painter.setPen(QPen(QColor(QStringLiteral("#39424e")), 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(area);

        // Top-left marker (decorative).
        const double marker_y = area.top() + 4.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(QStringLiteral("#f2f5f8")));
        painter.drawPolygon(QPolygonF({QPointF(1.0, marker_y - 6.0), QPointF(9.0, marker_y - 6.0),
            QPointF(14.0, marker_y), QPointF(9.0, marker_y + 6.0), QPointF(1.0, marker_y + 6.0)}));

        // Curve and points.
        painter.setPen(QPen(QColor(QStringLiteral("#f2f5f8")), 1.6));
        for (std::size_t i = 1; i < points_.size(); ++i)
            painter.drawLine(toPixel(points_[i - 1]), toPixel(points_[i]));
        painter.setPen(QPen(QColor(QStringLiteral("#9aa4b0")), 1.0));
        painter.setBrush(QColor(QStringLiteral("#f2f5f8")));
        for (const auto& point : points_) painter.drawEllipse(toPixel(point), 4.5, 4.5);
    }

    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() != Qt::LeftButton) return;
        int index = hitTest(event->position());
        if (index < 0) {
            // Add a point between its neighbors by x.
            const QPointF value = toUnit(event->position());
            if (value.x() <= 0.0 || value.x() >= 1.0) return;
            auto position = std::find_if(points_.begin(), points_.end(),
                [&](const QPointF& p) { return p.x() > value.x(); });
            index = static_cast<int>(position - points_.begin());
            points_.insert(position, value);
            publishPointCount();
        }
        dragging_ = index;
        update();
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (dragging_ < 0 || !(event->buttons() & Qt::LeftButton)) return;
        QPointF value = toUnit(event->position());
        const auto last = static_cast<int>(points_.size()) - 1;
        double x = value.x();
        if (dragging_ == 0) x = 0.0;
        else if (dragging_ == last) x = 1.0;
        else {
            // Keep interior points strictly between their neighbors.
            constexpr double kGap = 0.01;
            x = std::clamp(x, points_[static_cast<std::size_t>(dragging_ - 1)].x() + kGap,
                points_[static_cast<std::size_t>(dragging_ + 1)].x() - kGap);
        }
        points_[static_cast<std::size_t>(dragging_)] = QPointF(x, std::clamp(value.y(), 0.0, 1.0));
        update();
    }
    void mouseReleaseEvent(QMouseEvent*) override { dragging_ = -1; }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        const int index = hitTest(event->position());
        if (index > 0 && index < static_cast<int>(points_.size()) - 1) {
            points_.erase(points_.begin() + index);
            publishPointCount();
            dragging_ = -1;
            update();
        }
    }

private:
    // The point count is exposed as the "pointCount" property for tests.
    void publishPointCount() { setProperty("pointCount", static_cast<int>(points_.size())); }
    [[nodiscard]] QRectF graphRect() const { return QRectF(rect()).adjusted(18.0, 8.0, -8.0, -8.0); }
    [[nodiscard]] QPointF toPixel(const QPointF& value) const {
        const QRectF area = graphRect();
        return QPointF(area.left() + value.x() * area.width(),
            area.bottom() - value.y() * area.height());
    }
    [[nodiscard]] QPointF toUnit(const QPointF& pixel) const {
        const QRectF area = graphRect();
        return QPointF(std::clamp((pixel.x() - area.left()) / area.width(), 0.0, 1.0),
            std::clamp((area.bottom() - pixel.y()) / area.height(), 0.0, 1.0));
    }
    [[nodiscard]] int hitTest(const QPointF& pixel) const {
        for (std::size_t i = 0; i < points_.size(); ++i) {
            const QPointF delta = toPixel(points_[i]) - pixel;
            if (std::hypot(delta.x(), delta.y()) <= 8.0) return static_cast<int>(i);
        }
        return -1;
    }

    std::vector<QPointF> points_;
    int dragging_ = -1;
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

QPushButton* makeToggle(QWidget* parent, const QString& text, const QString& name,
    const QString& accessible_name, const char* tone, bool checked) {
    auto* button = makeButton(parent, text, name, accessible_name, 30);
    button->setCheckable(true);
    button->setChecked(checked);
    button->setProperty("tone", QLatin1String(tone));
    return button;
}

QPushButton* makeLink(QWidget* parent, const QString& name) {
    auto* link = makeButton(parent, QStringLiteral("⛓"), name, tr("Link channels"), 26);
    link->setCheckable(true);
    link->setChecked(true);
    return link;
}

}  // namespace

CurvesPage::CurvesPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorCurvesPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorCurvesPage QSpinBox, QWidget#colorCurvesPage QDoubleSpinBox { padding: 2px 2px; }
        QWidget#colorCurvesPage QPushButton[tone]:checked { color: #ffffff; }
        QWidget#colorCurvesPage QPushButton[tone="y"]:checked { background: #e6ebf0; color: #14181e; }
        QWidget#colorCurvesPage QPushButton[tone="r"]:checked { background: #d9423f; border-color: #d9423f; }
        QWidget#colorCurvesPage QPushButton[tone="g"]:checked { background: #2f9f48; border-color: #2f9f48; }
        QWidget#colorCurvesPage QPushButton[tone="b"]:checked { background: #3b57d9; border-color: #3b57d9; }
        QWidget#colorCurvesPage QPushButton:checked { background: #2f5f8f; }
        QWidget#colorCurvesPage QSlider::groove:horizontal { background: #6b7684; height: 2px; }
        QWidget#colorCurvesPage QSlider::handle:horizontal {
            background: #e6ebf0; width: 10px; height: 10px; margin: -4px 0; border-radius: 5px;
        }
        QWidget#colorCurvesPage QSlider[channel="r"]::handle:horizontal { background: #d9423f; }
        QWidget#colorCurvesPage QSlider[channel="g"]::handle:horizontal { background: #3fbf5a; }
        QWidget#colorCurvesPage QSlider[channel="b"]::handle:horizontal { background: #4a5fd9; }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    // Header: title, curve-type buttons, expand, reset and more.
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Curves - Custom"), this);
    title->setObjectName("colorCurvesTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* kinds = new QButtonGroup(this);
    kinds->setExclusive(true);
    const struct { const char* glyph; const char* object; const char* label; } kind_specs[] = {
        {"╱", "colorCurvesCustomModeButton", "Custom"},
        {"H/H", "colorCurvesHueVsHueModeButton", "Hue vs Hue"},
        {"H/S", "colorCurvesHueVsSatModeButton", "Hue vs Sat"},
        {"H/L", "colorCurvesHueVsLumModeButton", "Hue vs Lum"},
        {"L/S", "colorCurvesLumVsSatModeButton", "Lum vs Sat"},
        {"S/S", "colorCurvesSatVsSatModeButton", "Sat vs Sat"},
        {"S/L", "colorCurvesSatVsLumModeButton", "Sat vs Lum"},
    };
    for (const auto& spec : kind_specs) {
        auto* kind = makeButton(this, QString::fromUtf8(spec.glyph),
            QLatin1String(spec.object), tr(spec.label), 36);
        kind->setCheckable(true);
        kinds->addButton(kind);
        header->addWidget(kind);
    }
    kinds->buttons().first()->setChecked(true);
    header->addWidget(makeButton(this, QStringLiteral("⤢"),
        QStringLiteral("colorCurvesExpandButton"), tr("Expand")));
    auto* reset_all = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorCurvesResetButton"), tr("Reset Curves"));
    header->addWidget(reset_all);
    header->addWidget(makeButton(this, QStringLiteral("…"),
        QStringLiteral("colorCurvesMoreButton"), tr("More actions")));
    root->addLayout(header);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    auto* graph = new CurveGraph(this);
    graph->setObjectName("colorCurvesGraph");
    body->addWidget(graph, 3);

    // Right panel: Edit (channel toggles and strength) and Soft Clip.
    auto* side = new QVBoxLayout;
    side->setSpacing(8);
    auto* edit_row = new QHBoxLayout;
    edit_row->addWidget(new QLabel(tr("Edit"), this));
    edit_row->addStretch();
    edit_row->addWidget(makeLink(this, QStringLiteral("colorCurvesEditLink")));
    edit_row->addWidget(makeToggle(this, QStringLiteral("Y"), QStringLiteral("colorCurvesEditY"),
        tr("Luma"), "y", true));
    edit_row->addWidget(makeToggle(this, QStringLiteral("R"), QStringLiteral("colorCurvesEditR"),
        tr("Red"), "r", true));
    edit_row->addWidget(makeToggle(this, QStringLiteral("G"), QStringLiteral("colorCurvesEditG"),
        tr("Green"), "g", true));
    edit_row->addWidget(makeToggle(this, QStringLiteral("B"), QStringLiteral("colorCurvesEditB"),
        tr("Blue"), "b", true));
    edit_row->addWidget(makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorCurvesEditResetButton"), tr("Reset Edit"), 24));
    side->addLayout(edit_row);

    const struct { const char* id; const char* key; const char* label; } strengths[] = {
        {"Y", "y", "Luma strength"}, {"R", "r", "Red strength"},
        {"G", "g", "Green strength"}, {"B", "b", "Blue strength"},
    };
    for (const auto& strength : strengths) {
        const QString id = QLatin1String(strength.id);
        auto* row = new QHBoxLayout;
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setObjectName(QStringLiteral("colorCurvesStrength%1Slider").arg(id));
        slider->setProperty("channel", QLatin1String(strength.key));
        slider->setRange(0, 200);
        slider->setValue(100);
        slider->setFocusPolicy(Qt::NoFocus);
        slider->setAccessibleName(tr(strength.label));
        row->addWidget(slider, 1);
        auto* box = new QSpinBox(this);
        box->setObjectName(QStringLiteral("colorCurvesStrength%1Value").arg(id));
        box->setRange(0, 200);
        box->setValue(100);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setAlignment(Qt::AlignCenter);
        box->setFixedWidth(52);
        box->setKeyboardTracking(false);
        row->addWidget(box);
        auto* row_reset = makeButton(this, QStringLiteral("↺"),
            QStringLiteral("colorCurvesStrength%1ResetButton").arg(id), tr("Reset strength"), 24);
        row->addWidget(row_reset);
        side->addLayout(row);
        connect(slider, &QSlider::valueChanged, box, &QSpinBox::setValue);
        connect(box, qOverload<int>(&QSpinBox::valueChanged), slider, &QSlider::setValue);
        connect(row_reset, &QPushButton::clicked, this, [box] { box->setValue(100); });
    }

    auto* divider = new QFrame(this);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Plain);
    side->addWidget(divider);

    auto* soft_row = new QHBoxLayout;
    soft_row->addWidget(new QLabel(tr("Soft Clip"), this));
    soft_row->addStretch();
    soft_row->addWidget(makeLink(this, QStringLiteral("colorCurvesSoftClipLink")));
    soft_row->addWidget(makeToggle(this, QStringLiteral("R"),
        QStringLiteral("colorCurvesSoftClipR"), tr("Red"), "r", true));
    soft_row->addWidget(makeToggle(this, QStringLiteral("G"),
        QStringLiteral("colorCurvesSoftClipG"), tr("Green"), "g", true));
    soft_row->addWidget(makeToggle(this, QStringLiteral("B"),
        QStringLiteral("colorCurvesSoftClipB"), tr("Blue"), "b", true));
    auto* soft_reset = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorCurvesSoftClipResetButton"), tr("Reset Soft Clip"), 24);
    soft_row->addWidget(soft_reset);
    side->addLayout(soft_row);

    auto* soft_grid = new QGridLayout;
    soft_grid->setHorizontalSpacing(10);
    const struct { const char* label; const char* name; double value; int row; int column; } soft_fields[] = {
        {"Low", "Low", 50.0, 0, 0}, {"High", "High", 50.0, 0, 2},
        {"L.S.", "LS", 0.0, 1, 0}, {"H.S.", "HS", 0.0, 1, 2},
    };
    for (const auto& field : soft_fields) {
        soft_grid->addWidget(new QLabel(tr(field.label), this), field.row, field.column,
            Qt::AlignRight | Qt::AlignVCenter);
        auto* box = new QDoubleSpinBox(this);
        box->setObjectName(QStringLiteral("colorCurvesSoftClip%1").arg(QLatin1String(field.name)));
        box->setRange(0.0, 100.0);
        box->setDecimals(1);
        box->setValue(field.value);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setAlignment(Qt::AlignCenter);
        box->setFixedWidth(56);
        box->setKeyboardTracking(false);
        box->setProperty("defaultValue", field.value);
        soft_grid->addWidget(box, field.row, field.column + 1);
    }
    soft_grid->setColumnStretch(0, 1);
    side->addLayout(soft_grid);
    side->addStretch(1);
    auto* side_host = new QWidget(this);
    side_host->setLayout(side);
    side_host->setMinimumWidth(260);
    side_host->setMaximumWidth(340);
    body->addWidget(side_host, 2);
    root->addLayout(body, 1);

    connect(reset_all, &QPushButton::clicked, this, [this] { restoreDefaults(); });
    connect(findChild<QPushButton*>("colorCurvesEditResetButton"), &QPushButton::clicked, this,
        [this] {
            for (const char* name : {"colorCurvesEditLink", "colorCurvesEditY", "colorCurvesEditR",
                     "colorCurvesEditG", "colorCurvesEditB"})
                findChild<QPushButton*>(QLatin1String(name))->setChecked(true);
        });
    connect(soft_reset, &QPushButton::clicked, this, [this] {
        for (const char* name : {"colorCurvesSoftClipLink", "colorCurvesSoftClipR",
                 "colorCurvesSoftClipG", "colorCurvesSoftClipB"})
            findChild<QPushButton*>(QLatin1String(name))->setChecked(true);
        for (const char* name : {"colorCurvesSoftClipLow", "colorCurvesSoftClipHigh",
                 "colorCurvesSoftClipLS", "colorCurvesSoftClipHS"}) {
            auto* box = findChild<QDoubleSpinBox*>(QLatin1String(name));
            box->setValue(box->property("defaultValue").toDouble());
        }
    });
}

void CurvesPage::restoreDefaults() {
    // CurveGraph has no Q_OBJECT, so match it with dynamic_cast.
    if (auto* graph = dynamic_cast<CurveGraph*>(findChild<QWidget*>("colorCurvesGraph")))
        graph->resetPoints();
    for (auto* button : findChildren<QPushButton*>()) {
        if (button->isCheckable() && button->property("tone").isValid()) button->setChecked(true);
        if (button->objectName().endsWith(QLatin1String("Link"))) button->setChecked(true);
    }
    for (auto* box : findChildren<QSpinBox*>()) box->setValue(100);
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
}

}  // namespace ui
