#include "smart_mask_page.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("SmartMaskPage", text);
}

constexpr int kRulerHeight = 28;
constexpr int kMargin = 8;
constexpr double kSliderScale = 10.0;

// Timeline: a timecode ruler, a draggable playhead and the empty-mask message.
class MaskTimeline final : public QWidget {
public:
    explicit MaskTimeline(QWidget* parent) : QWidget(parent) {
        setMinimumSize(260, 180);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        publish();
    }

    void setPlayhead(double fraction) {
        playhead_ = std::clamp(fraction, 0.0, 1.0);
        publish();
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF area = QRectF(rect()).adjusted(kMargin, 0, -kMargin, 0);
        painter.fillRect(area, QColor("#20242a"));
        painter.fillRect(QRectF(area.left(), area.top(), area.width(), kRulerHeight), QColor("#14181e"));

        // Ruler: three notional timecodes.
        static const char* const kTimecodes[] = {"00:00:00:00", "00:00:13:26", "00:00:26:52"};
        painter.setPen(QColor("#9aa4b0"));
        for (int i = 0; i < 3; ++i) {
            const double x = area.left() + area.width() * i / 3.0;
            painter.drawLine(QPointF(x, kRulerHeight - 8.0), QPointF(x, kRulerHeight));
            painter.drawText(QPointF(x + 5.0, kRulerHeight - 10.0), QString::fromLatin1(kTimecodes[i]));
        }

        // Empty-mask message.
        QFont big = painter.font();
        big.setPointSize(big.pointSize() + 8);
        painter.setFont(big);
        painter.setPen(QColor("#4a5260"));
        const QRectF body(area.left(), kRulerHeight, area.width(), area.height() - kRulerHeight);
        painter.drawText(body.adjusted(0, 0, 0, -18), Qt::AlignCenter, tr("No Mask"));
        QFont small = painter.font();
        small.setPointSize(small.pointSize() - 6);
        painter.setFont(small);
        painter.drawText(body.adjusted(0, 40, 0, 0), Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap,
            tr("Draw a point in the viewer to create a mask."));

        // Playhead: red line with a flag on the ruler.
        const double x = area.left() + area.width() * playhead_;
        painter.setPen(QPen(QColor("#e0403a"), 1.5));
        painter.drawLine(QPointF(x, 6.0), QPointF(x, height()));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#e0403a"));
        painter.drawPolygon(QPolygonF({QPointF(x - 6.0, 2.0), QPointF(x + 6.0, 2.0),
            QPointF(x + 6.0, 10.0), QPointF(x, 16.0), QPointF(x - 6.0, 10.0)}));
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) moveTo(event->position().x());
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton) moveTo(event->position().x());
    }

private:
    void moveTo(double x) { setPlayhead((x - kMargin) / (width() - 2.0 * kMargin)); }
    // The playhead is exposed as the "playhead" property (0 to 1) for tests.
    void publish() { setProperty("playhead", playhead_); }

    double playhead_ = 0.0;
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

struct FieldSpec {
    const char* label;
    const char* name;
    double minimum;
    double maximum;
    int decimals;
    double value;
};

}  // namespace

SmartMaskPage::SmartMaskPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorSmartMaskPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorSmartMaskPage QPushButton:checked { background: #2f5f8f; }
        QWidget#colorSmartMaskPage QDoubleSpinBox { padding: 1px 2px; }
        QWidget#colorSmartMaskPage QSlider::groove:horizontal { background: #6b7684; height: 2px; }
        QWidget#colorSmartMaskPage QSlider::handle:horizontal {
            background: #e6ebf0; width: 4px; height: 8px; margin: -3px 0; border-radius: 1px;
        }
        QWidget#colorSmartMaskPage QSlider::handle:horizontal:disabled { background: #4a5260; }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Smart Mask"), this);
    title->setObjectName("colorSmartMaskTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* reset_all = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorSmartMaskResetButton"), tr("Reset Smart Mask"));
    header->addWidget(reset_all);
    header->addWidget(makeButton(this, QStringLiteral("…"),
        QStringLiteral("colorSmartMaskMoreButton"), tr("More actions")));
    root->addLayout(header);

    // Toolbar: range marks, transport, mask tools, view toggles and settings.
    auto* toolbar = new QHBoxLayout;
    toolbar->setSpacing(4);
    toolbar->addStretch();
    const struct { const char* glyph; const char* object; const char* label; } marks[] = {
        {"⤓", "colorSmartMaskSetInButton", "Set in point"},
        {"⊢", "colorSmartMaskSetOutButton", "Set out point"},
    };
    for (const auto& spec : marks)
        toolbar->addWidget(makeButton(this, QString::fromUtf8(spec.glyph), QLatin1String(spec.object),
            tr(spec.label), 30));
    toolbar->addSpacing(6);
    const struct { const char* glyph; const char* object; const char* label; bool toggle; } transport[] = {
        {"⏮", "colorSmartMaskToStartButton", "Go to start", false},
        {"◀", "colorSmartMaskTrackBackwardButton", "Track backward", false},
        {"⏸", "colorSmartMaskStopButton", "Stop", false},
        {"⇄", "colorSmartMaskLoopButton", "Loop", true},
        {"▶", "colorSmartMaskTrackForwardButton", "Track forward", false},
        {"⏭", "colorSmartMaskToEndButton", "Go to end", false},
    };
    for (const auto& spec : transport) {
        auto* button = makeButton(this, QString::fromUtf8(spec.glyph), QLatin1String(spec.object),
            tr(spec.label), 30);
        button->setCheckable(spec.toggle);
        toolbar->addWidget(button);
    }
    toolbar->addSpacing(10);
    auto* tools = new QButtonGroup(this);
    tools->setExclusive(true);
    const struct { const char* glyph; const char* label; bool enabled; } tool_specs[] = {
        {"✨", "Add point", true}, {"✧", "Subtract point", true},
        {"✎+", "Add stroke", false}, {"✎−", "Subtract stroke", false},
        {"✎▾", "Stroke options", false},
    };
    int tool_index = 0;
    for (const auto& spec : tool_specs) {
        auto* tool = makeButton(this, QString::fromUtf8(spec.glyph),
            QStringLiteral("colorSmartMaskTool%1").arg(++tool_index), tr(spec.label), 34);
        tool->setCheckable(true);
        // Strokes need an existing mask, so they stay off for now.
        tool->setEnabled(spec.enabled);
        tools->addButton(tool, tool_index - 1);
        toolbar->addWidget(tool);
    }
    tools->button(0)->setChecked(true);
    toolbar->addSpacing(6);
    for (const auto& spec : {std::pair<const char*, const char*>{"▣", "colorSmartMaskViewButton"},
             std::pair<const char*, const char*>{"▨", "colorSmartMaskOverlayButton"}}) {
        auto* toggle = makeButton(this, QString::fromUtf8(spec.first), QLatin1String(spec.second),
            QLatin1String(spec.second).endsWith(QLatin1String("ViewButton")) ? tr("Show mask") : tr("Show overlay"), 30);
        toggle->setCheckable(true);
        toolbar->addWidget(toggle);
    }
    toolbar->addWidget(makeButton(this, QStringLiteral("☰"),
        QStringLiteral("colorSmartMaskSettingsButton"), tr("Settings"), 30));
    root->addLayout(toolbar);

    auto* body = new QHBoxLayout;
    body->setSpacing(12);
    auto* timeline = new MaskTimeline(this);
    timeline->setObjectName("colorSmartMaskTimeline");
    body->addWidget(timeline, 3);

    // Right: Quality and refinement.
    auto* side = new QVBoxLayout;
    side->setSpacing(4);
    auto* quality_head = new QHBoxLayout;
    quality_head->addWidget(new QLabel(tr("Quality"), this));
    quality_head->addStretch();
    auto* quality_reset = makeButton(this, QStringLiteral("↺"),
        QStringLiteral("colorSmartMaskQualityResetButton"), tr("Reset Quality"), 24);
    quality_head->addWidget(quality_reset);
    side->addLayout(quality_head);

    auto* segment = new QHBoxLayout;
    segment->setSpacing(0);
    auto* quality_group = new QButtonGroup(this);
    quality_group->setExclusive(true);
    auto* faster = makeButton(this, tr("Faster"), QStringLiteral("colorSmartMaskQualityFaster"),
        tr("Faster quality"), 80);
    auto* better = makeButton(this, tr("Better"), QStringLiteral("colorSmartMaskQualityBetter"),
        tr("Better quality"), 80);
    for (auto* button : {faster, better}) {
        button->setCheckable(true);
        quality_group->addButton(button);
        segment->addWidget(button);
    }
    faster->setChecked(true);
    segment->addStretch();
    side->addLayout(segment);

    // Rows are laid out in two columns: label, value and a thin slider each.
    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(2);
    int row = 0;
    const auto addField = [&](const FieldSpec& spec, int column) {
        auto* cell = new QVBoxLayout;
        cell->setSpacing(0);
        auto* line = new QHBoxLayout;
        line->addWidget(new QLabel(tr(spec.label), this));
        line->addStretch();
        auto* box = new QDoubleSpinBox(this);
        box->setObjectName(QStringLiteral("colorSmartMask%1").arg(QLatin1String(spec.name)));
        box->setRange(spec.minimum, spec.maximum);
        box->setDecimals(spec.decimals);
        box->setValue(spec.value);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setFrame(false);
        box->setAlignment(Qt::AlignRight);
        box->setFixedWidth(52);
        box->setKeyboardTracking(false);
        box->setProperty("defaultValue", spec.value);
        line->addWidget(box);
        cell->addLayout(line);
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setObjectName(QStringLiteral("colorSmartMask%1Slider").arg(QLatin1String(spec.name)));
        slider->setRange(static_cast<int>(std::lround(spec.minimum * kSliderScale)),
            static_cast<int>(std::lround(spec.maximum * kSliderScale)));
        slider->setValue(static_cast<int>(std::lround(spec.value * kSliderScale)));
        slider->setFocusPolicy(Qt::NoFocus);
        cell->addWidget(slider);
        grid->addLayout(cell, row, column);
        connect(slider, &QSlider::valueChanged, box,
            [box](int value) { box->setValue(value / kSliderScale); });
        connect(box, qOverload<double>(&QDoubleSpinBox::valueChanged), slider,
            [slider](double value) { slider->setValue(static_cast<int>(std::lround(value * kSliderScale))); });
    };

    // Smart Refine sits beside Faster/Better and applies only to Better.
    addField({"Smart Refine", "SmartRefine", 0.0, 100.0, 0, 30.0}, 1);
    ++row;
    // Mode and Shape dropdowns.
    const auto addCombo = [&](const char* label, const char* name, const QStringList& items, int column) {
        auto* cell = new QVBoxLayout;
        cell->setSpacing(0);
        cell->addWidget(new QLabel(tr(label), this));
        auto* combo = new QComboBox(this);
        combo->setObjectName(QStringLiteral("colorSmartMask%1").arg(QLatin1String(name)));
        combo->addItems(items);
        cell->addWidget(combo);
        grid->addLayout(cell, row, column);
    };
    addCombo("Mode", "Mode", {tr("Shrink"), tr("Grow")}, 0);
    addCombo("Shape", "Shape", {tr("Circle"), tr("Square")}, 1);
    ++row;
    const FieldSpec left[] = {
        {"Radius", "Radius", 0.0, 100.0, 0, 0.0}, {"Smoothing", "Smoothing", 0.0, 100.0, 0, 0.0},
        {"Blur Radius", "BlurRadius", 0.0, 100.0, 1, 0.0}, {"Clean Black", "CleanBlack", 0.0, 100.0, 1, 0.0},
        {"Clean White", "CleanWhite", 0.0, 100.0, 1, 0.0}, {"Post Filter", "PostFilter", 0.0, 100.0, 1, 0.0},
    };
    const FieldSpec right[] = {
        {"Iterations", "Iterations", 1.0, 20.0, 0, 1.0}, {"Denoise", "Denoise", 0.0, 100.0, 1, 0.0},
        {"In/Out Ratio", "InOutRatio", -100.0, 100.0, 1, 0.0}, {"Black Clip", "BlackClip", 0.0, 100.0, 1, 0.0},
        {"White Clip", "WhiteClip", 0.0, 100.0, 1, 100.0},
    };
    for (int i = 0; i < 6; ++i) {
        addField(left[i], 0);
        if (i < 5) addField(right[i], 1);
        ++row;
    }
    side->addLayout(grid);
    side->addStretch(1);
    auto* side_host = new QWidget(this);
    side_host->setLayout(side);
    side_host->setMinimumWidth(280);
    side_host->setMaximumWidth(360);
    body->addWidget(side_host, 2);
    root->addLayout(body, 1);

    connect(quality_group, &QButtonGroup::buttonToggled, this,
        [this](QAbstractButton*, bool) { updateRefineEnabled(); });
    connect(quality_reset, &QPushButton::clicked, this, [this] { restoreDefaults(); });
    connect(reset_all, &QPushButton::clicked, this, [this] { restoreDefaults(); });
    updateRefineEnabled();
}

void SmartMaskPage::restoreDefaults() {
    for (auto* box : findChildren<QDoubleSpinBox*>())
        box->setValue(box->property("defaultValue").toDouble());
    for (auto* combo : findChildren<QComboBox*>()) combo->setCurrentIndex(0);
    if (auto* faster = findChild<QPushButton*>("colorSmartMaskQualityFaster")) faster->setChecked(true);
    if (auto* tool = findChild<QPushButton*>("colorSmartMaskTool1")) tool->setChecked(true);
    for (const char* name : {"colorSmartMaskLoopButton", "colorSmartMaskViewButton",
             "colorSmartMaskOverlayButton"})
        if (auto* toggle = findChild<QPushButton*>(QLatin1String(name))) toggle->setChecked(false);
    // MaskTimeline has no Q_OBJECT, so match it with dynamic_cast.
    if (auto* timeline = dynamic_cast<MaskTimeline*>(findChild<QWidget*>("colorSmartMaskTimeline")))
        timeline->setPlayhead(0.0);
    updateRefineEnabled();
}

void SmartMaskPage::updateRefineEnabled() {
    // Smart Refine applies only to the Better quality.
    const auto* better = findChild<QPushButton*>("colorSmartMaskQualityBetter");
    const bool enabled = better != nullptr && better->isChecked();
    for (const char* name : {"colorSmartMaskSmartRefine", "colorSmartMaskSmartRefineSlider"})
        if (auto* control = findChild<QWidget*>(QLatin1String(name))) control->setEnabled(enabled);
}

}  // namespace ui
