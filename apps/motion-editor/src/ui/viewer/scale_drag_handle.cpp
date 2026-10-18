#include "scale_drag_handle.h"
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <cmath>
#include <utility>

namespace motion::ui {
ScaleDragHandle::ScaleDragHandle(QWidget* surface,std::function<std::optional<QPointF>()> pivot,
    std::function<QRectF()> canvas,std::function<model::CanvasSize()> size)
    : QObject(surface),surface_(surface),pivot_(std::move(pivot)),canvas_(std::move(canvas)),size_(std::move(size))
{ surface_->installEventFilter(this); }
ScaleDragHandle::~ScaleDragHandle() { release(); }
void ScaleDragHandle::setHandlers(ScaleDragHandlers handlers) { cancel();handlers_=std::move(handlers); }
void ScaleDragHandle::setDirections(std::optional<std::array<QPointF,3>> value) {
    if(value)for(auto& direction:*value) {
        const auto length=std::hypot(direction.x(),direction.y());
        if(!std::isfinite(length)||length==0){value.reset();break;}direction/=length;
    }
    directions_=value;surface_->update();
}
void ScaleDragHandle::setIndicator(std::optional<QPointF> value) {indicator_=value;surface_->update();}
void ScaleDragHandle::setEnabled(bool enabled) {enabled_=enabled;if(!enabled)cancel();surface_->update();}
std::optional<QPointF> ScaleDragHandle::center(std::size_t index) const {
    if(!enabled_||!directions_||index>=3)return {};
    const auto pivot=pivot_();if(!pivot)return {};
    constexpr std::array radii{88.,112.,136.};const auto point=*pivot+(*directions_)[index]*radii[index];
    if(!std::isfinite(point.x())||!std::isfinite(point.y())||!QRectF(surface_->rect()).contains(point))return {};
    return point;
}
std::optional<model::ScaleDragControl> ScaleDragHandle::hit(QPointF point) const {
    for(std::size_t index=0;index<3;++index) {
        const auto c=center(index);if(c){const auto d=point-*c;if(d.x()*d.x()+d.y()*d.y()<=100)return static_cast<model::ScaleDragControl>(index);}
    }
    return {};
}
void ScaleDragHandle::release() {
    pressed_=dragged_=false;indicator_.reset();surface_->update();
    for(auto* ancestor:ancestors_)ancestor->removeEventFilter(this);
    ancestors_.clear();
    if(QWidget::mouseGrabber()==surface_)surface_->releaseMouse();
    surface_->unsetCursor();
}
void ScaleDragHandle::cancel() {if(!pressed_)return;release();if(handlers_.cancel)handlers_.cancel();}
bool ScaleDragHandle::preview(QPointF point) {
    const auto d=point-press_;
    if(!dragged_ && std::abs(d.x())+std::abs(d.y())<QApplication::startDragDistance())return true;
    dragged_=true;
    if(!handlers_.preview || !handlers_.preview(canvasPoint(point)-canvasPoint(press_))){cancel();return false;}
    return pressed_;
}
bool ScaleDragHandle::eventFilter(QObject* watched,QEvent* event) {
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
        const auto control=hit(mouse->position());
        if(mouse->button()==Qt::LeftButton&&control&&handlers_.begin) {
            surface_->setFocus(Qt::MouseFocusReason);
            frozen_canvas_=canvas_();frozen_size_=size_();
            if(frozen_canvas_.isEmpty()||frozen_size_.width<=0||frozen_size_.height<=0)return true;
            const auto point=center(static_cast<std::size_t>(*control)),pivot=pivot_();
            if(!point||!pivot)return true;
            const auto lever=canvasPoint(*point)-canvasPoint(*pivot);
            if(handlers_.begin(*control,lever)) {
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
QPointF ScaleDragHandle::canvasPoint(QPointF point) const {
    return {(point.x()-frozen_canvas_.left())*frozen_size_.width/frozen_canvas_.width(),
        (point.y()-frozen_canvas_.top())*frozen_size_.height/frozen_canvas_.height()};
}
void ScaleDragHandle::paint(QPainter& painter) const {
    const auto pivot=pivot_();if(!pivot)return;
    painter.save();painter.setPen(QPen(QColor(110,225,150),1.5));painter.setBrush(Qt::NoBrush);
    for(std::size_t index=0;index<3;++index) {
        const auto c=center(index);if(!c)continue;
        painter.drawLine(*pivot+(*directions_)[index]*11,*c-(*directions_)[index]*8);
        painter.drawRect(QRectF(c->x()-5,c->y()-5,10,10));
        painter.drawText(QRectF(c->x()+8,c->y()-9,28,18),index==0?QStringLiteral("X"):index==1?QStringLiteral("Y"):QStringLiteral("XY"));
    }
    if(pressed_&&indicator_) {
        const auto point=*pivot+QPointF(indicator_->x()*frozen_canvas_.width()/frozen_size_.width,
            indicator_->y()*frozen_canvas_.height()/frozen_size_.height);
        if(QRectF(surface_->rect()).contains(point)) {
            painter.setBrush(QColor(110,225,150));painter.drawLine(*pivot,point);painter.drawEllipse(point,4,4);
        }
    }
    painter.restore();
}
}
