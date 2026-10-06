#include "motion_workspace.h"

#include "../timeline/timeline_navigator.h"
#include <creative_suite/diagnostics/logger.h>

#include <QDockWidget>
#include <QSettings>

#include <algorithm>
#include <array>

namespace motion::ui {
namespace {
constexpr int kDockLayoutVersion = 1;
constexpr auto kDockLayoutSettingsKey = "workspace/dock_layout_state";

QDockWidget* makeDock(QMainWindow* window, QWidget* content,
                      const QString& title, const QString& object_name)
{
    auto* dock = new QDockWidget(title, window);
    dock->setObjectName(object_name);
    dock->setFeatures(QDockWidget::DockWidgetClosable |
                      QDockWidget::DockWidgetMovable |
                      QDockWidget::DockWidgetFloatable);
    dock->setWidget(content);
    return dock;
}
} // namespace

MotionWorkspace::MotionWorkspace(QMainWindow* window,
                                 QWidget* viewer,
                                 QWidget* media_pool,
                                 QWidget* inspector,
                                 QWidget* timeline,
                                 QWidget* graph_editor,
                                 TimelineNavigator* navigator)
    : QObject(window), window_(window), navigator_(navigator)
{
    if (window_ == nullptr) return;
    window_->setDockNestingEnabled(true);
    window_->setDockOptions(QMainWindow::AllowNestedDocks | QMainWindow::AllowTabbedDocks);
    window_->setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    window_->setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    window_->setCentralWidget(viewer);

    media_pool_dock_ = makeDock(window_, media_pool, QStringLiteral("Media Pool"),
                                QStringLiteral("motion-media-pool-dock"));
    inspector_dock_ = makeDock(window_, inspector, QStringLiteral("Inspector"),
                               QStringLiteral("motion-inspector-dock"));
    timeline_dock_ = makeDock(window_, timeline, QStringLiteral("Timeline"),
                              QStringLiteral("motion-timeline-dock"));
    graph_editor_dock_ = makeDock(window_, graph_editor, QStringLiteral("Graph Editor"),
                                  QStringLiteral("motion-graph-editor-dock"));

    window_->addDockWidget(Qt::LeftDockWidgetArea, media_pool_dock_);
    window_->addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    window_->addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);
    window_->addDockWidget(Qt::BottomDockWidgetArea, graph_editor_dock_);
    window_->tabifyDockWidget(timeline_dock_, graph_editor_dock_);
    graph_editor_dock_->show();
    timeline_dock_->raise();
    active_timeline_graph_dock_ = timeline_dock_;
    if (navigator_ != nullptr) navigator_->setGraphEditorOpen(false);

    QObject::connect(navigator_, &TimelineNavigator::graphEditorToggled,
                     this, [this](bool open) {
        if (navigator_ == nullptr || timeline_dock_ == nullptr || graph_editor_dock_ == nullptr)
            return;
        if (timelineGraphAreTabbed()) {
            auto* target = open ? graph_editor_dock_ : timeline_dock_;
            setActiveTimelineGraphDock(target);
            target->show();
            target->raise();
        } else {
            setActiveTimelineGraphDock(open ? graph_editor_dock_ : timeline_dock_);
            graph_editor_dock_->setVisible(open);
            navigator_->setGraphEditorOpen(open);
        }
        if (open && graph_editor_refresh_handler_) graph_editor_refresh_handler_();
    });
    QObject::connect(graph_editor_dock_, &QDockWidget::visibilityChanged,
                     this, [this](bool visible) {
        if (timeline_dock_ == nullptr || graph_editor_dock_ == nullptr) return;
        if (!timelineGraphAreTabbed() && visible) {
            setActiveTimelineGraphDock(graph_editor_dock_);
            if (navigator_ != nullptr) navigator_->setGraphEditorOpen(true);
            if (graph_editor_refresh_handler_) graph_editor_refresh_handler_();
        } else if (graph_editor_dock_->isHidden()) {
            setActiveTimelineGraphDock(timeline_dock_);
            if (navigator_ != nullptr) navigator_->setGraphEditorOpen(false);
        }
    });
    QObject::connect(window_, &QMainWindow::tabifiedDockWidgetActivated,
                     this, [this](QDockWidget* activated) {
        if (activated != timeline_dock_ && activated != graph_editor_dock_) return;
        setActiveTimelineGraphDock(activated);
    });
}

void MotionWorkspace::setGraphEditorRefreshHandler(std::function<void()> handler)
{
    graph_editor_refresh_handler_ = std::move(handler);
}

bool MotionWorkspace::timelineGraphAreTabbed() const
{
    if (window_ == nullptr || timeline_dock_ == nullptr || graph_editor_dock_ == nullptr)
        return false;
    return window_->tabifiedDockWidgets(timeline_dock_).contains(graph_editor_dock_) ||
           window_->tabifiedDockWidgets(graph_editor_dock_).contains(timeline_dock_);
}

void MotionWorkspace::setActiveTimelineGraphDock(QDockWidget* dock)
{
    if (dock != timeline_dock_ && dock != graph_editor_dock_) return;
    active_timeline_graph_dock_ = dock;
    if (navigator_ != nullptr) navigator_->setGraphEditorOpen(dock == graph_editor_dock_);
    if (dock == graph_editor_dock_ && graph_editor_refresh_handler_)
        graph_editor_refresh_handler_();
}

void MotionWorkspace::restoreLayout()
{
    if (window_ == nullptr || media_pool_dock_ == nullptr || inspector_dock_ == nullptr ||
        timeline_dock_ == nullptr || graph_editor_dock_ == nullptr) return;

    QSettings settings;
    const auto saved_state = settings.value(QString::fromLatin1(kDockLayoutSettingsKey)).toByteArray();
    if (saved_state.isEmpty() || !window_->restoreState(saved_state, kDockLayoutVersion)) {
        restoreDefaultLayout();
    } else {
        const std::array<QDockWidget*, 4> docks{{
            media_pool_dock_, inspector_dock_, timeline_dock_, graph_editor_dock_}};
        bool has_tabs = false;
        for (std::size_t i = 0; i < docks.size(); ++i) {
            for (std::size_t j = i + 1; j < docks.size(); ++j) {
                has_tabs = has_tabs || window_->tabifiedDockWidgets(docks[i]).contains(docks[j]) ||
                    window_->tabifiedDockWidgets(docks[j]).contains(docks[i]);
            }
        }
        const bool old_default_layout = !has_tabs &&
            std::all_of(docks.begin(), docks.end(), [](const QDockWidget* dock) {
                return !dock->isFloating();
            }) && window_->dockWidgetArea(media_pool_dock_) == Qt::LeftDockWidgetArea &&
            window_->dockWidgetArea(inspector_dock_) == Qt::RightDockWidgetArea &&
            window_->dockWidgetArea(timeline_dock_) == Qt::BottomDockWidgetArea &&
            window_->dockWidgetArea(graph_editor_dock_) == Qt::BottomDockWidgetArea &&
            !media_pool_dock_->isHidden() && !inspector_dock_->isHidden() &&
            !timeline_dock_->isHidden() && graph_editor_dock_->isHidden();
        if (old_default_layout) {
            window_->tabifyDockWidget(timeline_dock_, graph_editor_dock_);
            graph_editor_dock_->show();
            timeline_dock_->show();
            timeline_dock_->raise();
            active_timeline_graph_dock_ = timeline_dock_;
        }
    }

    if (timelineGraphAreTabbed()) {
        if (active_timeline_graph_dock_ != graph_editor_dock_)
            active_timeline_graph_dock_ = timeline_dock_;
    } else {
        active_timeline_graph_dock_ = graph_editor_dock_->isVisible()
            ? graph_editor_dock_ : timeline_dock_;
    }
    if (navigator_ != nullptr)
        navigator_->setGraphEditorOpen(active_timeline_graph_dock_ == graph_editor_dock_);
}

void MotionWorkspace::saveLayout()
{
    if (window_ == nullptr || media_pool_dock_ == nullptr || inspector_dock_ == nullptr ||
        timeline_dock_ == nullptr || graph_editor_dock_ == nullptr) return;
    QSettings settings;
    settings.setValue(QString::fromLatin1(kDockLayoutSettingsKey),
                      window_->saveState(kDockLayoutVersion));
    settings.sync();
    if (settings.status() == QSettings::NoError) return;
    creative_suite::diagnostics::Logger::instance().log(
        creative_suite::diagnostics::Level::Warning, "motion_workspace", "save_panel_layout",
        "The workspace panel layout could not be saved.",
        {{"settings_key", kDockLayoutSettingsKey}});
}

void MotionWorkspace::restoreDefaultLayout()
{
    if (window_ == nullptr || media_pool_dock_ == nullptr || inspector_dock_ == nullptr ||
        timeline_dock_ == nullptr || graph_editor_dock_ == nullptr) return;
    const std::array<QDockWidget*, 4> docks{{
        media_pool_dock_, inspector_dock_, timeline_dock_, graph_editor_dock_}};
    for (auto* dock : docks) {
        if (dock->isFloating()) dock->setFloating(false);
        window_->removeDockWidget(dock);
    }
    window_->setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    window_->setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    window_->addDockWidget(Qt::LeftDockWidgetArea, media_pool_dock_);
    window_->addDockWidget(Qt::RightDockWidgetArea, inspector_dock_);
    window_->addDockWidget(Qt::BottomDockWidgetArea, timeline_dock_);
    window_->addDockWidget(Qt::BottomDockWidgetArea, graph_editor_dock_);
    window_->tabifyDockWidget(timeline_dock_, graph_editor_dock_);
    for (auto* dock : docks) if (dock->isFloating()) dock->setFloating(false);
    media_pool_dock_->show();
    inspector_dock_->show();
    timeline_dock_->show();
    graph_editor_dock_->show();
    timeline_dock_->raise();
    active_timeline_graph_dock_ = timeline_dock_;
    window_->resizeDocks({media_pool_dock_, inspector_dock_}, {270, 300}, Qt::Horizontal);
    window_->resizeDocks({timeline_dock_}, {240}, Qt::Vertical);
    if (navigator_ != nullptr) navigator_->setGraphEditorOpen(false);
}

} // namespace motion::ui
