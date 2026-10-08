#pragma once

#include "ui/workspace/workspace_page_id.h"

#include <QObject>
#include <QList>
#include <QPointer>
#include <QVariantAnimation>
#include <QVector>

#include <functional>

class QWidget;

namespace ui {

// Animates snapshots of the visible workspace surfaces while the destination
// page is activated underneath them.
class WorkspacePageTransition final : public QObject {
    Q_OBJECT

public:
    explicit WorkspacePageTransition(QObject* parent = nullptr);

    void start(
        const QList<QWidget*>& surfaces,
        WorkspacePageId from_page,
        WorkspacePageId to_page,
        int duration_ms,
        std::function<void()> apply_page);
    void cancel();

    [[nodiscard]] bool isRunning() const noexcept;
    [[nodiscard]] int slideDirection() const noexcept;
    [[nodiscard]] qreal progress() const noexcept;

signals:
    void finished();

private:
    struct OverlayHandle {
        QPointer<QWidget> widget;
        std::function<void(qreal)> set_progress;
    };

    void clearOverlays();

    QVector<OverlayHandle> overlays_;
    QVariantAnimation animation_;
    int slide_direction_ = 1;
    qreal progress_ = 0.0;
};

}  // namespace ui
