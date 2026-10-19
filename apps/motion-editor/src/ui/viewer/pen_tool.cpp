#include "pen_tool.h"
#include "composition_viewer.h"
#include "ui/main_window/main_window.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/inspector/mask_inspector_widget.h"
#include "ui/timeline/timeline_navigator.h"
#include "ui/media_pool/media_pool_widget.h"
#include "ui/translation_helpers.h"
#include "rendering/path_rasterizer.h"
#include <creative_suite/diagnostics/logger.h>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <utility>
namespace motion::ui {
namespace m = model;
namespace c = creative_suite::composition;
namespace {
double length(QPointF p) {
    return std::hypot(p.x(), p.y());
}
QPointF add(m::PathPoint a, m::PathPoint b) {
    return {a.x + b.x, a.y + b.y};
}
} // namespace
PenTool::PenTool(MainWindow &owner) : QObject(&owner), owner_(owner) {
    controls_ = new QWidget;
    controls_->setObjectName("motion-pen-controls");
    auto *row = new QGridLayout(controls_);
    row->setContentsMargins(4, 2, 4, 2);
    mode_ = new QComboBox(controls_);
    mode_->setObjectName("motion-viewer-tool");
    mode_->addItems({MotionUiTranslation::tr("Transform"), MotionUiTranslation::tr("Pen")});
    destination_ = new QComboBox(controls_);
    destination_->setObjectName("motion-pen-destination");
    destination_->addItems({MotionUiTranslation::tr("New Shape"), MotionUiTranslation::tr("Mask")});
    row->addWidget(mode_, 0, 0);
    row->addWidget(destination_, 0, 1);
    const auto button = [&](const char *text, const char *name) {
        auto *b = new QPushButton(MotionUiTranslation::tr(text));
        b->setObjectName(name);
        return b;
    };
    new_ = button(QT_TRANSLATE_NOOP("MotionUiTranslation", "New Path"), "motion-pen-new");
    edit_ = button(QT_TRANSLATE_NOOP("MotionUiTranslation", "Edit Selected Path"), "motion-pen-edit");
    done_ = button(QT_TRANSLATE_NOOP("MotionUiTranslation", "Finish"), "motion-pen-finish");
    close_ = button(QT_TRANSLATE_NOOP("MotionUiTranslation", "Close Path"), "motion-pen-close");
    auto *cancel_button = button(QT_TRANSLATE_NOOP("MotionUiTranslation", "Cancel"), "motion-pen-cancel");
    row->addWidget(new_, 0, 2);
    row->addWidget(edit_, 1, 0, 1, 2);
    row->addWidget(done_, 1, 2);
    row->addWidget(close_, 1, 3);
    row->addWidget(cancel_button, 0, 3);
    panel_ = new QWidget;
    panel_->setObjectName("motion-path-inspector");
    auto *layout = new QVBoxLayout(panel_);
    auto *help =
        new QLabel(MotionUiTranslation::tr("Click for corners; drag for curves. Enter finishes, Escape "
                                           "cancels. Animated paths keep their point count."));
    help->setWordWrap(true);
    layout->addWidget(help);
    auto *form = new QFormLayout;
    point_selection_ = new QSpinBox(panel_);
    point_selection_->setObjectName("motion-path-point");
    form->addRow(MotionUiTranslation::tr("Point"), point_selection_);
    connect(point_selection_, &QSpinBox::valueChanged, this, [this](int value) {
        if (syncing_ || active())
            return;
        selected_ = value - 1;
        segment_ = -1;
        refreshFields();
        refreshControls();
        owner_.viewer_->updatePathOverlay();
    });
    const std::array<const char *, 6> names{QT_TRANSLATE_NOOP("MotionUiTranslation", "Point X"),
                                            QT_TRANSLATE_NOOP("MotionUiTranslation", "Point Y"),
                                            QT_TRANSLATE_NOOP("MotionUiTranslation", "Incoming X"),
                                            QT_TRANSLATE_NOOP("MotionUiTranslation", "Incoming Y"),
                                            QT_TRANSLATE_NOOP("MotionUiTranslation", "Outgoing X"),
                                            QT_TRANSLATE_NOOP("MotionUiTranslation", "Outgoing Y")};
    for (std::size_t i = 0; i < 6; ++i) {
        auto *field = new QDoubleSpinBox(panel_);
        fields_[i] = field;
        field->setObjectName(QString("motion-path-coordinate-%1").arg(i));
        field->setDecimals(6);
        field->setRange(-1e9, 1e9);
        field->setKeyboardTracking(false);
        form->addRow(MotionUiTranslation::tr(names[i]), field);
        connect(field, &QDoubleSpinBox::valueChanged, this, [this, i](double value) {
            if (syncing_ || pressed_ || creating_)
                return;
            editPoint([&](auto &path) {
                if (selected_ < 0 || std::size_t(selected_) >= path.nodes.size())
                    return;
                auto &n = path.nodes[selected_];
                std::array<m::PathPoint *, 3> points{&n.point, &n.incoming, &n.outgoing};
                auto &point = *points[i / 2];
                (i % 2 ? point.y : point.x) = value / (target_.mask ? 100. : 1.);
            });
        });
    }
    layout->addLayout(form);
    corner_ = button(QT_TRANSLATE_NOOP("MotionUiTranslation", "Corner"), "motion-path-corner");
    smooth_ = button(QT_TRANSLATE_NOOP("MotionUiTranslation", "Smooth"), "motion-path-smooth");
    insert_ = button(QT_TRANSLATE_NOOP("MotionUiTranslation", "Insert Point"), "motion-path-insert");
    erase_ = button(QT_TRANSLATE_NOOP("MotionUiTranslation", "Remove Point"), "motion-path-remove");
    key_ = button(QT_TRANSLATE_NOOP("MotionUiTranslation", "Toggle Path Keyframe"), "motion-path-key");
    remove_animation_ =
        button(QT_TRANSLATE_NOOP("MotionUiTranslation", "Remove Animation Keeping Current Frame"),
               "motion-path-clear-animation");
    for (auto *b : {corner_, smooth_, insert_, erase_, key_, remove_animation_})
        layout->addWidget(b);
    keys_ = new QListWidget(panel_);
    keys_->setObjectName("motion-path-keyframes");
    keys_->setMaximumHeight(180);
    layout->addWidget(keys_);
    layout->addStretch();
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(panel_);
    owner_.inspector_->addTab(scroll, MotionUiTranslation::tr("Path"));
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] {
        cancel();
        owner_.syncTransformInspector();
        sync();
    });
    connect(destination_, &QComboBox::currentIndexChanged, this, [this] {
        cancel();
        target_ = {};
        sync();
    });
    connect(new_, &QPushButton::clicked, this, [this] { startCreation(); });
    connect(edit_, &QPushButton::clicked, this, [this] { startEdit(); });
    connect(done_, &QPushButton::clicked, this, [this] { finishCreation(target_.mask); });
    connect(close_, &QPushButton::clicked, this, [this] {
        if (creating_)
            finishCreation(true);
        else
            editPoint([](auto &path) { path.closed = !path.closed; });
    });
    connect(cancel_button, &QPushButton::clicked, this, [this] { this->cancel(); });
    connect(corner_, &QPushButton::clicked, this, [this] {
        editPoint([&](auto &p) {
            if (selected_ >= 0)
                p.nodes[selected_].incoming = p.nodes[selected_].outgoing = {};
        });
    });
    connect(smooth_, &QPushButton::clicked, this, [this] {
        editPoint([&](auto &p) {
            if (selected_ < 0)
                return;
            const auto i = std::size_t(selected_), last = p.nodes.size() - 1;
            const auto &a = p.nodes[i ? i - 1 : (p.closed ? last : 0)].point;
            const auto &b = p.nodes[i < last ? i + 1 : (p.closed ? 0 : last)].point;
            auto &n = p.nodes[i];
            n.outgoing = {(b.x - a.x) / 6, (b.y - a.y) / 6};
            n.incoming = {-n.outgoing.x, -n.outgoing.y};
        });
    });
    connect(insert_, &QPushButton::clicked, this, [this] {
        editPoint([&](auto &p) {
            if (segment_ >= 0 && m::subdividePath(p, segment_, segment_t_))
                selected_ = segment_ + 1;
        });
    });
    connect(erase_, &QPushButton::clicked, this, [this] {
        editPoint([&](auto &p) {
            if (selected_ >= 0)
                p.nodes.erase(p.nodes.begin() + selected_);
        });
    });
    connect(key_, &QPushButton::clicked, this, [this] { key(); });
    connect(remove_animation_, &QPushButton::clicked, this, [this] { removeAnimation(); });
    connect(keys_, &QListWidget::itemClicked, this,
            [this](auto *item) { selectCurve(target_, item->data(Qt::UserRole).toLongLong()); });
    owner_.viewer_->installEventFilter(this);
    qApp->installEventFilter(this);
    sync();
}
PenTool::~PenTool() {
    release();
    if (owner_.viewer_)
        owner_.viewer_->setPathOverlayPainter({});
}
bool PenTool::mode() const noexcept {
    return mode_->currentIndex() == 1;
}
const m::CompositionLayer *PenTool::selectedLayer() const {
    if (!owner_.document_)
        return nullptr;
    for (const auto &l : owner_.document_->layers())
        if (l.id == owner_.selected_layer_id_)
            return &l;
    return nullptr;
}
std::optional<std::array<int, 2>> PenTool::receiverMediaSize() const {
    const auto *layer = selectedLayer();
    if (!layer)
        return {};
    const auto linked = owner_.linked_image_frames_.find(layer->id);
    if (linked != owner_.linked_image_frames_.end() && linked->second && linked->second->width > 0 &&
        linked->second->height > 0)
        return std::array{linked->second->width, linked->second->height};
    const auto &library = owner_.media_pool_->library();
    const auto index = library.indexForPath(layer->source_path);
    if (index >= library.items().size())
        return {};
    const auto &metadata = library.items()[index].metadata;
    if (metadata.width <= 0 || metadata.height <= 0)
        return {};
    return std::array{metadata.width, metadata.height};
}
bool PenTool::available(bool selected) const {
    if (!owner_.document_ || !owner_.timeline_ || owner_.export_worker_ || owner_.audio_keyframe_worker_)
        return false;
    if (!selected)
        return true;
    const auto *layer = selectedLayer();
    const auto frame = owner_.timeline_->currentFrame();
    if (!layer || layer->kind == m::LayerKind::Null || !layer->visible ||
        frame < layer->timeline_start_frame || frame - layer->timeline_start_frame >= layer->duration_frames)
        return false;
    if (destination_->currentIndex() == 1 && owner_.inspector_->masksInspector()->selectedTarget() != -1 &&
        (!layer->presentation ||
         layer->presentation->profile != creative_suite::composition_exchange::Profile::FusionCanvas) &&
        (layer->kind == m::LayerKind::Image || layer->kind == m::LayerKind::Video) && !receiverMediaSize())
        return false;
    return true;
}
model::AffineTransform PenTool::mappingForTarget(bool new_shape) const {
    const auto canvas = owner_.document_->canvasSize();
    if (new_shape)
        return {1, 0, 0, 1, canvas.width * .5, canvas.height * .5};
    const auto *layer = selectedLayer();
    if (!layer)
        throw std::invalid_argument("Missing path receiver");
    const auto stage = owner_.inspector_->masksInspector()->selectedTarget();
    if (target_.mask && stage == -1)
        return {double(canvas.width), 0, 0, double(canvas.height), 0, 0};
    if (!owner_.hierarchy_snapshot_ ||
        !owner_.hierarchy_snapshot_->matches(canvas, owner_.document_->layers()))
        owner_.hierarchy_snapshot_ =
            std::make_shared<const m::LayerTransformHierarchy>(canvas, owner_.document_->layers());
    const auto world =
        owner_.hierarchy_snapshot_->evaluate(owner_.timeline_->currentFrame()).at(layer->id).world;
    if (!target_.mask)
        return world;
    int width = canvas.width, height = canvas.height;
    bool media = false;
    const bool fusion =
        layer->presentation &&
        layer->presentation->profile == creative_suite::composition_exchange::Profile::FusionCanvas &&
        !(std::get_if<m::ShapeLayerContent>(&layer->content) &&
          std::get<m::ShapeLayerContent>(layer->content).path);
    if (!fusion) {
        if (const auto *shape = std::get_if<m::ShapeLayerContent>(&layer->content)) {
            width = shape->width;
            height = shape->height;
        } else if (const auto *text = std::get_if<m::TextLayerContent>(&layer->content)) {
            width = text->box_width;
            height = text->box_height;
        } else {
            media = true;
            const auto size = receiverMediaSize();
            if (!size)
                throw std::invalid_argument("Unknown path receiver dimensions");
            width = (*size)[0];
            height = (*size)[1];
        }
    }
    if (width <= 0 || height <= 0)
        throw std::invalid_argument("Unknown path receiver dimensions");
    const auto fit = media ? std::min(double(canvas.width) / width, double(canvas.height) / height) : 1.;
    if (owner_.mask_view_layer_ == layer->id && owner_.mask_view_target_ == stage) {
        const auto view_fit = std::min(double(canvas.width) / width, double(canvas.height) / height);
        return {width * view_fit,
                0,
                0,
                height * view_fit,
                (canvas.width - width * view_fit) * .5,
                (canvas.height - height * view_fit) * .5};
    }
    return c::composeAffine(world, {width * fit, 0, 0, height * fit, -width * fit * .5, -height * fit * .5});
}
bool PenTool::prepare(bool creating) {
    if (!available(creating ? destination_->currentIndex() == 1 : true))
        return false;
    owner_.finishPendingTransformEdit();
    if (destination_->currentIndex() == 0)
        owner_.inspector_->masksInspector()->leaveMaskView();
    frame_ = owner_.timeline_->currentFrame();
    owner_.timeline_->setCurrentFrame(frame_);
    document_ = &*owner_.document_;
    layer_ = owner_.selected_layer_id_;
    canvas_ = owner_.viewer_->compositionRect();
    if (canvas_.isEmpty())
        return false;
    const auto *layer = selectedLayer();
    mask_stage_ = owner_.inspector_->masksInspector()->selectedTarget();
    target_ = {creating ? 0 : layer_, destination_->currentIndex() == 1};
    if (target_.mask) {
        const auto *inspector = owner_.inspector_->masksInspector();
        target_.id = creating ? 0 : inspector->selectedMaskId();
    }
    mapping_ = mappingForTarget(creating && !target_.mask);
    inverse_ = c::requireInverseAffine(mapping_);
    if (!creating) {
        const auto *path = owner_.document_->pathFor(target_);
        if (!path)
            return false;
        geometry_ = m::evaluatePath(*path, target_.mask ? frame_ : frame_ - layer->timeline_start_frame);
    }
    raster_stroke_ = raster_margin_ = 0;
    if (layer && !target_.mask) {
        if (const auto *shape = std::get_if<m::ShapeLayerContent>(&layer->content))
            raster_stroke_ = shape->stroke_width_pixels;
        for (const auto &effect : layer->effects)
            if (const auto *blur = std::get_if<m::GaussianBlurEffect>(&effect); blur && blur->enabled)
                raster_margin_ += std::ceil(
                    3 *
                    m::evaluateGaussianBlurEffect(*blur, frame_ - layer->timeline_start_frame).radius_pixels);
    }
    initial_ = geometry_;
    snapshot_ = std::make_shared<const PreviewRequest>(owner_.buildPreviewRequest());
    session_ = ++owner_.viewer_gesture_session_;
    submitted_ = false;
    return true;
}
void PenTool::startCreation() {
    try {
        if (!prepare(true))
            return;
        creating_ = true;
        geometry_ = {};
        selected_ = -1;
        QSignalBlocker b(mode_);
        mode_->setCurrentIndex(1);
        owner_.syncTransformInspector();
        sync();
        owner_.viewer_->setFocus();
        owner_.updateHistoryActions();
    } catch (const std::exception &e) {
        fail("pen_create_begin", e);
    }
}
void PenTool::startEdit() {
    try {
        if (!prepare(false))
            return;
        QSignalBlocker b(mode_);
        mode_->setCurrentIndex(1);
        selected_ = geometry_.nodes.empty() ? -1 : 0;
        owner_.syncTransformInspector();
        sync();
        owner_.viewer_->setFocus();
    } catch (const std::exception &e) {
        fail("pen_edit_begin", e);
    }
}
void PenTool::release() {
    releasing_ = true;
    pressed_ = dragged_ = false;
    owner_.viewer_->setPathGestureActive(false);
    if (QWidget::mouseGrabber() == owner_.viewer_)
        owner_.viewer_->releaseMouse();
    releasing_ = false;
}
void PenTool::cancel() {
    const bool restore = submitted_;
    release();
    creating_ = false;
    snapshot_.reset();
    submitted_ = false;
    geometry_ = initial_;
    if (owner_.viewer_)
        owner_.viewer_->updatePathOverlay();
    owner_.updateHistoryActions();
    if (restore)
        owner_.requestPreview();
    sync();
}
void PenTool::finishCreation(bool close) {
    if (!creating_)
        return;
    if (target_.mask && owner_.inspector_->masksInspector()->selectedTarget() != mask_stage_) {
        cancel();
        return;
    }
    auto geometry = geometry_;
    geometry.closed = close;
    if (!m::validPathGeometry(geometry, target_.mask))
        return;
    const auto mask = target_.mask;
    const auto layer = layer_;
    const auto frame = frame_;
    const auto destination = mask_stage_;
    cancel();
    edit([&](auto &doc) {
        if (mask)
            return doc.createPathMask(layer, destination, geometry) != 0;
        m::LayerId id = 0;
        if (!doc.createPathLayer(geometry, frame, &id,
                                 MotionUiTranslation::tr("Path").toUtf8().toStdString()))
            return false;
        owner_.selected_layer_id_ = id;
        return true;
    });
    sync();
}
QPointF PenTool::canvasPoint(QPointF p) const {
    const auto size = owner_.document_->canvasSize();
    return {(p.x() - canvas_.left()) * size.width / canvas_.width(),
            (p.y() - canvas_.top()) * size.height / canvas_.height()};
}
QPointF PenTool::localPoint(QPointF p) const {
    const auto q = canvasPoint(p);
    const auto point = inverse_.map(q.x(), q.y());
    return {point[0], point[1]};
}
QPointF PenTool::screenPoint(m::PathPoint p) const {
    const auto q = mapping_.map(p.x, p.y);
    const auto size = owner_.document_->canvasSize();
    return {canvas_.left() + q[0] * canvas_.width() / size.width,
            canvas_.top() + q[1] * canvas_.height() / size.height};
}
PenTool::Hit PenTool::hit(QPointF p) const {
    Hit hit;
    double best = 10;
    for (std::size_t i = 0; i < geometry_.nodes.size(); ++i) {
        const auto &n = geometry_.nodes[i];
        for (int part = 1; part <= 2; ++part) {
            const auto v = part == 1 ? n.incoming : n.outgoing;
            if (v == m::PathPoint{})
                continue;
            const auto q = add(n.point, v);
            const auto distance = length(p - screenPoint({q.x(), q.y()}));
            if (distance <= best) {
                best = distance;
                hit = {int(i), part};
            }
        }
    }
    if (hit.node >= 0)
        return hit;
    for (std::size_t i = 0; i < geometry_.nodes.size(); ++i) {
        const auto distance = length(p - screenPoint(geometry_.nodes[i].point));
        if (distance <= best) {
            best = distance;
            hit = {int(i), 0};
        }
    }
    if (hit.node >= 0)
        return hit;
    const auto count = geometry_.closed          ? geometry_.nodes.size()
                       : geometry_.nodes.empty() ? 0
                                                 : geometry_.nodes.size() - 1;
    for (std::size_t i = 0; i < count; ++i) {
        auto previous = screenPoint(m::pathSegmentPoint(geometry_, i, 0));
        for (int j = 1; j <= 64; ++j) {
            const auto next = screenPoint(m::pathSegmentPoint(geometry_, i, j / 64.));
            const auto v = next - previous;
            const auto d = QPointF::dotProduct(v, v);
            const auto t = d ? std::clamp(QPointF::dotProduct(p - previous, v) / d, 0., 1.) : 0.;
            const auto distance = length(p - (previous + t * v));
            if (distance <= best) {
                best = distance;
                hit = {-1, 0, int(i), (j - 1 + t) / 64.};
            }
            previous = next;
        }
    }
    return hit;
}
void PenTool::press(QPointF point) {
    if (!available(!creating_ || target_.mask))
        return;
    if (creating_) {
        if (!geometry_.nodes.empty() && geometry_.nodes.size() >= 3 &&
            length(point - screenPoint(geometry_.nodes.front().point)) <= 10) {
            finishCreation(true);
            return;
        }
        if (geometry_.nodes.size() >= m::maximum_path_nodes)
            return;
        const auto q = localPoint(point);
        geometry_.nodes.push_back({{q.x(), q.y()}, {}, {}});
        selected_ = int(geometry_.nodes.size()) - 1;
        part_ = 2;
    } else {
        const auto selected = hit(point);
        if (selected.node < 0) {
            segment_ = selected.segment;
            segment_t_ = selected.t;
            refreshControls();
            return;
        }
        const auto node = selected.node, part = selected.part;
        if (!prepare(false))
            return;
        selected_ = node;
        part_ = part;
    }
    press_ = point;
    initial_ = geometry_;
    pressed_ = true;
    dragged_ = false;
    owner_.viewer_->setPathGestureActive(true);
    owner_.viewer_->grabMouse();
    owner_.viewer_->setFocus(Qt::MouseFocusReason);
    owner_.updateHistoryActions();
    refreshFields();
    refreshControls();
    owner_.viewer_->updatePathOverlay();
}
bool PenTool::move(QPointF point) {
    if (!pressed_)
        return true;
    if (!dragged_ && std::abs(point.x() - press_.x()) + std::abs(point.y() - press_.y()) <
                         QApplication::startDragDistance())
        return true;
    dragged_ = true;
    const auto delta = localPoint(point) - localPoint(press_);
    auto candidate = initial_;
    auto &n = candidate.nodes[selected_];
    auto &p = part_ == 0 ? n.point : part_ == 1 ? n.incoming : n.outgoing;
    p.x += delta.x();
    p.y += delta.y();
    if (creating_) {
        n.incoming = {-n.outgoing.x, -n.outgoing.y};
    }
    const auto finite = [](m::PathPoint p) {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::abs(p.x) <= 1e9 && std::abs(p.y) <= 1e9;
    };
    if (!finite(n.point) || !finite(n.incoming) || !finite(n.outgoing) ||
        (!creating_ &&
         (!m::validPathGeometry(candidate, target_.mask) ||
          (!target_.mask && !m::validPathRasterGeometry(candidate, raster_stroke_, raster_margin_))))) {
        cancel();
        return false;
    }
    geometry_ = std::move(candidate);
    changed();
    return true;
}
void PenTool::changed() {
    refreshFields();
    owner_.viewer_->updatePathOverlay();
    if (!creating_ && pressed_)
        owner_.requestPreview();
}
void PenTool::finishDrag() {
    if (!pressed_)
        return;
    const auto geometry = geometry_;
    const bool changed = dragged_ && geometry != initial_;
    release();
    if (creating_) {
        refreshControls();
        return;
    }
    const auto target = target_;
    const auto frame = frame_;
    const bool submitted = submitted_;
    snapshot_.reset();
    submitted_ = false;
    owner_.updateHistoryActions();
    if (changed)
        edit([&](auto &doc) { return doc.setPathAtFrame(target, geometry, frame); });
    else if (submitted)
        owner_.requestPreview();
    sync();
}
void PenTool::edit(std::function<bool(m::CompositionDocument &)> operation) {
    owner_.finishPendingTransformEdit();
    if (!available(false))
        return;
    try {
        auto before = owner_.captureEditState();
        if (!operation(*owner_.document_)) {
            sync();
            // A rejected commit must also invalidate any pending draft pixels.
            owner_.requestPreview();
            return;
        }
        (void)owner_.recordCompositionEdit(std::move(before));
        owner_.refreshTimeline();
        owner_.updateDocumentState();
        owner_.syncTransformInspector();
        owner_.refreshCurveEditor();
        owner_.requestPreview();
        sync();
    } catch (const std::exception &e) {
        fail("edit_path", e);
    }
}
void PenTool::editPoint(std::function<void(m::PathGeometry &)> operation) {
    if (!owner_.document_)
        return;
    const auto *old = owner_.document_->pathFor(target_);
    if (!old)
        return;
    auto candidate = m::evaluatePath(*old, target_.mask ? owner_.timeline_->currentFrame()
                                                        : owner_.timeline_->currentFrame() -
                                                              selectedLayer()->timeline_start_frame);
    operation(candidate);
    const auto target = target_;
    const auto frame = owner_.timeline_->currentFrame();
    edit([&](auto &doc) { return doc.setPathAtFrame(target, candidate, frame); });
}
void PenTool::key() {
    const auto target = target_;
    if (!owner_.document_ || !owner_.timeline_)
        return;
    const auto *path = owner_.document_->pathFor(target);
    if (!path)
        return;
    const auto frame = owner_.timeline_->currentFrame(),
               local = target.mask ? frame : frame - selectedLayer()->timeline_start_frame;
    const bool exists = std::any_of(path->keys.begin(), path->keys.end(),
                                    [local](const auto &k) { return k.frame == local; });
    edit([&](auto &doc) {
        return exists ? doc.removePathKeyframe(target, local) : doc.setPathKeyframe(target, frame);
    });
}
void PenTool::removeAnimation() {
    const auto target = target_;
    const auto frame = owner_.timeline_->currentFrame();
    edit([&](auto &doc) { return doc.clearPathAnimationAtFrame(target, frame); });
}
std::optional<PreviewRequest> PenTool::previewRequest() {
    if (!pressed_ || creating_ || !snapshot_)
        return {};
    auto request = *snapshot_;
    request.path_override = m::PathOverride{target_, std::make_shared<const m::PathGeometry>(geometry_)};
    request.gesture_session = session_;
    submitted_ = true;
    return request;
}
void PenTool::renderFailed() {
    fail("pen_preview", std::runtime_error("Path preview rendering failed"));
}
void PenTool::fail(const char *operation, const std::exception &e) {
    const auto id = target_.id;
    cancel();
    creative_suite::diagnostics::Logger::instance().log(creative_suite::diagnostics::Level::Error,
                                                        "motion_path", operation, e.what(),
                                                        {{"layer_id", std::to_string(layer_)},
                                                         {"path_id", std::to_string(id)},
                                                         {"mask", target_.mask ? "true" : "false"},
                                                         {"frame", std::to_string(frame_)}});
    owner_.statusBar()->showMessage(
        MotionUiTranslation::tr("The path could not be edited. Check the Motion Studio log for details."),
        5000);
}
void PenTool::refreshFields() {
    syncing_ = true;
    {
        QSignalBlocker block(point_selection_);
        point_selection_->setRange(geometry_.nodes.empty() ? 0 : 1, int(geometry_.nodes.size()));
        point_selection_->setValue(selected_ + 1);
        point_selection_->setEnabled(selected_ >= 0 && !active() && available(false));
    }
    const bool point = selected_ >= 0 && std::size_t(selected_) < geometry_.nodes.size();
    const auto node = point ? geometry_.nodes[selected_] : m::PathNode{};
    const std::array values{node.point.x,    node.point.y,    node.incoming.x,
                            node.incoming.y, node.outgoing.x, node.outgoing.y};
    for (std::size_t i = 0; i < 6; ++i) {
        QSignalBlocker b(fields_[i]);
        fields_[i]->setEnabled(point && available(false));
        fields_[i]->setRange(target_.mask ? -1e11 : -1e9, target_.mask ? 1e11 : 1e9);
        fields_[i]->setReadOnly(pressed_ || creating_);
        fields_[i]->setSuffix(target_.mask ? " %" : " px");
        fields_[i]->setValue(values[i] * (target_.mask ? 100 : 1));
    }
    syncing_ = false;
}
void PenTool::refreshControls() {
    const auto *path = owner_.document_ ? owner_.document_->pathFor(target_) : nullptr;
    const bool editable = available(false);
    const bool topology = editable && path && path->keys.empty() && !active();
    close_->setText(!creating_ && geometry_.closed ? MotionUiTranslation::tr("Open Path")
                                                 : MotionUiTranslation::tr("Close Path"));
    new_->setEnabled(available(destination_->currentIndex() == 1) && !active());
    edit_->setEnabled(path && available() && !active());
    done_->setEnabled(creating_ && geometry_.nodes.size() >= std::size_t(target_.mask ? 3 : 2));
    close_->setEnabled((creating_ && geometry_.nodes.size() >= 3) || (topology && !target_.mask));
    key_->setEnabled(editable && path && !active());
    remove_animation_->setEnabled(editable && path && !path->keys.empty() && !active());
    insert_->setEnabled(topology && segment_ >= 0);
    erase_->setEnabled(topology && selected_ >= 0);
    corner_->setEnabled(editable && path && selected_ >= 0 && !active());
    smooth_->setEnabled(editable && path && selected_ >= 0 && !active());
    destination_->setEnabled(!active());
    keys_->setEnabled(editable && !active());
}
void PenTool::sync() {
    if (mode())
        owner_.viewer_->setPathOverlayPainter([this](QPainter &p, QRectF r) { paint(p, r); });
    else
        owner_.viewer_->setPathOverlayPainter({});
    if (mode())
        owner_.viewer_->setCursor(Qt::CrossCursor);
    else
        owner_.viewer_->unsetCursor();
    if (!owner_.timeline_ || !owner_.document_) {
        if (active())
            cancel();
        geometry_ = {};
        selected_ = -1;
        refreshFields();
        refreshControls();
        return;
    }
    if (active()) {
        if (document_ != &*owner_.document_ || layer_ != owner_.selected_layer_id_ ||
            frame_ != owner_.timeline_->currentFrame() || canvas_ != owner_.viewer_->compositionRect() ||
            (target_.mask && mask_stage_ != owner_.inspector_->masksInspector()->selectedTarget()))
            cancel();
        else {
            refreshFields();
            refreshControls();
            return;
        }
    }
    const auto *layer = selectedLayer();
    target_ = {owner_.selected_layer_id_, destination_->currentIndex() == 1};
    if (target_.mask)
        target_.id = owner_.inspector_->masksInspector()->selectedMaskId();
    const auto *path = owner_.document_->pathFor(target_);
    if (path && layer) {
        geometry_ = m::evaluatePath(*path, target_.mask ? owner_.timeline_->currentFrame()
                                                        : owner_.timeline_->currentFrame() -
                                                              layer->timeline_start_frame);
        selected_ = geometry_.nodes.empty() ? -1 : std::clamp(selected_, 0, int(geometry_.nodes.size()) - 1);
        initial_ = geometry_;
    } else {
        geometry_ = {};
        selected_ = -1;
    }
    keys_->clear();
    if (path)
        for (const auto &k : path->keys) {
            auto *item = new QListWidgetItem(MotionUiTranslation::tr("Frame %1").arg(k.frame), keys_);
            item->setData(Qt::UserRole, QVariant::fromValue<qlonglong>(k.frame));
        }
    refreshFields();
    refreshControls();
}
void PenTool::paint(QPainter &painter, QRectF canvas) {
    if (!mode() || !owner_.document_ || geometry_.nodes.empty())
        return;
    if (!active()) {
        canvas_ = canvas;
        try {
            mapping_ = mappingForTarget(false);
            inverse_ = c::requireInverseAffine(mapping_);
        } catch (const std::exception &) {
            return;
        }
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(255, 200, 80), 1.5));
    painter.setBrush(Qt::NoBrush);
    QPainterPath line;
    if (!geometry_.nodes.empty()) {
        const auto p = screenPoint(geometry_.nodes.front().point);
        line.moveTo(p);
        const auto count = geometry_.closed ? geometry_.nodes.size() : geometry_.nodes.size() - 1;
        for (std::size_t i = 0; i < count; ++i) {
            const auto &a = geometry_.nodes[i];
            const auto &b = geometry_.nodes[(i + 1) % geometry_.nodes.size()];
            const auto x = add(a.point, a.outgoing), y = add(b.point, b.incoming);
            line.cubicTo(screenPoint({x.x(), x.y()}), screenPoint({y.x(), y.y()}), screenPoint(b.point));
        }
    }
    painter.drawPath(line);
    for (std::size_t i = 0; i < geometry_.nodes.size(); ++i) {
        const auto &n = geometry_.nodes[i];
        const auto p = screenPoint(n.point);
        painter.setPen(QPen(QColor(180, 180, 180), 1));
        for (auto v : {n.incoming, n.outgoing})
            if (v != m::PathPoint{}) {
                const auto q = add(n.point, v), h = screenPoint({q.x(), q.y()});
                painter.drawLine(p, h);
                painter.setBrush(QColor(70, 90, 120));
                painter.drawEllipse(h, 3, 3);
            }
        painter.setPen(QColor(255, 200, 80));
        painter.setBrush(int(i) == selected_ ? QColor(255, 200, 80) : QColor(40, 40, 40));
        painter.drawRect(QRectF(p - QPointF(4, 4), QSizeF(8, 8)));
    }
    painter.restore();
}
bool PenTool::eventFilter(QObject *watched, QEvent *event) {
    const auto type = event->type();
    if (active() && !releasing_ &&
        ((type == QEvent::WindowDeactivate && watched == owner_.window()) ||
         ((watched == owner_.viewer_) && (type == QEvent::Resize || type == QEvent::Move ||
                                          type == QEvent::Hide || type == QEvent::UngrabMouse)))) {
        cancel();
        return false;
    }
    // Control-state changes also request a layout. Only an actual move or resize
    // of the Viewer invalidates the frozen coordinate mapping (handled above).
    if (active() && type == QEvent::MouseButtonPress && watched != owner_.viewer_) {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && widget != controls_ && !controls_->isAncestorOf(widget) && widget != panel_ &&
            !panel_->isAncestorOf(widget))
            cancel();
    }
    if (active() && (type == QEvent::ShortcutOverride || type == QEvent::KeyPress)) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape || key->matches(QKeySequence::Undo) ||
            key->matches(QKeySequence::Redo)) {
            event->accept();
            if (type == QEvent::KeyPress && !key->matches(QKeySequence::Redo))
                cancel();
            return true;
        }
    }
    if (watched != owner_.viewer_ || !mode())
        return false;
    if (type == QEvent::ContextMenu) {
        event->accept();
        return true;
    }
    if (type == QEvent::ShortcutOverride || type == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool handled =
            key->key() == Qt::Key_Escape || key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter ||
            key->key() == Qt::Key_Backspace || key->key() == Qt::Key_Delete ||
            (active() && (key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)));
        if (handled) {
            event->accept();
            if (type == QEvent::ShortcutOverride)
                return true;
            if (key->key() == Qt::Key_Escape || key->matches(QKeySequence::Undo))
                cancel();
            else if (key->key() == Qt::Key_Enter || key->key() == Qt::Key_Return)
                finishCreation(target_.mask);
            else if (creating_ && key->key() == Qt::Key_Backspace) {
                release();
                if (!geometry_.nodes.empty())
                    geometry_.nodes.pop_back();
                selected_ = int(geometry_.nodes.size()) - 1;
                changed();
                refreshControls();
            } else if (!active() && key->key() == Qt::Key_Delete && erase_->isEnabled())
                erase_->click();
            return true;
        }
    }
    try {
        if (type == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton &&
                owner_.viewer_->rect().contains(mouse->position().toPoint()))
                press(mouse->position());
            event->accept();
            return true;
        }
        if (type == QEvent::MouseMove) {
            if (pressed_)
                move(static_cast<QMouseEvent *>(event)->position());
            event->accept();
            return true;
        }
        if (type == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (pressed_ && mouse->button() == Qt::LeftButton && move(mouse->position()))
                finishDrag();
            event->accept();
            return true;
        }
    } catch (const std::exception &e) {
        fail("pen_pointer", e);
        return true;
    }
    return false;
}
void PenTool::selectCurve(m::PathTarget target, std::int64_t frame) {
    if (!owner_.document_ || !owner_.timeline_)
        return;
    owner_.finishPendingTransformEdit();
    destination_->setCurrentIndex(target.mask ? 1 : 0);
    if (target.mask)
        owner_.inspector_->masksInspector()->selectDestinationForMask(target.id);
    sync();
    MainWindow::CurveSelection selection;
    selection.layer_id = owner_.selected_layer_id_;
    selection.path_target = target;
    selection.segment_start_frame = frame;
    owner_.curve_selection_ = selection;
    const auto *l = selectedLayer();
    owner_.timeline_->setCurrentFrame(target.mask ? frame : frame + l->timeline_start_frame);
    owner_.refreshCurveEditor();
}
} // namespace motion::ui
