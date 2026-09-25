#include "ui/workspace/pages/render/render_workspace.h"

#include <QWidget>

#include <utility>

namespace ui {

RenderWorkspace::RenderWorkspace(
    TimelineReadOnlyHandler timeline_read_only_handler,
    QObject* parent)
    : QObject(parent),
      timeline_read_only_handler_(std::move(timeline_read_only_handler)) {}

void RenderWorkspace::createPanels(QWidget* parent) {
    if (central_page_ != nullptr || parent == nullptr) return;

    central_page_ = new QWidget(parent);
    central_page_->setObjectName("renderWorkspacePage");
}

void RenderWorkspace::setActive(bool active) {
    if (active_ == active) return;

    active_ = active;
    if (timeline_read_only_handler_) {
        timeline_read_only_handler_(active_);
    }
}

}  // namespace ui
