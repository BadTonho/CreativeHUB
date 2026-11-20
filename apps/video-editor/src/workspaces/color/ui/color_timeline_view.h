#pragma once

#include "timeline/timeline_model.h"

#include <QString>
#include <QWidget>

#include <functional>
#include <vector>

namespace ui {

// "HH:MM:SS:FF" timecode for a timeline frame. The frame field counts frames at
// the rounded frame rate; invalid rates fall back to 30 fps.
[[nodiscard]] QString formatFrameTimecode(qint64 frame, int numerator, int denominator);

// Compact timeline for the Color workspace: a timecode ruler, one row per video
// track, the clips placed on them and the playhead. It only reads the data it
// is given; the Color workspace feeds it from the project's timeline model and
// it owns no timeline state. Clicking a clip reports its id.
class ColorTimelineView final : public QWidget {
public:
    struct Clip {
        timeline::ClipId id = 0;
        QString name;
        int track = 1;  // 1-based video track number (V1, V2, ...)
        qint64 start_frame = 0;
        qint64 duration_frames = 0;
    };

    explicit ColorTimelineView(QWidget* parent = nullptr);

    void setFrameRate(int numerator, int denominator);
    void setClips(std::vector<Clip> clips, timeline::ClipId selected);
    void setPlayheadFrame(qint64 frame);
    void setClipClickedHandler(std::function<void(timeline::ClipId)> handler);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    struct Geometry {
        double plot_left = 0.0;
        double plot_width = 0.0;
        double pixels_per_frame = 0.0;
        qint64 total_frames = 1;
    };
    [[nodiscard]] Geometry geometryInfo() const;
    [[nodiscard]] int trackCount() const;
    [[nodiscard]] QRectF clipRect(const Clip& clip, const Geometry& geometry) const;
    // Counts and selection are exposed as properties for tests.
    void publish();

    std::vector<Clip> clips_;
    timeline::ClipId selected_ = 0;
    qint64 playhead_frame_ = 0;
    int numerator_ = 30;
    int denominator_ = 1;
    std::function<void(timeline::ClipId)> clicked_;
};

}  // namespace ui
