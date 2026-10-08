#include "workspaces/fusion/ui/fusion_workspace.h"
#include "fusion/nodes/ui/node_canvas.h"

#include <creative_suite/effects/effects.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <utility>

namespace ui {
namespace {

const creative_suite::animation::ScalarKeyframes* effectParameterKeyframes(
    const fusion::nodes::Node& node,
    const std::string& parameter_id) {
    const auto found = std::find_if(node.effect_parameter_keyframes.begin(),
        node.effect_parameter_keyframes.end(), [&parameter_id](const auto& item) {
            return item.parameter_id == parameter_id;
        });
    return found == node.effect_parameter_keyframes.end() ? nullptr : &found->keyframes;
}

creative_suite::animation::ScalarKeyframes* effectParameterKeyframes(
    fusion::nodes::Node& node,
    const std::string& parameter_id) {
    const auto found = std::find_if(node.effect_parameter_keyframes.begin(),
        node.effect_parameter_keyframes.end(), [&parameter_id](const auto& item) {
            return item.parameter_id == parameter_id;
        });
    return found == node.effect_parameter_keyframes.end() ? nullptr : &found->keyframes;
}

fusion::nodes::Node* findMutableNode(
    fusion::nodes::NodeGraph& graph,
    fusion::nodes::NodeId id) {
    const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
        [id](const fusion::nodes::Node& node) { return node.id == id; });
    return found == graph.nodes.end() ? nullptr : &*found;
}

creative_suite::animation::ScalarKeyframes& ensureEffectParameterKeyframes(
    fusion::nodes::Node& node,
    const std::string& parameter_id) {
    auto found = std::find_if(node.effect_parameter_keyframes.begin(),
        node.effect_parameter_keyframes.end(), [&parameter_id](const auto& item) {
            return item.parameter_id == parameter_id;
        });
    if (found == node.effect_parameter_keyframes.end()) {
        node.effect_parameter_keyframes.push_back({parameter_id, {}});
        return node.effect_parameter_keyframes.back().keyframes;
    }
    return found->keyframes;
}

double evaluatedEffectParameter(
    const fusion::nodes::Node& node,
    const std::string& parameter_id,
    std::int64_t frame) {
    const auto base = creative_suite::effects::parameterValue(
        node.effect, parameter_id);
    const auto* keyframes = effectParameterKeyframes(node, parameter_id);
    return keyframes == nullptr ? base
        : creative_suite::animation::evaluateScalar(base, *keyframes, frame);
}

bool hasKeyAtFrame(
    const std::vector<creative_suite::animation::Keyframe>& keyframes,
    std::int64_t frame) {
    return std::any_of(keyframes.begin(), keyframes.end(),
        [frame](const auto& keyframe) { return keyframe.frame == frame; });
}

} // namespace

FusionWorkspace::FusionWorkspace(QObject* parent) : QObject(parent) {}

void FusionWorkspace::createPanels(QWidget* parent) {
    if (viewer_title_ != nullptr || parent == nullptr) return;

    auto* viewer_title = new QLabel("Viewer", parent);
    viewer_title->setObjectName("workspaceViewerTitle");
    viewer_title->setContentsMargins(8, 5, 8, 5);
    viewer_title->setStyleSheet(
        "font-weight: 600; color: #d6dce6; background: #20242b;");
    viewer_title->hide();
    viewer_title_ = viewer_title;

    node_editor_panel_ = new QWidget(parent);
    node_editor_panel_->setObjectName("fusionNodeEditor");
    auto* node_layout = new QVBoxLayout(node_editor_panel_);
    node_layout->setContentsMargins(0, 0, 0, 0);
    node_layout->setSpacing(0);

    auto* toolbar = new QWidget(node_editor_panel_);
    auto* toolbar_layout = new QHBoxLayout(toolbar);
    toolbar_layout->setContentsMargins(8, 6, 8, 6);
    auto* add_type = new QComboBox(toolbar);
    add_type->setObjectName("fusionAddNodeType");
    add_type->addItem("Input", static_cast<int>(fusion::nodes::NodeType::Input));
    add_type->addItem("Transform", static_cast<int>(fusion::nodes::NodeType::Transform));
    add_type->addItem("Color", static_cast<int>(fusion::nodes::NodeType::Color));
    add_type->addItem("Merge", static_cast<int>(fusion::nodes::NodeType::Merge));
    auto* add_button = new QPushButton("Add Node", toolbar);
    add_button->setObjectName("fusionAddNodeButton");
    toolbar_layout->addWidget(add_type);
    toolbar_layout->addWidget(add_button);
    toolbar_layout->addStretch(1);
    node_layout->addWidget(toolbar);

    canvas_host_ = new QWidget(node_editor_panel_);
    auto* canvas_layout = new QVBoxLayout(canvas_host_);
    canvas_layout->setContentsMargins(0, 0, 0, 0);
    auto* node_canvas = new fusion::nodes::NodeCanvas(canvas_host_);
    node_canvas->setObjectName("fusionNodeCanvas");
    canvas_layout->addWidget(node_canvas);
    node_layout->addWidget(canvas_host_, 1);
    node_canvas->setSelectionChangedHandler([this](fusion::nodes::NodeId id) {
        if (refreshing_) return;
        selected_node_id_ = id;
        refreshInspector();
    });
    node_canvas->setViewerNodeRequestedHandler([this](fusion::nodes::NodeId id) {
        if (refreshing_ || clip_id_ == 0 ||
            fusion::nodes::findNode(graph_, id) == nullptr ||
            preview_node_id_ == id) return;
        preview_node_id_ = id;
        refreshCanvas();
        publishPreviewTarget();
    });
    node_canvas->setPositionChangedHandler([this](fusion::nodes::NodeId id, double x, double y) {
        if (refreshing_) return;
        auto candidate = graph_;
        const auto found = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
            [id](const auto& node) { return node.id == id; });
        if (found == candidate.nodes.end()) return;
        found->x = x;
        found->y = y;
        commitGraph(std::move(candidate), QStringLiteral("Node moved."));
    });
    node_canvas->setConnectionRequestedHandler(
        [this](fusion::nodes::NodeId from, fusion::nodes::NodeId to, std::uint8_t input) {
            auto* status = inspector_panel_->findChild<QLabel*>("fusionNodeStatus");
            auto candidate = fusion::nodes::connect(graph_, from, to, input);
            if (!candidate) {
                if (status) status->setText(
                    "Connection rejected: the ports are incompatible or the link creates a cycle.");
                return;
            }
            commitGraph(std::move(*candidate), QStringLiteral("Nodes connected."));
        });
    node_canvas->setDisconnectionRequestedHandler(
        [this](fusion::nodes::NodeId to, std::uint8_t input) {
            auto* status = inspector_panel_->findChild<QLabel*>("fusionNodeStatus");
            auto candidate = fusion::nodes::disconnect(graph_, to, input);
            if (!candidate) {
                if (status) status->setText(
                    "This connection cannot be removed while keeping the graph valid.");
                return;
            }
            commitGraph(std::move(*candidate), QStringLiteral("Connection removed."));
        });
    node_canvas->setEffectDropRequestedHandler(
        [this](const QString& effect_id, const QPointF& position,
               std::optional<fusion::nodes::Connection> cable) {
            if (clip_id_ == 0) return false;
            const auto effect_bytes = effect_id.toUtf8();
            const std::string effect_key(effect_bytes.constData(),
                                         static_cast<std::size_t>(effect_bytes.size()));
            if (creative_suite::effects::findDefinition(effect_key) == nullptr) return false;
            auto candidate = graph_;
            fusion::nodes::Node effect_node;
            effect_node.id = candidate.next_id++;
            effect_node.type = fusion::nodes::NodeType::Effect;
            effect_node.x = position.x();
            effect_node.y = position.y();
            effect_node.effect = creative_suite::effects::makeDefaultInstance(effect_key);
            const auto effect_node_id = effect_node.id;
            candidate.nodes.push_back(std::move(effect_node));
            if (cable.has_value()) {
                candidate.connections.erase(std::remove(candidate.connections.begin(),
                    candidate.connections.end(), *cable), candidate.connections.end());
                candidate.connections.push_back({cable->from, effect_node_id, 0});
                candidate.connections.push_back({effect_node_id, cable->to, cable->input});
                if (!fusion::nodes::validate(candidate)) {
                    if (auto* label = inspector_panel_->findChild<QLabel*>("fusionNodeStatus"))
                        label->setText("Effect insertion rejected: the cable cannot be split without creating an invalid or cyclic graph.");
                    return false;
                }
            }
            selected_node_id_ = effect_node_id;
            commitGraph(std::move(candidate), cable.has_value()
                ? QStringLiteral("Effect inserted into the connection.")
                : QStringLiteral("Effect node added; connect it to the graph."));
            return true;
        });
    connect(add_button, &QPushButton::clicked, this, [this, add_type] {
        if (clip_id_ == 0) return;
        auto candidate = graph_;
        const auto type = static_cast<fusion::nodes::NodeType>(add_type->currentData().toInt());
        const auto index = candidate.nodes.size();
        candidate.nodes.push_back(fusion::nodes::Node{candidate.next_id++, type,
            80.0 + static_cast<double>(index % 5) * 245.0,
            90.0 + static_cast<double>(index / 5) * 145.0});
        selected_node_id_ = candidate.nodes.back().id;
        commitGraph(std::move(candidate), QStringLiteral("Node added."));
    });

    inspector_panel_ = new QWidget(parent);
    inspector_panel_->setObjectName("fusionInspector");
    auto* inspector_layout = new QVBoxLayout(inspector_panel_);
    inspector_layout->setContentsMargins(12, 12, 12, 12);
    inspector_layout->setSpacing(8);
    auto* inspector_title = new QLabel("Fusion Inspector", inspector_panel_);
    inspector_title->setObjectName("fusionInspectorTitle");
    inspector_title->setStyleSheet("font-weight: 600; font-size: 14px;");
    inspector_layout->addWidget(inspector_title);
    auto* status = new QLabel("Select a video or image clip in the Timeline.", inspector_panel_);
    status->setObjectName("fusionNodeStatus");
    status->setWordWrap(true);
    status->setStyleSheet("color: #9aa4b2;");
    inspector_layout->addWidget(status);
    node_properties_ = new QWidget(inspector_panel_);
    node_properties_->setObjectName("fusionNodeProperties");
    auto* property_layout = new QFormLayout(node_properties_);
    property_layout->setContentsMargins(0, 0, 0, 8);
    inspector_layout->addWidget(node_properties_);

    inspector_layout->addStretch(1);
    setSelection(nullptr, {});
}

void FusionWorkspace::setSelection(const timeline::TimelineClip* clip,
                                   std::vector<MediaChoice> media_choices) {
    const auto previous_clip_id = clip_id_;
    const auto previous_node_id = selected_node_id_;
    const auto previous_preview_node_id = preview_node_id_;
    clip_id_ = clip != nullptr && (clip->kind == timeline::ClipKind::Video ||
        clip->kind == timeline::ClipKind::Image) ? clip->clip_id : 0;
    clip_timeline_start_frame_ = clip_id_ != 0 ? clip->timeline_start_frame : 0;
    clip_duration_frames_ = clip_id_ != 0 ? clip->timeline_duration_frames : 0;
    local_frame_ = clip_id_ == previous_clip_id
        ? std::clamp<std::int64_t>(local_frame_, 0,
            std::max<std::int64_t>(0, clip_duration_frames_ - 1))
        : 0;
    graph_ = clip_id_ == 0 ? fusion::nodes::NodeGraph{}
        : clip->node_graph.value_or(fusion::nodes::makePassthroughGraph());
    media_choices_ = std::move(media_choices);
    selected_node_id_ = clip_id_ != 0 && clip_id_ == previous_clip_id &&
        fusion::nodes::findNode(graph_, previous_node_id) != nullptr
            ? previous_node_id
            : graph_.nodes.empty() ? 0 : graph_.nodes.front().id;
    const auto output = std::find_if(graph_.nodes.begin(), graph_.nodes.end(),
        [](const fusion::nodes::Node& node) {
            return node.type == fusion::nodes::NodeType::Output;
        });
    const auto output_id = output == graph_.nodes.end() ? fusion::nodes::NodeId{0}
                                                        : output->id;
    preview_node_id_ = clip_id_ != 0 && clip_id_ == previous_clip_id &&
        fusion::nodes::findNode(graph_, previous_preview_node_id) != nullptr
            ? previous_preview_node_id : output_id;
    refreshCanvas();
    refreshInspector();
    if (active_) publishPreviewTarget();
}

void FusionWorkspace::setActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    if (!active_) {
        emit previewTargetCleared();
        return;
    }

    if (clip_id_ == 0 ||
        fusion::nodes::findNode(graph_, preview_node_id_) == nullptr) {
        emit previewTargetCleared();
        return;
    }

    emit playbackPauseRequested();
    publishPreviewTarget();
    emit playbackClipActivationRequested(clip_id_, 0);
}

void FusionWorkspace::publishPreviewTarget() {
    if (!active_ || clip_id_ == 0 ||
        fusion::nodes::findNode(graph_, preview_node_id_) == nullptr) {
        emit previewTargetCleared();
        return;
    }
    emit previewTargetRequested(clip_id_, preview_node_id_);
}

void FusionWorkspace::setTimelinePlayheadFrame(std::int64_t timeline_frame) {
    if (clip_id_ == 0 || clip_duration_frames_ <= 0) return;
    const auto frame = timeline_frame <= clip_timeline_start_frame_
        ? std::int64_t{0}
        : std::min<std::int64_t>(
            timeline_frame - clip_timeline_start_frame_, clip_duration_frames_ - 1);
    if (frame == local_frame_) return;
    local_frame_ = frame;
    refreshAnimatedControls();
}

void FusionWorkspace::setMediaChoices(std::vector<MediaChoice> media_choices) {
    media_choices_ = std::move(media_choices);
    if (inspector_panel_ == nullptr) return;
    auto* source = inspector_panel_->findChild<QComboBox*>("fusionInputMediaSource");
    const auto* node = fusion::nodes::findNode(graph_, selected_node_id_);
    if (source == nullptr || node == nullptr ||
        node->type != fusion::nodes::NodeType::Input) return;

    const QSignalBlocker blocker(source);
    source->clear();
    source->addItem("Selected Timeline clip", -1);
    int selected = -1;
    for (std::size_t index = 0; index < media_choices_.size(); ++index) {
        const auto& choice = media_choices_[index];
        source->addItem(choice.label, static_cast<int>(index));
        if (choice.path == node->source_path) selected = static_cast<int>(index);
    }
    if (!node->source_path.empty() && selected < 0) {
        source->addItem(QStringLiteral("Unavailable media source"), -2);
        selected = source->count() - 1;
    }
    source->setCurrentIndex(std::max(0, source->findData(selected)));
}

void FusionWorkspace::refreshCanvas() {
    if (node_editor_panel_ == nullptr) return;
    auto* canvas = node_editor_panel_->findChild<fusion::nodes::NodeCanvas*>("fusionNodeCanvas");
    if (canvas == nullptr) return;
    refreshing_ = true;
    canvas->setEnabled(clip_id_ != 0);
    canvas->setGraph(graph_);
    canvas->setViewerNode(preview_node_id_);
    canvas->setSelectedNode(selected_node_id_);
    refreshing_ = false;
}

void FusionWorkspace::refreshInspector() {
    if (inspector_panel_ == nullptr) return;
    auto* status = inspector_panel_->findChild<QLabel*>("fusionNodeStatus");
    auto* layout = qobject_cast<QFormLayout*>(node_properties_->layout());
    if (layout == nullptr) return;
    while (layout->rowCount() > 0) layout->removeRow(0);
    const auto* node = fusion::nodes::findNode(graph_, selected_node_id_);
    if (clip_id_ == 0) {
        status->setText("Select a video or image clip in the Timeline.");
        node_properties_->setEnabled(false);
        return;
    }
    status->setText(node ? QStringLiteral("Selected node #%1").arg(node->id)
                         : QStringLiteral("Select a node to edit its settings."));
    node_properties_->setEnabled(node != nullptr);
    if (node == nullptr) return;
    auto* apply = new QPushButton("Apply Settings", node_properties_);
    apply->setObjectName("fusionApplyNodeSettingsButton");
    auto* remove = new QPushButton("Remove Node", node_properties_);
    remove->setObjectName("fusionRemoveNodeButton");
    auto spin = [this, layout](const QString& name, const QString& label, double value,
                               double minimum, double maximum, double step) {
        auto* control = new QDoubleSpinBox(node_properties_);
        control->setObjectName(name);
        control->setRange(minimum, maximum);
        control->setSingleStep(step);
        control->setDecimals(3);
        control->setValue(value);
        layout->addRow(label, control);
        return control;
    };
    const auto animatedSpin = [this, layout](
        const QString& name, const QString& label, double value,
        double minimum, double maximum, double step, bool keyed,
        std::function<void()> toggle_keyframe) {
        auto* row = new QWidget(node_properties_);
        auto* row_layout = new QHBoxLayout(row);
        row_layout->setContentsMargins(0, 0, 0, 0);
        row_layout->setSpacing(4);
        auto* control = new QDoubleSpinBox(row);
        control->setObjectName(name);
        control->setRange(minimum, maximum);
        control->setSingleStep(step);
        control->setDecimals(3);
        control->setValue(value);
        auto* key = new QPushButton(keyed ? QStringLiteral("◆") : QStringLiteral("◇"), row);
        key->setObjectName(name + QStringLiteral("Keyframe"));
        key->setCheckable(true);
        key->setChecked(keyed);
        key->setFixedWidth(30);
        key->setStyleSheet(
            "QPushButton { font-size: 16px; font-weight: 600; padding: 0px; }"
            "QPushButton:checked { color: #171a20; background: #e8b94f; }");
        key->setToolTip(keyed
            ? QStringLiteral("Remove keyframe at the current frame")
            : QStringLiteral("Add keyframe at the current frame"));
        row_layout->addWidget(control, 1);
        row_layout->addWidget(key);
        layout->addRow(label, row);
        connect(key, &QPushButton::clicked, this,
            [this, toggle_keyframe = std::move(toggle_keyframe)] {
                if (refreshing_) return;
                toggle_keyframe();
            });
        return control;
    };
    if (node->type == fusion::nodes::NodeType::Input) {
        auto* source = new QComboBox(node_properties_);
        source->setObjectName("fusionInputMediaSource");
        source->addItem("Selected Timeline clip", -1);
        for (std::size_t i = 0; i < media_choices_.size(); ++i)
            source->addItem(media_choices_[i].label, static_cast<int>(i));
        int selected = -1;
        if (!node->source_path.empty()) for (std::size_t i = 0; i < media_choices_.size(); ++i)
            if (media_choices_[i].path == node->source_path) selected = static_cast<int>(i);
        source->setCurrentIndex(std::max(0, source->findData(selected)));
        layout->addRow("Media", source);
        connect(source, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this, source, node_id = node->id] {
                if (refreshing_) return;
                auto candidate = graph_;
                const auto found = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
                    [node_id](const auto& value) { return value.id == node_id; });
                if (found == candidate.nodes.end()) return;
                found->source_path.clear();
                found->source_frame_rate = 30.0;
                found->source_frame_count = 0;
                found->source_is_still = false;
                const auto choice = source->currentData().toInt();
                if (choice >= 0 && static_cast<std::size_t>(choice) < media_choices_.size()) {
                    const auto& media = media_choices_[static_cast<std::size_t>(choice)];
                    found->source_path = media.path;
                    found->source_frame_rate = media.frame_rate;
                    found->source_frame_count = media.frame_count;
                    found->source_is_still = media.still;
                }
                commitGraph(std::move(candidate), QStringLiteral("Input source updated."));
            });
    } else if (node->type == fusion::nodes::NodeType::Transform) {
        const auto add_transform = [&](const QString& name, const QString& label,
                                       timeline::TransformProperty property,
                                       double minimum, double maximum, double step) {
            const auto& keys = timeline::keyframesFor(
                node->transform_keyframes, property);
            const auto value = timeline::evaluateProperty(
                node->transform, node->transform_keyframes, property, local_frame_);
            animatedSpin(name, label, value, minimum, maximum, step,
                hasKeyAtFrame(keys, local_frame_),
                [this, node_id = node->id, property] {
                    toggleTransformKeyframe(node_id, property);
                });
        };
        add_transform("fusionTransformX", "Center X",
            timeline::TransformProperty::PositionX, -2.0, 3.0, 0.05);
        add_transform("fusionTransformY", "Center Y",
            timeline::TransformProperty::PositionY, -2.0, 3.0, 0.05);
        add_transform("fusionTransformScale", "Scale",
            timeline::TransformProperty::Scale, 0.01, 10.0, 0.05);
        add_transform("fusionTransformRotation", "Rotation",
            timeline::TransformProperty::Rotation, -360.0, 360.0, 1.0);
        add_transform("fusionTransformOpacity", "Opacity",
            timeline::TransformProperty::Opacity, 0.0, 1.0, 0.05);
    } else if (node->type == fusion::nodes::NodeType::Color) {
        spin("fusionColorBrightness", "Brightness", node->color.brightness, -100.0, 100.0, 1.0);
        spin("fusionColorContrast", "Contrast %", node->color.contrast_percent, 0.0, 200.0, 1.0);
        spin("fusionColorSaturation", "Saturation %", node->color.saturation_percent, 0.0, 200.0, 1.0);
    } else if (node->type == fusion::nodes::NodeType::Effect) {
        auto* enabled = new QCheckBox(node_properties_);
        enabled->setObjectName("fusionEffectEnabled");
        enabled->setChecked(node->effect.enabled);
        layout->addRow("Enabled", enabled);
        if (const auto* definition = creative_suite::effects::findDefinition(node->effect.id)) {
            for (const auto& parameter : definition->parameters) {
                const auto parameter_id = QString::fromUtf8(
                    parameter.id.data(), static_cast<qsizetype>(parameter.id.size()));
                const auto parameter_name = QString::fromUtf8(
                    parameter.name.data(), static_cast<qsizetype>(parameter.name.size()));
                const auto control_name = QStringLiteral("fusionEffectParameter_%1")
                    .arg(parameter_id);
                if (node->effect.id == "video.brightness" &&
                    parameter.id == "amount") {
                    const std::string effect_parameter_id(parameter.id);
                    const auto* keys = effectParameterKeyframes(
                        *node, effect_parameter_id);
                    const auto evaluated = evaluatedEffectParameter(
                        *node, effect_parameter_id, local_frame_);
                    animatedSpin(control_name, parameter_name, evaluated,
                        parameter.minimum, parameter.maximum, parameter.step,
                        keys != nullptr && hasKeyAtFrame(*keys, local_frame_),
                        [this, node_id = node->id, effect_parameter_id] {
                            toggleEffectParameterKeyframe(
                                node_id, effect_parameter_id);
                        });
                } else {
                    spin(control_name, parameter_name,
                        creative_suite::effects::parameterValue(node->effect, parameter.id),
                        parameter.minimum, parameter.maximum, parameter.step);
                }
            }
        }
    }
    layout->addRow(apply);
    layout->addRow(remove);
    connect(apply, &QPushButton::clicked, this, [this, node_id = node->id] {
        auto candidate = graph_;
        auto found = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
            [node_id](const auto& value) { return value.id == node_id; });
        if (found == candidate.nodes.end()) return;
        if (found->type == fusion::nodes::NodeType::Transform) {
            const auto frame = std::clamp<std::int64_t>(local_frame_, 0,
                std::max<std::int64_t>(0, clip_duration_frames_ - 1));
            const auto apply_property = [found, frame](
                timeline::TransformProperty property,
                QDoubleSpinBox* control) {
                if (control == nullptr) return;
                const auto value = control->value();
                const auto& existing = timeline::keyframesFor(
                    found->transform_keyframes, property);
                if (!existing.empty()) {
                    static_cast<void>(timeline::setKeyframe(
                        found->transform_keyframes, property, frame, value));
                    return;
                }
                switch (property) {
                case timeline::TransformProperty::PositionX:
                    found->transform.position_x = value; break;
                case timeline::TransformProperty::PositionY:
                    found->transform.position_y = value; break;
                case timeline::TransformProperty::Scale:
                    found->transform.scale = value; break;
                case timeline::TransformProperty::Rotation:
                    found->transform.rotation_degrees = value; break;
                case timeline::TransformProperty::Opacity:
                    found->transform.opacity = value; break;
                }
            };
            apply_property(timeline::TransformProperty::PositionX,
                node_properties_->findChild<QDoubleSpinBox*>("fusionTransformX"));
            apply_property(timeline::TransformProperty::PositionY,
                node_properties_->findChild<QDoubleSpinBox*>("fusionTransformY"));
            apply_property(timeline::TransformProperty::Scale,
                node_properties_->findChild<QDoubleSpinBox*>("fusionTransformScale"));
            apply_property(timeline::TransformProperty::Rotation,
                node_properties_->findChild<QDoubleSpinBox*>("fusionTransformRotation"));
            apply_property(timeline::TransformProperty::Opacity,
                node_properties_->findChild<QDoubleSpinBox*>("fusionTransformOpacity"));
        } else if (found->type == fusion::nodes::NodeType::Color) {
            found->color.brightness = node_properties_->findChild<QDoubleSpinBox*>("fusionColorBrightness")->value();
            found->color.contrast_percent = node_properties_->findChild<QDoubleSpinBox*>("fusionColorContrast")->value();
            found->color.saturation_percent = node_properties_->findChild<QDoubleSpinBox*>("fusionColorSaturation")->value();
        } else if (found->type == fusion::nodes::NodeType::Effect) {
            const auto* definition = creative_suite::effects::findDefinition(found->effect.id);
            auto* enabled = node_properties_->findChild<QCheckBox*>("fusionEffectEnabled");
            if (definition == nullptr || enabled == nullptr) return;
            found->effect.enabled = enabled->isChecked();
            for (const auto& parameter : definition->parameters) {
                const auto parameter_id = QString::fromUtf8(
                    parameter.id.data(), static_cast<qsizetype>(parameter.id.size()));
                auto* control = node_properties_->findChild<QDoubleSpinBox*>(
                    QStringLiteral("fusionEffectParameter_%1").arg(parameter_id));
                if (control == nullptr) return;
                if (found->effect.id == "video.brightness" &&
                    parameter.id == "amount") {
                    auto curve = std::find_if(
                        found->effect_parameter_keyframes.begin(),
                        found->effect_parameter_keyframes.end(),
                        [&parameter](const auto& value) {
                            return value.parameter_id == parameter.id;
                        });
                    if (curve != found->effect_parameter_keyframes.end() &&
                        !curve->keyframes.empty()) {
                        const auto frame = std::clamp<std::int64_t>(local_frame_, 0,
                            std::max<std::int64_t>(0, clip_duration_frames_ - 1));
                        static_cast<void>(creative_suite::animation::setKeyframe(
                            curve->keyframes, frame, control->value()));
                    } else if (!creative_suite::effects::setParameterValue(
                            found->effect, parameter.id, control->value())) {
                        return;
                    }
                } else if (!creative_suite::effects::setParameterValue(
                        found->effect, parameter.id, control->value())) {
                    return;
                }
            }
        }
        commitGraph(std::move(candidate), QStringLiteral("Node settings updated."));
    });
    connect(remove, &QPushButton::clicked, this, [this, node_id = node->id, status] {
        auto candidate = graph_;
        const auto selected = std::find_if(candidate.nodes.begin(), candidate.nodes.end(),
            [node_id](const auto& value) { return value.id == node_id; });
        if (selected == candidate.nodes.end() ||
            selected->type == fusion::nodes::NodeType::Output) {
            status->setText("The Output node is required and cannot be removed.");
            return;
        }
        std::optional<fusion::nodes::NodeId> replacement;
        const auto upstream = std::find_if(candidate.connections.begin(), candidate.connections.end(),
            [node_id](const auto& edge) { return edge.to == node_id && edge.input == 0; });
        if (upstream != candidate.connections.end()) replacement = upstream->from;
        std::vector<fusion::nodes::Connection> reconnect;
        for (const auto& edge : candidate.connections) {
            if (edge.from == node_id && replacement.has_value() &&
                *replacement != edge.to) {
                reconnect.push_back({*replacement, edge.to, edge.input});
            }
        }
        candidate.nodes.erase(std::remove_if(candidate.nodes.begin(), candidate.nodes.end(),
            [node_id](const auto& value) { return value.id == node_id; }), candidate.nodes.end());
        candidate.connections.erase(std::remove_if(candidate.connections.begin(), candidate.connections.end(),
            [node_id](const auto& edge) { return edge.from == node_id || edge.to == node_id; }),
            candidate.connections.end());
        candidate.connections.insert(candidate.connections.end(), reconnect.begin(), reconnect.end());
        if (!fusion::nodes::validate(candidate)) {
            status->setText("The node cannot be removed because the remaining graph would be invalid. Reconnect its downstream nodes first.");
            return;
        }
        selected_node_id_ = candidate.nodes.empty() ? 0 : candidate.nodes.front().id;
        commitGraph(std::move(candidate), QStringLiteral("Node removed."));
    });
}

void FusionWorkspace::refreshAnimatedControls() {
    if (inspector_panel_ == nullptr) return;
    const auto* node = fusion::nodes::findNode(graph_, selected_node_id_);
    if (node == nullptr) return;
    const auto set_value = [this](const QString& name, double value) {
        auto* control = node_properties_->findChild<QDoubleSpinBox*>(name);
        if (control == nullptr) return;
        const QSignalBlocker blocker(control);
        control->setValue(value);
    };
    const auto set_key_button = [this](const QString& name, bool keyed) {
        auto* button = node_properties_->findChild<QPushButton*>(
            name + QStringLiteral("Keyframe"));
        if (button == nullptr) return;
        const QSignalBlocker blocker(button);
        button->setChecked(keyed);
        button->setText(keyed ? QStringLiteral("◆") : QStringLiteral("◇"));
        button->setToolTip(keyed
            ? QStringLiteral("Remove keyframe at the current frame")
            : QStringLiteral("Add keyframe at the current frame"));
    };
    if (node->type == fusion::nodes::NodeType::Transform) {
        const std::array<std::pair<QString, timeline::TransformProperty>, 5> properties{{
            {QStringLiteral("fusionTransformX"), timeline::TransformProperty::PositionX},
            {QStringLiteral("fusionTransformY"), timeline::TransformProperty::PositionY},
            {QStringLiteral("fusionTransformScale"), timeline::TransformProperty::Scale},
            {QStringLiteral("fusionTransformRotation"), timeline::TransformProperty::Rotation},
            {QStringLiteral("fusionTransformOpacity"), timeline::TransformProperty::Opacity},
        }};
        for (const auto& [name, property] : properties) {
            const auto& keys = timeline::keyframesFor(
                node->transform_keyframes, property);
            set_value(name, timeline::evaluateProperty(
                node->transform, node->transform_keyframes, property, local_frame_));
            set_key_button(name, hasKeyAtFrame(keys, local_frame_));
        }
    } else if (node->type == fusion::nodes::NodeType::Effect &&
               node->effect.id == "video.brightness") {
        const std::string parameter_id = "amount";
        const auto* keys = effectParameterKeyframes(*node, parameter_id);
        const auto name = QStringLiteral("fusionEffectParameter_amount");
        set_value(name, evaluatedEffectParameter(*node, parameter_id, local_frame_));
        set_key_button(name, keys != nullptr && hasKeyAtFrame(*keys, local_frame_));
    }
}

void FusionWorkspace::toggleTransformKeyframe(
    fusion::nodes::NodeId node_id,
    timeline::TransformProperty property) {
    auto candidate = graph_;
    auto* node = findMutableNode(candidate, node_id);
    if (node == nullptr || node->type != fusion::nodes::NodeType::Transform ||
        clip_duration_frames_ <= 0) return;
    const auto frame = std::clamp<std::int64_t>(
        local_frame_, 0, clip_duration_frames_ - 1);
    const auto& keys = timeline::keyframesFor(node->transform_keyframes, property);
    if (hasKeyAtFrame(keys, frame)) {
        static_cast<void>(timeline::removeKeyframe(
            node->transform_keyframes, property, frame));
    } else {
        const auto value = timeline::evaluateProperty(
            node->transform, node->transform_keyframes, property, frame);
        if (!timeline::setKeyframe(
                node->transform_keyframes, property, frame, value)) return;
    }
    commitGraph(std::move(candidate), QStringLiteral("Node keyframe updated."));
}

void FusionWorkspace::toggleEffectParameterKeyframe(
    fusion::nodes::NodeId node_id,
    const std::string& parameter_id) {
    auto candidate = graph_;
    auto* node = findMutableNode(candidate, node_id);
    if (node == nullptr || node->type != fusion::nodes::NodeType::Effect ||
        node->effect.id != "video.brightness" || parameter_id != "amount" ||
        clip_duration_frames_ <= 0) return;
    const auto frame = std::clamp<std::int64_t>(
        local_frame_, 0, clip_duration_frames_ - 1);
    auto* existing = effectParameterKeyframes(*node, parameter_id);
    const bool has_key = existing != nullptr && hasKeyAtFrame(*existing, frame);
    if (has_key) {
        static_cast<void>(creative_suite::animation::removeKeyframe(*existing, frame));
        if (existing->empty()) {
            std::erase_if(node->effect_parameter_keyframes,
                [&parameter_id](const auto& item) {
                    return item.parameter_id == parameter_id;
                });
        }
    } else {
        auto& keyframes = ensureEffectParameterKeyframes(*node, parameter_id);
        const auto value = evaluatedEffectParameter(*node, parameter_id, frame);
        if (!creative_suite::animation::setKeyframe(keyframes, frame, value)) return;
    }
    commitGraph(std::move(candidate), QStringLiteral("Node keyframe updated."));
}

void FusionWorkspace::commitGraph(fusion::nodes::NodeGraph graph, const QString& status) {
    if (clip_id_ == 0 || !fusion::nodes::validate(graph) ||
        !fusion::nodes::validKeyframeRange(graph, clip_duration_frames_)) return;
    emit playbackPauseRequested();
    graph_ = std::move(graph);
    refreshCanvas();
    refreshInspector();
    if (auto* label = inspector_panel_->findChild<QLabel*>("fusionNodeStatus")) label->setText(status);
    emit graphEditRequested(clip_id_, graph_);
}

}  // namespace ui
