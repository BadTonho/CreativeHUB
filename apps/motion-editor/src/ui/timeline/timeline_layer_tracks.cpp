#include "ui/translation_helpers.h"
#include "timeline_layer_tracks.h"
#include "timeline_view_mapping.h"

#include <QColor>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygon>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <unordered_set>
#include <unordered_map>
#include <utility>

namespace motion::ui {
namespace {

constexpr int kTimelineRowHeight = 34;
constexpr int kTimelineClipHandleWidth = 7;
constexpr int kTimelineSnapTolerancePixels = 8;
constexpr std::int64_t kMaximumFrame = std::numeric_limits<std::int64_t>::max();
const QString kMotionMediaMimeType = QStringLiteral("application/x-creative-suite-motion-media");

std::filesystem::path pathFromQString(const QString& value)
{
    const auto utf8 = value.toUtf8();
    const auto* first = reinterpret_cast<const char8_t*>(utf8.constData());
    return std::filesystem::path(std::u8string(first, first + utf8.size()));
}

enum class TrackProperty { PositionX, PositionY, ScaleX, Rotation, Opacity, AnchorX, AnchorY, ScaleY };

constexpr std::array<TrackProperty, 8> kTimelineProperties{{
    TrackProperty::PositionX,
    TrackProperty::PositionY,
    TrackProperty::ScaleX,
    TrackProperty::ScaleY,
    TrackProperty::Rotation,
    TrackProperty::Opacity,
    TrackProperty::AnchorX,
    TrackProperty::AnchorY,
}};

const creative_suite::animation::ScalarKeyframes& trackKeys(const TimelineLayerRow& row, TrackProperty property)
{
    if(property==TrackProperty::ScaleX)return row.keyframes.scale;
    if(property==TrackProperty::ScaleY)return row.scale_y?row.scale_y->keyframes:row.keyframes.scale;
    if (property == TrackProperty::AnchorX) return row.anchor_keyframes[0];
    if (property == TrackProperty::AnchorY) return row.anchor_keyframes[1];
    return creative_suite::animation::keyframesFor(row.keyframes,
        static_cast<creative_suite::animation::TransformProperty>(property));
}

QString propertyName(TrackProperty property)
{
    using P = TrackProperty;
    switch (property) {
    case P::PositionX: return MotionUiTranslation::tr("Position X");
    case P::PositionY: return MotionUiTranslation::tr("Position Y");
    case P::ScaleX: return MotionUiTranslation::tr("Scale X (%)");
    case P::ScaleY: return MotionUiTranslation::tr("Scale Y (%)");
    case P::Rotation: return MotionUiTranslation::tr("Rotation");
    case P::Opacity: return MotionUiTranslation::tr("Opacity");
    case P::AnchorX: return MotionUiTranslation::tr("Anchor X (px)");
    case P::AnchorY: return MotionUiTranslation::tr("Anchor Y (px)");
    }
    return {};
}

QColor propertyColor(TrackProperty property)
{
    using P = TrackProperty;
    switch (property) {
    case P::PositionX: return QColor(100, 190, 255);
    case P::PositionY: return QColor(130, 225, 180);
    case P::ScaleX: return QColor(210, 160, 255);
    case P::ScaleY: return QColor(180, 140, 230);
    case P::Rotation: return QColor(255, 190, 90);
    case P::Opacity: return QColor(255, 125, 150);
    case P::AnchorX: return QColor(130, 200, 240);
    case P::AnchorY: return QColor(120, 220, 190);
    }
    return QColor(255, 255, 255);
}

using EffectParameterProperty = model::EffectParameterProperty;

QString effectPropertyName(std::size_t effect_index, EffectParameterProperty property)
{
    using P = EffectParameterProperty;
    QString prop_label;
    switch (property) {
    case P::GaussianBlurRadius:
        prop_label = MotionUiTranslation::tr("Radius (sigma)");
        break;
    case P::ColorBrightness:
        prop_label = MotionUiTranslation::tr("Brightness");
        break;
    case P::ColorContrastPercent:
        prop_label = MotionUiTranslation::tr("Contrast");
        break;
    case P::ColorSaturationPercent:
        prop_label = MotionUiTranslation::tr("Saturation");
        break;
    }
    return MotionUiTranslation::tr("Effect %1: %2")
        .arg(static_cast<qlonglong>(effect_index + 1))
        .arg(prop_label);
}

QColor effectPropertyColor(EffectParameterProperty property)
{
    using P = EffectParameterProperty;
    switch (property) {
    case P::GaussianBlurRadius: return QColor(115, 205, 235);
    case P::ColorBrightness: return QColor(245, 205, 110);
    case P::ColorContrastPercent: return QColor(195, 155, 245);
    case P::ColorSaturationPercent: return QColor(245, 140, 175);
    }
    return QColor(255, 255, 255);
}

struct VisualTimelineRow {
    enum class Kind : std::uint8_t {
        Layer,
        TransformGroup,
        Property,
        EffectsGroup,
        EffectProperty,
        PathProperty,
    };
    std::size_t layer_index = 0;
    Kind kind = Kind::Layer;
    std::optional<TrackProperty> property;
    std::size_t effect_index = 0;
    std::optional<model::EffectParameterProperty> effect_property;
    std::size_t path_index=0;
};
class LayerRowsCanvas final : public QWidget {
public:
    explicit LayerRowsCanvas(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("motion-timeline-layer-rows"));
        setAcceptDrops(true);
        setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        setMinimumHeight(kTimelineRowHeight);
        pick_whip = new ParentPickWhip(this,
            [this](QPointF point) { return parentHit(point); },
            [this](QPointF point) { return parentTooltip(point); });
    }

    ~LayerRowsCanvas() override { delete pick_whip; }
    ParentPickWhip* pick_whip = nullptr;

    void setRows(std::vector<TimelineLayerRow> rows)
    {
        pick_whip->cancel();
        rows_ = std::move(rows);
        parent_names_.clear();
        for (const auto& row : rows_) parent_names_.emplace(row.id,row.name);
        std::unordered_set<model::LayerId> existing;
        existing.reserve(rows_.size());
        for (const auto& row : rows_) existing.insert(row.id);
        std::erase_if(expanded_layers_, [&existing](model::LayerId id) {
            return !existing.contains(id);
        });
        std::erase_if(expanded_transform_groups_, [&existing](model::LayerId id) {
            return !existing.contains(id);
        });
        std::erase_if(expanded_effects_groups_, [&existing](model::LayerId id) {
            return !existing.contains(id);
        });
        rebuildVisualRows();
    }

    void setLayerExpanded(model::LayerId id, bool expanded)
    {
        if (findRow(id) == nullptr) return;
        if (expanded) expanded_layers_.insert(id);
        else expanded_layers_.erase(id);
        rebuildVisualRows();
    }

    void setTransformGroupExpanded(model::LayerId id, bool expanded)
    {
        if (findRow(id) == nullptr) return;
        if (expanded && expanded_layers_.contains(id)) {
            expanded_transform_groups_.insert(id);
        } else {
            expanded_transform_groups_.erase(id);
        }
        rebuildVisualRows();
    }

    void setEffectsGroupExpanded(model::LayerId id, bool expanded)
    {
        if (findRow(id) == nullptr) return;
        if (expanded && expanded_layers_.contains(id)) {
            expanded_effects_groups_.insert(id);
        } else {
            expanded_effects_groups_.erase(id);
        }
        rebuildVisualRows();
    }

    void collapseAllLayerTracks()
    {
        expanded_layers_.clear();
        expanded_transform_groups_.clear();
        expanded_effects_groups_.clear();
        rebuildVisualRows();
    }

    void setViewState(std::int64_t end_frame,
                      std::int64_t current_frame,
                      std::int64_t start_frame,
                      std::int64_t frames_per_view)
    {
        if (current_frame != current_frame_ || start_frame != view_start_frame_ || frames_per_view != frames_per_view_)
            pick_whip->cancel();
        visible_end_frame_ = std::max<std::int64_t>(0, end_frame);
        current_frame_ = std::clamp(current_frame, std::int64_t{0}, visible_end_frame_);
        view_start_frame_ = std::clamp(start_frame, std::int64_t{0}, visible_end_frame_);
        frames_per_view_ = std::max<std::int64_t>(1, frames_per_view);
        update();
    }

    void setSelectedLayerId(model::LayerId id)
    {
        if (selected_layer_id_ != id) pick_whip->cancel();
        selected_layer_id_ = id;
        update();
    }

    std::function<void(const std::filesystem::path&, std::int64_t, model::LayerId)> media_drop;
    std::function<void(model::LayerId)> layer_selected;
    std::function<void(model::LayerId, const QPoint&)> layer_context_requested;
    std::function<void(model::LayerId, std::int64_t)> layer_move;
    std::function<void(model::LayerId, std::int64_t)> layer_resize;
    std::function<void(model::LayerId, std::size_t)> layer_reorder;
    std::function<void(model::LayerId, bool)> layer_visibility;
    std::function<void(model::LayerId)> layer_remove;
    std::function<void(model::LayerId, model::ScaleAxis, std::int64_t)> scale_axis_keyframe_selected;
    std::function<bool(model::LayerId, model::ScaleAxis, std::int64_t, std::int64_t)> scale_axis_keyframe_move;
    std::function<void(model::LayerId, model::ScaleAxis, std::int64_t)> scale_axis_curve_segment_selected;
    std::function<void(model::LayerId, model::AnchorProperty, std::int64_t)> anchor_keyframe_selected;
    std::function<bool(model::LayerId, model::AnchorProperty, std::int64_t, std::int64_t)> anchor_keyframe_move;
    std::function<void(model::LayerId, model::AnchorProperty, std::int64_t)> anchor_curve_segment_selected;
    void selectTrackCurve(model::LayerId id, TrackProperty property, std::int64_t frame) {
        if(property==TrackProperty::ScaleX || property==TrackProperty::ScaleY) {
            if(scale_axis_curve_segment_selected)(void)scale_axis_curve_segment_selected(id,property==TrackProperty::ScaleX?model::ScaleAxis::X:model::ScaleAxis::Y,frame);return;
        }
        if ((property == TrackProperty::AnchorX || property == TrackProperty::AnchorY)) {
            if (anchor_curve_segment_selected) anchor_curve_segment_selected(id, property == TrackProperty::AnchorX ? model::AnchorProperty::X : model::AnchorProperty::Y, frame);
        } else if (curve_segment_selected) curve_segment_selected(id,static_cast<creative_suite::animation::TransformProperty>(property),frame);
    }
    void selectTrackKey(model::LayerId id, TrackProperty property, std::int64_t frame) {
        if(property==TrackProperty::ScaleX || property==TrackProperty::ScaleY) {
            if(scale_axis_keyframe_selected)(void)scale_axis_keyframe_selected(id,property==TrackProperty::ScaleX?model::ScaleAxis::X:model::ScaleAxis::Y,frame);return;
        }
        if ((property == TrackProperty::AnchorX || property == TrackProperty::AnchorY)) {
            if (anchor_keyframe_selected) anchor_keyframe_selected(id, property == TrackProperty::AnchorX ? model::AnchorProperty::X : model::AnchorProperty::Y, frame);
        } else if (keyframe_selected) keyframe_selected(id,static_cast<creative_suite::animation::TransformProperty>(property),frame);
    }
    void moveTrackKey(model::LayerId id, TrackProperty property, std::int64_t from, std::int64_t to) {
        if(property==TrackProperty::ScaleX || property==TrackProperty::ScaleY) {
            if(scale_axis_keyframe_move)(void)scale_axis_keyframe_move(id,property==TrackProperty::ScaleX?model::ScaleAxis::X:model::ScaleAxis::Y,from,to);return;
        }
        if ((property == TrackProperty::AnchorX || property == TrackProperty::AnchorY)) {
            if (anchor_keyframe_move) (void)anchor_keyframe_move(id,property == TrackProperty::AnchorX ? model::AnchorProperty::X : model::AnchorProperty::Y,from,to);
        } else if (keyframe_move) (void)keyframe_move(id,static_cast<creative_suite::animation::TransformProperty>(property),from,to);
    }
    std::function<void(model::LayerId,model::PathTarget,std::int64_t)> path_selected;
    std::function<bool(model::LayerId,model::PathTarget,std::int64_t,std::int64_t)> path_moved;
    std::function<void(model::LayerId, creative_suite::animation::TransformProperty, std::int64_t)> keyframe_selected;
    std::function<bool(model::LayerId, creative_suite::animation::TransformProperty, std::int64_t, std::int64_t)>
        keyframe_move;
    std::function<void(model::LayerId, creative_suite::animation::TransformProperty, std::int64_t)>
        curve_segment_selected;
    std::function<void(model::LayerId, std::size_t, EffectParameterProperty, std::int64_t)>
        effect_keyframe_selected;
    std::function<bool(model::LayerId,
                       std::size_t,
                       EffectParameterProperty,
                       std::int64_t,
                       std::int64_t)>
        effect_keyframe_move;
    std::function<void(model::LayerId, std::size_t, EffectParameterProperty, std::int64_t)>
        effect_curve_segment_selected;
    std::function<void(int)> zoom_step_requested;
    std::function<void(int)> viewport_width_changed;

protected:
    void contextMenuEvent(QContextMenuEvent* event) override
    {
        const int row_index = rowAtY(event->pos().y());
        if (row_index < 0) {
            QWidget::contextMenuEvent(event);
            return;
        }
        const auto visual = visual_rows_[static_cast<std::size_t>(row_index)];
        if (visual.kind != VisualTimelineRow::Kind::Layer) {
            QWidget::contextMenuEvent(event);
            return;
        }
        const auto row = rows_[visual.layer_index];
        if (layer_selected) layer_selected(row.id);
        if (layer_context_requested)
            layer_context_requested(row.id, event->globalPos());
        event->accept();
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        if (viewport_width_changed) {
            viewport_width_changed(event->size().width());
        }
    }

    QSize sizeHint() const override
    {
        return {520, std::max(kTimelineRowHeight,
            static_cast<int>(visual_rows_.size()) * kTimelineRowHeight)};
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(31, 35, 41));
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (rows_.empty()) {
            painter.setPen(QColor(170, 176, 184));
            painter.drawText(rect(), Qt::AlignCenter,
                             MotionUiTranslation::tr("Drag media here to add a layer"));
        }

        const auto mapping = viewMapping();
        for (std::size_t visual_index = 0; visual_index < visual_rows_.size(); ++visual_index) {
            const auto& visual = visual_rows_[visual_index];
            const auto& row = rows_[visual.layer_index];
            const int top = static_cast<int>(visual_index) * kTimelineRowHeight;
            const bool layer_row = visual.kind == VisualTimelineRow::Kind::Layer;
            const bool transform_group =
                visual.kind == VisualTimelineRow::Kind::TransformGroup;
            const bool effects_group =
                visual.kind == VisualTimelineRow::Kind::EffectsGroup;
            const bool group_row = transform_group || effects_group;
            const bool property_track =
                visual.kind == VisualTimelineRow::Kind::Property ||
                visual.kind == VisualTimelineRow::Kind::EffectProperty || visual.kind == VisualTimelineRow::Kind::PathProperty;
            const QRect row_rect(0, top, width(), kTimelineRowHeight);
            const bool selected = row.id == selected_layer_id_;
            QColor row_background;
            if (property_track) {
                row_background = QColor(34, 38, 45);
            } else if (group_row) {
                row_background = QColor(39, 43, 50);
            } else if (selected) {
                row_background = QColor(64, 69, 78);
            } else {
                row_background = visual.layer_index % 2 == 0
                    ? QColor(39, 43, 50) : QColor(35, 39, 46);
            }
            painter.fillRect(row_rect, row_background);
            painter.fillRect(QRect(0, top, kTimelineHeaderWidth, kTimelineRowHeight),
                property_track ? QColor(39, 43, 50)
                    : (group_row ? QColor(47, 51, 58) : QColor(43, 47, 54)));
            painter.setPen(QPen(QColor(63, 68, 76), 1));
            painter.drawLine(0, top + kTimelineRowHeight - 1,
                             width(), top + kTimelineRowHeight - 1);

            if (layer_row) {
                const QPoint eye_center(15, top + kTimelineRowHeight / 2);
                painter.setBrush(row.visible ? QColor(210, 215, 222) : Qt::NoBrush);
                painter.setPen(QPen(row.visible ? QColor(210, 215, 222) : QColor(130, 136, 144), 1.5));
                painter.drawEllipse(eye_center, 5, 4);
                if (row.visible) {
                    painter.setPen(QPen(QColor(43, 47, 54), 1));
                    painter.drawPoint(eye_center);
                }
                const bool expanded = expanded_layers_.contains(row.id);
                QPolygon disclosure;
                if (expanded) {
                    disclosure << QPoint(34, top + 13) << QPoint(44, top + 13)
                               << QPoint(39, top + 20);
                } else {
                    disclosure << QPoint(36, top + 11) << QPoint(43, top + 17)
                               << QPoint(36, top + 23);
                }
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(185, 190, 198));
                painter.drawPolygon(disclosure);

                painter.save();
                painter.setClipRect(QRect(kTimelineHeaderWidth,top,std::max(0,width()-kTimelineHeaderWidth),kTimelineRowHeight));
                const auto end_frame = row.start_frame > kMaximumFrame - row.duration_frames
                    ? kMaximumFrame : row.start_frame + row.duration_frames;
                if (end_frame >= view_start_frame_ && row.start_frame <= mapping.viewEndFrame()) {
                    const int clip_left = mapping.xForFrame(row.start_frame);
                    const int clip_right = std::max(clip_left + 2, mapping.xForFrame(end_frame));
                    const QRect clip_rect(clip_left, top + 5,
                                          std::max(2, clip_right - clip_left),
                                          kTimelineRowHeight - 10);
                    QColor clip_color;
                    switch (row.kind) {
                    case model::LayerKind::Image: clip_color = QColor(54, 115, 160); break;
                    case model::LayerKind::Video: clip_color = QColor(57, 132, 101); break;
                    case model::LayerKind::Text: clip_color = QColor(91, 89, 174); break;
                    case model::LayerKind::Shape: clip_color = QColor(164, 104, 52); break;
                    case model::LayerKind::Null: clip_color = QColor(110, 115, 125); break;
                    }
                    if (!row.visible) clip_color = clip_color.darker(190);
                    painter.setPen(QPen(selected ? QColor(255, 183, 54) : clip_color.lighter(125),
                                        selected ? 2 : 1));
                    painter.setBrush(clip_color);
                    painter.drawRoundedRect(clip_rect, 3, 3);
                    painter.setPen(QColor(245, 247, 250));
                    painter.drawText(clip_rect.adjusted(6, 0, -8, 0),
                                     Qt::AlignLeft | Qt::AlignVCenter, row.kind == model::LayerKind::Null
                                         ? QStringLiteral("◇ ") + row.name : row.name);

                    if (interaction_layer_id_ == row.id &&
                        (interaction_mode_ == InteractionMode::Move ||
                         interaction_mode_ == InteractionMode::Resize)) {
                        const auto preview_start = interaction_mode_ == InteractionMode::Move
                            ? preview_value_ : row.start_frame;
                        const auto preview_duration = interaction_mode_ == InteractionMode::Resize
                            ? preview_value_ : row.duration_frames;
                        const auto preview_end = preview_start > kMaximumFrame - preview_duration
                            ? kMaximumFrame : preview_start + preview_duration;
                        const int preview_left = mapping.xForFrame(preview_start);
                        const int preview_right = std::max(
                            preview_left + 2, mapping.xForFrame(preview_end));
                        painter.setPen(QPen(QColor(255, 183, 54), 2, Qt::DashLine));
                        painter.setBrush(QColor(255, 183, 54, 55));
                        painter.drawRoundedRect(QRect(preview_left, top + 3,
                            std::max(2, preview_right - preview_left), kTimelineRowHeight - 6), 3, 3);
                    }
                }
                painter.restore();
                paintParentControl(painter,row,row_rect);
            } else if (group_row) {
                const bool expanded = transform_group
                    ? expanded_transform_groups_.contains(row.id)
                    : expanded_effects_groups_.contains(row.id);
                QPolygon disclosure;
                if (expanded) {
                    disclosure << QPoint(50, top + 13) << QPoint(60, top + 13)
                               << QPoint(55, top + 20);
                } else {
                    disclosure << QPoint(52, top + 11) << QPoint(59, top + 17)
                               << QPoint(52, top + 23);
                }
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor(185, 190, 198));
                painter.drawPolygon(disclosure);
                painter.setPen(QColor(228, 231, 235));
                painter.drawText(QRect(66, top, kTimelineHeaderWidth - 72, kTimelineRowHeight),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 transform_group
                                     ? MotionUiTranslation::tr("Transform")
                                     : MotionUiTranslation::tr("Effects"));
            } else if(visual.kind==VisualTimelineRow::Kind::PathProperty){
                const auto& path=row.paths[visual.path_index];const int center=top+kTimelineRowHeight/2;painter.setPen(QColor(255,200,80));painter.drawText(QRect(68,top,kTimelineHeaderWidth-74,kTimelineRowHeight),Qt::AlignLeft|Qt::AlignVCenter,path.name);
                for(const auto& key:path.keys()){auto frame=key.frame;if(interaction_mode_==InteractionMode::MovePathKeyframe&&interaction_layer_id_==row.id&&interaction_path_index_==visual.path_index&&frame==interaction_keyframe_frame_)frame=preview_value_;const auto composition=path.start+frame;if(composition<view_start_frame_||composition>mapping.viewEndFrame())continue;const int x=mapping.xForFrame(composition);QPolygon diamond;diamond<<QPoint(x,center-6)<<QPoint(x+6,center)<<QPoint(x,center+6)<<QPoint(x-6,center);painter.setBrush(QColor(255,200,80));painter.drawPolygon(diamond);}
            } else if (visual.kind == VisualTimelineRow::Kind::Property) {
                const auto property = *visual.property;
                painter.setPen(propertyColor(property));
                painter.drawText(QRect(68, top, kTimelineHeaderWidth - 74, kTimelineRowHeight),
                                 Qt::AlignLeft | Qt::AlignVCenter, propertyName(property));
                const int lane_center_y = top + kTimelineRowHeight / 2;
                painter.setPen(QPen(QColor(68, 74, 83), 1));
                painter.drawLine(mapping.axisLeft(), lane_center_y, width(), lane_center_y);
                const auto& keyframes = trackKeys(row, property);
                for (const auto& keyframe : keyframes) {
                    if (keyframe.frame < 0 || keyframe.frame >= row.duration_frames) continue;
                    const auto composition_frame = row.start_frame + keyframe.frame;
                    if (composition_frame < view_start_frame_ ||
                        composition_frame > mapping.viewEndFrame()) continue;
                    if (interaction_mode_ == InteractionMode::MoveKeyframe &&
                        interaction_layer_id_ == row.id && interaction_property_ == property &&
                        keyframe.frame == interaction_keyframe_frame_ &&
                        preview_value_ != interaction_keyframe_frame_) continue;
                    const int x = mapping.xForFrame(composition_frame);
                    QPolygon diamond;
                    diamond << QPoint(x, lane_center_y - 6) << QPoint(x + 6, lane_center_y)
                            << QPoint(x, lane_center_y + 6) << QPoint(x - 6, lane_center_y);
                    painter.setPen(QPen(propertyColor(property).lighter(135), 1));
                    painter.setBrush(propertyColor(property));
                    painter.drawPolygon(diamond);
                }
                if (interaction_mode_ == InteractionMode::MoveKeyframe &&
                    interaction_layer_id_ == row.id && interaction_property_ == property) {
                    const auto composition_frame = row.start_frame + preview_value_;
                    if (composition_frame >= view_start_frame_ &&
                        composition_frame <= mapping.viewEndFrame()) {
                        const int x = mapping.xForFrame(composition_frame);
                        QPolygon diamond;
                        diamond << QPoint(x, lane_center_y - 7) << QPoint(x + 7, lane_center_y)
                                << QPoint(x, lane_center_y + 7) << QPoint(x - 7, lane_center_y);
                        painter.setPen(QPen(QColor(255, 183, 54), 2));
                        painter.setBrush(propertyColor(property));
                        painter.drawPolygon(diamond);
                    }
                }
            } else if (visual.kind == VisualTimelineRow::Kind::EffectProperty &&
                       visual.effect_index < row.effects.size()) {
                const auto effect_index = visual.effect_index;
                const auto effect_property = *visual.effect_property;
                const auto color = effectPropertyColor(effect_property);
                painter.setPen(color);
                painter.drawText(QRect(68, top, kTimelineHeaderWidth - 74, kTimelineRowHeight),
                                 Qt::AlignLeft | Qt::AlignVCenter,
                                 effectPropertyName(effect_index, effect_property));
                const int lane_center_y = top + kTimelineRowHeight / 2;
                painter.setPen(QPen(QColor(68, 74, 83), 1));
                painter.drawLine(mapping.axisLeft(), lane_center_y, width(), lane_center_y);
                const auto* keyframes = model::effectKeyframesFor(
                    row.effects[effect_index], effect_property);
                if (keyframes != nullptr) {
                    for (const auto& keyframe : *keyframes) {
                        if (keyframe.frame < 0 || keyframe.frame >= row.duration_frames) continue;
                        const auto composition_frame = row.start_frame + keyframe.frame;
                        if (composition_frame < view_start_frame_ ||
                            composition_frame > mapping.viewEndFrame()) continue;
                        if (interaction_mode_ == InteractionMode::MoveEffectKeyframe &&
                            interaction_layer_id_ == row.id &&
                            interaction_effect_index_ == effect_index &&
                            interaction_effect_property_ == effect_property &&
                            keyframe.frame == interaction_keyframe_frame_ &&
                            preview_value_ != interaction_keyframe_frame_) continue;
                        const int x = mapping.xForFrame(composition_frame);
                        QPolygon diamond;
                        diamond << QPoint(x, lane_center_y - 6) << QPoint(x + 6, lane_center_y)
                                << QPoint(x, lane_center_y + 6) << QPoint(x - 6, lane_center_y);
                        painter.setPen(QPen(color.lighter(135), 1));
                        painter.setBrush(color);
                        painter.drawPolygon(diamond);
                    }
                }
                if (interaction_mode_ == InteractionMode::MoveEffectKeyframe &&
                    interaction_layer_id_ == row.id &&
                    interaction_effect_index_ == effect_index &&
                    interaction_effect_property_ == effect_property) {
                    const auto composition_frame = row.start_frame + preview_value_;
                    if (composition_frame >= view_start_frame_ &&
                        composition_frame <= mapping.viewEndFrame()) {
                        const int x = mapping.xForFrame(composition_frame);
                        QPolygon diamond;
                        diamond << QPoint(x, lane_center_y - 7) << QPoint(x + 7, lane_center_y)
                                << QPoint(x, lane_center_y + 7) << QPoint(x - 7, lane_center_y);
                        painter.setPen(QPen(QColor(255, 183, 54), 2));
                        painter.setBrush(color);
                        painter.drawPolygon(diamond);
                    }
                }
            }
        }

        if (current_frame_ >= view_start_frame_ && current_frame_ <= mapping.viewEndFrame()) {
            const int playhead_x = mapping.xForFrame(current_frame_);
            painter.setPen(QPen(QColor(255, 183, 54), 1));
            painter.drawLine(playhead_x, 0, playhead_x, height());
        }

        if (drag_preview_active_) {
            const int visual = rowAtY(drag_preview_y_);
            const int top = visual >= 0 ? visual * kTimelineRowHeight : 0;
            painter.setPen(QPen(QColor(255, 183, 54), 2, Qt::DashLine));
            painter.drawLine(drag_preview_x_, top, drag_preview_x_, top + kTimelineRowHeight);
            painter.setBrush(QColor(255, 183, 54, 55));
            painter.setPen(QPen(QColor(255, 183, 54), 1, Qt::DashLine));
            painter.drawRoundedRect(QRect(drag_preview_x_, top + 5, 74,
                kTimelineRowHeight - 10), 3, 3);
        }
        pick_whip->paint(painter);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(event);
            return;
        }
        setFocus(Qt::MouseFocusReason);
        const auto position = event->position().toPoint();
        const int row_index = rowAtY(position.y());
        if (row_index < 0) return;
        // Selecting a layer refreshes this widget's row vectors. Keep local
        // copies so the selection callback cannot invalidate data used by the
        // remainder of this mouse-press event.
        const auto visual = visual_rows_[static_cast<std::size_t>(row_index)];
        const auto row = rows_[visual.layer_index];
        if (visual.kind == VisualTimelineRow::Kind::Layer && position.x() < 29) {
            if (layer_visibility) layer_visibility(row.id, !row.visible);
            event->accept();
            return;
        }
        if (layer_selected) layer_selected(row.id);
        interaction_layer_id_ = row.id;
        press_position_ = position;
        if(visual.kind==VisualTimelineRow::Kind::PathProperty){
            const auto& path=row.paths[visual.path_index];const int center=row_index*kTimelineRowHeight+kTimelineRowHeight/2;
            for(const auto& key:path.keys())if(std::abs(position.x()-viewMapping().xForFrame(path.start+key.frame))<=8&&std::abs(position.y()-center)<=9){interaction_mode_=InteractionMode::MovePathKeyframe;interaction_path_index_=visual.path_index;interaction_keyframe_frame_=preview_value_=key.frame;event->accept();return;}
            if(path_selected&&!path.keys().empty())path_selected(row.id,path.target,std::max<std::int64_t>(0,frameAtX(position.x())-path.start));interaction_mode_=InteractionMode::None;interaction_layer_id_=0;event->accept();return;
        }
        if (visual.kind == VisualTimelineRow::Kind::Property) {
            const auto property = *visual.property;
            const int lane_center_y = row_index * kTimelineRowHeight + kTimelineRowHeight / 2;
            const auto& keyframes = trackKeys(row, property);
            for (std::size_t key_index = 0; key_index < keyframes.size(); ++key_index) {
                const auto& keyframe = keyframes[key_index];
                if (keyframe.frame < 0 || keyframe.frame >= row.duration_frames) continue;
                const int marker_x = viewMapping().xForFrame(row.start_frame + keyframe.frame);
                if (std::abs(position.x() - marker_x) <= 8 &&
                    std::abs(position.y() - lane_center_y) <= 9) {
                    if ((curve_segment_selected || anchor_curve_segment_selected || scale_axis_curve_segment_selected) && keyframes.size() >= 2) {
                        const auto segment_index = std::min(key_index, keyframes.size() - 2);
                        selectTrackCurve(row.id, property,
                                               keyframes[segment_index].frame);
                    }
                    interaction_mode_ = InteractionMode::MoveKeyframe;
                    interaction_property_ = property;
                    interaction_keyframe_frame_ = keyframe.frame;
                    preview_value_ = keyframe.frame;
                    event->accept();
                    return;
                }
            }
            if ((curve_segment_selected || anchor_curve_segment_selected || scale_axis_curve_segment_selected) && keyframes.size() >= 2) {
                const auto clicked_local_frame = std::max<std::int64_t>(0,
                    frameAtX(position.x()) - row.start_frame);
                const auto upper = std::upper_bound(keyframes.begin(), keyframes.end(),
                    clicked_local_frame, [](std::int64_t frame, const auto& keyframe) {
                        return frame < keyframe.frame;
                    });
                const auto segment_index = upper == keyframes.begin()
                    ? std::size_t{0}
                    : std::min(static_cast<std::size_t>(
                        std::distance(keyframes.begin(), upper) - 1), keyframes.size() - 2);
                selectTrackCurve(row.id, property,
                                       keyframes[segment_index].frame);
            }
            interaction_mode_ = InteractionMode::None;
            interaction_layer_id_ = 0;
            event->accept();
            return;
        }
        if (visual.kind == VisualTimelineRow::Kind::EffectProperty &&
            visual.effect_index < row.effects.size()) {
            const auto effect_index = visual.effect_index;
            const auto effect_property = *visual.effect_property;
            const int lane_center_y = row_index * kTimelineRowHeight + kTimelineRowHeight / 2;
            const auto* keyframes_ptr = model::effectKeyframesFor(
                row.effects[effect_index], effect_property);
            if (keyframes_ptr != nullptr) {
                const auto& keyframes = *keyframes_ptr;
                for (std::size_t key_index = 0; key_index < keyframes.size(); ++key_index) {
                    const auto& keyframe = keyframes[key_index];
                    if (keyframe.frame < 0 || keyframe.frame >= row.duration_frames) continue;
                    const int marker_x = viewMapping().xForFrame(row.start_frame + keyframe.frame);
                    if (std::abs(position.x() - marker_x) <= 8 &&
                        std::abs(position.y() - lane_center_y) <= 9) {
                        if (effect_curve_segment_selected && keyframes.size() >= 2) {
                            const auto segment_index = std::min(key_index, keyframes.size() - 2);
                            effect_curve_segment_selected(row.id,
                                                          effect_index,
                                                          effect_property,
                                                          keyframes[segment_index].frame);
                        }
                        interaction_mode_ = InteractionMode::MoveEffectKeyframe;
                        interaction_effect_index_ = effect_index;
                        interaction_effect_property_ = effect_property;
                        interaction_keyframe_frame_ = keyframe.frame;
                        preview_value_ = keyframe.frame;
                        event->accept();
                        return;
                    }
                }
                if (effect_curve_segment_selected && keyframes.size() >= 2) {
                    const auto clicked_local_frame = std::max<std::int64_t>(0,
                        frameAtX(position.x()) - row.start_frame);
                    const auto upper = std::upper_bound(keyframes.begin(), keyframes.end(),
                        clicked_local_frame, [](std::int64_t frame, const auto& keyframe) {
                            return frame < keyframe.frame;
                        });
                    const auto segment_index = upper == keyframes.begin()
                        ? std::size_t{0}
                        : std::min(static_cast<std::size_t>(
                            std::distance(keyframes.begin(), upper) - 1), keyframes.size() - 2);
                    effect_curve_segment_selected(row.id,
                                                  effect_index,
                                                  effect_property,
                                                  keyframes[segment_index].frame);
                }
            }
            interaction_mode_ = InteractionMode::None;
            interaction_layer_id_ = 0;
            event->accept();
            return;
        }
        if (visual.kind == VisualTimelineRow::Kind::Layer && position.x() < 48) {
            setLayerExpanded(row.id, !expanded_layers_.contains(row.id));
            interaction_layer_id_ = 0;
            event->accept();
            return;
        }
        if (visual.kind == VisualTimelineRow::Kind::TransformGroup) {
            if (position.x() < 64) {
                setTransformGroupExpanded(
                    row.id, !expanded_transform_groups_.contains(row.id));
            }
            interaction_layer_id_ = 0;
            event->accept();
            return;
        }
        if (visual.kind == VisualTimelineRow::Kind::EffectsGroup) {
            if (position.x() < 64) {
                setEffectsGroupExpanded(
                    row.id, !expanded_effects_groups_.contains(row.id));
            }
            interaction_layer_id_ = 0;
            event->accept();
            return;
        }
        if (press_position_.x() < kTimelineHeaderWidth) {
            interaction_mode_ = InteractionMode::Reorder;
            target_row_ = static_cast<int>(visual.layer_index);
        } else {
            const auto end_frame = row.start_frame > kMaximumFrame - row.duration_frames
                ? kMaximumFrame : row.start_frame + row.duration_frames;
            const int right = viewMapping().xForFrame(end_frame);
            const int left = viewMapping().xForFrame(row.start_frame);
            const int visible_right = std::max(left + 2, right);
            if (press_position_.x() < left || press_position_.x() > visible_right) {
                event->accept();
                return;
            }
            const int clip_width = visible_right - left;
            const int handle_width = std::min(kTimelineClipHandleWidth,
                                                std::max(1, clip_width / 3));
            interaction_mode_ = press_position_.x() >= visible_right - handle_width
                ? InteractionMode::Resize : InteractionMode::Move;
            grabbed_frame_offset_ = frameAtX(press_position_.x()) - row.start_frame;
            preview_value_ = interaction_mode_ == InteractionMode::Move
                ? row.start_frame : row.duration_frames;
        }
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        const int x = event->position().toPoint().x();
        const int y = event->position().toPoint().y();
        if ((event->buttons() & Qt::LeftButton) != 0 &&
            interaction_mode_ != InteractionMode::None) {
            if (interaction_mode_ == InteractionMode::Reorder) {
                const int visual_index = rowAtY(y);
                if (visual_index >= 0) {
                    target_row_ = static_cast<int>(
                        visual_rows_[static_cast<std::size_t>(visual_index)].layer_index);
                }
            } else if(interaction_mode_==InteractionMode::MovePathKeyframe){
                if(const auto* row=findRow(interaction_layer_id_);row&&interaction_path_index_<row->paths.size()){const auto& path=row->paths[interaction_path_index_];preview_value_=std::clamp(frameAtX(x)-path.start,std::int64_t(0),path.duration-1);}
            } else if (interaction_mode_ == InteractionMode::MoveKeyframe ||
                       interaction_mode_ == InteractionMode::MoveEffectKeyframe) {
                if (const auto* row = findRow(interaction_layer_id_);
                    row != nullptr && row->duration_frames > 0) {
                    const auto composition_frame = frameAtX(x);
                    preview_value_ = std::clamp(
                        composition_frame - row->start_frame,
                        std::int64_t{0}, row->duration_frames - 1);
                }
            } else if (const auto* row = findRow(interaction_layer_id_); row != nullptr) {
                if (interaction_mode_ == InteractionMode::Move) {
                    const auto raw = std::max<std::int64_t>(0,
                        frameAtX(x) - grabbed_frame_offset_);
                    preview_value_ = snappedFrame(raw, interaction_layer_id_);
                } else {
                    auto boundary = snappedFrame(frameAtX(x), interaction_layer_id_);
                    auto duration = std::max<std::int64_t>(1, boundary - row->start_frame);
                    if (row->maximum_duration_frames > 0) {
                        duration = std::min(duration, row->maximum_duration_frames);
                    }
                    duration = std::min(duration,
                        kMaximumFrame - row->start_frame);
                    preview_value_ = std::max<std::int64_t>(1, duration);
                }
            }
            update();
            event->accept();
            return;
        }
        if (interaction_mode_ == InteractionMode::None) {
            const int row_index = rowAtY(y);
            if (row_index >= 0 &&
                visual_rows_[static_cast<std::size_t>(row_index)].kind ==
                    VisualTimelineRow::Kind::Layer &&
                x >= kTimelineHeaderWidth) {
                const auto& row = rows_[visual_rows_[static_cast<std::size_t>(row_index)].layer_index];
                const auto end_frame = row.start_frame > kMaximumFrame - row.duration_frames
                    ? kMaximumFrame : row.start_frame + row.duration_frames;
                const int left = viewMapping().xForFrame(row.start_frame);
                const int right = viewMapping().xForFrame(end_frame);
                const int visible_right = std::max(left + 2, right);
                if (x < left || x > visible_right) {
                    unsetCursor();
                } else {
                    const int clip_width = visible_right - left;
                    const int handle_width = std::min(kTimelineClipHandleWidth,
                                                        std::max(1, clip_width / 3));
                    setCursor(x >= visible_right - handle_width
                        ? Qt::SizeHorCursor : Qt::OpenHandCursor);
                }
            } else {
                unsetCursor();
            }
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || interaction_mode_ == InteractionMode::None) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        const auto id = interaction_layer_id_;
        const auto mode = interaction_mode_;
        const auto value = preview_value_;
        const auto target = target_row_;
        const auto property = interaction_property_;
        const auto effect_index = interaction_effect_index_;
        const auto effect_property = interaction_effect_property_;
        const auto old_keyframe = interaction_keyframe_frame_;
        const bool moved = (event->position().toPoint() - press_position_).manhattanLength() >= 4;
        interaction_mode_ = InteractionMode::None;
        interaction_layer_id_ = 0;
        update();
        if (mode == InteractionMode::Reorder && moved && target >= 0 && layer_reorder) {
            layer_reorder(id, static_cast<std::size_t>(target));
        } else if (mode == InteractionMode::Move && moved && layer_move) {
            layer_move(id, value);
        } else if (mode == InteractionMode::Resize && moved && layer_resize) {
            layer_resize(id, value);
        } else if(mode==InteractionMode::MovePathKeyframe){
            if(const auto* row=findRow(id);row&&interaction_path_index_<row->paths.size()){const auto path=row->paths[interaction_path_index_];const auto final=std::clamp(frameAtX(event->position().toPoint().x())-path.start,std::int64_t(0),path.duration-1);if(moved&&final!=old_keyframe){if(path_moved)(void)path_moved(id,path.target,old_keyframe,final);}else if(path_selected)path_selected(id,path.target,old_keyframe);}
        } else if (mode == InteractionMode::MoveKeyframe) {
            if (moved && value != old_keyframe) {
                moveTrackKey(id, property, old_keyframe, value);
            } else {
                selectTrackKey(id, property, old_keyframe);
            }
        } else if (mode == InteractionMode::MoveEffectKeyframe) {
            if (moved && value != old_keyframe) {
                if (effect_keyframe_move) {
                    effect_keyframe_move(id, effect_index, effect_property, old_keyframe, value);
                }
            } else if (effect_keyframe_selected) {
                effect_keyframe_selected(id, effect_index, effect_property, old_keyframe);
            }
        }
        event->accept();
    }

    void wheelEvent(QWheelEvent* event) override
    {
        if (event->modifiers().testFlag(Qt::ControlModifier)) {
            const int delta = event->angleDelta().y() != 0
                ? event->angleDelta().y() : event->pixelDelta().y();
            if (delta != 0 && zoom_step_requested) {
                zoom_step_requested(delta > 0 ? 1 : -1);
                event->accept();
                return;
            }
        }
        event->ignore();
    }

    void keyPressEvent(QKeyEvent* event) override
    {
        if (event->key() == Qt::Key_Delete && selected_layer_id_ != 0 && layer_remove) {
            layer_remove(selected_layer_id_);
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void dragEnterEvent(QDragEnterEvent* event) override
    {
        if (event->mimeData()->hasFormat(kMotionMediaMimeType) && media_drop) {
            event->acceptProposedAction();
            drag_preview_active_ = true;
            const int x = event->position().toPoint().x();
            drag_preview_x_ = viewMapping().xForFrame(snappedFrame(frameAtX(x), 0));
            drag_preview_y_ = event->position().toPoint().y();
            update();
            return;
        }
        event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        if (!event->mimeData()->hasFormat(kMotionMediaMimeType) || !media_drop) {
            event->ignore();
            return;
        }
        const auto frame = snappedFrame(frameAtX(event->position().toPoint().x()), 0);
        drag_preview_x_ = viewMapping().xForFrame(frame);
        drag_preview_y_ = event->position().toPoint().y();
        drag_preview_active_ = true;
        update();
        event->acceptProposedAction();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override
    {
        drag_preview_active_ = false;
        update();
        event->accept();
    }

    void dropEvent(QDropEvent* event) override
    {
        if (!event->mimeData()->hasFormat(kMotionMediaMimeType) || !media_drop) {
            event->ignore();
            return;
        }
        const auto position = event->position().toPoint();
        const auto frame = snappedFrame(frameAtX(position.x()), 0);
        const int row_index = rowAtY(position.y());
        const auto before = row_index >= 0
            ? rows_[visual_rows_[static_cast<std::size_t>(row_index)].layer_index].id
            : model::LayerId{0};
        const auto path = pathFromQString(QString::fromUtf8(
            event->mimeData()->data(kMotionMediaMimeType)));
        drag_preview_active_ = false;
        update();
        media_drop(path, frame, before);
        event->acceptProposedAction();
    }

private:
    enum class InteractionMode {
        None,
        Move,
        Resize,
        Reorder,
        MoveKeyframe,
        MoveEffectKeyframe,
        MovePathKeyframe,
    };

    [[nodiscard]] int rowAtY(int y) const noexcept
    {
        if (y < 0) return -1;
        const auto index = y / kTimelineRowHeight;
        return index >= 0 && static_cast<std::size_t>(index) < visual_rows_.size() ? index : -1;
    }

    [[nodiscard]] const TimelineLayerRow* findRow(model::LayerId id) const noexcept
    {
        const auto found = std::find_if(rows_.begin(), rows_.end(), [id](const TimelineLayerRow& row) {
            return row.id == id;
        });
        return found == rows_.end() ? nullptr : &*found;
    }

    void rebuildVisualRows()
    {
        pick_whip->cancel();
        visual_rows_.clear();
        visual_rows_.reserve(rows_.size() * 8);
        for (std::size_t index = 0; index < rows_.size(); ++index) {
            const auto layer_id = rows_[index].id;
            visual_rows_.push_back({index, VisualTimelineRow::Kind::Layer, std::nullopt});
            if (!expanded_layers_.contains(layer_id)) continue;
            for(std::size_t i=0;i<rows_[index].paths.size();++i){VisualTimelineRow path{index,VisualTimelineRow::Kind::PathProperty};path.path_index=i;visual_rows_.push_back(path);}
            visual_rows_.push_back(
                {index, VisualTimelineRow::Kind::TransformGroup, std::nullopt});
            if (expanded_transform_groups_.contains(layer_id)) {
                for (const auto property : kTimelineProperties) {
                    if (rows_[index].kind == model::LayerKind::Null && property == TrackProperty::Opacity) continue;
                    visual_rows_.push_back({index, VisualTimelineRow::Kind::Property, property});
                }
            }
            if (!rows_[index].effects.empty()) {
                visual_rows_.push_back(
                    {index, VisualTimelineRow::Kind::EffectsGroup, std::nullopt});
                if (expanded_effects_groups_.contains(layer_id)) {
                    for (std::size_t effect_index = 0;
                         effect_index < rows_[index].effects.size();
                         ++effect_index) {
                        const auto& effect = rows_[index].effects[effect_index];
                        if (std::holds_alternative<model::GaussianBlurEffect>(effect)) {
                            visual_rows_.push_back({
                                index,
                                VisualTimelineRow::Kind::EffectProperty,
                                std::nullopt,
                                effect_index,
                                model::EffectParameterProperty::GaussianBlurRadius});
                        } else if (std::holds_alternative<model::ColorAdjustmentEffect>(effect)) {
                            visual_rows_.push_back({
                                index,
                                VisualTimelineRow::Kind::EffectProperty,
                                std::nullopt,
                                effect_index,
                                model::EffectParameterProperty::ColorBrightness});
                            visual_rows_.push_back({
                                index,
                                VisualTimelineRow::Kind::EffectProperty,
                                std::nullopt,
                                effect_index,
                                model::EffectParameterProperty::ColorContrastPercent});
                            visual_rows_.push_back({
                                index,
                                VisualTimelineRow::Kind::EffectProperty,
                                std::nullopt,
                                effect_index,
                                model::EffectParameterProperty::ColorSaturationPercent});
                        }
                    }
                }
            }
        }
        const auto row_count = static_cast<int>(visual_rows_.size());
        const int content_height = std::max(kTimelineRowHeight, row_count * kTimelineRowHeight);
        setMinimumHeight(content_height);
        resize(width(), content_height);
        updateGeometry();
        update();
    }

    [[nodiscard]] TimelineViewMapping viewMapping() const noexcept
    {
        return {width(), kTimelineHeaderWidth, visible_end_frame_,
                view_start_frame_, frames_per_view_};
    }

    [[nodiscard]] std::int64_t frameAtX(int x) const noexcept
    {
        return viewMapping().frameAtX(x);
    }

    [[nodiscard]] std::int64_t snappedFrame(
        std::int64_t frame,
        model::LayerId excluded) const noexcept
    {
        frame = std::clamp(frame, std::int64_t{0}, visible_end_frame_);
        const auto mapping = viewMapping();
        const int span = std::max(1, mapping.axisWidth());
        const auto threshold = std::max<std::int64_t>(1,
            static_cast<std::int64_t>(std::ceil(
                static_cast<long double>(kTimelineSnapTolerancePixels) *
                static_cast<long double>(frames_per_view_ - 1) / span)));
        auto snapped = frame;
        auto best_distance = std::numeric_limits<std::int64_t>::max();
        const auto consider = [&](std::int64_t candidate) {
            candidate = std::clamp(candidate, std::int64_t{0}, visible_end_frame_);
            const auto distance = candidate > frame ? candidate - frame : frame - candidate;
            if (distance <= threshold && distance < best_distance) {
                best_distance = distance;
                snapped = candidate;
            }
        };
        consider(0);
        for (const auto& row : rows_) {
            if (row.id == excluded) continue;
            consider(row.start_frame);
            if (row.start_frame <= kMaximumFrame - row.duration_frames)
                consider(row.start_frame + row.duration_frames);
        }
        return snapped;
    }

    [[nodiscard]] QString parentLabel(model::LayerId id) const
    {
        if (!id) return MotionUiTranslation::tr("No parent");
        const auto found = parent_names_.find(id);
        return MotionUiTranslation::tr("%1 [#%2]").arg(found == parent_names_.end() ? QString() : found->second,QString::number(id));
    }

    [[nodiscard]] std::optional<ParentPickWhip::Hit> parentHit(QPointF point) const
    {
        const auto visual_index = rowAtY(static_cast<int>(std::floor(point.y())));
        if (visual_index < 0) return {};
        const auto& visual = visual_rows_[static_cast<std::size_t>(visual_index)];
        if (visual.kind != VisualTimelineRow::Kind::Layer) return {};
        const auto& row = rows_[visual.layer_index];
        return ParentPickWhip::Hit{row.id,row.name,QRectF(0,visual_index*kTimelineRowHeight,width(),kTimelineRowHeight)};
    }

    [[nodiscard]] QString parentTooltip(QPointF point) const
    {
        const auto hit = parentHit(point);
        if (!hit || point.x() < 48 || point.x() >= kTimelineHeaderWidth) return {};
        const auto* row = findRow(hit->id);
        const auto name = MotionUiTranslation::tr("%1 [#%2]").arg(row->name,QString::number(row->id));
        return MotionUiTranslation::tr("Layer: %1\nParent & Link: %2").arg(name,parentLabel(row->parent_id)) + QLatin1Char('\n') +
            MotionUiTranslation::tr("Drag to choose a parent. Release outside a layer to cancel.");
    }

    void paintParentControl(QPainter& painter,const TimelineLayerRow& row,QRectF bounds) const
    {
        const auto icon = ParentPickWhip::iconRect(bounds);
        painter.setPen(QPen(QColor(190,199,210),1.5));painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(icon.center()-QPointF(2,0),4,4);
        painter.drawLine(icon.center()+QPointF(2,0),icon.center()+QPointF(7,-5));
        painter.drawLine(icon.center()+QPointF(7,-5),icon.center()+QPointF(3,-5));
        const QRectF label(74,bounds.top(),kTimelineHeaderWidth-78,kTimelineRowHeight);
        const auto text = painter.fontMetrics().elidedText(parentLabel(row.parent_id),Qt::ElideRight,static_cast<int>(label.width()));
        painter.drawText(label,Qt::AlignVCenter,text);
    }

    std::unordered_map<model::LayerId,QString> parent_names_;
    std::vector<TimelineLayerRow> rows_;
    std::vector<VisualTimelineRow> visual_rows_;
    std::unordered_set<model::LayerId> expanded_layers_;
    std::unordered_set<model::LayerId> expanded_transform_groups_;
    std::unordered_set<model::LayerId> expanded_effects_groups_;
    std::int64_t visible_end_frame_ = 0;
    std::int64_t current_frame_ = 0;
    std::int64_t view_start_frame_ = 0;
    std::int64_t frames_per_view_ = 1;
    model::LayerId selected_layer_id_ = 0;
    InteractionMode interaction_mode_ = InteractionMode::None;
    model::LayerId interaction_layer_id_ = 0;
    std::size_t interaction_path_index_=0;
    TrackProperty interaction_property_ = TrackProperty::PositionX;
    std::size_t interaction_effect_index_ = 0;
    EffectParameterProperty interaction_effect_property_ =
        EffectParameterProperty::GaussianBlurRadius;
    std::int64_t interaction_keyframe_frame_ = 0;
    QPoint press_position_;
    std::int64_t grabbed_frame_offset_ = 0;
    std::int64_t preview_value_ = 0;
    int target_row_ = -1;
    bool drag_preview_active_ = false;
    int drag_preview_x_ = 0;
    int drag_preview_y_ = 0;
};


} // namespace

class TimelineLayerTracks::Impl {
public:
    explicit Impl(TimelineLayerTracks* owner)
    {
        auto* layout = new QVBoxLayout(owner);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        canvas = new LayerRowsCanvas(owner);
        layout->addWidget(canvas);
    }

    LayerRowsCanvas* canvas = nullptr;
};

TimelineLayerTracks::TimelineLayerTracks(QWidget* parent)
    : QWidget(parent), impl_(std::make_unique<Impl>(this))
{
    setObjectName(QStringLiteral("motion-timeline-layer-tracks"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
}

TimelineLayerTracks::~TimelineLayerTracks() = default;

void TimelineLayerTracks::setRows(std::vector<TimelineLayerRow> rows)
{ impl_->canvas->setRows(std::move(rows)); }
void TimelineLayerTracks::setLayerExpanded(model::LayerId id, bool expanded)
{ impl_->canvas->setLayerExpanded(id, expanded); }
void TimelineLayerTracks::setTransformGroupExpanded(model::LayerId id, bool expanded)
{ impl_->canvas->setTransformGroupExpanded(id, expanded); }
void TimelineLayerTracks::setEffectsGroupExpanded(model::LayerId id, bool expanded)
{ impl_->canvas->setEffectsGroupExpanded(id, expanded); }
void TimelineLayerTracks::collapseAllLayerTracks()
{ impl_->canvas->collapseAllLayerTracks(); }
void TimelineLayerTracks::setViewState(std::int64_t end, std::int64_t current,
                                        std::int64_t start, std::int64_t span)
{ impl_->canvas->setViewState(end, current, start, span); }
void TimelineLayerTracks::setSelectedLayerId(model::LayerId id)
{ impl_->canvas->setSelectedLayerId(id); }
void TimelineLayerTracks::setParentPickWhipHandlers(ParentPickWhipHandlers handlers)
{ impl_->canvas->pick_whip->setHandlers(std::move(handlers)); }
void TimelineLayerTracks::setParentPickWhipScrollArea(QScrollArea* area)
{ impl_->canvas->pick_whip->setScrollArea(area); }
void TimelineLayerTracks::cancelParentPickWhip()
{ impl_->canvas->pick_whip->cancel(); }
bool TimelineLayerTracks::parentPickWhipActive() const noexcept
{ return impl_->canvas->pick_whip->active(); }

void TimelineLayerTracks::setMediaDropHandler(
    std::function<void(const std::filesystem::path&, std::int64_t, model::LayerId)> handler)
{ impl_->canvas->media_drop = std::move(handler); }
void TimelineLayerTracks::setLayerSelectedHandler(std::function<void(model::LayerId)> handler)
{ impl_->canvas->layer_selected = std::move(handler); }
void TimelineLayerTracks::setLayerContextMenuHandler(
    std::function<void(model::LayerId, const QPoint&)> handler)
{ impl_->canvas->layer_context_requested = std::move(handler); }
void TimelineLayerTracks::setLayerMoveHandler(
    std::function<void(model::LayerId, std::int64_t)> handler)
{ impl_->canvas->layer_move = std::move(handler); }
void TimelineLayerTracks::setLayerResizeHandler(
    std::function<void(model::LayerId, std::int64_t)> handler)
{ impl_->canvas->layer_resize = std::move(handler); }
void TimelineLayerTracks::setLayerReorderHandler(
    std::function<void(model::LayerId, std::size_t)> handler)
{ impl_->canvas->layer_reorder = std::move(handler); }
void TimelineLayerTracks::setLayerVisibilityHandler(
    std::function<void(model::LayerId, bool)> handler)
{ impl_->canvas->layer_visibility = std::move(handler); }
void TimelineLayerTracks::setLayerRemoveHandler(std::function<void(model::LayerId)> handler)
{ impl_->canvas->layer_remove = std::move(handler); }
void TimelineLayerTracks::setKeyframeSelectedHandler(
    std::function<void(model::LayerId, creative_suite::animation::TransformProperty,
                       std::int64_t)> handler)
{ impl_->canvas->keyframe_selected = std::move(handler); }
void TimelineLayerTracks::setKeyframeMoveHandler(
    std::function<bool(model::LayerId, creative_suite::animation::TransformProperty,
                       std::int64_t, std::int64_t)> handler)
{ impl_->canvas->keyframe_move = std::move(handler); }
void TimelineLayerTracks::setCurveSegmentSelectedHandler(
    std::function<void(model::LayerId, creative_suite::animation::TransformProperty,
                       std::int64_t)> handler)
{ impl_->canvas->curve_segment_selected = std::move(handler); }
void TimelineLayerTracks::setEffectKeyframeSelectedHandler(
    std::function<void(model::LayerId, std::size_t, model::EffectParameterProperty,
                       std::int64_t)> handler)
{ impl_->canvas->effect_keyframe_selected = std::move(handler); }
void TimelineLayerTracks::setEffectKeyframeMoveHandler(
    std::function<bool(model::LayerId, std::size_t, model::EffectParameterProperty,
                       std::int64_t, std::int64_t)> handler)
{ impl_->canvas->effect_keyframe_move = std::move(handler); }
void TimelineLayerTracks::setEffectCurveSegmentSelectedHandler(
    std::function<void(model::LayerId, std::size_t, model::EffectParameterProperty,
                       std::int64_t)> handler)
{ impl_->canvas->effect_curve_segment_selected = std::move(handler); }
void TimelineLayerTracks::setZoomStepHandler(std::function<void(int)> handler)
{ impl_->canvas->zoom_step_requested = std::move(handler); }
void TimelineLayerTracks::setViewportWidthChangedHandler(std::function<void(int)> handler)
{ impl_->canvas->viewport_width_changed = std::move(handler); }

void TimelineLayerTracks::setAnchorKeyframeSelectedHandler(std::function<void(model::LayerId, model::AnchorProperty, std::int64_t)> handler)
{ impl_->canvas->anchor_keyframe_selected = std::move(handler); }
void TimelineLayerTracks::setAnchorKeyframeMoveHandler(std::function<bool(model::LayerId, model::AnchorProperty, std::int64_t, std::int64_t)> handler)
{ impl_->canvas->anchor_keyframe_move = std::move(handler); }
void TimelineLayerTracks::setAnchorCurveSegmentSelectedHandler(std::function<void(model::LayerId, model::AnchorProperty, std::int64_t)> handler)
{ impl_->canvas->anchor_curve_segment_selected = std::move(handler); }

void TimelineLayerTracks::setScaleAxisKeyframeSelectedHandler(std::function<void(model::LayerId, model::ScaleAxis, std::int64_t)> handler)
{ impl_->canvas->scale_axis_keyframe_selected = std::move(handler); }
void TimelineLayerTracks::setPathKeyframeHandlers(std::function<void(model::LayerId,model::PathTarget,std::int64_t)> selected,std::function<bool(model::LayerId,model::PathTarget,std::int64_t,std::int64_t)> moved){impl_->canvas->path_selected=std::move(selected);impl_->canvas->path_moved=std::move(moved);}
void TimelineLayerTracks::setScaleAxisKeyframeMoveHandler(std::function<bool(model::LayerId, model::ScaleAxis, std::int64_t, std::int64_t)> handler)
{ impl_->canvas->scale_axis_keyframe_move = std::move(handler); }
void TimelineLayerTracks::setScaleAxisCurveSegmentSelectedHandler(std::function<void(model::LayerId, model::ScaleAxis, std::int64_t)> handler)
{ impl_->canvas->scale_axis_curve_segment_selected = std::move(handler); }

} // namespace motion::ui

