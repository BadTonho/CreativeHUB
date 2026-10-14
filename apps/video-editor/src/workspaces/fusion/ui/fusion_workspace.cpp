#include "workspaces/fusion/ui/fusion_workspace.h"
#include "workspaces/fusion/nodes/ui/node_canvas.h"
#include "ui/effects/effects_catalog.h"

#include <creative_suite/effects/effects.h>

#include <QCheckBox>
#include <QColorDialog>
#include <QFontComboBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
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

std::optional<fusion::nodes::Node> catalogNode(const QString& id) {
    using effects::Action;
    using fusion::nodes::NodeType;
    fusion::nodes::Node node;
    switch (effects::actionFor(id, effects::Workspace::Fusion)) {
    case Action::VisualEffect: {
        const auto key = id.toStdString();
        if (!creative_suite::effects::findDefinition(key)) return std::nullopt;
        node.type = NodeType::Effect;
        node.effect = creative_suite::effects::makeDefaultInstance(key);
        break;
    }
    case Action::InputNode: node.type = NodeType::Input; break;
    case Action::TransformNode: node.type = NodeType::Transform; break;
    case Action::ColorNode: node.type = NodeType::Color; break;
    case Action::MergeNode: node.type = NodeType::Merge; break;
    case Action::BackgroundNode: node.type = NodeType::Background; break;
    case Action::TextNode: node.type = NodeType::Text; break;
    case Action::RectangleMaskNode: node.type = NodeType::RectangleMask; break;
    case Action::EllipseMaskNode: node.type = NodeType::EllipseMask; break;
    default: return std::nullopt;
    }
    return node;
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
    auto* grid_button = new QPushButton("Grid", toolbar);
    grid_button->setObjectName("fusionGridSettingsButton");
    grid_button->setToolTip("Adjust the Node Editor background grid.");
    grid_button->setAccessibleName("Grid Settings");
    toolbar_layout->addWidget(grid_button);
    toolbar_layout->addStretch(1);
    node_layout->addWidget(toolbar);

    canvas_host_ = new QWidget(node_editor_panel_);
    auto* canvas_layout = new QVBoxLayout(canvas_host_);
    canvas_layout->setContentsMargins(0, 0, 0, 0);
    auto* node_canvas = new fusion::nodes::NodeCanvas(canvas_host_);
    node_canvas->setObjectName("fusionNodeCanvas");
    canvas_layout->addWidget(node_canvas);
    node_layout->addWidget(canvas_host_, 1);

    auto* grid_dialog = new QDialog(node_editor_panel_, Qt::Tool);
    grid_dialog->setObjectName("fusionGridSettingsDialog");
    grid_dialog->setWindowTitle("Grid Settings");
    grid_dialog->setWindowModality(Qt::NonModal);
    auto* grid_layout = new QVBoxLayout(grid_dialog);
    auto* grid_form = new QFormLayout;
    auto* grid_visible = new QCheckBox("Show grid", grid_dialog);
    grid_visible->setObjectName("fusionGridVisible");
    auto* grid_style = new QComboBox(grid_dialog);
    grid_style->setObjectName("fusionGridStyle");
    grid_style->addItem("Lines", static_cast<int>(
        fusion::nodes::BackgroundGridStyle::Lines));
    grid_style->addItem("Dots", static_cast<int>(
        fusion::nodes::BackgroundGridStyle::Dots));
    auto* spacing_slider = new QSlider(Qt::Horizontal, grid_dialog);
    spacing_slider->setObjectName("fusionGridSpacing");
    spacing_slider->setRange(12, 64);
    auto* spacing_value = new QLabel(grid_dialog);
    spacing_value->setObjectName("fusionGridSpacingValue");
    auto* spacing_row = new QWidget(grid_dialog);
    auto* spacing_row_layout = new QHBoxLayout(spacing_row);
    spacing_row_layout->setContentsMargins(0, 0, 0, 0);
    spacing_row_layout->addWidget(spacing_slider, 1);
    spacing_row_layout->addWidget(spacing_value);
    auto* intensity_slider = new QSlider(Qt::Horizontal, grid_dialog);
    intensity_slider->setObjectName("fusionGridIntensity");
    intensity_slider->setRange(10, 60);
    auto* intensity_value = new QLabel(grid_dialog);
    intensity_value->setObjectName("fusionGridIntensityValue");
    auto* intensity_row = new QWidget(grid_dialog);
    auto* intensity_row_layout = new QHBoxLayout(intensity_row);
    intensity_row_layout->setContentsMargins(0, 0, 0, 0);
    intensity_row_layout->addWidget(intensity_slider, 1);
    intensity_row_layout->addWidget(intensity_value);
    const auto initial_grid = node_canvas->backgroundGridSettings();
    grid_visible->setChecked(initial_grid.visible);
    grid_style->setCurrentIndex(grid_style->findData(
        static_cast<int>(initial_grid.style)));
    spacing_slider->setValue(initial_grid.spacing);
    intensity_slider->setValue(initial_grid.intensity_percent);
    spacing_value->setText(QString::number(initial_grid.spacing));
    intensity_value->setText(QStringLiteral("%1%").arg(
        initial_grid.intensity_percent));
    grid_form->addRow(QString(), grid_visible);
    grid_form->addRow("Style", grid_style);
    grid_form->addRow("Spacing", spacing_row);
    grid_form->addRow("Intensity", intensity_row);
    grid_layout->addLayout(grid_form);
    auto* grid_buttons = new QDialogButtonBox(QDialogButtonBox::Close, grid_dialog);
    grid_layout->addWidget(grid_buttons);
    QObject::connect(grid_buttons, &QDialogButtonBox::rejected,
                     grid_dialog, &QDialog::hide);
    const auto apply_grid_settings = [node_canvas, grid_visible, grid_style,
                                      spacing_slider, intensity_slider,
                                      spacing_value, intensity_value] {
        auto settings = node_canvas->backgroundGridSettings();
        settings.visible = grid_visible->isChecked();
        settings.style = static_cast<fusion::nodes::BackgroundGridStyle>(
            grid_style->currentData().toInt());
        settings.spacing = spacing_slider->value();
        settings.intensity_percent = intensity_slider->value();
        spacing_value->setText(QString::number(settings.spacing));
        intensity_value->setText(QStringLiteral("%1%").arg(
            settings.intensity_percent));
        node_canvas->setBackgroundGridSettings(settings);
    };
    QObject::connect(grid_visible, &QCheckBox::toggled,
                     grid_dialog, apply_grid_settings);
    QObject::connect(grid_style, qOverload<int>(&QComboBox::currentIndexChanged),
                     grid_dialog, apply_grid_settings);
    QObject::connect(spacing_slider, &QSlider::valueChanged,
                     grid_dialog, apply_grid_settings);
    QObject::connect(intensity_slider, &QSlider::valueChanged,
                     grid_dialog, apply_grid_settings);
    QObject::connect(grid_button, &QPushButton::clicked, grid_dialog, [grid_dialog] {
        grid_dialog->show();
        grid_dialog->raise();
        grid_dialog->activateWindow();
    });

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
        [this](const QString& id, const QPointF& position,
               std::optional<fusion::nodes::Connection> cable) {
            return createCatalogNode(id, position, cable);
        });

    inspector_panel_ = new QWidget(parent);
    inspector_panel_->setObjectName("fusionInspector");
    auto* inspector_panel_layout = new QVBoxLayout(inspector_panel_);
    inspector_panel_layout->setContentsMargins(0, 0, 0, 0);
    inspector_panel_layout->setSpacing(0);

    auto* inspector_scroll_area = new QScrollArea(inspector_panel_);
    inspector_scroll_area->setObjectName("fusionInspectorScrollArea");
    inspector_scroll_area->setWidgetResizable(true);
    inspector_scroll_area->setFrameShape(QFrame::NoFrame);
    inspector_scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    inspector_scroll_area->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    auto* inspector_content = new QWidget(inspector_scroll_area);
    inspector_content->setObjectName("fusionInspectorContent");
    auto* inspector_layout = new QVBoxLayout(inspector_content);
    inspector_layout->setContentsMargins(12, 12, 12, 12);
    inspector_layout->setSpacing(8);
    auto* inspector_title = new QLabel("Fusion Inspector", inspector_content);
    inspector_title->setObjectName("fusionInspectorTitle");
    inspector_title->setStyleSheet("font-weight: 600; font-size: 14px;");
    inspector_layout->addWidget(inspector_title);
    auto* status = new QLabel("Select a video, image, or Fusion Composition clip in the Timeline.", inspector_content);
    status->setObjectName("fusionNodeStatus");
    status->setWordWrap(true);
    status->setStyleSheet("color: #9aa4b2;");
    inspector_layout->addWidget(status);
    node_properties_ = new QWidget(inspector_content);
    node_properties_->setObjectName("fusionNodeProperties");
    auto* property_layout = new QFormLayout(node_properties_);
    property_layout->setContentsMargins(0, 0, 0, 8);
    inspector_layout->addWidget(node_properties_);

    inspector_layout->addStretch(1);
    inspector_scroll_area->setWidget(inspector_content);
    inspector_panel_layout->addWidget(inspector_scroll_area);
    setSelection(nullptr, {});
}

bool FusionWorkspace::addCatalogNode(const QString& id) {
    if (!active_ || clip_id_ == 0 || node_editor_panel_ == nullptr) return false;
    const auto node = catalogNode(id);
    auto* canvas = node_editor_panel_->findChild<fusion::nodes::NodeCanvas*>("fusionNodeCanvas");
    if (!node || !canvas) return false;
    const auto position = canvas->freeNodePosition(node->type);
    if (!createCatalogNode(id, position, std::nullopt)) return false;
    const QRectF bounds(position, QSizeF(230, 160));
    canvas->setSceneRect(canvas->sceneRect().united(bounds.adjusted(-30, -30, 30, 30)));
    canvas->ensureVisible(bounds, 20, 20);
    return true;
}

bool FusionWorkspace::createCatalogNode(
    const QString& id, const QPointF& position,
    std::optional<fusion::nodes::Connection> cable) {
    if (!active_ || clip_id_ == 0) return false;
    auto node = catalogNode(id);
    if (!node) return false;
    const bool split_cable = cable && node->type == fusion::nodes::NodeType::Effect;
    if (split_cable && cable->input == 2) return false;
    auto candidate = graph_;
    node->id = candidate.next_id++;
    node->x = position.x();
    node->y = position.y();
    const auto node_id = node->id;
    candidate.nodes.push_back(std::move(*node));
    if (split_cable) {
        candidate.connections.erase(std::remove(candidate.connections.begin(),
            candidate.connections.end(), *cable), candidate.connections.end());
        candidate.connections.push_back({cable->from, node_id, 0});
        candidate.connections.push_back({node_id, cable->to, cable->input});
    }
    if (!fusion::nodes::validate(candidate) ||
        !fusion::nodes::validKeyframeRange(candidate, clip_duration_frames_)) {
        if (auto* label = inspector_panel_->findChild<QLabel*>("fusionNodeStatus"))
            label->setText("Node creation rejected: the graph would be invalid.");
        return false;
    }
    selected_node_id_ = node_id;
    commitGraph(std::move(candidate), split_cable
        ? QStringLiteral("Effect inserted into the connection.")
        : QStringLiteral("Node added; connect it to the graph."));
    return true;
}

void FusionWorkspace::setSelection(const timeline::TimelineClip* clip,
                                   std::vector<MediaChoice> media_choices) {
    const auto previous_clip_id = clip_id_;
    const auto previous_node_id = selected_node_id_;
    const auto previous_preview_node_id = preview_node_id_;
    independent_composition_ = clip && clip->kind == timeline::ClipKind::FusionComposition;
    clip_id_ = clip && timeline::supportsNodeGraph(clip->kind) ? clip->clip_id : 0;
    clip_timeline_start_frame_ = clip_id_ != 0 ? clip->timeline_start_frame : 0;
    clip_duration_frames_ = clip_id_ != 0 ? clip->timeline_duration_frames : 0;
    local_frame_ = clip_id_ == previous_clip_id
        ? std::clamp<std::int64_t>(local_frame_, 0,
            std::max<std::int64_t>(0, clip_duration_frames_ - 1))
        : 0;
    graph_ = clip_id_ == 0 ? fusion::nodes::NodeGraph{}
        : clip->node_graph.value_or(independent_composition_ ? fusion::nodes::makeCompositionGraph() : fusion::nodes::makePassthroughGraph());
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
    source->addItem(independent_composition_ ? "Transparent" : "Selected Timeline clip", -1);
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
        status->setText("Select a video, image, or Fusion Composition clip in the Timeline.");
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
        auto* key = new QPushButton(keyed ? QStringLiteral("â—†") : QStringLiteral("â—‡"), row);
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
    if (node->presentation && (node->type == fusion::nodes::NodeType::Input ||
        node->type == fusion::nodes::NodeType::Text || node->type == fusion::nodes::NodeType::Background)) {
        auto* visible = new QCheckBox(node_properties_);
        visible->setObjectName("fusionSourceVisible");
        visible->setChecked(node->presentation->visible);
        layout->addRow("Visible", visible);
    }
    if (fusion::nodes::isMaskNode(node->type)) {
        static const char* labels[]{"Center X (%)","Center Y (%)","Width (%)","Height (%)","Rotation (degrees)"};
        const auto evaluated=fusion::nodes::evaluateMask(node->mask,node->mask_keyframes,local_frame_);
        for (std::size_t i=0; i<5; ++i) {
            const auto name=QStringLiteral("fusionMask%1").arg(i);
            animatedSpin(name,QString::fromLatin1(labels[i]),fusion::nodes::maskValue(evaluated,i)*(i<4 ? 100 : 1),
                i<2 ? -200 : i<4 ? 0 : -3600, i<2 ? 300 : i<4 ? 400 : 3600,1,
                hasKeyAtFrame(node->mask_keyframes[i],local_frame_),[this,id=node->id,i] {
                    auto candidate=graph_; auto* n=findMutableNode(candidate,id);
                    if (!n || local_frame_<0 || local_frame_>=clip_duration_frames_) return;
                    auto& keys=n->mask_keyframes[i];
                    const auto value=creative_suite::animation::evaluateScalar(fusion::nodes::maskValue(n->mask,i),keys,local_frame_);
                    if (hasKeyAtFrame(keys,local_frame_)) {
                        static_cast<void>(creative_suite::animation::removeKeyframe(keys,local_frame_));
                        if (keys.empty()) fusion::nodes::setMaskValue(n->mask,i,value);
                    } else static_cast<void>(creative_suite::animation::setKeyframe(keys,local_frame_,value));
                    commitGraph(std::move(candidate),QStringLiteral("Mask keyframe updated."));
                });
        }
        spin("fusionMaskSoftEdge","Soft Edge (%)",node->mask.soft_edge*100,0,100,1);
        auto* invert=new QCheckBox("Invert",node_properties_); invert->setObjectName("fusionMaskInvert");
        invert->setChecked(node->mask.invert); layout->addRow(invert);
    }
    if (node->type == fusion::nodes::NodeType::Background || node->type == fusion::nodes::NodeType::Text) {
        const auto node_id = node->id;
        auto* color = new QPushButton("Choose RGBA color", node_properties_);
        color->setObjectName("fusionGeneratorColor");
        layout->addRow("Color", color);
        connect(color, &QPushButton::clicked, this, [this, node_id] {
            const auto* current = fusion::nodes::findNode(graph_, node_id);
            if (!current) return;
            const auto rgba = current->type == fusion::nodes::NodeType::Background ? current->background_color : current->text.color;
            const auto chosen = QColorDialog::getColor(QColor(rgba[0], rgba[1], rgba[2], rgba[3]), inspector_panel_, "Generator color", QColorDialog::ShowAlphaChannel);
            if (!chosen.isValid()) return;
            auto candidate = graph_; auto* target = findMutableNode(candidate, node_id);
            const std::array<std::uint8_t, 4> value{static_cast<std::uint8_t>(chosen.red()), static_cast<std::uint8_t>(chosen.green()), static_cast<std::uint8_t>(chosen.blue()), static_cast<std::uint8_t>(chosen.alpha())};
            if (target->type == fusion::nodes::NodeType::Background) target->background_color = value; else target->text.color = value;
            commitGraph(std::move(candidate), "Generator color updated.");
        });
        if (node->type == fusion::nodes::NodeType::Text) {
            auto* content = new QPlainTextEdit(node_properties_); content->setObjectName("fusionTextContent");
            content->setPlainText(QString::fromUtf8(node->text.content.data(), static_cast<qsizetype>(node->text.content.size())));
            content->setMaximumHeight(100); layout->addRow("Text", content);
            // Apply together so graph refresh does not interrupt text entry.
            auto* text_apply = new QPushButton("Apply Text", node_properties_); text_apply->setObjectName("fusionApplyText"); layout->addRow(text_apply);
            auto* font = new QFontComboBox(node_properties_); font->setObjectName("fusionTextFont"); font->setCurrentFont(QFont(QString::fromUtf8(node->text.font_family.c_str()))); layout->addRow("Font", font);
            auto* size = new QDoubleSpinBox(node_properties_); size->setObjectName("fusionTextSize"); size->setRange(1, 512); size->setValue(node->text.font_size_pixels); layout->addRow("Size", size);
            auto* alignment = new QComboBox(node_properties_); alignment->setObjectName("fusionTextAlignment"); alignment->addItems({"Left", "Center", "Right"}); alignment->setCurrentIndex(static_cast<int>(node->text.alignment)); layout->addRow("Alignment", alignment);
            connect(text_apply, &QPushButton::clicked, this, [this, node_id, content, font, size, alignment] {
                auto candidate = graph_; auto* target = findMutableNode(candidate, node_id); if (!target) return;
                target->text.content = content->toPlainText().toUtf8().toStdString();
                target->text.font_family = font->currentFont().family().toUtf8().toStdString();
                target->text.font_size_pixels = size->value(); target->text.alignment = static_cast<timeline::TextAlignment>(alignment->currentIndex());
                commitGraph(std::move(candidate), "Text updated.");
            });
        }
    }
    if (node->type == fusion::nodes::NodeType::Input) {
        auto* source = new QComboBox(node_properties_);
        source->setObjectName("fusionInputMediaSource");
        source->addItem(independent_composition_ ? "Transparent" : "Selected Timeline clip", -1);
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
        auto* enabled = new QCheckBox(node_properties_);
        enabled->setObjectName("fusionColorEnabled");
        enabled->setChecked(node->color.enabled);
        layout->addRow("Enabled", enabled);
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
        if (auto* visible = node_properties_->findChild<QCheckBox*>("fusionSourceVisible"); visible && found->presentation)
            found->presentation->visible = visible->isChecked();
        if (fusion::nodes::isMaskNode(found->type)) {
            const auto frame=std::clamp<std::int64_t>(local_frame_,0,std::max<std::int64_t>(0,clip_duration_frames_-1));
            for (std::size_t i=0; i<5; ++i) {
                const auto value=node_properties_->findChild<QDoubleSpinBox*>(QStringLiteral("fusionMask%1").arg(i))->value()/(i<4 ? 100 : 1);
                if (found->mask_keyframes[i].empty()) fusion::nodes::setMaskValue(found->mask,i,value);
                else static_cast<void>(creative_suite::animation::setKeyframe(found->mask_keyframes[i],frame,value));
            }
            found->mask.soft_edge=node_properties_->findChild<QDoubleSpinBox*>("fusionMaskSoftEdge")->value()/100;
            found->mask.invert=node_properties_->findChild<QCheckBox*>("fusionMaskInvert")->isChecked();
        } else if (found->type == fusion::nodes::NodeType::Transform) {
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
            found->color.enabled = node_properties_->findChild<QCheckBox*>("fusionColorEnabled")->isChecked();
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
        button->setText(keyed ? QStringLiteral("â—†") : QStringLiteral("â—‡"));
        button->setToolTip(keyed
            ? QStringLiteral("Remove keyframe at the current frame")
            : QStringLiteral("Add keyframe at the current frame"));
    };
    if (fusion::nodes::isMaskNode(node->type)) {
        const auto p=fusion::nodes::evaluateMask(node->mask,node->mask_keyframes,local_frame_);
        for (std::size_t i=0;i<5;++i) {
            const auto name=QStringLiteral("fusionMask%1").arg(i);
            set_value(name,fusion::nodes::maskValue(p,i)*(i<4 ? 100 : 1));
            set_key_button(name,hasKeyAtFrame(node->mask_keyframes[i],local_frame_));
        }
        return;
    }
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
