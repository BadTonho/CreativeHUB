#include "ui/workspace/workspace_page_transition.h"

#include <QCoreApplication>
#include <QEvent>
#include <QLayout>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QWidget>

#include <algorithm>
#include <utility>
#include <vector>

namespace ui {
namespace {

class SlideSnapshotOverlay final : public QWidget {
public:
    SlideSnapshotOverlay(
        QWidget* parent,
        QPixmap outgoing,
        QPixmap incoming,
        int direction)
        : QWidget(parent),
          outgoing_(std::move(outgoing)),
          incoming_(std::move(incoming)),
          direction_(direction) {
        setObjectName(QStringLiteral("workspacePageTransitionOverlay"));
        setProperty("slideDirection", direction_);
        setAttribute(Qt::WA_NoSystemBackground);
        setFocusPolicy(Qt::NoFocus);
    }

    void setProgress(qreal progress) {
        progress_ = std::clamp(progress, 0.0, 1.0);
        setProperty("transitionProgress", progress_);
        setProperty("incomingOffset", qRound(
            static_cast<qreal>(direction_) * (1.0 - progress_) * width()));
        setProperty("outgoingOffset", qRound(
            -static_cast<qreal>(direction_) * progress_ * width()));
        update();
    }

protected:
    bool event(QEvent* event) override {
        switch (event->type()) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonRelease:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::Wheel:
        case QEvent::ContextMenu:
        case QEvent::DragEnter:
        case QEvent::DragMove:
        case QEvent::DragLeave:
        case QEvent::Drop:
            event->accept();
            return true;
        default:
            return QWidget::event(event);
        }
    }

    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), palette().color(QPalette::Window));

        const int travel = width();
        const int outgoing_x = qRound(
            -static_cast<qreal>(direction_) * progress_ * travel);
        const int incoming_x = qRound(
            static_cast<qreal>(direction_) * (1.0 - progress_) * travel);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(
            QRect(outgoing_x, 0, width(), height()), outgoing_);
        painter.drawPixmap(
            QRect(incoming_x, 0, width(), height()), incoming_);
    }

private:
    QPixmap outgoing_;
    QPixmap incoming_;
    int direction_ = 1;
    qreal progress_ = 0.0;
};

struct SurfaceSnapshot {
    QPointer<QWidget> surface;
    QPixmap outgoing;
};

}  // namespace

WorkspacePageTransition::WorkspacePageTransition(QObject* parent)
    : QObject(parent) {
    animation_.setStartValue(0.0);
    animation_.setEndValue(1.0);
    animation_.setEasingCurve(QEasingCurve::InOutCubic);
    connect(&animation_, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                progress_ = value.toReal();
                for (const auto& overlay : overlays_) {
                    if (overlay.widget == nullptr || !overlay.set_progress) continue;
                    overlay.set_progress(progress_);
                }
            });
    connect(&animation_, &QVariantAnimation::finished, this, [this]() {
        progress_ = 1.0;
        clearOverlays();
        emit finished();
    });
}

void WorkspacePageTransition::start(
    const QList<QWidget*>& surfaces,
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

    std::vector<SurfaceSnapshot> snapshots;
    snapshots.reserve(static_cast<std::size_t>(surfaces.size()));
    for (auto* surface : surfaces) {
        if (surface == nullptr) continue;
        const auto outgoing = surface->isVisible() && !surface->size().isEmpty()
            ? surface->grab()
            : QPixmap{};
        snapshots.push_back({surface, outgoing});
    }

    apply_page();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    for (const auto& snapshot : snapshots) {
        auto* surface = snapshot.surface.data();
        if (surface == nullptr || !surface->isVisible() ||
            surface->size().isEmpty()) {
            continue;
        }

        const auto incoming = surface->grab();
        if (incoming.isNull()) continue;

        auto outgoing = snapshot.outgoing;
        if (outgoing.isNull()) {
            outgoing = QPixmap(incoming.size());
            outgoing.fill(surface->palette().color(QPalette::Window));
        }

        auto* overlay = new SlideSnapshotOverlay(
            surface, std::move(outgoing), incoming, slide_direction_);
        overlay->setGeometry(surface->rect());
        overlay->setProgress(0.0);
        overlay->show();
        overlay->raise();
        const QPointer<SlideSnapshotOverlay> guarded_overlay(overlay);
        overlays_.push_back({
            overlay,
            [guarded_overlay](qreal progress) {
                if (guarded_overlay != nullptr) {
                    guarded_overlay->setProgress(progress);
                }
            }});
    }

    if (overlays_.isEmpty()) {
        progress_ = 1.0;
        emit finished();
        return;
    }

    progress_ = 0.0;
    animation_.setDuration(std::max(duration_ms, 1));
    animation_.start();
}

void WorkspacePageTransition::cancel() {
    animation_.stop();
    clearOverlays();
    progress_ = 0.0;
}

bool WorkspacePageTransition::isRunning() const noexcept {
    return animation_.state() != QAbstractAnimation::Stopped;
}

int WorkspacePageTransition::slideDirection() const noexcept {
    return slide_direction_;
}

qreal WorkspacePageTransition::progress() const noexcept {
    return progress_;
}

void WorkspacePageTransition::clearOverlays() {
    for (const auto& overlay : overlays_) {
        if (overlay.widget != nullptr) delete overlay.widget.data();
    }
    overlays_.clear();
}

}  // namespace ui
