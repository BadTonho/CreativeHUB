#pragma once

#include "ui/workspace/workspace_page_id.h"

#include <QObject>
#include <QMainWindow>

#include <array>
#include <functional>
#include <optional>

#include <QByteArray>

class QDockWidget;
class QEvent;
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
        QDockWidget* render_settings = nullptr;
    };

    struct Selectors {
        QPushButton* edit = nullptr;
        QPushButton* fusion = nullptr;
        QPushButton* color = nullptr;
        QPushButton* audio = nullptr;
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
    void setAudioPagePreparationHandler(std::function<void()> handler);
    void setColorPagePreparationHandler(std::function<void()> handler);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] std::array<QDockWidget*, 9> dockWidgets() const noexcept;
    void updateSelectors(WorkspacePageId page);
    void applyPage(WorkspacePageId page);
    void applyQueuedPage();
    void scheduleRenderSettingsDockResize();
    void resizeRenderSettingsDock();
    void enterAudioLayout();
    void leaveAudioLayout();

    WorkspaceHost* workspace_host_ = nullptr;
    QMainWindow* render_main_window_ = nullptr;
    WorkspacePageTransition* page_transition_ = nullptr;
    DockWidgets docks_;
    Selectors selectors_;
    QByteArray dock_layout_before_render_;
    QByteArray dock_layout_before_audio_;
    QByteArray dock_layout_before_color_;
    std::array<bool, 9> dock_visibility_before_render_{};
    bool has_render_dock_visibility_snapshot_ = false;
    bool render_settings_resize_pending_ = false;
    bool preparing_for_close_ = false;
    bool deferred_page_change_pending_ = false;
    unsigned int deferred_page_change_generation_ = 0;
    Qt::DockWidgetArea render_bottom_left_corner_before_render_ =
        Qt::BottomDockWidgetArea;
    Qt::DockWidgetArea render_bottom_right_corner_before_render_ =
        Qt::BottomDockWidgetArea;
    std::optional<WorkspacePageId> queued_page_;
    std::function<void(WorkspacePageId)> page_changed_handler_;
    std::function<void()> audio_page_preparation_handler_;
    std::function<void()> color_page_preparation_handler_;
};

}  // namespace ui
