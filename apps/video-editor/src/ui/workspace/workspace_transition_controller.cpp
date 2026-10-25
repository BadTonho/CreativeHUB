#include "ui/workspace/workspace_transition_controller.h"

#include "settings/user_preferences.h"
#include "ui/workspace/workspace_host.h"
#include "ui/workspace/workspace_page_transition.h"

#include <QCoreApplication>
#include <QDockWidget>
#include <QEvent>
#include <QMainWindow>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>

#include <algorithm>
#include <utility>

namespace ui {
namespace {

constexpr int kRenderSettingsDockWidth = 480;
constexpr int kRenderSettingsMinimumWidth = 200;
constexpr int kRenderCenterMinimumWidth = 360;

}  // namespace

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
    if (selectors_.color != nullptr) {
        connect(selectors_.color, &QPushButton::clicked, this, [this]() {
            setPage(WorkspacePageId::Color);
        });
    }
    if (selectors_.render != nullptr) {
        connect(selectors_.render, &QPushButton::clicked, this, [this]() {
            setPage(WorkspacePageId::Render);
        });
    }
    if (selectors_.audio != nullptr) {
        connect(selectors_.audio, &QPushButton::clicked, this, [this]() {
            setPage(WorkspacePageId::Audio);
        });
    }
    for (auto* dock : dockWidgets()) {
        if (dock == nullptr || dock == docks_.render_settings) continue;
        connect(dock, &QDockWidget::visibilityChanged, this, [this](bool) {
            if (workspace_host_ != nullptr)
                workspace_host_->refreshCentralWorkspaceVisibility();
        }, Qt::QueuedConnection);
        connect(dock, &QDockWidget::topLevelChanged, this, [this](bool) {
            if (workspace_host_ != nullptr)
                workspace_host_->refreshCentralWorkspaceVisibility();
        }, Qt::QueuedConnection);
        connect(dock, &QDockWidget::dockLocationChanged, this, [this](Qt::DockWidgetArea) {
            if (workspace_host_ != nullptr)
                workspace_host_->refreshCentralWorkspaceVisibility();
        }, Qt::QueuedConnection);
    }
}

void WorkspaceTransitionController::setPage(WorkspacePageId page) {
    if (workspace_host_ == nullptr) return;
    if (page == WorkspacePageId::Audio && audio_page_preparation_handler_)
        audio_page_preparation_handler_();
    if (page == WorkspacePageId::Color && color_page_preparation_handler_)
        color_page_preparation_handler_();
    if (deferred_page_change_pending_) {
        if (workspace_host_->currentPage() == page) {
            queued_page_.reset();
        } else {
            queued_page_ = page;
        }
        updateSelectors(workspace_host_->currentPage());
        return;
    }
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
        workspace_host_->setPage(page, false);
        updateSelectors(page);
        return;
    }

    const auto current_page = workspace_host_->currentPage();
    auto* top_level_window = workspace_host_->window();
    if (page_transition_ != nullptr &&
        settings::workspacePageTransitionsEnabled() &&
        top_level_window != nullptr &&
        top_level_window->isVisible() &&
        !top_level_window->isMinimized()) {
        // QPushButton's auto-exclusive state changes before its clicked slot.
        // Keep the outgoing page selected until it has left the screen.
        updateSelectors(current_page);
        page_transition_->start(
            top_level_window,
            current_page,
            page,
            settings::workspacePageTransitionDurationMs(),
            [this, page]() { applyPage(page); },
            settings::workspacePageTransitionStyle());
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
    if (page == WorkspacePageId::Color &&
        workspace_host_->currentPage() == WorkspacePageId::Render) {
        queued_page_ = page;
        applyPage(WorkspacePageId::Edit);
        return;
    }
    if (page == WorkspacePageId::Color &&
        workspace_host_->currentPage() == WorkspacePageId::Audio) {
        leaveAudioLayout();
        workspace_host_->setPage(WorkspacePageId::Edit, false);
    }

    auto* main_window = qobject_cast<QMainWindow*>(workspace_host_->window());
    if (workspace_host_->currentPage() == WorkspacePageId::Color) {
        if (color_page_effects_catalog_handler_)
            color_page_effects_catalog_handler_(false);
        workspace_host_->setPage(WorkspacePageId::Edit, false);
        if (main_window != nullptr && !dock_layout_before_color_.isEmpty())
            main_window->restoreState(dock_layout_before_color_, 9);
        dock_layout_before_color_.clear();
    }
    if (page == WorkspacePageId::Color) {
        if (main_window != nullptr) dock_layout_before_color_ = main_window->saveState(9);
        if (color_page_effects_catalog_handler_)
            color_page_effects_catalog_handler_(true);
        workspace_host_->setPage(page, false);
        for (auto* dock : dockWidgets()) if (dock != nullptr) dock->hide();
        workspace_host_->refreshCentralWorkspaceVisibility();
        updateSelectors(page);
        if (page_changed_handler_) page_changed_handler_(page);
        return;
    }

    const bool entering_audio = page == WorkspacePageId::Audio &&
        workspace_host_->currentPage() != WorkspacePageId::Audio;
    const bool leaving_audio = page != WorkspacePageId::Audio &&
        workspace_host_->currentPage() == WorkspacePageId::Audio;
    if (leaving_audio) leaveAudioLayout();
    const bool entering_render =
        page == WorkspacePageId::Render &&
        workspace_host_->currentPage() != WorkspacePageId::Render;
    const bool leaving_render =
        page != WorkspacePageId::Render &&
        workspace_host_->currentPage() == WorkspacePageId::Render;
    const auto docks = dockWidgets();

    if (entering_render) {
        if (main_window != nullptr) {
            // Render temporarily changes dock visibility, the lower-left
            // corner, and the settings width. Restore the native layout when
            // leaving so the other workspaces keep their saved arrangement.
            dock_layout_before_render_ = main_window->saveState(9);
            render_bottom_left_corner_before_render_ =
                main_window->corner(Qt::BottomLeftCorner);
            render_bottom_right_corner_before_render_ =
                main_window->corner(Qt::BottomRightCorner);
        }
        for (std::size_t index = 0; index < docks.size(); ++index) {
            dock_visibility_before_render_[index] =
                docks[index] != nullptr && !docks[index]->isHidden();
        }
        has_render_dock_visibility_snapshot_ = true;
        // Switch the central page while the normal dock tree is still stable.
        // The Render page changes the shared Timeline controls and reparents
        // Preview, both of which can trigger layout work.
        workspace_host_->setPage(page, false);
        for (auto* dock : docks) {
            if (dock != nullptr && dock != docks_.timeline &&
                dock != docks_.render_settings) {
                dock->hide();
            }
        }
        if (docks_.timeline != nullptr) {
            if (main_window != nullptr &&
                (docks_.timeline->isFloating() ||
                 main_window->dockWidgetArea(docks_.timeline) !=
                     Qt::BottomDockWidgetArea)) {
                docks_.timeline->setFloating(false);
                main_window->addDockWidget(
                    Qt::BottomDockWidgetArea, docks_.timeline);
            }
            docks_.timeline->show();
            docks_.timeline->raise();
        }
        if (docks_.render_settings != nullptr) {
            if (main_window != nullptr) {
                docks_.render_settings->setFloating(false);
                main_window->addDockWidget(
                    Qt::LeftDockWidgetArea, docks_.render_settings);
            }
            docks_.render_settings->show();
            docks_.render_settings->raise();
            if (main_window != nullptr) {
                render_main_window_ = main_window;
                render_main_window_->installEventFilter(this);
                render_main_window_->setCorner(
                    Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
                render_main_window_->setCorner(
                    Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
            }
        }
        workspace_host_->refreshCentralWorkspaceVisibility();
    }

    if (leaving_render && has_render_dock_visibility_snapshot_) {
        if (render_main_window_ != nullptr) {
            render_main_window_->removeEventFilter(this);
            render_main_window_ = nullptr;
        }
        if (main_window != nullptr && docks_.render_settings != nullptr) {
            docks_.render_settings->hide();
            main_window->removeDockWidget(docks_.render_settings);
        }
        const bool restored_layout = main_window != nullptr &&
            !dock_layout_before_render_.isEmpty() &&
            main_window->restoreState(dock_layout_before_render_, 9);
        if (main_window != nullptr) {
            main_window->setCorner(
                Qt::BottomLeftCorner,
                render_bottom_left_corner_before_render_);
            main_window->setCorner(
                Qt::BottomRightCorner,
                render_bottom_right_corner_before_render_);
        }
        if (!restored_layout) {
            for (std::size_t index = 0; index < docks.size(); ++index) {
                if (docks[index] == nullptr) continue;
                if (docks[index] == docks_.render_settings) {
                    docks[index]->hide();
                    continue;
                }
                if (dock_visibility_before_render_[index]) {
                    docks[index]->show();
                } else {
                    docks[index]->hide();
                }
            }
        }
        dock_layout_before_render_.clear();
        has_render_dock_visibility_snapshot_ = false;
    }

    if (leaving_render) {
        const auto complete_page_change = [this, page] {
            if (workspace_host_ == nullptr) return;
            if (page == WorkspacePageId::Audio) enterAudioLayout();
            // Let QMainWindow finish restoring dock visibility and geometry
            // before moving Preview and changing the central page.
            workspace_host_->setPage(page, false);
            workspace_host_->refreshCentralWorkspaceVisibility();
            if (docks_.timeline != nullptr) {
                docks_.timeline->setWindowTitle(
                    page == WorkspacePageId::Fusion
                        ? QCoreApplication::translate(
                              "WorkspaceTransitionController", "Node Editor")
                        : QCoreApplication::translate(
                              "WorkspaceTransitionController", "Timeline"));
            }
            updateSelectors(page);
            if (page_changed_handler_) page_changed_handler_(page);
        };
        if (preparing_for_close_) {
            complete_page_change();
        } else {
            deferred_page_change_pending_ = true;
            const auto generation = ++deferred_page_change_generation_;
            QTimer::singleShot(0, this,
                [this, complete_page_change, generation] {
                    if (!deferred_page_change_pending_ ||
                        generation != deferred_page_change_generation_) {
                        return;
                    }
                    deferred_page_change_pending_ = false;
                    complete_page_change();
                    applyQueuedPage();
                });
        }
        return;
    }

    if (!entering_render) {
        if (entering_audio && !leaving_render) enterAudioLayout();
        workspace_host_->setPage(page);
    }
    if (entering_render && main_window != nullptr &&
        docks_.render_settings != nullptr) {
        scheduleRenderSettingsDockResize();
    }
    if (docks_.timeline != nullptr && !leaving_render) {
        docks_.timeline->setWindowTitle(
            page == WorkspacePageId::Fusion ? QCoreApplication::translate("WorkspaceTransitionController", "Node Editor") : QCoreApplication::translate("WorkspaceTransitionController", "Timeline"));
    }

    updateSelectors(page);
    if (page_changed_handler_) page_changed_handler_(page);
}

void WorkspaceTransitionController::setPageChangedHandler(
    std::function<void(WorkspacePageId)> handler) {
    page_changed_handler_ = std::move(handler);
}

void WorkspaceTransitionController::setAudioPagePreparationHandler(
    std::function<void()> handler) {
    audio_page_preparation_handler_ = std::move(handler);
}

void WorkspaceTransitionController::setColorPagePreparationHandler(
    std::function<void()> handler) {
    color_page_preparation_handler_ = std::move(handler);
}

void WorkspaceTransitionController::setColorPageEffectsCatalogHandler(
    std::function<void(bool)> handler) {
    color_page_effects_catalog_handler_ = std::move(handler);
}

bool WorkspaceTransitionController::eventFilter(
    QObject* watched, QEvent* event) {
    if (event == nullptr || workspace_host_ == nullptr ||
        workspace_host_->currentPage() != WorkspacePageId::Render ||
        !has_render_dock_visibility_snapshot_) {
        return QObject::eventFilter(watched, event);
    }

    if (watched == render_main_window_ &&
        (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        scheduleRenderSettingsDockResize();
    }

    return QObject::eventFilter(watched, event);
}

void WorkspaceTransitionController::scheduleRenderSettingsDockResize() {
    if (render_settings_resize_pending_) return;
    render_settings_resize_pending_ = true;
    QTimer::singleShot(1, this, [this] {
        render_settings_resize_pending_ = false;
        if (workspace_host_ != nullptr &&
            render_main_window_ != nullptr &&
            render_main_window_->isVisible() &&
            workspace_host_->currentPage() == WorkspacePageId::Render &&
            has_render_dock_visibility_snapshot_) {
            resizeRenderSettingsDock();
        }
    });
}

void WorkspaceTransitionController::resizeRenderSettingsDock() {
    if (render_main_window_ == nullptr || docks_.render_settings == nullptr) return;
    const auto maximum_width = std::max(
        kRenderSettingsMinimumWidth,
        render_main_window_->width() - kRenderCenterMinimumWidth);
    const auto target_width = std::min(kRenderSettingsDockWidth, maximum_width);
    if (docks_.render_settings->width() == target_width) return;

    render_main_window_->resizeDocks(
        {docks_.render_settings}, {target_width}, Qt::Horizontal);
}

void WorkspaceTransitionController::prepareForClose() {
    if (page_transition_ != nullptr) page_transition_->cancel();
    if (deferred_page_change_pending_) {
        deferred_page_change_pending_ = false;
        ++deferred_page_change_generation_;
    }
    queued_page_.reset();
    if (workspace_host_ != nullptr &&
        (workspace_host_->currentPage() == WorkspacePageId::Color ||
         workspace_host_->currentPage() == WorkspacePageId::Render ||
         workspace_host_->currentPage() == WorkspacePageId::Audio)) {
        preparing_for_close_ = true;
        applyPage(WorkspacePageId::Edit);
        preparing_for_close_ = false;
    }
}

void WorkspaceTransitionController::applyQueuedPage() {
    if (deferred_page_change_pending_ || !queued_page_.has_value()) return;
    const auto page = *queued_page_;
    queued_page_.reset();
    if (workspace_host_ != nullptr &&
        workspace_host_->currentPage() != page) {
        setPage(page);
    }
}

std::array<QDockWidget*, 9>
WorkspaceTransitionController::dockWidgets() const noexcept {
    return {
        docks_.bins,
        docks_.media,
        docks_.toolbox,
        docks_.favorites,
        docks_.effects,
        docks_.inspector,
        docks_.preview,
        docks_.timeline,
        docks_.render_settings};
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
    if (selectors_.color != nullptr) {
        const QSignalBlocker blocker(selectors_.color);
        selectors_.color->setChecked(page == WorkspacePageId::Color);
    }
    if (selectors_.audio != nullptr) {
        const QSignalBlocker blocker(selectors_.audio);
        selectors_.audio->setChecked(page == WorkspacePageId::Audio);
    }
    if (selectors_.render != nullptr) {
        const QSignalBlocker blocker(selectors_.render);
        selectors_.render->setChecked(page == WorkspacePageId::Render);
    }
}

void WorkspaceTransitionController::enterAudioLayout() {
    auto* main_window = workspace_host_ != nullptr
        ? qobject_cast<QMainWindow*>(workspace_host_->window()) : nullptr;
    if (main_window == nullptr || !dock_layout_before_audio_.isEmpty()) return;
    dock_layout_before_audio_ = main_window->saveState(9);
    for (auto* dock : {docks_.bins, docks_.media, docks_.toolbox,
                       docks_.favorites, docks_.effects}) {
        if (dock != nullptr) dock->hide();
    }
    for (auto* dock : {docks_.preview, docks_.inspector, docks_.timeline}) {
        if (dock != nullptr) dock->show();
    }
    if (docks_.render_settings != nullptr) docks_.render_settings->hide();
}

void WorkspaceTransitionController::leaveAudioLayout() {
    auto* main_window = workspace_host_ != nullptr
        ? qobject_cast<QMainWindow*>(workspace_host_->window()) : nullptr;
    if (main_window != nullptr && !dock_layout_before_audio_.isEmpty()) {
        main_window->restoreState(dock_layout_before_audio_, 9);
    }
    dock_layout_before_audio_.clear();
}

}  // namespace ui
