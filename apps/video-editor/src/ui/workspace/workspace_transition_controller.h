#pragma once

#include "ui/workspace/workspace_page_id.h"

#include <QObject>

#include <array>
#include <functional>
#include <optional>

#include <QByteArray>

class QDockWidget;
class QPushButton;

namespace ui {

class WorkspaceHost;
class WorkspacePageTransition;

// Coordinates workspace transitions without owning the window's native docks.
class WorkspaceTransitionController final : public QObject {
public:
    struct DockWidgets {
        QDockWidget* bins = nullptr;
        QDockWidget* media = nullptr;
        QDockWidget* toolbox = nullptr;
        QDockWidget* favorites = nullptr;
        QDockWidget* effects = nullptr;
        QDockWidget* inspector = nullptr;
        QDockWidget* preview = nullptr;
        QDockWidget* timeline = nullptr;
    };

    struct Selectors {
        QPushButton* edit = nullptr;
        QPushButton* fusion = nullptr;
        QPushButton* render = nullptr;
    };

    explicit WorkspaceTransitionController(
        WorkspaceHost* workspace_host,
        DockWidgets docks,
        Selectors selectors,
        QObject* parent = nullptr);

    void setPage(WorkspacePageId page);
    void prepareForClose();
    void setPageChangedHandler(std::function<void(WorkspacePageId)> handler);

private:
    [[nodiscard]] std::array<QDockWidget*, 8> dockWidgets() const noexcept;
    void updateSelectors(WorkspacePageId page);
    void applyPage(WorkspacePageId page);
    void applyQueuedPage();

    WorkspaceHost* workspace_host_ = nullptr;
    WorkspacePageTransition* page_transition_ = nullptr;
    DockWidgets docks_;
    Selectors selectors_;
    QByteArray dock_layout_before_render_;
    std::array<bool, 8> dock_visibility_before_render_{};
    bool has_render_dock_visibility_snapshot_ = false;
    std::optional<WorkspacePageId> queued_page_;
    std::function<void(WorkspacePageId)> page_changed_handler_;
};

}  // namespace ui
