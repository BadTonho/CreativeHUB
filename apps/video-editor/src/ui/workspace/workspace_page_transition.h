#pragma once

#include "ui/workspace/workspace_page_id.h"

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QVariantAnimation>

#include <functional>

class QWidget;

namespace ui {

// Moves the application window off-screen, switches pages, then returns it
// from the opposite side while preserving its original geometry and state.
class WorkspacePageTransition final : public QObject {
    Q_OBJECT

public:
    explicit WorkspacePageTransition(QObject* parent = nullptr);

    void start(
        QWidget* window,
        WorkspacePageId from_page,
        WorkspacePageId to_page,
        int duration_ms,
        std::function<void()> apply_page);
    void cancel();

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] int slideDirection() const noexcept;
    [[nodiscard]] qreal progress() const noexcept;
    [[nodiscard]] bool pageWasApplied() const noexcept;

signals:
    void finished();

private:
    enum class Phase {
        None,
        Exiting,
        Entering,
    };

    void startPhase(Phase phase, int duration_ms);
    void advancePhase();
    void restoreWindow();
    void clearTransitionState();

    QPointer<QWidget> window_;
    QVariantAnimation animation_;
    std::function<void()> apply_page_;
    QRect original_geometry_;
    QPoint original_position_;
    QPoint exit_position_;
    QPoint entry_position_;
    Phase phase_ = Phase::None;
    int slide_direction_ = 1;
    int exit_duration_ms_ = 0;
    int entry_duration_ms_ = 0;
    bool was_maximized_ = false;
    bool page_was_applied_ = false;
    qreal progress_ = 0.0;
};

}  // namespace ui
