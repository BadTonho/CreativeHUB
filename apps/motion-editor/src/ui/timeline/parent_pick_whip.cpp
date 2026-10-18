#include "parent_pick_whip.h"
#include "ui/translation_helpers.h"

#include <QApplication>
#include <QHelpEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScrollArea>
#include <QScrollBar>
#include <QToolTip>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <utility>

namespace motion::ui {
ParentPickWhip::ParentPickWhip(QWidget* surface, HitTest hit, std::function<QString(QPointF)> tooltip)
    : QObject(surface), surface_(surface), hit_(std::move(hit)), tooltip_(std::move(tooltip))
{
    surface_->installEventFilter(this);
    surface_->setMouseTracking(true);
    timer_.setParent(this);
    timer_.setObjectName(QStringLiteral("motion-parent-pick-whip-scroll-timer"));
    timer_.setInterval(30);
    connect(&timer_, &QTimer::timeout, this, [this] { scrollTick(); });
}

ParentPickWhip::~ParentPickWhip() { endCapture(); }

void ParentPickWhip::setHandlers(ParentPickWhipHandlers handlers)
{
    cancel();
    handlers_ = std::move(handlers);
}

void ParentPickWhip::setScrollArea(QScrollArea* area)
{
    cancel();
    if (scroll_area_) {
        scroll_area_->viewport()->removeEventFilter(this);
        disconnect(scroll_area_->verticalScrollBar(), nullptr, this, nullptr);
    }
    scroll_area_ = area;
    if (area) {
        area->viewport()->installEventFilter(this);
        connect(area->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
            if (active()) updateTarget();
        });
    }
}

QRectF ParentPickWhip::iconRect(QRectF row) noexcept { return {50, row.top()+7, 20, 20}; }

QRectF ParentPickWhip::visibleRect() const
{
    if (!scroll_area_) return surface_->rect();
    return QRectF(surface_->mapFrom(scroll_area_->viewport(), QPointF(0,0)), scroll_area_->viewport()->size());
}

bool ParentPickWhip::pointerInside() const
{
    return visibleRect().contains(surface_->mapFromGlobal(pointer_global_));
}

void ParentPickWhip::updatePointer(QPointF global)
{
    pointer_global_ = global;
    const auto delta = global - press_global_;
    if (std::abs(delta.x()) + std::abs(delta.y()) >= QApplication::startDragDistance()) dragged_ = true;
    updateTarget();
}

void ParentPickWhip::updateTarget()
{
    target_ = dragged_ && pointerInside() ? hit_(surface_->mapFromGlobal(pointer_global_)) : std::nullopt;
    if (dragged_) surface_->setCursor(target_ && eligible_.contains(target_->id) ? Qt::CrossCursor : Qt::ForbiddenCursor);
    if (scrollDirection()) { if (!timer_.isActive()) timer_.start(); }
    else timer_.stop();
    surface_->update();
}

int ParentPickWhip::scrollDirection() const
{
    if (!active() || !dragged_ || !scroll_area_ || !pointerInside()) return 0;
    const auto p = scroll_area_->viewport()->mapFromGlobal(pointer_global_);
    const auto* bar = scroll_area_->verticalScrollBar();
    if (p.y() < 24 && bar->value() > bar->minimum()) return -1;
    if (p.y() >= scroll_area_->viewport()->height()-24 && bar->value() < bar->maximum()) return 1;
    return 0;
}

void ParentPickWhip::scrollTick()
{
    const auto direction = scrollDirection();
    if (!direction) { timer_.stop(); return; }
    auto* bar = scroll_area_->verticalScrollBar();
    bar->setValue(bar->value()+direction*8);
    updateTarget();
}

void ParentPickWhip::endCapture()
{
    // Clear before release: UngrabMouse and controller row refresh can reenter.
    source_ = 0;
    dragged_ = false;
    timer_.stop();
    target_.reset();
    eligible_.clear();
    for (auto* ancestor : layout_ancestors_) ancestor->removeEventFilter(this);
    layout_ancestors_.clear();
    if (QWidget::mouseGrabber() == surface_) surface_->releaseMouse();
    surface_->unsetCursor();
    surface_->update();
}

void ParentPickWhip::cancel()
{
    if (!active()) return;
    endCapture();
    if (handlers_.cancel) handlers_.cancel();
}

bool ParentPickWhip::eventFilter(QObject* watched, QEvent* event)
{
    if (active()) {
        const auto type = event->type();
        const bool layout_ancestor = std::find(layout_ancestors_.begin(), layout_ancestors_.end(), watched) != layout_ancestors_.end();
        if (type == QEvent::Hide || type == QEvent::WindowDeactivate || type == QEvent::Close ||
            (watched == surface_ && type == QEvent::UngrabMouse) ||
            (layout_ancestor && (type == QEvent::Move || type == QEvent::Resize || type == QEvent::ParentChange)) ||
            ((watched == surface_ || (scroll_area_ && watched == scroll_area_->viewport())) && type == QEvent::Resize)) {
            cancel();
        }
        if (type == QEvent::ShortcutOverride) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape || key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)) {
                event->accept(); return true;
            }
        }
        if (type == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape || key->matches(QKeySequence::Undo)) {
                cancel(); event->accept(); return true;
            }
            if (key->matches(QKeySequence::Redo)) { event->accept(); return true; }
            cancel(); // Other commands continue through the existing key handler.
        }
        if (type == QEvent::Wheel) {
            auto* wheel = static_cast<QWheelEvent*>(event);
            if (wheel->modifiers().testFlag(Qt::ControlModifier)) cancel();
            else if (scroll_area_) {
                pointer_global_ = wheel->globalPosition();
                const auto pixels = wheel->pixelDelta().y();
                const int step = pixels ? pixels : qRound(wheel->angleDelta().y()/120.0 * QApplication::wheelScrollLines()*34);
                auto* bar = scroll_area_->verticalScrollBar();
                bar->setValue(bar->value()-step);
                updateTarget(); event->accept(); return true;
            }
        }
    }
    if (watched != surface_) return QObject::eventFilter(watched,event);
    if (event->type() == QEvent::ToolTip) {
        const auto* help = static_cast<QHelpEvent*>(event);
        const auto text = tooltip_(help->pos());
        if (!text.isEmpty()) { QToolTip::showText(help->globalPos(),text,surface_); return true; }
        QToolTip::hideText();
    }
    if (event->type() == QEvent::MouseButtonPress && !active()) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        const auto row = hit_(mouse->position());
        if (mouse->button() == Qt::LeftButton && row && iconRect(row->row).contains(mouse->position()) && handlers_.begin) {
            surface_->setFocus(Qt::MouseFocusReason);
            if (auto candidates = handlers_.begin(row->id)) {
                source_ = row->id;
                origin_ = iconRect(row->row).center();
                press_global_ = pointer_global_ = mouse->globalPosition();
                dragged_ = false;
                eligible_.insert(candidates->begin(),candidates->end());
                // Watch layout movement above the scroll area, excluding the
                // scrolling content: its own vertical movement is intentional.
                for (QWidget* ancestor = scroll_area_ ? scroll_area_ : surface_->window(); ancestor;
                     ancestor = ancestor->parentWidget()) {
                    layout_ancestors_.push_back(ancestor);
                    ancestor->installEventFilter(this);
                }
                surface_->grabMouse();
                surface_->setCursor(Qt::ClosedHandCursor);
            }
            // A refused pick-whip never falls through into layer reordering.
            mouse->accept(); return true;
        }
    }
    if (event->type() == QEvent::MouseMove) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (active()) {
            if (!(mouse->buttons() & Qt::LeftButton)) cancel();
            else updatePointer(mouse->globalPosition());
            mouse->accept(); return true;
        }
        const auto row = hit_(mouse->position());
        if (!mouse->buttons() && row && iconRect(row->row).contains(mouse->position()) && handlers_.begin) {
            surface_->setCursor(Qt::OpenHandCursor); mouse->accept(); return true;
        }
    }
    if (event->type() == QEvent::MouseButtonRelease && active()) {
        auto* mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton) {
            updatePointer(mouse->globalPosition());
            const auto source = source_;
            const auto parent = target_ && eligible_.contains(target_->id) ? target_->id : 0;
            endCapture();
            if (parent && handlers_.commit) handlers_.commit(source,parent);
            else if (handlers_.cancel) handlers_.cancel();
            mouse->accept(); return true;
        }
    }
    return QObject::eventFilter(watched,event);
}

void ParentPickWhip::paint(QPainter& painter) const
{
    if (!active() || !dragged_) return;
    painter.save();
    const auto viewport = visibleRect();
    painter.setClipRect(viewport);
    const bool valid = target_ && eligible_.contains(target_->id);
    const QColor color = valid ? QColor(255,183,54) : QColor(235,110,110);
    if (target_) painter.fillRect(target_->row, QColor(color.red(),color.green(),color.blue(),45));
    const auto end = surface_->mapFromGlobal(pointer_global_);
    QPainterPath line(origin_);
    const auto bend = std::max(40.0,std::abs(end.x()-origin_.x())*.5);
    line.cubicTo(origin_+QPointF(bend,0),end-QPointF(bend,0),end);
    painter.setPen(QPen(color,2));painter.setBrush(Qt::NoBrush);painter.drawPath(line);
    if (target_) {
        const auto label_format = valid ? MotionUiTranslation::tr("Parent: %1") : MotionUiTranslation::tr("Invalid parent: %1");
        const auto identity = [this](const QString& name) {
            return MotionUiTranslation::tr("%1 [#%2]").arg(name,QString::number(target_->id));
        };
        // Reserve the ID and status before eliding only the user-facing name.
        // Long duplicate names must still identify different destinations.
        const auto name_width = std::max(0,qRound(viewport.width())-16-
            painter.fontMetrics().horizontalAdvance(label_format.arg(identity({}))));
        const auto name = painter.fontMetrics().elidedText(target_->name,Qt::ElideRight,name_width);
        const auto text = label_format.arg(identity(name));
        const auto width = painter.fontMetrics().horizontalAdvance(text)+12;
        const auto height = painter.fontMetrics().height()+8;
        const auto x = std::clamp(end.x()+12,viewport.left(),std::max(viewport.left(),viewport.right()-width));
        const auto y = std::clamp(end.y()+12,viewport.top(),std::max(viewport.top(),viewport.bottom()-height));
        const QRectF label(x,y,width,height);
        painter.fillRect(label,QColor(30,33,39));painter.setPen(color);
        painter.drawText(label.adjusted(6,0,-6,0),Qt::AlignVCenter,text);
    }
    painter.restore();
}
} // namespace motion::ui
