#include "tracker_page.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

#include <algorithm>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("TrackerPage", text);
}

constexpr int kRulerHeight = 28;
constexpr int kMargin = 8;

// Timeline graph: a timecode ruler, an empty plot, a draggable playhead, a
// keyframe marker and the four channel readouts. The notional timecodes are
// fixed text; no media is involved.
class TrackerGraph final : public QWidget {
public:
    explicit TrackerGraph(QWidget* parent) : QWidget(parent) {
        setMinimumSize(300, 150);
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

        // Ruler: six evenly spaced timecodes.
        static const char* const kTimecodes[] = {"00:00:00:00", "00:00:06:38", "00:00:13:16",
            "00:00:19:55", "00:00:26:33", "00:00:33:11"};
        painter.setPen(QColor("#9aa4b0"));
        for (int i = 0; i < 6; ++i) {
            const double x = area.left() + area.width() * i / 6.0;
            painter.drawLine(QPointF(x, kRulerHeight - 8.0), QPointF(x, kRulerHeight));
            painter.drawText(QPointF(x + 5.0, kRulerHeight - 10.0), QString::fromLatin1(kTimecodes[i]));
        }

        // Keyframe marker (decorative) near the top right of the plot.
        const QPointF marker(area.right() - 24.0, kRulerHeight + 24.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#c8cdd4"));
        painter.drawPolygon(QPolygonF({QPointF(marker.x(), marker.y() - 4.0),
            QPointF(marker.x() + 4.0, marker.y()), QPointF(marker.x(), marker.y() + 4.0),
            QPointF(marker.x() - 4.0, marker.y())}));

        // Channel readouts: Pan, Tilt, Zoom and Rotate, all at 0.00.
        const struct { const char* color; double slot; } readouts[] = {
            {"#2fd0b0", 0.0}, {"#6a7be8", 1.0}, {"#e040b0", 2.0}, {"#e8e040", 3.0},
        };
        for (const auto& readout : readouts) {
            painter.setPen(QColor(QLatin1String(readout.color)));
            painter.drawText(QPointF(area.right() - 230.0 + readout.slot * 58.0, height() - 14.0),
                QStringLiteral("0.00"));
        }

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
    void moveTo(double x) {
        const double width = this->width() - 2.0 * kMargin;
        setPlayhead((x - kMargin) / width);
    }
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

}  // namespace

TrackerPage::TrackerPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorTrackerPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorTrackerPage QPushButton:checked { background: #2f5f8f; }
        QWidget#colorTrackerPage QCheckBox#colorTrackerPan { color: #2fd0b0; }
        QWidget#colorTrackerPage QCheckBox#colorTrackerTilt { color: #6a7be8; }
        QWidget#colorTrackerPage QCheckBox#colorTrackerZoom { color: #e040b0; }
        QWidget#colorTrackerPage QCheckBox#colorTrackerRotate { color: #e8e040; }
        QWidget#colorTrackerPage QCheckBox#colorTrackerThreeD { color: #5fb0e0; }
        QWidget#colorTrackerPage QSlider::groove:horizontal { background: #6b7684; height: 2px; }
        QWidget#colorTrackerPage QSlider::handle:horizontal {
            background: #c8cdd4; width: 8px; height: 8px; margin: -3px 0; border-radius: 4px;
        }
        QWidget#colorTrackerPage QSlider::groove:vertical { background: #6b7684; width: 2px; }
        QWidget#colorTrackerPage QSlider::handle:vertical {
            background: #c8cdd4; width: 8px; height: 8px; margin: 0 -3px; border-radius: 4px;
        }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    // Header: title, tracker-kind buttons, reset and more.
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Tracker - Window"), this);
    title->setObjectName("colorTrackerTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* kinds = new QButtonGroup(this);
    kinds->setExclusive(true);
    const struct { const char* glyph; const char* object; const char* label; } kind_specs[] = {
        {"⌖", "colorTrackerWindowModeButton", "Window"},
        {"▣", "colorTrackerThreeDModeButton", "3D"},
        {"fx", "colorTrackerFxModeButton", "Effects"},
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
        QStringLiteral("colorTrackerResetButton"), tr("Reset Tracker"));
    header->addWidget(reset_all);
    header->addWidget(makeButton(this, QStringLiteral("…"),
        QStringLiteral("colorTrackerMoreButton"), tr("More actions")));
    root->addLayout(header);

    // Transport row, channel toggles and Clip / Frame scope.
    auto* transport = new QHBoxLayout;
    transport->setSpacing(4);
    const struct { const char* glyph; const char* object; const char* label; bool toggle; } transport_specs[] = {
        {"⏮", "colorTrackerToStartButton", "Go to start", false},
        {"◀", "colorTrackerTrackBackwardButton", "Track backward", false},
        {"⏸", "colorTrackerStopButton", "Stop", false},
        {"⇄", "colorTrackerLoopButton", "Loop", true},
        {"▶", "colorTrackerTrackForwardButton", "Track forward", false},
        {"⏭", "colorTrackerToEndButton", "Go to end", false},
    };
    for (const auto& spec : transport_specs) {
        auto* button = makeButton(this, QString::fromUtf8(spec.glyph), QLatin1String(spec.object),
            tr(spec.label), 30);
        button->setCheckable(spec.toggle);
        transport->addWidget(button);
    }
    transport->addSpacing(10);
    const struct { const char* object; const char* label; } channel_specs[] = {
        {"colorTrackerPan", "Pan"}, {"colorTrackerTilt", "Tilt"}, {"colorTrackerZoom", "Zoom"},
        {"colorTrackerRotate", "Rotate"}, {"colorTrackerThreeD", "3D"},
    };
    for (const auto& spec : channel_specs) {
        auto* channel = new QCheckBox(tr(spec.label), this);
        channel->setObjectName(QLatin1String(spec.object));
        channel->setChecked(true);
        transport->addWidget(channel);
    }
    transport->addSpacing(10);
    auto* scope = new QButtonGroup(this);
    scope->setExclusive(true);
    auto* clip = makeButton(this, tr("Clip"), QStringLiteral("colorTrackerClipButton"),
        tr("Track the clip"), 90);
    auto* frame = makeButton(this, tr("Frame"), QStringLiteral("colorTrackerFrameButton"),
        tr("Track the frame"), 90);
    for (auto* button : {clip, frame}) {
        button->setCheckable(true);
        scope->addButton(button);
        transport->addWidget(button);
    }
    clip->setChecked(true);
    transport->addStretch();
    root->addLayout(transport);

    // Timeline graph with its two sliders.
    auto* graph_row = new QHBoxLayout;
    auto* graph = new TrackerGraph(this);
    graph->setObjectName("colorTrackerGraph");
    graph_row->addWidget(graph, 1);
    auto* vertical = new QSlider(Qt::Vertical, this);
    vertical->setObjectName("colorTrackerVerticalSlider");
    vertical->setAccessibleName(tr("Graph zoom"));
    vertical->setRange(0, 100);
    vertical->setValue(0);
    vertical->setFocusPolicy(Qt::NoFocus);
    graph_row->addWidget(vertical);
    root->addLayout(graph_row, 1);
    auto* horizontal = new QSlider(Qt::Horizontal, this);
    horizontal->setObjectName("colorTrackerHorizontalSlider");
    horizontal->setAccessibleName(tr("Timeline position"));
    horizontal->setRange(0, 1000);
    horizontal->setValue(0);
    horizontal->setFocusPolicy(Qt::NoFocus);
    root->addWidget(horizontal);

    // Footer: Interactive Mode, its two tools and the tracker method.
    auto* footer = new QHBoxLayout;
    auto* interactive = new QCheckBox(tr("Interactive Mode"), this);
    interactive->setObjectName("colorTrackerInteractiveMode");
    footer->addWidget(interactive);
    auto* region = makeButton(this, QStringLiteral("⬚"),
        QStringLiteral("colorTrackerRegionButton"), tr("Define tracking region"), 34);
    auto* points = makeButton(this, QStringLiteral("⌖"),
        QStringLiteral("colorTrackerPointsButton"), tr("Edit tracking points"), 34);
    for (auto* tool : {region, points}) {
        tool->setEnabled(false);
        footer->addWidget(tool);
    }
    footer->addStretch();
    auto* method = new QComboBox(this);
    method->setObjectName("colorTrackerMethod");
    method->addItem(tr("Cloud Tracker"));
    method->addItem(tr("Point Tracker"));
    footer->addWidget(method);
    root->addLayout(footer);

    // The two tools only apply in Interactive Mode.
    connect(interactive, &QCheckBox::toggled, this, [region, points](bool on) {
        region->setEnabled(on);
        points->setEnabled(on);
    });
    connect(reset_all, &QPushButton::clicked, this, [this] { restoreDefaults(); });
}

void TrackerPage::restoreDefaults() {
    for (auto* check : findChildren<QCheckBox*>()) {
        // The channel toggles default to on; Interactive Mode defaults to off.
        check->setChecked(check->objectName() != QLatin1String("colorTrackerInteractiveMode"));
    }
    if (auto* window = findChild<QPushButton*>("colorTrackerWindowModeButton")) window->setChecked(true);
    if (auto* clip = findChild<QPushButton*>("colorTrackerClipButton")) clip->setChecked(true);
    if (auto* loop = findChild<QPushButton*>("colorTrackerLoopButton")) loop->setChecked(false);
    if (auto* method = findChild<QComboBox*>("colorTrackerMethod")) method->setCurrentIndex(0);
    if (auto* vertical = findChild<QSlider*>("colorTrackerVerticalSlider")) vertical->setValue(0);
    if (auto* horizontal = findChild<QSlider*>("colorTrackerHorizontalSlider")) horizontal->setValue(0);
    // TrackerGraph has no Q_OBJECT, so match it with dynamic_cast.
    if (auto* graph = dynamic_cast<TrackerGraph*>(findChild<QWidget*>("colorTrackerGraph")))
        graph->setPlayhead(0.0);
}

}  // namespace ui
