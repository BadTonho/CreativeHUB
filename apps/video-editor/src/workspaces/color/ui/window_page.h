#pragma once

#include "workspaces/color/model/color_graph.h"

#include <QWidget>

#include <functional>
#include <optional>
#include <vector>

namespace ui {

// Window page of the Color workspace's Sizing dock: a list of shape windows
// with add/delete actions and Transform and Softness fields for the selected
// one.
//
// Stage 2: wired to color::PowerWindow in ColorWorkspace when handlers are
// attached; standalone instances keep the Stage 1 sample rows.
// Control object names are "colorWindow*".
class WindowPage final : public QWidget {
public:
    explicit WindowPage(QWidget* parent = nullptr);

    void setHandlers(
        std::function<void(creative_suite::composition::ShapeMaskKind)> on_add,
        std::function<void(color::PowerWindowId)> on_remove,
        std::function<void(std::optional<color::PowerWindowId>)> on_select,
        std::function<void(color::PowerWindowId, double, bool, color::WindowCombineMode)> on_settings,
        std::function<void(color::PowerWindowId, double, double, double, double, double)> on_geometry);
    void syncWindows(const std::vector<color::PowerWindow>& windows,
                     std::optional<color::PowerWindowId> selected_id,
                     std::int64_t frame);
    [[nodiscard]] bool isBound() const noexcept { return bound_; }

    // Restores the five sample rows, no selection and every field's default.
    void restoreDefaults();

private:
    bool updating_ = false;
    bool bound_ = false;
    std::optional<color::PowerWindowId> selected_id_;
    std::function<void(creative_suite::composition::ShapeMaskKind)> on_add_;
    std::function<void(color::PowerWindowId)> on_remove_;
    std::function<void(std::optional<color::PowerWindowId>)> on_select_;
    std::function<void(color::PowerWindowId, double, bool, color::WindowCombineMode)> on_settings_;
    std::function<void(color::PowerWindowId, double, double, double, double, double)> on_geometry_;
};

}  // namespace ui
