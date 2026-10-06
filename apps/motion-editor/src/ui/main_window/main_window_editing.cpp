#include "main_window.h"
#include "main_window_support.h"

#include "ui/viewer/composition_viewer.h"
#include "ui/inspector/inspector_widget.h"
#include "ui/media_pool/media_pool_widget.h"
#include "ui/timeline/graph_editor/property_curve_editor.h"
#include "ui/timeline/timeline_navigator.h"

#include <creative_suite/animation/animation.h>
#include <creative_suite/diagnostics/logger.h>
#include <creative_suite/media/media_library.h>

#include <QColor>
#include <QColorDialog>
#include <QComboBox>
#include <QLabel>
#include <QMessageBox>
#include <QStatusBar>
#include <QSignalBlocker>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace motion::ui {
using detail::pathForLog;

namespace {

QColor qColor(const model::ColorRgba& color)
{
    return QColor(color[0], color[1], color[2], color[3]);
}

model::ColorRgba modelColor(const QColor& color)
{
    return {static_cast<std::uint8_t>(color.red()),
            static_cast<std::uint8_t>(color.green()),
            static_cast<std::uint8_t>(color.blue()),
            static_cast<std::uint8_t>(color.alpha())};
}

std::string utf8String(const QString& value)
{
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

using TransformProperty = creative_suite::animation::TransformProperty;
using InterpolationMode = creative_suite::animation::InterpolationMode;
using CubicBezierEasing = creative_suite::animation::CubicBezierEasing;

constexpr std::array<TransformProperty, 5> kTransformProperties{{
    TransformProperty::PositionX,
    TransformProperty::PositionY,
    TransformProperty::Scale,
    TransformProperty::Rotation,
    TransformProperty::Opacity,
}};

std::pair<InterpolationMode, CubicBezierEasing> easingForPreset(int preset_index)
{
    switch (preset_index) {
    case 1: return {InterpolationMode::CubicBezier, {0.42, 0.0, 1.0, 1.0}};
    case 2: return {InterpolationMode::CubicBezier, {0.0, 0.0, 0.58, 1.0}};
    case 3: return {InterpolationMode::CubicBezier, {0.42, 0.0, 0.58, 1.0}};
    default: return {InterpolationMode::Linear, {}};
    }
}

int curvePresetIndex(const creative_suite::animation::Keyframe& keyframe)
{
    if (keyframe.interpolation == InterpolationMode::Linear) return 0;
    const std::array<CubicBezierEasing, 3> presets{{
        {0.42, 0.0, 1.0, 1.0},
        {0.0, 0.0, 0.58, 1.0},
        {0.42, 0.0, 0.58, 1.0},
    }};
    for (std::size_t index = 0; index < presets.size(); ++index) {
        if (keyframe.easing == presets[index]) return static_cast<int>(index) + 1;
    }
    return -1;
}

double transformPropertyValue(
    const creative_suite::animation::Transform2D& transform,
    TransformProperty property) noexcept
{
    switch (property) {
    case TransformProperty::PositionX: return transform.position_x;
    case TransformProperty::PositionY: return transform.position_y;
    case TransformProperty::Scale: return transform.scale;
    case TransformProperty::Rotation: return transform.rotation_degrees;
    case TransformProperty::Opacity: return transform.opacity;
    }
    return 0.0;
}

void setTransformPropertyValue(
    creative_suite::animation::Transform2D& transform,
    TransformProperty property,
    double value) noexcept
{
    switch (property) {
    case TransformProperty::PositionX: transform.position_x = value; break;
    case TransformProperty::PositionY: transform.position_y = value; break;
    case TransformProperty::Scale: transform.scale = value; break;
    case TransformProperty::Rotation: transform.rotation_degrees = value; break;
    case TransformProperty::Opacity: transform.opacity = value; break;
    }
}

bool containsKeyframeAt(
    const creative_suite::animation::TransformKeyframes& keyframes,
    TransformProperty property,
    std::int64_t local_frame) noexcept
{
    const auto& frames = creative_suite::animation::keyframesFor(keyframes, property);
    const auto found = std::lower_bound(
        frames.begin(), frames.end(), local_frame,
        [](const creative_suite::animation::Keyframe& keyframe, std::int64_t frame) {
            return keyframe.frame < frame;
        });
    return found != frames.end() && found->frame == local_frame;
}

} // namespace

CompositionEditState MainWindow::captureEditState() const
{
    if (!document_) throw std::logic_error("There is no open Motion Studio composition");
    return {*document_, selected_layer_id_};
}
bool MainWindow::recordCompositionEdit(CompositionEditState before)
{
    if (!document_) return false;
    const bool unchanged = before.document.canvasSize() == document_->canvasSize() &&
        before.document.frameRate() == document_->frameRate() &&
        before.document.layers() == document_->layers();
    if (unchanged) return false;
    composition_history_.recordBeforeEdit(std::move(before));
    updateHistoryActions();
    return true;
}
void MainWindow::finishPendingTransformEdit()
{
    if (active_transform_edit_.has_value()) {
        active_transform_edit_.reset();
        if (document_) {
            composition_history_.finishCoalescedEdit(captureEditState());
        }
        updateHistoryActions();
    }
    finishPendingEffectEdit();
    finishPendingContentEdit();
}
void MainWindow::finishPendingEffectEdit()
{
    if (!active_effect_edit_.has_value()) return;
    active_effect_edit_.reset();
    if (document_) {
        composition_history_.finishCoalescedEdit(captureEditState());
    }
    updateHistoryActions();
}
void MainWindow::finishPendingContentEdit()
{
    if (!active_content_edit_layer_.has_value()) return;
    active_content_edit_layer_.reset();
    if (document_) {
        composition_history_.finishCoalescedEdit(captureEditState());
    }
    updateHistoryActions();
}
void MainWindow::updateHistoryActions()
{
    const bool has_document = document_.has_value();
    if (undo_action_ != nullptr)
        undo_action_->setEnabled(has_document && composition_history_.canUndo());
    if (redo_action_ != nullptr)
        redo_action_->setEnabled(has_document && composition_history_.canRedo());
}
void MainWindow::undoComposition()
{
    finishPendingTransformEdit();
    if (!document_) return;
    auto state = composition_history_.undo(captureEditState());
    if (!state.has_value()) {
        updateHistoryActions();
        return;
    }
    applyEditState(std::move(*state));
}
void MainWindow::redoComposition()
{
    finishPendingTransformEdit();
    if (!document_) return;
    auto state = composition_history_.redo(captureEditState());
    if (!state.has_value()) {
        updateHistoryActions();
        return;
    }
    applyEditState(std::move(*state));
}
void MainWindow::applyEditState(CompositionEditState state)
{
    if (!document_) return;
    *document_ = std::move(state.document);
    const auto selected = std::find_if(
        document_->layers().begin(), document_->layers().end(),
        [&state](const auto& layer) { return layer.id == state.selected_layer_id; });
    selected_layer_id_ = selected == document_->layers().end() ? 0 : state.selected_layer_id;
    refreshTimeline();
    syncTransformInspector();
    updateDocumentState();
    requestPreview();
}
void MainWindow::selectCurveSegment(
    model::LayerId id,
    TransformProperty property,
    std::int64_t local_start_frame)
{
    if (!document_) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [id](const auto& layer) { return layer.id == id; });
    if (found == document_->layers().end()) return;
    const auto& keys = creative_suite::animation::keyframesFor(found->keyframes, property);
    std::int64_t segment_start = 0;
    if (keys.size() >= 2) {
        const auto upper = std::upper_bound(keys.begin(), keys.end(), local_start_frame,
            [](std::int64_t frame, const auto& keyframe) {
                return frame < keyframe.frame;
            });
        const auto segment_index = upper == keys.begin()
            ? std::size_t{0}
            : std::min(static_cast<std::size_t>(
                std::distance(keys.begin(), upper) - 1), keys.size() - 2);
        segment_start = keys[segment_index].frame;
    }
    curve_selection_ = CurveSelection{id, property, segment_start};
    refreshCurveEditor();
}
void MainWindow::refreshCurveEditor()
{
    if (curve_editor_ == nullptr) return;
    if (!document_ || !curve_selection_) {
        curve_editor_->setCurve(0, TransformProperty::PositionX, {}, std::nullopt);
        if (curve_preset_combo_ != nullptr) curve_preset_combo_->setEnabled(false);
        if (curve_custom_label_ != nullptr) curve_custom_label_->hide();
        return;
    }
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == curve_selection_->layer_id; });
    if (found == document_->layers().end()) {
        curve_selection_.reset();
        refreshCurveEditor();
        return;
    }
    const auto& keys = creative_suite::animation::keyframesFor(
        found->keyframes, curve_selection_->property);
    if (keys.size() >= 2 && std::none_of(keys.begin(), keys.end(), [this](const auto& keyframe) {
            return keyframe.frame == curve_selection_->segment_start_frame;
        })) {
        curve_selection_->segment_start_frame = keys.front().frame;
    }
    curve_editor_->setCurve(found->id, curve_selection_->property, keys,
        keys.size() >= 2
            ? std::optional<std::int64_t>(curve_selection_->segment_start_frame)
            : std::nullopt);
    const auto selected_key = std::lower_bound(keys.begin(), keys.end(),
        curve_selection_->segment_start_frame, [](const auto& item, std::int64_t frame) {
            return item.frame < frame;
        });
    const bool has_segment = selected_key != keys.end() &&
        selected_key->frame == curve_selection_->segment_start_frame &&
        std::next(selected_key) != keys.end();
    if (curve_preset_combo_ != nullptr) {
        const QSignalBlocker blocker(curve_preset_combo_);
        curve_preset_combo_->setEnabled(has_segment);
        if (has_segment) {
            const int preset_index = curvePresetIndex(*selected_key);
            curve_preset_combo_->setCurrentIndex(preset_index);
            if (curve_custom_label_ != nullptr)
                curve_custom_label_->setVisible(preset_index < 0);
        } else {
            curve_preset_combo_->setCurrentIndex(0);
            if (curve_custom_label_ != nullptr) curve_custom_label_->hide();
        }
    }
}
void MainWindow::applyCurvePreset(int preset_index)
{
    if (preset_index < 0 || preset_index > 3 || !document_ || !curve_selection_) return;
    finishPendingTransformEdit();
    const auto [interpolation, easing] = easingForPreset(preset_index);
    const auto before = captureEditState();
    if (!document_->setLayerKeyframeInterpolation(
            curve_selection_->layer_id, curve_selection_->property,
            curve_selection_->segment_start_frame, interpolation, easing)) {
        refreshCurveEditor();
        return;
    }
    (void)recordCompositionEdit(before);
    updateDocumentState();
    refreshTimeline();
    syncTransformInspector();
    requestPreview();
}
void MainWindow::applyCurveEasing(
    std::int64_t local_start_frame,
    CubicBezierEasing easing)
{
    if (!document_ || !curve_selection_) return;
    finishPendingTransformEdit();
    auto before = captureEditState();
    if (!document_->setLayerKeyframeInterpolation(
            curve_selection_->layer_id, curve_selection_->property,
            local_start_frame, InterpolationMode::CubicBezier, easing)) {
        refreshCurveEditor();
        return;
    }
    curve_selection_->segment_start_frame = local_start_frame;
    (void)recordCompositionEdit(std::move(before));
    updateDocumentState();
    refreshTimeline();
    syncTransformInspector();
    requestPreview();
}
void MainWindow::resetCurveEditor()
{
    curve_selection_.reset();
    refreshCurveEditor();
}
void MainWindow::selectLayer(model::LayerId id)
{
    finishPendingTransformEdit();
    if (!document_) return;
    const auto& layers = document_->layers();
    const auto found = std::find_if(layers.begin(), layers.end(), [id](const auto& layer) {
        return layer.id == id;
    });
    if (found == layers.end()) return;
    selected_layer_id_ = id;
    timeline_->setSelectedLayerId(id);
    if (!curve_selection_ || curve_selection_->layer_id != id) {
        selectCurveSegment(id, TransformProperty::PositionX, 0);
    } else {
        refreshCurveEditor();
    }
    syncTransformInspector();
    if (found->kind == model::LayerKind::Text || found->kind == model::LayerKind::Shape) {
        inspector_->selectLayerTab();
    } else {
        inspector_->selectTransformTab();
    }
    updateDocumentState();
}
void MainWindow::createContentLayer(model::LayerKind kind, model::ShapeKind shape)
{
    finishPendingTransformEdit();
    if (!document_ || timeline_ == nullptr) return;

    auto before = captureEditState();
    const std::size_t same_kind_count = static_cast<std::size_t>(std::count_if(
        document_->layers().begin(), document_->layers().end(),
        [kind, shape](const model::CompositionLayer& layer) {
            if (layer.kind != kind) return false;
            if (kind != model::LayerKind::Shape) return true;
            const auto* content = std::get_if<model::ShapeLayerContent>(&layer.content);
            return content != nullptr && content->shape == shape;
        }));
    QString name;
    if (kind == model::LayerKind::Text) {
        name = QStringLiteral("Text %1").arg(same_kind_count + 1);
    } else {
        name = QStringLiteral("%1 %2")
            .arg(shape == model::ShapeKind::Ellipse
                    ? QStringLiteral("Ellipse") : QStringLiteral("Rectangle"))
            .arg(same_kind_count + 1);
    }

    model::LayerId added_id = 0;
    if (!document_->addContentLayer(kind, utf8String(name), timeline_->currentFrame(), &added_id)) {
        statusBar()->showMessage(
            QStringLiteral("The layer could not be added at the current frame."), 4000);
        return;
    }
    if (kind == model::LayerKind::Shape && shape == model::ShapeKind::Ellipse) {
        auto layer = std::find_if(
            document_->layers().begin(), document_->layers().end(),
            [added_id](const model::CompositionLayer& item) { return item.id == added_id; });
        if (layer != document_->layers().end()) {
            auto content = std::get<model::ShapeLayerContent>(layer->content);
            content.shape = model::ShapeKind::Ellipse;
            (void)document_->setShapeLayerContent(added_id, content);
        }
    }
    selected_layer_id_ = added_id;
    (void)recordCompositionEdit(std::move(before));
    refreshTimeline();
    syncTransformInspector();
    inspector_->selectLayerTab();
    updateDocumentState();
    requestPreview();
}
void MainWindow::syncTransformInspector()
{
    const model::CompositionLayer* selected = nullptr;
    if (document_ && selected_layer_id_ != 0) {
        const auto& layers = document_->layers();
        const auto found = std::find_if(layers.begin(), layers.end(), [this](const auto& layer) {
            return layer.id == selected_layer_id_;
        });
        if (found != layers.end()) selected = &*found;
    }

    int effect_row = -1;
    if (selected_effect_.has_value() &&
        (selected == nullptr || selected_effect_->first != selected->id)) {
        selected_effect_.reset();
    }
    if (selected != nullptr && selected_effect_.has_value()) {
        if (selected_effect_->second < selected->effects.size()) {
            effect_row = static_cast<int>(selected_effect_->second);
        } else {
            selected_effect_.reset();
        }
    }
    if (selected != nullptr && effect_row < 0 && !selected->effects.empty()) {
        effect_row = 0;
        selected_effect_ = std::pair{selected->id, std::size_t{0}};
    }

    if (inspector_ != nullptr)
        inspector_->syncLayer(selected, timeline_ != nullptr ? timeline_->currentFrame() : 0,
                              effect_row);
    if (selected == nullptr) {
        if (viewer_ != nullptr) viewer_->setSelectedLayerAnchor(std::nullopt);
        return;
    }
    const auto frame = timeline_ != nullptr ? timeline_->currentFrame() : 0;
    const auto raw_local_frame = frame - selected->timeline_start_frame;
    const auto local_frame = selected->duration_frames > 0
        ? std::clamp(raw_local_frame, std::int64_t{0}, selected->duration_frames - 1)
        : std::int64_t{0};
    const auto evaluated = creative_suite::animation::evaluateTransform(
        selected->transform, selected->keyframes, local_frame);
    if (viewer_ != nullptr) viewer_->setSelectedLayerAnchor(selected->visible
        ? std::optional<QPointF>(QPointF(evaluated.position_x, evaluated.position_y))
        : std::nullopt);
}
void MainWindow::syncLayerContentInspector(const model::CompositionLayer*)
{
    syncTransformInspector();
}
void MainWindow::syncEffectsInspector(const model::CompositionLayer*)
{
    syncTransformInspector();
}
void MainWindow::syncSelectedEffectInspector(
    const model::CompositionLayer* selected, int selected_row)
{
    if (inspector_ != nullptr) inspector_->syncSelectedEffect(selected, selected_row);
}
void MainWindow::selectEffectRow(int row)
{
    if (!document_ || selected_layer_id_ == 0 || row < 0) {
        selected_effect_.reset();
        const auto found = document_ ? std::find_if(document_->layers().begin(),
            document_->layers().end(), [this](const auto& layer) {
                return layer.id == selected_layer_id_;
            }) : std::vector<model::CompositionLayer>::const_iterator{};
        syncSelectedEffectInspector(
            document_ && found != document_->layers().end() ? &*found : nullptr, -1);
        return;
    }
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end() ||
        static_cast<std::size_t>(row) >= found->effects.size()) {
        selected_effect_.reset();
        syncSelectedEffectInspector(found == document_->layers().end() ? nullptr : &*found, -1);
        return;
    }
    selected_effect_ = std::pair{selected_layer_id_, static_cast<std::size_t>(row)};
    syncSelectedEffectInspector(&*found, row);
}
void MainWindow::addLayerEffect(int kind)
{
    finishPendingTransformEdit();
    if (!document_ || selected_layer_id_ == 0 || (kind != 0 && kind != 1)) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end()) return;
    auto before = captureEditState();
    auto effects = found->effects;
    if (kind == 0) effects.emplace_back(model::GaussianBlurEffect{});
    else effects.emplace_back(model::ColorAdjustmentEffect{});
    const auto new_index = effects.size() - 1;
    if (!document_->setLayerEffects(selected_layer_id_, effects)) return;
    selected_effect_ = std::pair{selected_layer_id_, new_index};
    (void)recordCompositionEdit(std::move(before));
    syncEffectsInspector(&*found);
    inspector_->selectEffectsTab();
    updateDocumentState();
    requestPreview();
}
void MainWindow::applySelectedEffectStack(
    std::vector<model::LayerEffect> effects,
    bool coalesce_parameters)
{
    if (!document_ || selected_layer_id_ == 0 || !selected_effect_.has_value() ||
        selected_effect_->first != selected_layer_id_) return;
    const auto effect_identity = *selected_effect_;
    const bool coalescing = coalesce_parameters && active_effect_edit_ == effect_identity;
    if (!coalescing) finishPendingTransformEdit();

    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end() ||
        effect_identity.second >= effects.size() || effects == found->effects) return;

    std::optional<CompositionEditState> before;
    if (!coalescing) before.emplace(captureEditState());
    if (!document_->setLayerEffects(selected_layer_id_, effects)) {
        syncEffectsInspector(&*found);
        statusBar()->showMessage(
            QStringLiteral("The effect parameters are outside the supported range."), 4000);
        return;
    }
    if (coalesce_parameters) {
        if (before.has_value()) {
            composition_history_.beginCoalescedEdit(std::move(*before));
            active_effect_edit_ = effect_identity;
            updateHistoryActions();
        }
    } else {
        (void)recordCompositionEdit(std::move(*before));
    }
    syncEffectsInspector(&*found);
    updateDocumentState();
    requestPreview();
}
void MainWindow::editSelectedEffectParameters()
{
    if (!document_ || !selected_effect_.has_value() ||
        selected_effect_->first != selected_layer_id_) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end() || selected_effect_->second >= found->effects.size())
        return;
    auto effects = found->effects;
    effects[selected_effect_->second] = inspector_->editedEffect(
        std::move(effects[selected_effect_->second]));
    applySelectedEffectStack(std::move(effects), true);
}
void MainWindow::reorderEffectsFromList(std::size_t source_index, int insertion_row)
{
    finishPendingTransformEdit();
    if (!document_ || selected_layer_id_ == 0 || inspector_ == nullptr) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end() || source_index >= found->effects.size() ||
        inspector_->effectCount() != static_cast<int>(found->effects.size())) {
        if (found != document_->layers().end()) syncEffectsInspector(&*found);
        return;
    }

    const auto size = found->effects.size();
    auto destination_index = static_cast<std::size_t>(
        std::clamp(insertion_row, 0, inspector_->effectCount()));
    if (destination_index > source_index) --destination_index;
    if (destination_index >= size) destination_index = size - 1;
    if (destination_index == source_index) return;

    auto reordered = found->effects;
    auto moved_effect = std::move(reordered[source_index]);
    reordered.erase(reordered.begin() + static_cast<std::ptrdiff_t>(source_index));
    reordered.insert(reordered.begin() + static_cast<std::ptrdiff_t>(destination_index),
                     std::move(moved_effect));

    auto before = captureEditState();
    if (!document_->setLayerEffects(selected_layer_id_, reordered)) {
        syncEffectsInspector(&*found);
        return;
    }
    selected_effect_ = std::pair{selected_layer_id_, destination_index};
    (void)recordCompositionEdit(std::move(before));
    syncEffectsInspector(&*found);
    updateDocumentState();
    requestPreview();
}
void MainWindow::moveSelectedEffect(int direction)
{
    finishPendingTransformEdit();
    if (!document_ || !selected_effect_.has_value() || direction == 0) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end()) return;
    const auto from = selected_effect_->second;
    if ((direction < 0 && from == 0) ||
        (direction > 0 && from + 1 >= found->effects.size())) return;
    auto before = captureEditState();
    auto effects = found->effects;
    const auto to = direction < 0 ? from - 1 : from + 1;
    std::swap(effects[from], effects[to]);
    if (!document_->setLayerEffects(selected_layer_id_, effects)) return;
    selected_effect_ = std::pair{selected_layer_id_, to};
    (void)recordCompositionEdit(std::move(before));
    syncEffectsInspector(&*found);
    updateDocumentState();
    requestPreview();
}
void MainWindow::removeSelectedEffect()
{
    finishPendingTransformEdit();
    if (!document_ || !selected_effect_.has_value()) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end() || selected_effect_->second >= found->effects.size())
        return;
    auto before = captureEditState();
    auto effects = found->effects;
    const auto removed = selected_effect_->second;
    effects.erase(effects.begin() + static_cast<std::ptrdiff_t>(removed));
    if (!effects.empty()) {
        selected_effect_ = std::pair{selected_layer_id_, std::min(removed, effects.size() - 1)};
    } else {
        selected_effect_.reset();
    }
    if (!document_->setLayerEffects(selected_layer_id_, effects)) return;
    (void)recordCompositionEdit(std::move(before));
    syncEffectsInspector(&*found);
    updateDocumentState();
    requestPreview();
}
void MainWindow::editSelectedLayerContent()
{
    if (!document_ || selected_layer_id_ == 0) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end()) return;
    const bool is_text = found->kind == model::LayerKind::Text;
    const bool is_shape = found->kind == model::LayerKind::Shape;
    if (!is_text && !is_shape) return;

    const bool coalescing = active_content_edit_layer_ == selected_layer_id_;
    std::optional<CompositionEditState> before;
    if (!coalescing) {
        finishPendingTransformEdit();
        before.emplace(captureEditState());
    }

    bool changed = false;
    bool rejected = false;
    if (is_text) {
        auto content = inspector_->editedTextContent(
            std::get<model::TextLayerContent>(found->content));
        if (content != std::get<model::TextLayerContent>(found->content)) {
            changed = document_->setTextLayerContent(selected_layer_id_, content);
            rejected = !changed;
        }
    } else {
        auto content = inspector_->editedShapeContent(
            std::get<model::ShapeLayerContent>(found->content));
        if (content != std::get<model::ShapeLayerContent>(found->content)) {
            changed = document_->setShapeLayerContent(selected_layer_id_, content);
            rejected = !changed;
        }
    }
    if (rejected) {
        syncLayerContentInspector(&*found);
        statusBar()->showMessage(
            QStringLiteral("The layer content value is outside the supported range."), 4000);
        return;
    }
    if (!changed) return;
    if (!coalescing) {
        composition_history_.beginCoalescedEdit(std::move(*before));
        active_content_edit_layer_ = selected_layer_id_;
        updateHistoryActions();
    }
    updateDocumentState();
    requestPreview();
}
void MainWindow::chooseSelectedLayerColor(bool text_color, bool stroke_color)
{
    finishPendingTransformEdit();
    if (!document_ || selected_layer_id_ == 0) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end()) return;

    model::ColorRgba old_color{};
    if (text_color && found->kind == model::LayerKind::Text) {
        old_color = std::get<model::TextLayerContent>(found->content).color;
    } else if (!text_color && found->kind == model::LayerKind::Shape) {
        const auto& shape = std::get<model::ShapeLayerContent>(found->content);
        old_color = stroke_color ? shape.stroke_color : shape.fill_color;
    } else {
        return;
    }
    const QColor chosen = QColorDialog::getColor(
        qColor(old_color), this,
        text_color ? QStringLiteral("Text Color")
            : stroke_color ? QStringLiteral("Stroke Color") : QStringLiteral("Fill Color"),
        QColorDialog::ShowAlphaChannel);
    if (!chosen.isValid()) return;
    const auto new_color = modelColor(chosen);
    if (new_color == old_color) return;

    auto before = captureEditState();
    bool changed = false;
    if (text_color) {
        auto content = std::get<model::TextLayerContent>(found->content);
        content.color = new_color;
        changed = document_->setTextLayerContent(selected_layer_id_, content);
    } else {
        auto content = std::get<model::ShapeLayerContent>(found->content);
        if (stroke_color) content.stroke_color = new_color;
        else content.fill_color = new_color;
        changed = document_->setShapeLayerContent(selected_layer_id_, content);
    }
    if (!changed) return;
    (void)recordCompositionEdit(std::move(before));
    syncLayerContentInspector(found == document_->layers().end() ? nullptr : &*found);
    updateDocumentState();
    requestPreview();
}
void MainWindow::editSelectedLayerTransform(std::size_t property_index)
{
    if (!document_ || selected_layer_id_ == 0 || property_index >= kTransformProperties.size()) return;
    const auto edit_identity = std::pair{selected_layer_id_, property_index};
    const bool coalescing = active_transform_edit_ == edit_identity;
    if (!coalescing) finishPendingTransformEdit();
    std::optional<CompositionEditState> before;
    if (!coalescing) before.emplace(captureEditState());
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end()) return;

    const auto property = kTransformProperties[property_index];
    const auto raw_local_frame = (timeline_ != nullptr ? timeline_->currentFrame() : 0) -
        found->timeline_start_frame;
    const auto& frames = creative_suite::animation::keyframesFor(found->keyframes, property);
    const double new_value = inspector_->transformFieldValue(property_index);
    bool changed = false;
    if (frames.empty()) {
        auto transform = found->transform;
        setTransformPropertyValue(transform, property, new_value);
        if (transformPropertyValue(found->transform, property) != new_value)
            changed = document_->setLayerTransform(selected_layer_id_, transform);
    } else if (raw_local_frame >= 0 && raw_local_frame < found->duration_frames &&
               containsKeyframeAt(found->keyframes, property, raw_local_frame)) {
        const auto key = std::lower_bound(
            frames.begin(), frames.end(), raw_local_frame,
            [](const creative_suite::animation::Keyframe& item, std::int64_t frame) {
                return item.frame < frame;
            });
        if (key != frames.end() && key->frame == raw_local_frame && key->value != new_value)
            changed = document_->setLayerKeyframe(
                selected_layer_id_, property, raw_local_frame, new_value);
    }
    if (!changed) {
        syncTransformInspector();
        return;
    }
    if (!coalescing) {
        composition_history_.beginCoalescedEdit(std::move(*before));
        active_transform_edit_ = edit_identity;
        updateHistoryActions();
    }
    updateDocumentState();
    refreshTimeline();
    syncTransformInspector();
    requestPreview();
}
void MainWindow::toggleSelectedLayerKeyframe(std::size_t property_index)
{
    finishPendingTransformEdit();
    if (!document_ || selected_layer_id_ == 0 || property_index >= kTransformProperties.size() ||
        timeline_ == nullptr) return;
    const auto found = std::find_if(document_->layers().begin(), document_->layers().end(),
        [this](const auto& layer) { return layer.id == selected_layer_id_; });
    if (found == document_->layers().end()) return;
    const auto local_frame = timeline_->currentFrame() - found->timeline_start_frame;
    if (local_frame < 0 || local_frame >= found->duration_frames) return;
    auto before = captureEditState();

    const auto property = kTransformProperties[property_index];
    const bool has_key = containsKeyframeAt(found->keyframes, property, local_frame);
    bool changed = false;
    if (has_key) {
        changed = document_->removeLayerKeyframe(selected_layer_id_, property, local_frame);
    } else {
        const auto value = creative_suite::animation::evaluateProperty(
            found->transform, found->keyframes, property, local_frame);
        changed = document_->setLayerKeyframe(
            selected_layer_id_, property, local_frame, value);
        if (changed) {
            timeline_->setLayerExpanded(selected_layer_id_, true);
            timeline_->setTransformGroupExpanded(selected_layer_id_, true);
        }
    }
    if (!changed) return;
    (void)recordCompositionEdit(std::move(before));
    updateDocumentState();
    refreshTimeline();
    syncTransformInspector();
    requestPreview();
}
void MainWindow::handleMediaDrop(const std::filesystem::path& path,
                                std::int64_t start_frame,
                                model::LayerId before_layer_id)
{
    finishPendingTransformEdit();
    if (!document_ || media_pool_ == nullptr || timeline_ == nullptr) return;
    const auto index = media_pool_->library().indexForPath(path);
    if (index >= media_pool_->library().size()) {
        statusBar()->showMessage(QStringLiteral("This media item is no longer in the Media Pool."),
                                 5000);
        return;
    }
    const auto& item = media_pool_->library().items()[index];
    if (item.offline) {
        statusBar()->showMessage(QStringLiteral("Restore this offline media item before adding it."),
                                 5000);
        return;
    }

    auto before = captureEditState();
    model::LayerId added_id = 0;
    model::AddMediaLayerResult result = model::AddMediaLayerResult::InvalidTimingMetadata;
    try {
        result = document_->addMediaLayer(item.metadata, start_frame, &added_id);
    } catch (const std::exception& error) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_timeline", "insert_media_layer", error.what(),
            {{"path", pathForLog(path)}, {"frame", std::to_string(start_frame)}});
        QMessageBox::warning(this, QStringLiteral("Cannot Add Media"),
            QStringLiteral("The selected media could not be added to this composition."));
        return;
    } catch (...) {
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_timeline", "insert_media_layer", "Unknown media layer insertion failure",
            {{"path", pathForLog(path)}, {"frame", std::to_string(start_frame)}});
        QMessageBox::warning(this, QStringLiteral("Cannot Add Media"),
            QStringLiteral("The selected media could not be added to this composition."));
        return;
    }
    if (result != model::AddMediaLayerResult::Added) {
        const std::string cause = result == model::AddMediaLayerResult::InvalidTimingMetadata
            ? "Media does not contain enough valid timing metadata to create a timeline layer"
            : "Media layer could not be added to the Motion Studio composition";
        creative_suite::diagnostics::Logger::instance().log(
            creative_suite::diagnostics::Level::Error,
            "motion_timeline", "insert_media_layer", cause,
            {{"path", pathForLog(path)}, {"frame", std::to_string(start_frame)}});
        QMessageBox::warning(this, QStringLiteral("Cannot Add Media"),
            result == model::AddMediaLayerResult::InvalidTimingMetadata
                ? QStringLiteral("This video needs a valid frame rate and positive duration or frame count.")
                : QStringLiteral("The selected media could not be added to this composition."));
        return;
    }

    if (before_layer_id != 0) {
        const auto target_layer = std::find_if(
            document_->layers().begin(), document_->layers().end(),
            [before_layer_id](const auto& layer) { return layer.id == before_layer_id; });
        if (target_layer != document_->layers().end()) {
            const auto target_model_index = static_cast<std::size_t>(
                std::distance(document_->layers().begin(), target_layer));
            const auto destination = target_model_index + 1;
            (void)document_->moveLayer(added_id, destination);
        }
    }
    selected_layer_id_ = added_id;
    (void)recordCompositionEdit(std::move(before));
    updateDocumentState();
    refreshTimeline();
    syncTransformInspector();
    inspector_->selectTransformTab();
    requestPreview();
}

} // namespace motion::ui
