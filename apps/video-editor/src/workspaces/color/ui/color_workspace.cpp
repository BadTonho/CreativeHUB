#include "workspaces/color/ui/color_workspace.h"

#include "ui/preview/preview_widget.h"
#include "workspaces/color/evaluation/color_graph_evaluator.h"

#include <QCoreApplication>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QMetaObject>
#include <QPixmap>
#include <QPointer>
#include <QPushButton>
#include <QInputDialog>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <cmath>

namespace ui {
namespace {

QString labelFor(std::string_view id) {
    if (id == "video.primary_grade") return QCoreApplication::translate("ColorWorkspace", "Primary Grade");
    if (id == "video.brightness") return QCoreApplication::translate("ColorWorkspace", "Brightness");
    if (id == "video.contrast") return QCoreApplication::translate("ColorWorkspace", "Contrast");
    if (id == "video.saturation") return QCoreApplication::translate("ColorWorkspace", "Saturation");
    if (id == "video.grayscale") return QCoreApplication::translate("ColorWorkspace", "Grayscale");
    return QString::fromStdString(std::string(id));
}

QString parameterLabel(std::string_view id) {
    if (id == "brightness") return QCoreApplication::translate("ColorWorkspace", "Brightness");
    if (id == "contrast") return QCoreApplication::translate("ColorWorkspace", "Contrast");
    if (id == "saturation") return QCoreApplication::translate("ColorWorkspace", "Saturation");
    if (id == "red_gain") return QCoreApplication::translate("ColorWorkspace", "Red gain");
    if (id == "green_gain") return QCoreApplication::translate("ColorWorkspace", "Green gain");
    if (id == "blue_gain") return QCoreApplication::translate("ColorWorkspace", "Blue gain");
    return QCoreApplication::translate("ColorWorkspace", "Amount");
}

QImage calculateWaveform(const QImage& source) {
    constexpr int width = 320;
    constexpr int height = 256;
    std::array<std::vector<unsigned short>, 3> bins;
    for (auto& channel : bins) channel.resize(width * height);
    const auto sampled = source.scaled(width, 180, Qt::IgnoreAspectRatio,
                                       Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < sampled.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(sampled.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            const std::array<int, 3> values{qRed(row[x]), qGreen(row[x]), qBlue(row[x])};
            for (int channel = 0; channel < 3; ++channel)
                ++bins[channel][(height - 1 - values[channel]) * width + x];
        }
    }
    QImage plot(width, height, QImage::Format_RGB32);
    plot.fill(QColor(18, 21, 27));
    for (int y = 0; y < height; ++y) {
        auto* row = reinterpret_cast<QRgb*>(plot.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const auto index = y * width + x;
            row[x] = qRgb(std::min(255, 18 + int(bins[0][index]) * 38),
                            std::min(255, 21 + int(bins[1][index]) * 38),
                            std::min(255, 27 + int(bins[2][index]) * 38));
        }
    }
    return plot;
}

}  // namespace

ColorWorkspace::ColorWorkspace(QWidget* parent) : QWidget(parent) {
    setObjectName("colorWorkspacePage");
    setStyleSheet(QStringLiteral(R"(
        QWidget#colorWorkspacePage { background: #1c2028; color: #d7e0ea; }
        QWidget#colorNodesPanel, QWidget#colorPrimaryControls {
            background: #252b35; border: 1px solid #39424e; border-radius: 5px;
        }
        QWidget#colorWorkspacePage QLabel { color: #d7e0ea; }
        QLabel#colorWaveform {
            background: #141a22; border: 1px solid #39424e; border-radius: 5px;
        }
        QWidget#colorWorkspacePage QListWidget {
            background: #202630; color: #d7e0ea; border: 1px solid #39424e;
            padding: 3px; outline: 0;
        }
        QWidget#colorWorkspacePage QPushButton,
        QWidget#colorWorkspacePage QComboBox,
        QWidget#colorWorkspacePage QDoubleSpinBox {
            background: #303946; color: #e2e8f0; border: 1px solid #526070;
            border-radius: 4px; padding: 5px 7px;
        }
        QWidget#colorWorkspacePage QPushButton:hover { background: #39516a; }
    )"));
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    root->setSpacing(8);
    auto* upper = new QHBoxLayout;
    preview_container_ = new QWidget(this);
    preview_container_->setObjectName("colorViewerContainer");
    auto* preview_layout = new QVBoxLayout(preview_container_);
    preview_layout->setContentsMargins(0, 0, 0, 0);
    upper->addWidget(preview_container_, 3);
    auto* node_panel = new QWidget(this);
    node_panel->setObjectName("colorNodesPanel");
    auto* node_layout = new QVBoxLayout(node_panel);
    node_layout->addWidget(new QLabel(QCoreApplication::translate("ColorWorkspace", "Color nodes"), node_panel));
    nodes_ = new ColorNodeCanvas(node_panel);
    node_layout->addWidget(nodes_, 1);
    auto* node_action_controls = new QWidget(node_panel);
    node_action_controls->setObjectName("colorNodeActionControls");
    auto* node_action_layout = new QVBoxLayout(node_action_controls);
    node_action_layout->setContentsMargins(0, 0, 0, 0);
    node_kind_ = new QComboBox(node_action_controls);
    node_kind_->setObjectName("colorNewNodeType");
    for (const auto& effect : creative_suite::effects::builtInEffects()) {
        node_kind_->addItem(labelFor(effect.id), QString::fromUtf8(
            effect.id.data(), static_cast<qsizetype>(effect.id.size())));
    }
    const auto primary_grade = node_kind_->findData(QStringLiteral("video.primary_grade"));
    if (primary_grade >= 0) node_kind_->setCurrentIndex(primary_grade);
    node_action_layout->addWidget(node_kind_);
    auto* node_buttons = new QHBoxLayout;
    auto make_button = [node_action_controls, node_buttons](const QString& label, const char* object_name) {
        auto* button = new QPushButton(label, node_action_controls);
        button->setObjectName(QString::fromLatin1(object_name));
        node_buttons->addWidget(button);
        return button;
    };
    auto* add = make_button(QCoreApplication::translate("ColorWorkspace", "Add Effect"), "colorAddNodeButton");
    add_branch_button_ = make_button(QCoreApplication::translate("ColorWorkspace", "Add Branch"), "colorAddParallelBranchButton");
    auto* toggle = make_button(QCoreApplication::translate("ColorWorkspace", "On/Off"), "colorToggleNodeButton");
    auto* remove = make_button(QCoreApplication::translate("ColorWorkspace", "Remove"), "colorRemoveNodeButton");
    node_action_layout->addLayout(node_buttons);
    node_action_controls->hide();
    node_layout->addWidget(node_action_controls);
    upper->addWidget(node_panel, 2);
    root->addLayout(upper, 3);

    auto* transport = new QHBoxLayout;
    transport->addStretch();
    auto* previous = new QPushButton(QCoreApplication::translate("ColorWorkspace", "Previous frame"), this);
    auto* play_pause = new QPushButton(QCoreApplication::translate("ColorWorkspace", "Play / Pause"), this);
    auto* next = new QPushButton(QCoreApplication::translate("ColorWorkspace", "Next frame"), this);
    previous->setObjectName("colorPreviousFrameButton");
    play_pause->setObjectName("colorPlayPauseButton");
    next->setObjectName("colorNextFrameButton");
    transport->addWidget(previous);
    transport->addWidget(play_pause);
    transport->addWidget(next);
    transport->addStretch();
    root->addLayout(transport);
    connect(previous, &QPushButton::clicked, this, [this] {
        if (playback_handler_) playback_handler_(playback::PlaybackCommand::StepBackward);
    });
    connect(play_pause, &QPushButton::clicked, this, [this] {
        if (playback_handler_) playback_handler_(playback::PlaybackCommand::Play);
    });
    connect(next, &QPushButton::clicked, this, [this] {
        if (playback_handler_) playback_handler_(playback::PlaybackCommand::StepForward);
    });

    clips_ = new QListWidget(this);
    clips_->setObjectName("colorClipStrip");
    clips_->setViewMode(QListView::IconMode);
    clips_->setFlow(QListView::LeftToRight);
    clips_->setWrapping(false);
    clips_->setMovement(QListView::Static);
    clips_->setIconSize(QSize(112, 63));
    clips_->setFixedHeight(110);
    root->addWidget(clips_);

    auto* gallery_panel = new QWidget(this);
    gallery_panel->setObjectName("colorGalleryPanel");
    auto* gallery_layout = new QVBoxLayout(gallery_panel);
    gallery_layout->setContentsMargins(0, 0, 0, 0);
    auto* gallery_header = new QHBoxLayout;
    gallery_header->addWidget(new QLabel(QCoreApplication::translate("ColorWorkspace", "Gallery"), gallery_panel));
    gallery_header->addStretch();
    capture_still_button_ = new QPushButton(QCoreApplication::translate("ColorWorkspace", "Capture still"), gallery_panel);
    capture_still_button_->setObjectName("colorCaptureStillButton");
    auto* rename_still = new QPushButton(QCoreApplication::translate("ColorWorkspace", "Rename"), gallery_panel);
    rename_still->setObjectName("colorRenameStillButton");
    auto* remove_still = new QPushButton(QCoreApplication::translate("ColorWorkspace", "Remove still"), gallery_panel);
    remove_still->setObjectName("colorRemoveStillButton");
    gallery_header->addWidget(capture_still_button_);
    gallery_header->addWidget(rename_still);
    gallery_header->addWidget(remove_still);
    gallery_layout->addLayout(gallery_header);
    stills_ = new QListWidget(gallery_panel);
    stills_->setObjectName("colorGalleryStills");
    stills_->setViewMode(QListView::IconMode);
    stills_->setFlow(QListView::LeftToRight);
    stills_->setWrapping(false);
    stills_->setMovement(QListView::Static);
    stills_->setIconSize(QSize(112, 63));
    stills_->setFixedHeight(106);
    gallery_layout->addWidget(stills_);
    root->addWidget(gallery_panel);
    connect(capture_still_button_, &QPushButton::clicked, this, [this] {
        if (capture_still_handler_) capture_still_handler_();
    });
    connect(rename_still, &QPushButton::clicked, this, [this] { renameSelectedStill(); });
    connect(remove_still, &QPushButton::clicked, this, [this] {
        const auto* item = stills_->currentItem();
        if (item && remove_still_handler_)
            remove_still_handler_(item->data(Qt::UserRole).toString());
    });

    auto* lower = new QHBoxLayout;
    controls_ = new QWidget(this);
    controls_->setObjectName("colorPrimaryControls");
    controls_->setMaximumWidth(480);
    controls_layout_ = new QFormLayout(controls_);
    controls_layout_->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
    lower->addWidget(controls_, 2);
    waveform_ = new QLabel(QCoreApplication::translate("ColorWorkspace", "RGB waveform"), this);
    waveform_->setObjectName("colorWaveform");
    waveform_->setMinimumSize(320, 180);
    waveform_->setAlignment(Qt::AlignCenter);
    lower->addWidget(waveform_, 1);
    root->addLayout(lower, 2);

    connect(clips_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || !clip_selected_handler_) return;
        clip_selected_handler_(clips_->item(row)->data(Qt::UserRole).toULongLong());
    });
    nodes_->setGraphChangedHandler([this](color::Graph graph) {
        submit(std::move(graph), nodes_->selectedNode());
    });
    nodes_->setSelectionChangedHandler([this](color::NodeId id) {
        selected_node_ = id;
        refreshControls();
    });
    connect(add, &QPushButton::clicked, this, [this] {
        if (!selected_clip_ || !selected_node_) return;
        auto graph = graph_;
        const auto node_id = graph.next_id;
        const auto effect = creative_suite::effects::makeDefaultInstance(
            node_kind_->currentData().toString().toStdString());
        if (color::insertEffectAfter(graph, *selected_node_, effect))
            submit(std::move(graph), node_id);
    });
    connect(add_branch_button_, &QPushButton::clicked, this, [this] {
        if (!selected_clip_ || !selected_node_) return;
        auto graph = graph_;
        const auto node_id = graph.next_id;
        const auto effect = creative_suite::effects::makeDefaultInstance(
            node_kind_->currentData().toString().toStdString());
        if (color::addParallelBranch(graph, *selected_node_, effect))
            submit(std::move(graph), node_id);
    });
    connect(toggle, &QPushButton::clicked, this, [this] {
        if (!selected_node_) return;
        auto graph = graph_;
        const auto* node = color::findNode(graph, *selected_node_);
        if (!node || !node->effect) return;
        const auto node_id = node->id;
        if (color::setEffectEnabled(graph, node_id, !node->effect->enabled))
            submit(std::move(graph), node_id);
    });
    connect(remove, &QPushButton::clicked, this, [this] {
        if (!selected_node_) return;
        auto graph = graph_;
        const auto removed = *selected_node_;
        const auto* node = color::findNode(graph, removed);
        const bool removed_node = node && (node->type == color::NodeType::Effect
            ? color::removeEffect(graph, removed)
            : node->type == color::NodeType::ParallelMixer &&
                color::removeParallelMixer(graph, removed));
        if (!removed_node) return;
        const auto input = std::find_if(graph.nodes.begin(), graph.nodes.end(),
            [](const color::Node& node) { return node.type == color::NodeType::Input; });
        const auto next_selection = input == graph.nodes.end()
            ? std::optional<color::NodeId>{}
            : std::optional<color::NodeId>{input->id};
        submit(std::move(graph), next_selection);
    });
    waveform_timer_ = new QTimer(this);
    waveform_timer_->setInterval(100);
    connect(waveform_timer_, &QTimer::timeout, this, [this] { scheduleWaveform(); });
    refreshNodes();
}

void ColorWorkspace::setPreviewWidget(QWidget* preview) {
    if (preview_ == preview) return;
    preview_ = preview;
    if (preview_) preview_container_->layout()->addWidget(preview_);
}

QWidget* ColorWorkspace::takePreviewWidget() {
    auto* result = preview_;
    if (result) preview_container_->layout()->removeWidget(result);
    preview_ = nullptr;
    return result;
}

void ColorWorkspace::setActive(bool active) {
    active_ = active;
    ++waveform_generation_;
    if (active) waveform_timer_->start();
    else { waveform_timer_->stop(); waveform_pending_ = false; }
}

void ColorWorkspace::setClips(std::vector<ClipItem> clips, timeline::ClipId selected,
    color::Graph graph) {
    const auto old_selected = selected_node_;
    selected_clip_ = selected;
    graph_ = color::validate(graph) ? std::move(graph) : color::makePassthroughGraph();
    {
        const QSignalBlocker blocker(clips_);
        clips_->clear();
        int selected_row = -1;
        for (const auto& clip : clips) {
            auto* item = new QListWidgetItem(clip.name, clips_);
            if (!clip.thumbnail.isNull()) item->setIcon(QIcon(QPixmap::fromImage(clip.thumbnail)));
            item->setSizeHint(QSize(128, 96));
            item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(clip.id));
            if (clip.id == selected) selected_row = clips_->count() - 1;
        }
        clips_->setCurrentRow(selected_row);
    }
    refreshNodes(old_selected);
}

void ColorWorkspace::setClipSelectedHandler(std::function<void(timeline::ClipId)> handler) {
    clip_selected_handler_ = std::move(handler);
}

void ColorWorkspace::setEffectsChangedHandler(std::function<void(timeline::ClipId,
    color::Graph)> handler) {
    effects_changed_handler_ = std::move(handler);
}

void ColorWorkspace::setPlaybackHandler(std::function<void(playback::PlaybackCommand)> handler) {
    playback_handler_ = std::move(handler);
}

void ColorWorkspace::setGalleryStills(std::vector<StillItem> stills) {
    const auto selected_id = stills_->currentItem()
        ? stills_->currentItem()->data(Qt::UserRole).toString() : QString{};
    const QSignalBlocker blocker(stills_);
    stills_->clear();
    int selected_row = -1;
    for (const auto& still : stills) {
        auto* item = new QListWidgetItem(still.available
            ? still.name
            : QCoreApplication::translate("ColorWorkspace", "%1 (Unavailable)").arg(still.name),
            stills_);
        item->setData(Qt::UserRole, still.id);
        item->setToolTip(still.available
            ? QCoreApplication::translate("ColorWorkspace", "%1 · Frame %2").arg(still.source).arg(still.frame)
            : QCoreApplication::translate("ColorWorkspace", "Still image unavailable · %1 · Frame %2").arg(still.source).arg(still.frame));
        if (!still.thumbnail.isNull()) item->setIcon(QIcon(QPixmap::fromImage(still.thumbnail)));
        if (!still.available) item->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
        item->setSizeHint(QSize(128, 92));
        if (still.id == selected_id) selected_row = stills_->count() - 1;
    }
    if (selected_row >= 0) stills_->setCurrentRow(selected_row);
}

void ColorWorkspace::setCapturePending(bool pending) {
    capture_still_button_->setEnabled(!pending);
}

void ColorWorkspace::setCaptureStillHandler(std::function<void()> handler) {
    capture_still_handler_ = std::move(handler);
}

void ColorWorkspace::setRenameStillHandler(
    std::function<void(const QString&, const QString&)> handler) {
    rename_still_handler_ = std::move(handler);
}

void ColorWorkspace::setRemoveStillHandler(std::function<void(const QString&)> handler) {
    remove_still_handler_ = std::move(handler);
}

void ColorWorkspace::renameSelectedStill() {
    const auto* item = stills_->currentItem();
    if (!item || !rename_still_handler_) return;
    bool accepted = false;
    const auto name = QInputDialog::getText(this,
        QCoreApplication::translate("ColorWorkspace", "Rename still"),
        QCoreApplication::translate("ColorWorkspace", "Name"),
        QLineEdit::Normal, item->text(), &accepted).trimmed();
    if (accepted && !name.isEmpty())
        rename_still_handler_(item->data(Qt::UserRole).toString(), name);
}

void ColorWorkspace::refreshNodes(std::optional<color::NodeId> selected) {
    if (selected && color::findNode(graph_, *selected)) selected_node_ = selected;
    else if (!selected_node_ || !color::findNode(graph_, *selected_node_)) {
        const auto input = std::find_if(graph_.nodes.begin(), graph_.nodes.end(),
            [](const color::Node& node) { return node.type == color::NodeType::Input; });
        selected_node_ = input == graph_.nodes.end()
            ? std::optional<color::NodeId>{}
            : std::optional<color::NodeId>{input->id};
    }
    nodes_->setGraph(graph_, selected_node_);
    refreshControls();
}

void ColorWorkspace::refreshControls() {
    while (auto* item = controls_layout_->takeAt(0)) {
        if (auto* widget = item->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete item;
    }
    const auto* node = selected_node_ ? color::findNode(graph_, *selected_node_) : nullptr;
    const bool effect_selected = node && node->type == color::NodeType::Effect && node->effect;
    auto* toggle = findChild<QPushButton*>(QStringLiteral("colorToggleNodeButton"));
    auto* remove = findChild<QPushButton*>(QStringLiteral("colorRemoveNodeButton"));
    auto* add = findChild<QPushButton*>(QStringLiteral("colorAddNodeButton"));
    if (toggle) toggle->setEnabled(effect_selected);
    if (remove) remove->setEnabled(effect_selected ||
        (node && node->type == color::NodeType::ParallelMixer));
    if (add) add->setEnabled(node && node->type != color::NodeType::Output);
    if (add_branch_button_) add_branch_button_->setEnabled(node &&
        (node->type == color::NodeType::Input || node->type == color::NodeType::Effect));

    if (effect_selected) {
        const auto* definition = creative_suite::effects::findDefinition(node->effect->id);
        if (definition) for (const auto& parameter : definition->parameters) {
            auto* spin = new QDoubleSpinBox(controls_);
            spin->setObjectName(QStringLiteral("colorParameter_%1").arg(
                QString::fromLatin1(parameter.id.data(), parameter.id.size())));
            spin->setRange(parameter.minimum, parameter.maximum);
            spin->setSingleStep(parameter.step);
            spin->setDecimals(0);
            spin->setFixedWidth(110);
            spin->setValue(creative_suite::effects::parameterValue(*node->effect, parameter.id));
            controls_layout_->addRow(parameterLabel(parameter.id), spin);
            const auto parameter_id = std::string(parameter.id);
            const auto node_id = node->id;
            connect(spin, &QDoubleSpinBox::editingFinished, this,
                [this, spin, node_id, parameter_id] {
                    auto graph = graph_;
                    if (!color::setEffectParameter(graph, node_id, parameter_id, spin->value())) return;
                    submit(std::move(graph), node_id);
                });
        }
    } else if (node && node->type == color::NodeType::ParallelMixer) {
        for (const auto& edge : graph_.connections) {
            if (edge.to != node->id) continue;
            auto* spin = new QDoubleSpinBox(controls_);
            spin->setObjectName(QStringLiteral("colorMixerWeight_%1").arg(edge.input));
            spin->setRange(0.0, 1.0);
            spin->setSingleStep(0.05);
            spin->setDecimals(2);
            spin->setFixedWidth(110);
            spin->setValue(edge.weight);
            controls_layout_->addRow(
                QCoreApplication::translate("ColorWorkspace", "Branch %1 weight")
                    .arg(edge.input + 1), spin);
            const auto node_id = node->id;
            const auto input = edge.input;
            connect(spin, &QDoubleSpinBox::editingFinished, this,
                [this, spin, node_id, input] {
                    auto graph = graph_;
                    if (color::setMixerInputWeight(graph, node_id, input, spin->value()))
                        submit(std::move(graph), node_id);
                });
        }
    }
}

void ColorWorkspace::submit(color::Graph graph, std::optional<color::NodeId> selected) {
    if (graph == graph_) return;
    graph_ = std::move(graph);
    selected_node_ = selected && color::findNode(graph_, *selected)
        ? selected : std::optional<color::NodeId>{};
    refreshNodes(selected_node_);
    if (selected_clip_ && effects_changed_handler_)
        effects_changed_handler_(selected_clip_, graph_);
}

void ColorWorkspace::scheduleWaveform() {
    if (!active_ || waveform_pending_ || !preview_) return;
    auto* viewer = qobject_cast<PreviewWidget*>(preview_);
    if (!viewer) return;
    const auto payload = viewer->currentPayload();
    if (!payload.valid()) { waveform_->setText(QCoreApplication::translate("ColorWorkspace", "RGB waveform")); return; }
    QImage source;
    if (payload.rgba) {
        const auto& frame = *payload.rgba;
        if (frame.width <= 0 || frame.height <= 0 || frame.stride < frame.width * 4 ||
            frame.rgba_pixels.size() < static_cast<std::size_t>(frame.stride) * frame.height) return;
        source = QImage(frame.rgba_pixels.data(), frame.width, frame.height,
                        frame.stride, QImage::Format_RGBA8888);
    } else {
        source = viewer->grab().toImage();
    }
    if (source.isNull()) return;
    waveform_pending_ = true;
    const auto generation = waveform_generation_;
    const QPointer<ColorWorkspace> target(this);
    const auto frame_owner = payload.rgba;
    // Keep the immutable CPU frame alive while QImage references its pixels.
    QThreadPool::globalInstance()->start([target, generation, source = std::move(source), frame_owner] {
        auto image = calculateWaveform(source);
        (void)frame_owner;
        if (!target) return;
        QMetaObject::invokeMethod(target, [target, generation, image = std::move(image)] {
            if (!target || generation != target->waveform_generation_) return;
            target->waveform_pending_ = false;
            if (target->active_) target->waveform_->setPixmap(QPixmap::fromImage(image));
        }, Qt::QueuedConnection);
    });
}

}  // namespace ui
