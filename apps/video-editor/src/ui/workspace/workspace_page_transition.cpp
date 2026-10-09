#include "ui/workspace/workspace_page_transition.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QEventLoop>
#include <QScreen>
#include <QWidget>

#include <algorithm>

namespace ui {

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

void WorkspacePageTransition::start(
    QWidget* window,
    WorkspacePageId from_page,
    WorkspacePageId to_page,
    int duration_ms,
    std::function<void()> apply_page) {
    cancel();
    if (!apply_page) return;

    if (from_page == to_page) {
        apply_page();
        return;
    }

    slide_direction_ = static_cast<int>(to_page) > static_cast<int>(from_page)
        ? 1
        : -1;

    if (window == nullptr || !window->isVisible() || window->isMinimized()) {
        apply_page();
        progress_ = 1.0;
        emit finished();
        return;
    }

    window_ = window;
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
    if (phase_ != Phase::None) restoreWindow();
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
    if (window_ != nullptr) {
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
    window_.clear();
    apply_page_ = {};
    original_geometry_ = {};
    original_position_ = {};
    exit_position_ = {};
    entry_position_ = {};
    phase_ = Phase::None;
    exit_duration_ms_ = 0;
    entry_duration_ms_ = 0;
    was_maximized_ = false;
    page_was_applied_ = false;
}

}  // namespace ui
