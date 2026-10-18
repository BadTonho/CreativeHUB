#pragma once

#include "model/composition_document.h"
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QTimer>
#include <functional>
#include <optional>
#include <unordered_set>

class QWidget;
class QPainter;
class QScrollArea;

namespace motion::ui {

struct ParentPickWhipHandlers {
    // Called before mouse capture. nullopt refuses a busy/unavailable operation;
    // an empty vector allows a gesture with no eligible destination.
    std::function<std::optional<std::vector<model::LayerId>>(model::LayerId)> begin;
    std::function<void(model::LayerId, model::LayerId)> commit;
    std::function<void()> cancel;
};

// A media-free internal gesture on the timeline canvas. Hit testing is supplied
// by its owner, so expanded rows and front-to-back display never become IDs.
class ParentPickWhip final : public QObject {
public:
    struct Hit { model::LayerId id; QString name; QRectF row; };
    using HitTest = std::function<std::optional<Hit>(QPointF)>;
    ParentPickWhip(QWidget* surface, HitTest hit, std::function<QString(QPointF)> tooltip);
    ~ParentPickWhip() override;
    void setHandlers(ParentPickWhipHandlers handlers);
    void setScrollArea(QScrollArea* area);
    void cancel();
    [[nodiscard]] bool active() const noexcept { return source_ != 0; }
    void paint(QPainter& painter) const;
    [[nodiscard]] static QRectF iconRect(QRectF row) noexcept;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    [[nodiscard]] QRectF visibleRect() const;
    [[nodiscard]] bool pointerInside() const;
    void updatePointer(QPointF global);
    void updateTarget();
    [[nodiscard]] int scrollDirection() const;
    void scrollTick();
    void endCapture();

    QWidget* surface_;
    QScrollArea* scroll_area_ = nullptr; // owned by TimelineNavigator
    std::vector<QWidget*> layout_ancestors_; // borrowed only while capturing
    HitTest hit_;
    std::function<QString(QPointF)> tooltip_;
    ParentPickWhipHandlers handlers_;
    QTimer timer_;
    model::LayerId source_ = 0;
    std::unordered_set<model::LayerId> eligible_;
    std::optional<Hit> target_;
    QPointF origin_, press_global_, pointer_global_;
    bool dragged_ = false;
};
} // namespace motion::ui
