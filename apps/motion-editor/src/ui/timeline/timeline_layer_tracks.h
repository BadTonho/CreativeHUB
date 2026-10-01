#pragma once

#include "model/composition_document.h"

#include <QString>
#include <QWidget>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace motion::ui {

struct TimelineLayerRow {
    model::LayerId id = 0;
    model::LayerKind kind = model::LayerKind::Image;
    QString name;
    bool visible = true;
    std::int64_t start_frame = 0;
    std::int64_t duration_frames = 0;
    std::int64_t maximum_duration_frames = 0;
    creative_suite::animation::TransformKeyframes keyframes;
};

class TimelineLayerTracks final : public QWidget {
public:
    explicit TimelineLayerTracks(QWidget* parent = nullptr);
    ~TimelineLayerTracks() override;

    TimelineLayerTracks(const TimelineLayerTracks&) = delete;
    TimelineLayerTracks& operator=(const TimelineLayerTracks&) = delete;

    void setRows(std::vector<TimelineLayerRow> rows);
    void setLayerExpanded(model::LayerId id, bool expanded);
    void setTransformGroupExpanded(model::LayerId id, bool expanded);
    void collapseAllLayerTracks();
    void setViewState(std::int64_t end_frame,
                      std::int64_t current_frame,
                      std::int64_t start_frame,
                      std::int64_t frames_per_view);
    void setSelectedLayerId(model::LayerId id);

    void setMediaDropHandler(
        std::function<void(const std::filesystem::path&, std::int64_t, model::LayerId)> handler);
    void setLayerSelectedHandler(std::function<void(model::LayerId)> handler);
    void setLayerMoveHandler(std::function<void(model::LayerId, std::int64_t)> handler);
    void setLayerResizeHandler(std::function<void(model::LayerId, std::int64_t)> handler);
    void setLayerReorderHandler(std::function<void(model::LayerId, std::size_t)> handler);
    void setLayerVisibilityHandler(std::function<void(model::LayerId, bool)> handler);
    void setLayerRemoveHandler(std::function<void(model::LayerId)> handler);
    void setKeyframeSelectedHandler(
        std::function<void(model::LayerId,
                           creative_suite::animation::TransformProperty,
                           std::int64_t)> handler);
    void setKeyframeMoveHandler(
        std::function<bool(model::LayerId,
                           creative_suite::animation::TransformProperty,
                           std::int64_t,
                           std::int64_t)> handler);
    void setCurveSegmentSelectedHandler(
        std::function<void(model::LayerId,
                           creative_suite::animation::TransformProperty,
                           std::int64_t)> handler);

    void setZoomStepHandler(std::function<void(int)> handler);
    void setViewportWidthChangedHandler(std::function<void(int)> handler);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace motion::ui
