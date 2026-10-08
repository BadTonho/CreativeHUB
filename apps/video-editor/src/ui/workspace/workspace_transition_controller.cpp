#include "ui/workspace/workspace_transition_controller.h"

#include "settings/user_preferences.h"
#include "ui/workspace/workspace_host.h"
#include "ui/workspace/workspace_page_transition.h"

#include <QDockWidget>
#include <QPushButton>
#include <QSignalBlocker>

#include <utility>

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
    page_transition_ = new WorkspacePageTransition(this);
    connect(page_transition_, &WorkspacePageTransition::finished,
            this, &WorkspaceTransitionController::applyQueuedPage);

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
    if (page_transition_ != nullptr && page_transition_->isRunning()) {
        if (workspace_host_->currentPage() == page) {
            queued_page_.reset();
            updateSelectors(page);
        } else {
            queued_page_ = page;
            updateSelectors(workspace_host_->currentPage());
        }
        return;
    }
    if (workspace_host_->currentPage() == page) {
        updateSelectors(page);
        return;
    }

    const auto current_page = workspace_host_->currentPage();
    if (page_transition_ != nullptr &&
        settings::workspacePageTransitionsEnabled() &&
        workspace_host_->isVisible()) {
        page_transition_->start(
            {workspace_host_, docks_.timeline, docks_.inspector},
            current_page,
            page,
            settings::workspacePageTransitionDurationMs(),
            [this, page]() { applyPage(page); });
        return;
    }

    applyPage(page);
}

void WorkspaceTransitionController::applyPage(WorkspacePageId page) {
    if (workspace_host_ == nullptr) return;
    if (workspace_host_->currentPage() == page) {
        updateSelectors(page);
        return;
    }

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
    if (page_changed_handler_) page_changed_handler_(page);
}

void WorkspaceTransitionController::setPageChangedHandler(
    std::function<void(WorkspacePageId)> handler) {
    page_changed_handler_ = std::move(handler);
}

void WorkspaceTransitionController::prepareForClose() {
    if (page_transition_ != nullptr) page_transition_->cancel();
    queued_page_.reset();
    if (workspace_host_ != nullptr &&
        workspace_host_->currentPage() == WorkspacePageId::Render) {
        applyPage(WorkspacePageId::Edit);
    }
}

void WorkspaceTransitionController::applyQueuedPage() {
    if (!queued_page_.has_value()) return;
    const auto page = *queued_page_;
    queued_page_.reset();
    if (workspace_host_ != nullptr &&
        workspace_host_->currentPage() != page) {
        setPage(page);
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
