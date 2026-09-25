#include "ui/workspace/workspace_transition_controller.h"

#include "ui/workspace/workspace_host.h"

#include <QDockWidget>
#include <QPushButton>
#include <QSignalBlocker>

namespace ui {

WorkspaceTransitionController::WorkspaceTransitionController(
    WorkspaceHost* workspace_host,
    DockWidgets docks,
    Selectors selectors,
    QObject* parent)
    : QObject(parent),
      workspace_host_(workspace_host),
      docks_(docks),
      selectors_(selectors) {
    if (selectors_.edit != nullptr) {
        connect(selectors_.edit, &QPushButton::clicked, this, [this]() {
            setPage(WorkspacePageId::Edit);
        });
    }
    if (selectors_.fusion != nullptr) {
        connect(selectors_.fusion, &QPushButton::clicked, this, [this]() {
            setPage(WorkspacePageId::Fusion);
        });
    }
    if (selectors_.render != nullptr) {
        connect(selectors_.render, &QPushButton::clicked, this, [this]() {
            setPage(WorkspacePageId::Render);
        });
    }
}

void WorkspaceTransitionController::setPage(WorkspacePageId page) {
    if (workspace_host_ == nullptr) return;

    const bool entering_render =
        page == WorkspacePageId::Render &&
        workspace_host_->currentPage() != WorkspacePageId::Render;
    const bool leaving_render =
        page != WorkspacePageId::Render &&
        workspace_host_->currentPage() == WorkspacePageId::Render;
    const auto docks = dockWidgets();

    if (entering_render) {
        for (std::size_t index = 0; index < docks.size(); ++index) {
            dock_visibility_before_render_[index] =
                docks[index] != nullptr && !docks[index]->isHidden();
        }
        has_render_dock_visibility_snapshot_ = true;
        for (auto* dock : docks) {
            if (dock != nullptr && dock != docks_.timeline) dock->hide();
        }
        if (docks_.timeline != nullptr) docks_.timeline->show();
    }

    workspace_host_->setPage(page);
    if (docks_.timeline != nullptr) {
        docks_.timeline->setWindowTitle(
            page == WorkspacePageId::Fusion ? "Node Editor" : "Timeline");
    }

    if (leaving_render && has_render_dock_visibility_snapshot_) {
        for (std::size_t index = 0; index < docks.size(); ++index) {
            if (docks[index] == nullptr) continue;
            if (dock_visibility_before_render_[index]) {
                docks[index]->show();
            } else {
                docks[index]->hide();
            }
        }
        has_render_dock_visibility_snapshot_ = false;
    }

    updateSelectors(page);
}

void WorkspaceTransitionController::prepareForClose() {
    if (workspace_host_ != nullptr &&
        workspace_host_->currentPage() == WorkspacePageId::Render) {
        setPage(WorkspacePageId::Edit);
    }
}

std::array<QDockWidget*, 7>
WorkspaceTransitionController::dockWidgets() const noexcept {
    return {
        docks_.bins,
        docks_.media,
        docks_.toolbox,
        docks_.favorites,
        docks_.effects,
        docks_.inspector,
        docks_.timeline};
}

void WorkspaceTransitionController::updateSelectors(WorkspacePageId page) {
    if (selectors_.edit != nullptr) {
        const QSignalBlocker blocker(selectors_.edit);
        selectors_.edit->setChecked(page == WorkspacePageId::Edit);
    }
    if (selectors_.fusion != nullptr) {
        const QSignalBlocker blocker(selectors_.fusion);
        selectors_.fusion->setChecked(page == WorkspacePageId::Fusion);
    }
    if (selectors_.render != nullptr) {
        const QSignalBlocker blocker(selectors_.render);
        selectors_.render->setChecked(page == WorkspacePageId::Render);
    }
}

}  // namespace ui
