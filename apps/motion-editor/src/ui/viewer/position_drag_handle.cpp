#include "position_drag_handle.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <cmath>
#include <utility>

namespace motion::ui {
PositionDragHandle::PositionDragHandle(QWidget* surface,std::function<std::optional<QPointF>()> pivot,
    std::function<QRectF()> canvas,std::function<model::CanvasSize()> size)
    : QObject(surface),surface_(surface),pivot_(std::move(pivot)),canvas_(std::move(canvas)),size_(std::move(size))
{ surface_->installEventFilter(this); }
PositionDragHandle::~PositionDragHandle() { release(); }
void PositionDragHandle::setHandlers(PositionDragHandlers handlers) { cancel();handlers_=std::move(handlers); }
void PositionDragHandle::setEnabled(bool enabled) {enabled_=enabled;if(!enabled)cancel();surface_->update();}
std::optional<QPointF> PositionDragHandle::center() const {
    if (!enabled_) return {};
    const auto pivot=pivot_();if(!pivot)return {};
    const auto point=*pivot+QPointF(32,0);
    if (!std::isfinite(point.x())||!std::isfinite(point.y())||!surface_->rect().contains(point.toPoint()))return {};
    return point;
}
bool PositionDragHandle::hit(QPointF p) const {
    const auto c=center();if(!c)return false;const auto d=p-*c;return d.x()*d.x()+d.y()*d.y()<=100;
}
void PositionDragHandle::release() {
    pressed_=dragged_=false;
    for(auto* ancestor:ancestors_)ancestor->removeEventFilter(this);
    ancestors_.clear();
    if(QWidget::mouseGrabber()==surface_)surface_->releaseMouse();
    surface_->unsetCursor();
}
void PositionDragHandle::cancel() {if(!pressed_)return;release();if(handlers_.cancel)handlers_.cancel();}
bool PositionDragHandle::preview(QPointF point) {
    const auto d=point-press_;
    if(!dragged_ && std::abs(d.x())+std::abs(d.y())<QApplication::startDragDistance())return true;
    dragged_=true;
    const QPointF pixels(d.x()*frozen_size_.width/frozen_canvas_.width(),d.y()*frozen_size_.height/frozen_canvas_.height());
    if(!handlers_.preview || !handlers_.preview(pixels)){cancel();return false;}
    return pressed_;
}
bool PositionDragHandle::eventFilter(QObject* watched,QEvent* event) {
    const auto type=event->type();
    if(pressed_) {
        if(type==QEvent::Hide||type==QEvent::Close||type==QEvent::WindowDeactivate||type==QEvent::Move||
            type==QEvent::Resize||type==QEvent::ParentChange||(watched==surface_&&type==QEvent::UngrabMouse))cancel();
        if(type==QEvent::ShortcutOverride) {
            const auto* key=static_cast<QKeyEvent*>(event);
            if(key->key()==Qt::Key_Escape||key->matches(QKeySequence::Undo)||key->matches(QKeySequence::Redo)){event->accept();return true;}
        }
        if(type==QEvent::KeyPress) {
            const auto* key=static_cast<QKeyEvent*>(event);
            if(key->matches(QKeySequence::Redo)){event->accept();return true;}
            const bool consume=key->key()==Qt::Key_Escape||key->matches(QKeySequence::Undo);
            cancel();if(consume){event->accept();return true;}
        }
    }
    if(watched!=surface_)return QObject::eventFilter(watched,event);
    if(type==QEvent::MouseButtonPress&&!pressed_) {
        const auto* mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()==Qt::LeftButton&&hit(mouse->position())&&handlers_.begin) {
            surface_->setFocus(Qt::MouseFocusReason);
            if(handlers_.begin()) {
                // Widget-local QPointF preserves fractional logical input. All
                // layout movement cancels, so this frozen space stays valid.
                press_=mouse->position();frozen_canvas_=canvas_();frozen_size_=size_();
                pressed_=true;dragged_=false;
                if(frozen_canvas_.isEmpty()||frozen_size_.width<=0||frozen_size_.height<=0){cancel();return true;}
                for(auto* ancestor=surface_->parentWidget();ancestor;ancestor=ancestor->parentWidget()) {
                    ancestors_.push_back(ancestor);ancestor->installEventFilter(this);
                }
                surface_->grabMouse();surface_->setCursor(Qt::ClosedHandCursor);
            }
            event->accept();return true;
        }
    }
    if(type==QEvent::MouseMove) {
        const auto* mouse=static_cast<QMouseEvent*>(event);
        if(pressed_) {
            if(!(mouse->buttons()&Qt::LeftButton))cancel();else (void)preview(mouse->position());
            event->accept();return true;
        }
        if(hit(mouse->position())&&!mouse->buttons()){surface_->setCursor(Qt::OpenHandCursor);return true;}
    }
    if(type==QEvent::MouseButtonRelease&&pressed_) {
        const auto* mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()==Qt::LeftButton) {
            if(preview(mouse->position())) {
                const bool changed=dragged_;release();
                if(changed&&handlers_.commit)handlers_.commit();else if(handlers_.cancel)handlers_.cancel();
            }
            event->accept();return true;
        }
    }
    return QObject::eventFilter(watched,event);
}
void PositionDragHandle::paint(QPainter& painter) const {
    const auto c=center();if(!c)return;
    painter.save();painter.setPen(QPen(QColor(90,210,240),1.5));painter.setBrush(Qt::NoBrush);
    painter.drawLine(*c-QPointF(22,0),*c-QPointF(10,0));
    painter.drawLine(*c-QPointF(8,0),*c+QPointF(8,0));painter.drawLine(*c-QPointF(0,8),*c+QPointF(0,8));
    for(const auto d:{QPointF(1,0),QPointF(-1,0),QPointF(0,1),QPointF(0,-1)}) {
        const auto tip=*c+d*8,back=tip-d*3,side=QPointF(-d.y(),d.x())*3;
        painter.drawLine(tip,back+side);painter.drawLine(tip,back-side);
    }
    painter.restore();
}
}
