#include "keyframes_page.h"

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
#include <array>
#include <cmath>
#include <functional>
#include <vector>

namespace ui {
namespace {

QString tr(const char* text) {
    return QCoreApplication::translate("KeyframesPage", text);
}

constexpr int kRulerHeight = 32;
constexpr int kRowHeight = 26;
constexpr int kTrackCount = 3;  // Master, Corrector 1, Sizing
// Notional timeline: the second ruler label sits 69% along and reads 26:40 at
// 60 frames per second, which sets the total length used for the timecode.
constexpr double kSecondTickFraction = 0.69;
constexpr double kSecondTickSeconds = 26.0 + 40.0 / 60.0;
constexpr double kTotalSeconds = kSecondTickSeconds / kSecondTickFraction;
constexpr int kFramesPerSecond = 60;

QString timecodeAt(double fraction) {
    const double seconds = std::clamp(fraction, 0.0, 1.0) * kTotalSeconds;
    const int whole = static_cast<int>(seconds);
    const int frames = static_cast<int>((seconds - whole) * kFramesPerSecond);
    return QStringLiteral("%1:%2:%3:%4")
        .arg(whole / 3600, 2, 10, QLatin1Char('0'))
        .arg(whole / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(whole % 60, 2, 10, QLatin1Char('0'))
        .arg(frames, 2, 10, QLatin1Char('0'));
}

// Timeline: a ruler, one row per track, keyframe markers and a draggable
// playhead. The Master row shows the union of the other rows' keyframes.
class KeyframesGraph final : public QWidget {
public:
    explicit KeyframesGraph(QWidget* parent) : QWidget(parent) {
        setMinimumSize(220, kRulerHeight + kTrackCount * kRowHeight + 40);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        resetKeyframes();
    }

    void resetKeyframes() {
        keyframes_ = {std::vector<double>{}, std::vector<double>{0.0}, std::vector<double>{0.0}};
        playhead_ = 0.0;
        publish();
        update();
    }
    void setPlayheadChangedHandler(std::function<void(double)> handler) {
        playhead_changed_ = std::move(handler);
    }
    // Adds a keyframe at the playhead on a track (1 = Corrector, 2 = Sizing).
    void addKeyframe(int track) {
        auto& list = keyframes_.at(static_cast<std::size_t>(track));
        const bool exists = std::any_of(list.begin(), list.end(),
            [this](double value) { return std::abs(value - playhead_) < 1e-6; });
        if (!exists) {
            list.push_back(playhead_);
            std::sort(list.begin(), list.end());
        }
        publish();
        update();
    }
    void setPlayhead(double fraction) {
        playhead_ = std::clamp(fraction, 0.0, 1.0);
        publish();
        update();
        if (playhead_changed_) playhead_changed_(playhead_);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.fillRect(rect(), QColor("#20242a"));
        // Ruler with two timecodes.
        painter.fillRect(QRectF(0, 0, width(), kRulerHeight), QColor("#1a1e24"));
        painter.setPen(QColor("#9aa4b0"));
        painter.drawText(QPointF(6.0, kRulerHeight - 8.0), timecodeAt(0.0));
        const double second_x = width() * kSecondTickFraction;
        painter.drawText(QPointF(second_x + 6.0, kRulerHeight - 8.0), QStringLiteral("00:00:26:40"));
        painter.setPen(QPen(QColor("#39424e"), 1.0));
        painter.drawLine(QPointF(second_x, kRulerHeight - 10.0), QPointF(second_x, height()));

        // Master row: a grey bar across the whole timeline.
        painter.fillRect(QRectF(0, kRulerHeight, width(), kRowHeight), QColor("#6b6b6b"));
        painter.setPen(QPen(QColor("#14181e"), 1.0));
        for (int row = 1; row <= kTrackCount; ++row)
            painter.drawLine(QPointF(0, kRulerHeight + row * kRowHeight),
                QPointF(width(), kRulerHeight + row * kRowHeight));

        // Keyframe markers.
        painter.setPen(QPen(QColor("#14181e"), 1.0));
        painter.setBrush(QColor("#f2f5f8"));
        for (int track = 0; track < kTrackCount; ++track) {
            const double y = kRulerHeight + track * kRowHeight + kRowHeight / 2.0;
            for (const double value : keyframesFor(track))
                painter.drawEllipse(QPointF(value * width(), y), 3.5, 3.5);
        }

        // Playhead: red line with a flag.
        const double x = playhead_ * width();
        painter.setPen(QPen(QColor("#e0403a"), 1.5));
        painter.drawLine(QPointF(x, 6.0), QPointF(x, height()));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#e0403a"));
        painter.drawPolygon(QPolygonF({QPointF(x - 6.0, 2.0), QPointF(x + 6.0, 2.0),
            QPointF(x + 6.0, 10.0), QPointF(x, 16.0), QPointF(x - 6.0, 10.0)}));
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) setPlayhead(event->position().x() / width());
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (event->buttons() & Qt::LeftButton) setPlayhead(event->position().x() / width());
    }

private:
    // Master shows the union of the other tracks.
    [[nodiscard]] std::vector<double> keyframesFor(int track) const {
        if (track != 0) return keyframes_[static_cast<std::size_t>(track)];
        std::vector<double> merged = keyframes_[1];
        merged.insert(merged.end(), keyframes_[2].begin(), keyframes_[2].end());
        std::sort(merged.begin(), merged.end());
        merged.erase(std::unique(merged.begin(), merged.end(),
                         [](double a, double b) { return std::abs(a - b) < 1e-6; }),
            merged.end());
        return merged;
    }
    // Playhead and counts are exposed as properties for tests.
    void publish() {
        setProperty("playhead", playhead_);
        setProperty("keyframeCountMaster", static_cast<int>(keyframesFor(0).size()));
        setProperty("keyframeCountCorrector", static_cast<int>(keyframes_[1].size()));
        setProperty("keyframeCountSizing", static_cast<int>(keyframes_[2].size()));
    }

    std::array<std::vector<double>, kTrackCount> keyframes_;
    double playhead_ = 0.0;
    std::function<void(double)> playhead_changed_;
};

QPushButton* makeButton(QWidget* parent, const QString& text, const QString& name,
    const QString& accessible_name, int width = 22) {
    auto* button = new QPushButton(text, parent);
    button->setObjectName(name);
    button->setAccessibleName(accessible_name);
    button->setToolTip(accessible_name);
    button->setFixedSize(width, 22);
    button->setFocusPolicy(Qt::NoFocus);
    button->setFlat(true);
    return button;
}

}  // namespace

KeyframesPage::KeyframesPage(QWidget* parent) : QWidget(parent) {
    setObjectName("colorKeyframesPage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorKeyframesPage QPushButton:checked { background: #2f5f8f; }
        QWidget#colorKeyframesPage QSlider::groove:horizontal { background: #3a424e; height: 3px; }
        QWidget#colorKeyframesPage QSlider::handle:horizontal {
            background: #9aa4b0; width: 9px; height: 9px; margin: -3px 0; border-radius: 4px;
        }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    // Header: title, track filter, zoom slider and collapse button.
    auto* header = new QHBoxLayout;
    auto* title = new QLabel(tr("Keyframes"), this);
    title->setObjectName("colorKeyframesPageTitle");
    QFont title_font = title->font();
    title_font.setBold(true);
    title->setFont(title_font);
    header->addWidget(title);
    header->addStretch();
    auto* filter = new QComboBox(this);
    filter->setObjectName("colorKeyframesFilter");
    filter->addItem(tr("All"));
    filter->addItem(tr("Corrector"));
    filter->addItem(tr("Sizing"));
    header->addWidget(filter);
    auto* zoom = new QSlider(Qt::Horizontal, this);
    zoom->setObjectName("colorKeyframesZoomSlider");
    zoom->setAccessibleName(tr("Timeline zoom"));
    zoom->setRange(0, 100);
    zoom->setValue(0);
    zoom->setFixedWidth(110);
    zoom->setFocusPolicy(Qt::NoFocus);
    header->addWidget(zoom);
    header->addWidget(makeButton(this, QStringLiteral("⇤"),
        QStringLiteral("colorKeyframesCollapseButton"), tr("Collapse"), 26));
    root->addLayout(header);

    // Body: track list on the left, timeline on the right, rows aligned.
    auto* body = new QHBoxLayout;
    body->setSpacing(0);
    auto* list = new QWidget(this);
    list->setFixedWidth(190);
    auto* list_layout = new QVBoxLayout(list);
    list_layout->setContentsMargins(0, 0, 0, 0);
    list_layout->setSpacing(0);
    auto* timecode = new QLabel(timecodeAt(0.0), list);
    timecode->setObjectName("colorKeyframesTimecode");
    timecode->setAlignment(Qt::AlignCenter);
    timecode->setFixedHeight(kRulerHeight);
    QFont timecode_font = timecode->font();
    timecode_font.setPointSize(timecode_font.pointSize() + 2);
    timecode->setFont(timecode_font);
    timecode->setStyleSheet(QStringLiteral("color: #9aa4b0;"));
    list_layout->addWidget(timecode);
    auto* master = new QLabel(tr("Master"), list);
    master->setObjectName("colorKeyframesTrackMaster");
    master->setAlignment(Qt::AlignCenter);
    master->setFixedHeight(kRowHeight);
    QFont master_font = master->font();
    master_font.setBold(true);
    master->setFont(master_font);
    list_layout->addWidget(master);
    const struct { const char* name; const char* label; } tracks[] = {
        {"Corrector", "Corrector 1"}, {"Sizing", "Sizing"},
    };
    int track_index = 0;
    for (const auto& track : tracks) {
        ++track_index;
        const QString stem = QStringLiteral("colorKeyframesTrack") + QLatin1String(track.name);
        auto* row = new QWidget(list);
        row->setFixedHeight(kRowHeight);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(4, 0, 4, 0);
        row_layout->setSpacing(2);
        auto* enable = makeButton(this, QStringLiteral("●"), stem + QStringLiteral("Enable"),
            tr("Enable track"));
        enable->setCheckable(true);
        enable->setChecked(true);
        auto* lock = makeButton(this, QStringLiteral("▣"), stem + QStringLiteral("Lock"),
            tr("Lock track"));
        lock->setCheckable(true);
        auto* diamond = makeButton(this, QStringLiteral("◆"), stem + QStringLiteral("Diamond"),
            tr("Add keyframe at playhead"));
        auto* expand = makeButton(this, QStringLiteral("›"), stem + QStringLiteral("Expand"),
            tr("Expand track"));
        expand->setCheckable(true);
        for (auto* button : {enable, lock, diamond, expand}) row_layout->addWidget(button);
        row_layout->addWidget(new QLabel(tr(track.label), row), 1);
        list_layout->addWidget(row);
        // Expanding only swaps the chevron for now.
        connect(expand, &QPushButton::toggled, this, [expand](bool open) {
            expand->setText(open ? QStringLiteral("⌄") : QStringLiteral("›"));
        });
        const int index = track_index;
        connect(diamond, &QPushButton::clicked, this, [this, index] {
            // KeyframesGraph has no Q_OBJECT, so match it with dynamic_cast.
            if (auto* graph = dynamic_cast<KeyframesGraph*>(findChild<QWidget*>("colorKeyframesGraph")))
                graph->addKeyframe(index);
        });
    }
    list_layout->addStretch(1);
    body->addWidget(list);
    auto* graph = new KeyframesGraph(this);
    graph->setObjectName("colorKeyframesGraph");
    graph->setPlayheadChangedHandler(
        [timecode](double fraction) { timecode->setText(timecodeAt(fraction)); });
    body->addWidget(graph, 1);
    root->addLayout(body, 1);

}

void KeyframesPage::restoreDefaults() {
    for (auto* button : findChildren<QPushButton*>()) {
        if (!button->isCheckable()) continue;
        // Track enables are on by default; locks and expanders are off.
        button->setChecked(button->objectName().endsWith(QLatin1String("Enable")));
    }
    if (auto* filter = findChild<QComboBox*>("colorKeyframesFilter")) filter->setCurrentIndex(0);
    if (auto* zoom = findChild<QSlider*>("colorKeyframesZoomSlider")) zoom->setValue(0);
    // KeyframesGraph has no Q_OBJECT, so match it with dynamic_cast.
    if (auto* graph = dynamic_cast<KeyframesGraph*>(findChild<QWidget*>("colorKeyframesGraph"))) {
        graph->resetKeyframes();
        graph->setPlayhead(0.0);
    }
}

}  // namespace ui
