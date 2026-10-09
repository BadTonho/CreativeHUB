#include "ui/workspace/workspace_page_transition.h"

#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QLayout>
#include <QMainWindow>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QPaintEvent>
#include <QPixmap>
#include <QScreen>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <utility>

namespace ui {
namespace {

class WorkspacePageTransitionOverlay final : public QWidget {
public:
    WorkspacePageTransitionOverlay(QWidget* parent, QPixmap image)
        : QWidget(parent), image_(std::move(image)) {
        setObjectName(QStringLiteral("workspacePageTransitionOverlay"));
        setAttribute(Qt::WA_OpaquePaintEvent);
        setFocusPolicy(Qt::NoFocus);
    }

    void setImage(QPixmap image) {
        image_ = std::move(image);
        update();
    }

    void setSlideOffset(int offset_x) {
        offset_x_ = offset_x;
        setProperty("slideOffset", offset_x_);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Window));
        painter.drawPixmap(QPoint(offset_x_, 0), image_);
    }

    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override { event->accept(); }
    void mouseMoveEvent(QMouseEvent* event) override { event->accept(); }
    void wheelEvent(QWheelEvent* event) override { event->accept(); }
    void contextMenuEvent(QContextMenuEvent* event) override { event->accept(); }

private:
    QPixmap image_;
    int offset_x_ = 0;
};

}  // namespace

WorkspacePageTransition::WorkspacePageTransition(QObject* parent)
    : QObject(parent) {
    animation_.setStartValue(0.0);
    animation_.setEndValue(1.0);
    animation_.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&animation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                if (window_ == nullptr) return;

                const auto phase_progress = std::clamp(
                    value.toReal(), 0.0, 1.0);
                if (style_ ==
                    settings::WorkspacePageTransitionStyle::WorkspaceContent) {
                    if (content_overlay_ == nullptr) return;
                    const int width = content_rect_.width();
                    const int from_x = phase_ == Phase::Exiting
                        ? 0
                        : slide_direction_ * width;
                    const int to_x = phase_ == Phase::Exiting
                        ? -slide_direction_ * width
                        : 0;
                    progress_ = phase_ == Phase::Exiting
                        ? phase_progress * 0.5
                        : 0.5 + phase_progress * 0.5;
                    const int offset_x = qRound(
                        static_cast<qreal>(from_x) +
                        static_cast<qreal>(to_x - from_x) * phase_progress);
                    auto* overlay = static_cast<WorkspacePageTransitionOverlay*>(
                        content_overlay_.data());
                    overlay->setSlideOffset(offset_x);
                    return;
                }

                int from_x = 0;
                int to_x = 0;
                if (phase_ == Phase::Exiting) {
                    progress_ = phase_progress * 0.5;
                    from_x = original_position_.x();
                    to_x = exit_position_.x();
                } else if (phase_ == Phase::Entering) {
                    progress_ = 0.5 + phase_progress * 0.5;
                    from_x = entry_position_.x();
                    to_x = original_position_.x();
                } else {
                    return;
                }

                const int x = qRound(
                    static_cast<qreal>(from_x) +
                    static_cast<qreal>(to_x - from_x) * phase_progress);
                window_->move(x, original_position_.y());
            });
    connect(&animation_, &QVariantAnimation::finished,
            this, &WorkspacePageTransition::advancePhase);
}

WorkspacePageTransition::~WorkspacePageTransition() {
    cancel();
}

void WorkspacePageTransition::start(
    QWidget* window,
    WorkspacePageId from_page,
    WorkspacePageId to_page,
    int duration_ms,
    std::function<void()> apply_page,
    settings::WorkspacePageTransitionStyle style) {
    cancel();
    if (!apply_page) return;

    if (from_page == to_page) {
        apply_page();
        return;
    }

    slide_direction_ = static_cast<int>(to_page) > static_cast<int>(from_page)
        ? 1
        : -1;
    style_ = style;

    if (window == nullptr || !window->isVisible() || window->isMinimized()) {
        apply_page();
        progress_ = 1.0;
        emit finished();
        return;
    }

    window_ = window;
    if (style_ == settings::WorkspacePageTransitionStyle::WorkspaceContent) {
        startContentTransition(
            window, duration_ms, std::move(apply_page));
        return;
    }

    was_maximized_ = window->isMaximized();
    if (was_maximized_) {
        window->showNormal();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    original_geometry_ = window->geometry();
    original_position_ = window->pos();
    if (original_geometry_.isEmpty()) {
        restoreWindow();
        apply_page();
        progress_ = 1.0;
        clearTransitionState();
        emit finished();
        return;
    }

    auto* screen = QGuiApplication::screenAt(
        window->frameGeometry().center());
    if (screen == nullptr) screen = window->screen();
    if (screen == nullptr) screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) {
        restoreWindow();
        clearTransitionState();
        apply_page();
        progress_ = 1.0;
        emit finished();
        return;
    }

    const auto screen_geometry = screen->geometry();
    const int framed_width = std::max(
        1, window->frameGeometry().width());
    const int offscreen_left = screen_geometry.left() - framed_width - 1;
    const int offscreen_right = screen_geometry.right() + 2;
    exit_position_ = QPoint(
        slide_direction_ > 0 ? offscreen_left : offscreen_right,
        original_position_.y());
    entry_position_ = QPoint(
        slide_direction_ > 0 ? offscreen_right : offscreen_left,
        original_position_.y());

    const int total_duration = std::max(duration_ms, 2);
    exit_duration_ms_ = std::max(total_duration / 2, 1);
    entry_duration_ms_ = std::max(total_duration - exit_duration_ms_, 1);
    progress_ = 0.0;
    page_was_applied_ = false;
    apply_page_ = std::move(apply_page);
    startPhase(Phase::Exiting, exit_duration_ms_);
}

void WorkspacePageTransition::cancel() {
    animation_.stop();
    if (phase_ != Phase::None &&
        style_ == settings::WorkspacePageTransitionStyle::EntireApplicationWindow) {
        restoreWindow();
    }
    clearTransitionState();
    progress_ = 0.0;
}

bool WorkspacePageTransition::isRunning() const noexcept {
    return phase_ != Phase::None &&
        animation_.state() != QAbstractAnimation::Stopped;
}

int WorkspacePageTransition::slideDirection() const noexcept {
    return slide_direction_;
}

qreal WorkspacePageTransition::progress() const noexcept {
    return progress_;
}

bool WorkspacePageTransition::pageWasApplied() const noexcept {
    return page_was_applied_;
}

void WorkspacePageTransition::startPhase(
    Phase phase,
    int duration_ms) {
    phase_ = phase;
    animation_.setStartValue(0.0);
    animation_.setEndValue(1.0);
    animation_.setDuration(std::max(duration_ms, 1));
    if (style_ == settings::WorkspacePageTransitionStyle::WorkspaceContent) {
        if (content_overlay_ != nullptr) {
            auto* overlay = static_cast<WorkspacePageTransitionOverlay*>(
                content_overlay_.data());
            overlay->setSlideOffset(
                phase == Phase::Entering
                    ? slide_direction_ * content_rect_.width()
                    : 0);
        }
    } else if (window_ != nullptr) {
        window_->move(
            phase == Phase::Exiting
                ? original_position_
                : entry_position_);
    }
    animation_.start();
}

void WorkspacePageTransition::advancePhase() {
    if (window_ == nullptr) {
        clearTransitionState();
        progress_ = 1.0;
        emit finished();
        return;
    }

    if (style_ == settings::WorkspacePageTransitionStyle::WorkspaceContent) {
        advanceContentTransition();
        return;
    }

    if (phase_ == Phase::Exiting) {
        window_->move(exit_position_);
        progress_ = 0.5;
        if (apply_page_) apply_page_();
        page_was_applied_ = true;
        window_->move(entry_position_);
        startPhase(Phase::Entering, entry_duration_ms_);
        return;
    }

    if (phase_ == Phase::Entering) {
        restoreWindow();
        clearTransitionState();
        progress_ = 1.0;
        emit finished();
    }
}

QRect WorkspacePageTransition::contentRect(QWidget* window) const {
    if (window == nullptr) return {};
    auto* main_window = qobject_cast<QMainWindow*>(window);
    if (main_window == nullptr || main_window->menuBar() == nullptr ||
        main_window->menuBar()->isHidden() ||
        main_window->menuBar()->isNativeMenuBar()) {
        return window->rect();
    }
    const auto menu_geometry = main_window->menuBar()->geometry();
    const int top = std::clamp(
        menu_geometry.bottom() + 1, 0, window->height());
    return QRect(0, top, window->width(), window->height() - top);
}

void WorkspacePageTransition::startContentTransition(
    QWidget* window,
    int duration_ms,
    std::function<void()> apply_page) {
    content_rect_ = contentRect(window);
    if (content_rect_.isEmpty()) {
        apply_page();
        progress_ = 1.0;
        clearTransitionState();
        emit finished();
        return;
    }

    content_image_ = window->grab(content_rect_);
    if (content_image_.isNull()) {
        apply_page();
        progress_ = 1.0;
        clearTransitionState();
        emit finished();
        return;
    }

    auto* overlay = new WorkspacePageTransitionOverlay(
        window, std::move(content_image_));
    overlay->setGeometry(content_rect_);
    overlay->show();
    overlay->raise();
    content_overlay_ = overlay;
    apply_page_ = std::move(apply_page);
    const int total_duration = std::max(duration_ms, 2);
    exit_duration_ms_ = std::max(total_duration / 2, 1);
    entry_duration_ms_ = std::max(total_duration - exit_duration_ms_, 1);
    progress_ = 0.0;
    page_was_applied_ = false;
    startPhase(Phase::Exiting, exit_duration_ms_);
}

void WorkspacePageTransition::advanceContentTransition() {
    if (window_ == nullptr || content_overlay_ == nullptr) {
        clearTransitionState();
        progress_ = 1.0;
        emit finished();
        return;
    }

    if (phase_ == Phase::Exiting) {
        auto* overlay = static_cast<WorkspacePageTransitionOverlay*>(
            content_overlay_.data());
        overlay->setSlideOffset(-slide_direction_ * content_rect_.width());
        content_overlay_->hide();

        if (apply_page_) apply_page_();
        page_was_applied_ = true;
        if (window_->layout() != nullptr) window_->layout()->activate();
        content_image_ = window_->grab(content_rect_);
        if (content_image_.isNull()) {
            clearTransitionState();
            progress_ = 1.0;
            emit finished();
            return;
        }

        overlay->setImage(std::move(content_image_));
        overlay->setSlideOffset(slide_direction_ * content_rect_.width());
        content_overlay_->show();
        content_overlay_->raise();
        startPhase(Phase::Entering, entry_duration_ms_);
        return;
    }

    if (phase_ == Phase::Entering) {
        clearTransitionState();
        progress_ = 1.0;
        emit finished();
    }
}

void WorkspacePageTransition::restoreWindow() {
    if (window_ == nullptr) return;
    if (!original_geometry_.isEmpty()) {
        window_->setGeometry(original_geometry_);
    } else {
        window_->move(original_position_);
    }
    if (was_maximized_) window_->showMaximized();
}

void WorkspacePageTransition::clearTransitionState() {
    if (content_overlay_ != nullptr) {
        auto* overlay = content_overlay_.data();
        content_overlay_.clear();
        delete overlay;
    }
    window_.clear();
    apply_page_ = {};
    original_geometry_ = {};
    original_position_ = {};
    exit_position_ = {};
    entry_position_ = {};
    content_rect_ = {};
    content_image_ = {};
    phase_ = Phase::None;
    exit_duration_ms_ = 0;
    entry_duration_ms_ = 0;
    was_maximized_ = false;
    page_was_applied_ = false;
    style_ = settings::WorkspacePageTransitionStyle::EntireApplicationWindow;
}

}  // namespace ui
