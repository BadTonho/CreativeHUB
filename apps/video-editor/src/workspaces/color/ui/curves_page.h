#pragma once

#include "workspaces/color/model/color_graph.h"

#include <QWidget>

#include <functional>

namespace ui {

// Curves - Custom page of the Color workspace's Sizing dock.
//
// Stage 2: wired to color::CurvesNode in ColorWorkspace when handlers are
// attached; standalone instances still keep local Stage 1 defaults.
// Control object names are "colorCurves*".
class CurvesPage final : public QWidget {
public:
    explicit CurvesPage(QWidget* parent = nullptr);

    void setHandlers(
        std::function<void()> edit_started,
        std::function<void()> edit_finished,
        std::function<void(color::CurveKind, creative_suite::effects::CurvesPointList)> points_changed,
        std::function<void(color::CurveKind)> kind_changed);
    void setCurvePoints(color::CurveKind kind,
                        const creative_suite::effects::CurvesPointList& points);
    [[nodiscard]] color::CurveKind currentKind() const noexcept { return current_kind_; }

    // Restores every control and the graph to their documented defaults.
    void restoreDefaults();

private:
    bool updating_ = false;
    color::CurveKind current_kind_ = color::CurveKind::Master;
    std::function<void()> edit_started_;
    std::function<void()> edit_finished_;
    std::function<void(color::CurveKind, creative_suite::effects::CurvesPointList)> points_changed_;
    std::function<void(color::CurveKind)> kind_changed_;
};

}  // namespace ui
