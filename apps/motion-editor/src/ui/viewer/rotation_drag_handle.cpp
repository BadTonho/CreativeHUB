#include "rotation_drag_handle.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <cmath>
#include <utility>

namespace motion::ui {
RotationDragHandle::RotationDragHandle(QWidget* surface,std::function<std::optional<QPointF>()> pivot,
    std::function<QRectF()> canvas,std::function<model::CanvasSize()> size)
    : QObject(surface),surface_(surface),pivot_(std::move(pivot)),canvas_(std::move(canvas)),size_(std::move(size))
{ surface_->installEventFilter(this); }
RotationDragHandle::~RotationDragHandle() { release(); }
void RotationDragHandle::setHandlers(RotationDragHandlers handlers) { cancel();handlers_=std::move(handlers); }
void RotationDragHandle::setDirection(std::optional<QPointF> value) {
    if(value) {const auto length=std::hypot(value->x(),value->y());
        if(!std::isfinite(length)||length==0)value.reset();else *value/=length;}
    direction_=value;surface_->update();
}
void RotationDragHandle::setEnabled(bool enabled) {enabled_=enabled;if(!enabled)cancel();surface_->update();}
std::optional<QPointF> RotationDragHandle::center() const {
    if (!enabled_) return {};
    const auto pivot=pivot_();if(!pivot)return {};
    if(!direction_)return {};
    const auto point=*pivot+*direction_*56;
    if (!std::isfinite(point.x())||!std::isfinite(point.y())||!QRectF(surface_->rect()).contains(point))return {};
    return point;
}
bool RotationDragHandle::hit(QPointF p) const {
    const auto c=center();if(!c)return false;const auto d=p-*c;return d.x()*d.x()+d.y()*d.y()<=100;
}
void RotationDragHandle::release() {
    pressed_=dragged_=false;
    for(auto* ancestor:ancestors_)ancestor->removeEventFilter(this);
    ancestors_.clear();
    if(QWidget::mouseGrabber()==surface_)surface_->releaseMouse();
    surface_->unsetCursor();
}
void RotationDragHandle::cancel() {if(!pressed_)return;release();if(handlers_.cancel)handlers_.cancel();}
bool RotationDragHandle::preview(QPointF point) {
    const auto d=point-press_;
    if(!dragged_ && std::abs(d.x())+std::abs(d.y())<QApplication::startDragDistance())return true;
    dragged_=true;
    if(!handlers_.preview || !handlers_.preview(canvasPoint(point))){cancel();return false;}
    return pressed_;
}
bool RotationDragHandle::eventFilter(QObject* watched,QEvent* event) {
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
            frozen_canvas_=canvas_();frozen_size_=size_();
            if(frozen_canvas_.isEmpty()||frozen_size_.width<=0||frozen_size_.height<=0)return true;
            if(handlers_.begin(canvasPoint(mouse->position()),frozen_size_.width/frozen_canvas_.width())) {
                press_=mouse->position();pressed_=true;dragged_=false;
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
QPointF RotationDragHandle::canvasPoint(QPointF point) const {
    return {(point.x()-frozen_canvas_.left())*frozen_size_.width/frozen_canvas_.width(),
        (point.y()-frozen_canvas_.top())*frozen_size_.height/frozen_canvas_.height()};
}
void RotationDragHandle::paint(QPainter& painter) const {
    const auto c=center();if(!c)return;
    painter.save();painter.setPen(QPen(QColor(255,195,90),1.5));painter.setBrush(Qt::NoBrush);
    const auto pivot=pivot_();
    painter.drawLine(*pivot+*direction_*11,*c-*direction_*11);
    painter.drawArc(QRectF(c->x()-7,c->y()-7,14,14),35*16,285*16);
    const auto tip=*c+QPointF(5.7,-4.0);
    painter.drawLine(tip,tip+QPointF(-5,-1));painter.drawLine(tip,tip+QPointF(-1,5));
    painter.restore();
}
}
