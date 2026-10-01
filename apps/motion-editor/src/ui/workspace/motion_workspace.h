#pragma once

#include <QMainWindow>

#include <functional>

class QDockWidget;

namespace motion::ui {

class TimelineNavigator;

class MotionWorkspace final : public QObject {
    Q_OBJECT

public:
    MotionWorkspace(QMainWindow* window,
                    QWidget* viewer,
                    QWidget* media_pool,
                    QWidget* inspector,
                    QWidget* timeline,
                    QWidget* graph_editor,
                    TimelineNavigator* navigator);

    [[nodiscard]] QDockWidget* mediaPoolDock() const noexcept { return media_pool_dock_; }
    [[nodiscard]] QDockWidget* inspectorDock() const noexcept { return inspector_dock_; }
    [[nodiscard]] QDockWidget* timelineDock() const noexcept { return timeline_dock_; }
    [[nodiscard]] QDockWidget* graphEditorDock() const noexcept { return graph_editor_dock_; }

    void restoreLayout();
    void saveLayout();
    void restoreDefaultLayout();
    void setGraphEditorRefreshHandler(std::function<void()> handler);

private:
    void setActiveTimelineGraphDock(QDockWidget* dock);
    [[nodiscard]] bool timelineGraphAreTabbed() const;

    QMainWindow* window_ = nullptr;
    TimelineNavigator* navigator_ = nullptr;
    QDockWidget* media_pool_dock_ = nullptr;
    QDockWidget* inspector_dock_ = nullptr;
    QDockWidget* timeline_dock_ = nullptr;
    QDockWidget* graph_editor_dock_ = nullptr;
    QDockWidget* active_timeline_graph_dock_ = nullptr;
    std::function<void()> graph_editor_refresh_handler_;
};

} // namespace motion::ui
