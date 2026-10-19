#pragma once
#include "rendering/preview_renderer.h"
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <array>
#include <functional>
class QComboBox;
class QPushButton;
class QDoubleSpinBox;
class QSpinBox;
class QListWidget;
class QPainter;
namespace motion::ui {
class MainWindow;
// Motion controller owns drafts/history. Viewer only forwards input and draws
// the overlay; no document or curve is copied during pointer movement.
class PenTool final : public QObject {
  public:
    explicit PenTool(MainWindow &);
    ~PenTool() override;
    QWidget *controls() const {
        return controls_;
    }
    [[nodiscard]] bool active() const noexcept {
        return creating_ || pressed_;
    }
    [[nodiscard]] bool mode() const noexcept;
    void cancel();
    void sync();
    void paint(QPainter &, QRectF);
    [[nodiscard]] std::optional<PreviewRequest> previewRequest();
    void renderFailed();
    void selectCurve(model::PathTarget, std::int64_t frame);

  protected:
    bool eventFilter(QObject *, QEvent *) override;

  private:
    bool available(bool selected = true) const;
    std::optional<std::array<int, 2>> receiverMediaSize() const;
    const model::CompositionLayer *selectedLayer() const;
    model::AffineTransform mappingForTarget(bool new_shape) const;
    bool prepare(bool creating);
    void startCreation();
    void startEdit();
    void finishCreation(bool close);
    void finishDrag();
    void release();
    void changed();
    void refreshFields();
    void refreshControls();
    void edit(std::function<bool(model::CompositionDocument &)>);
    void editPoint(std::function<void(model::PathGeometry &)>);
    void key();
    void removeAnimation();
    bool move(QPointF);
    void press(QPointF);
    void fail(const char *, const std::exception &);
    QPointF canvasPoint(QPointF) const;
    QPointF localPoint(QPointF) const;
    QPointF screenPoint(model::PathPoint) const;
    struct Hit {
        int node = -1;
        int part = 0;
        int segment = -1;
        double t = .5;
    };
    Hit hit(QPointF) const;
    MainWindow &owner_;
    QWidget *controls_ = nullptr;
    QWidget *panel_ = nullptr;
    QComboBox *mode_;
    QComboBox *destination_;
    QPushButton *new_;
    QPushButton *edit_;
    QPushButton *done_;
    QPushButton *close_;
    QPushButton *key_;
    QPushButton *remove_animation_;
    QPushButton *insert_;
    QPushButton *erase_;
    QPushButton *corner_;
    QPushButton *smooth_;
    QSpinBox *point_selection_;
    std::array<QDoubleSpinBox *, 6> fields_{};
    QListWidget *keys_;
    model::PathTarget target_;
    model::PathGeometry geometry_, initial_;
    model::AffineTransform mapping_, inverse_;
    QRectF canvas_;
    const model::CompositionDocument *document_ = nullptr;
    int mask_stage_ = -2;
    model::LayerId layer_ = 0;
    std::int64_t frame_ = 0;
    bool creating_ = false, pressed_ = false, dragged_ = false, releasing_ = false, syncing_ = false,
         submitted_ = false;
    int selected_ = -1, part_ = 0, segment_ = -1;
    double segment_t_ = .5;
    double raster_stroke_ = 0, raster_margin_ = 0;
    QPointF press_;
    std::shared_ptr<const PreviewRequest> snapshot_;
    std::uint64_t session_ = 0;
};
} // namespace motion::ui
