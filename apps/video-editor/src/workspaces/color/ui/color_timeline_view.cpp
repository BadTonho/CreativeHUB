#include "color_timeline_view.h"

#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>

#include <algorithm>
#include <cmath>
#include <iterator>

namespace ui {
namespace {

constexpr double kRulerHeight = 24.0;
constexpr double kRowHeight = 28.0;
constexpr double kBarInset = 8.0;       // the clip bar is a thin strip in its row
constexpr double kBottomPadding = 4.0;
constexpr int kEmptySeconds = 10;       // ruler length shown while no clip exists
constexpr double kGutter = 30.0;
constexpr double kRightMargin = 8.0;
constexpr int kMaxRows = 6;

int framesPerSecond(int numerator, int denominator) {
    if (numerator <= 0 || denominator <= 0) return 30;
    return std::max(1, static_cast<int>(std::lround(
        static_cast<double>(numerator) / static_cast<double>(denominator))));
}

}  // namespace

QString formatFrameTimecode(qint64 frame, int numerator, int denominator) {
    const qint64 fps = framesPerSecond(numerator, denominator);
    const qint64 clamped = std::max<qint64>(0, frame);
    const qint64 total_seconds = clamped / fps;
    return QStringLiteral("%1:%2:%3:%4")
        .arg(total_seconds / 3600, 2, 10, QLatin1Char('0'))
        .arg(total_seconds / 60 % 60, 2, 10, QLatin1Char('0'))
        .arg(total_seconds % 60, 2, 10, QLatin1Char('0'))
        .arg(clamped % fps, 2, 10, QLatin1Char('0'));
}

ColorTimelineView::ColorTimelineView(QWidget* parent) : QWidget(parent) {
    setObjectName("colorTimelineView");
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumWidth(200);
    setClips({}, 0);
}

void ColorTimelineView::setFrameRate(int numerator, int denominator) {
    numerator_ = numerator;
    denominator_ = denominator;
    update();
}

void ColorTimelineView::setClips(std::vector<Clip> clips, timeline::ClipId selected) {
    clips_ = std::move(clips);
    selected_ = selected;
    setFixedHeight(static_cast<int>(kRulerHeight + trackCount() * kRowHeight + kBottomPadding));
    publish();
    update();
}

void ColorTimelineView::setPlayheadFrame(qint64 frame) {
    playhead_frame_ = std::max<qint64>(0, frame);
    publish();
    update();
}

void ColorTimelineView::setClipClickedHandler(std::function<void(timeline::ClipId)> handler) {
    clicked_ = std::move(handler);
}

int ColorTimelineView::trackCount() const {
    int tracks = 1;
    for (const auto& clip : clips_) tracks = std::max(tracks, clip.track);
    return std::min(tracks, kMaxRows);
}

ColorTimelineView::Geometry ColorTimelineView::geometryInfo() const {
    Geometry result;
    result.plot_left = kGutter;
    result.plot_width = std::max(1.0, width() - kGutter - kRightMargin);
    // With no clips the ruler still shows a notional length so its ticks and
    // timecodes are visible.
    qint64 end = clips_.empty()
        ? static_cast<qint64>(kEmptySeconds) * framesPerSecond(numerator_, denominator_)
        : 1;
    for (const auto& clip : clips_)
        end = std::max(end, clip.start_frame + std::max<qint64>(1, clip.duration_frames));
    result.total_frames = end;
    result.pixels_per_frame = result.plot_width / static_cast<double>(end);
    return result;
}

QRectF ColorTimelineView::clipRect(const Clip& clip, const Geometry& geometry) const {
    const int row = std::clamp(clip.track, 1, kMaxRows) - 1;
    const double x = geometry.plot_left + static_cast<double>(clip.start_frame) * geometry.pixels_per_frame;
    const double w = std::max(2.0, static_cast<double>(std::max<qint64>(1, clip.duration_frames)) *
                                       geometry.pixels_per_frame);
    return QRectF(x, kRulerHeight + row * kRowHeight + kBarInset, w, kRowHeight - 2.0 * kBarInset);
}

void ColorTimelineView::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor("#1a1d22"));
    // Thin separators above the timeline and under the ruler.
    painter.setPen(QPen(QColor("#343b45"), 1.0));
    painter.drawLine(QPointF(0, 0.5), QPointF(width(), 0.5));
    painter.drawLine(QPointF(0, kRulerHeight), QPointF(width(), kRulerHeight));
    const Geometry geometry = geometryInfo();

    // Ruler: ticks at a round number of seconds, spaced at least ~180 px apart.
    const int fps = framesPerSecond(numerator_, denominator_);
    static constexpr int kSecondSteps[] = {1, 2, 5, 10, 15, 30, 60, 120, 300, 600, 1800, 3600};
    qint64 step_frames = static_cast<qint64>(kSecondSteps[std::size(kSecondSteps) - 1]) * fps;
    for (const int seconds : kSecondSteps) {
        if (static_cast<double>(seconds) * fps * geometry.pixels_per_frame >= 180.0) {
            step_frames = static_cast<qint64>(seconds) * fps;
            break;
        }
    }
    QFont small = painter.font();
    small.setPointSizeF(std::max(7.0, small.pointSizeF() - 1.0));
    painter.setFont(small);
    // Tick lines run the full height of the ruler; the label sits to their right.
    for (qint64 frame = 0; frame <= geometry.total_frames; frame += step_frames) {
        const double x = geometry.plot_left + static_cast<double>(frame) * geometry.pixels_per_frame;
        painter.setPen(QPen(QColor("#4a5058"), 1.0));
        painter.drawLine(QPointF(x, 2.0), QPointF(x, kRulerHeight));
        painter.setPen(QColor("#b0b6bf"));
        painter.drawText(QRectF(x + 5.0, 0.0, 90.0, kRulerHeight), Qt::AlignLeft | Qt::AlignVCenter,
            formatFrameTimecode(frame, numerator_, denominator_));
    }

    // Track rows with their labels.
    const int rows = trackCount();
    QFont label_font = painter.font();
    label_font.setBold(true);
    painter.setFont(label_font);
    painter.setPen(QColor("#d7e0ea"));
    for (int row = 0; row < rows; ++row) {
        const double y = kRulerHeight + row * kRowHeight;
        painter.drawText(QRectF(4.0, y, kGutter - 6.0, kRowHeight), Qt::AlignLeft | Qt::AlignVCenter,
            QStringLiteral("V%1").arg(row + 1));
    }
    label_font.setBold(false);
    painter.setFont(label_font);

    // Clips; the selected one is outlined in red.
    for (const auto& clip : clips_) {
        const QRectF block = clipRect(clip, geometry);
        const bool selected = clip.id == selected_;
        painter.setPen(QPen(selected ? QColor("#e0403a") : QColor("#1b3a5c"), 1.5));
        painter.setBrush(QColor("#3b6ea5"));
        painter.drawRect(block);
        if (block.width() > 40.0) {
            painter.setPen(QColor("#e6ebf0"));
            painter.drawText(block.adjusted(4.0, 0.0, -2.0, 0.0), Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetricsF(painter.font()).elidedText(clip.name, Qt::ElideRight, block.width() - 6.0));
        }
    }

    // Playhead: red line with a flag on the ruler.
    const double x = geometry.plot_left + static_cast<double>(playhead_frame_) * geometry.pixels_per_frame;
    if (x >= geometry.plot_left - 1.0 && x <= width()) {
        painter.setPen(QPen(QColor("#e0403a"), 1.5));
        painter.drawLine(QPointF(x, 6.0), QPointF(x, height()));
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#e0403a"));
        painter.drawPolygon(QPolygonF({QPointF(x - 5.0, 2.0), QPointF(x + 5.0, 2.0),
            QPointF(x + 5.0, 9.0), QPointF(x, 14.0), QPointF(x - 5.0, 9.0)}));
    }
}

void ColorTimelineView::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !clicked_) return;
    const Geometry geometry = geometryInfo();
    // Later clips are drawn on top, so test them first.
    for (auto it = clips_.rbegin(); it != clips_.rend(); ++it) {
        // The bar is thin, so accept clicks anywhere in its row.
        if (clipRect(*it, geometry).adjusted(0.0, -kBarInset, 0.0, kBarInset).contains(event->position())) {
            clicked_(it->id);
            return;
        }
    }
}

void ColorTimelineView::publish() {
    setProperty("clipCount", static_cast<int>(clips_.size()));
    setProperty("trackCount", trackCount());
    setProperty("selectedClip", static_cast<qulonglong>(selected_));
    setProperty("playheadFrame", playhead_frame_);
}

}  // namespace ui
