#include "main_window.h"

#include "composition_viewer.h"
#include "new_composition_dialog.h"

#include <QAction>
#include <QAbstractItemView>
#include <QDoubleValidator>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <string>
#include <utility>

namespace motion::ui {
namespace {

QString layerKindName(model::LayerKind kind)
{
    switch (kind) {
    case model::LayerKind::Text: return QStringLiteral("Text");
    case model::LayerKind::Shape: return QStringLiteral("Shape");
    case model::LayerKind::Image: return QStringLiteral("Image");
    case model::LayerKind::Video: return QStringLiteral("Video");
    }
    return QStringLiteral("Layer");
}

std::string generatedLayerName(model::LayerKind kind, std::size_t layer_number)
{
    return QStringLiteral("%1 %2")
        .arg(layerKindName(kind))
        .arg(layer_number)
        .toStdString();
}

QString formatTransformValue(double value)
{
    return QString::number(value, 'g', std::numeric_limits<double>::max_digits10);
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("Motion Studio"));

    empty_state_ = new QLabel(QStringLiteral("No composition open"), this);
    empty_state_->setObjectName(QStringLiteral("motion-empty-state"));
    empty_state_->setAlignment(Qt::AlignCenter);
    setCentralWidget(empty_state_);

    QMenu* file_menu = menuBar()->addMenu(QStringLiteral("File"));
    QAction* new_composition_action = file_menu->addAction(QStringLiteral("New Composition..."));
    new_composition_action->setObjectName(QStringLiteral("motion-new-composition-action"));
    connect(new_composition_action, &QAction::triggered, this, [this] {
        createNewComposition();
    });
}

const model::CompositionDocument* MainWindow::compositionDocument() const noexcept
{
    return document_.has_value() ? &*document_ : nullptr;
}

std::optional<model::LayerId> MainWindow::selectedLayerId() const noexcept
{
    return selected_layer_id_;
}

void MainWindow::createNewComposition()
{
    if (document_.has_value()) {
        QMessageBox replace_prompt(
            QMessageBox::Warning,
            QStringLiteral("Replace Composition"),
            QStringLiteral("The current composition has not been saved. Replace it?"),
            QMessageBox::Yes | QMessageBox::No,
            this);
        replace_prompt.setObjectName(QStringLiteral("motion-replace-composition-prompt"));
        replace_prompt.setDefaultButton(QMessageBox::No);
        if (replace_prompt.exec() != QMessageBox::Yes) {
            return;
        }
    }

    NewCompositionDialog dialog(this);
    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const auto canvas_size = dialog.canvasSize();
    if (!canvas_size.has_value()) {
        return;
    }

    document_.emplace(canvas_size->width, canvas_size->height);
    selected_layer_id_.reset();
    if (workspace_ == nullptr) {
        createWorkspace();
    }
    refreshLayerList();
}

void MainWindow::createWorkspace()
{
    workspace_ = new QSplitter(Qt::Horizontal, this);
    workspace_->setObjectName(QStringLiteral("motion-workspace"));
    workspace_->setChildrenCollapsible(false);

    auto* layer_panel = new QWidget(workspace_);
    layer_panel->setObjectName(QStringLiteral("motion-layer-panel"));
    layer_panel->setMinimumWidth(190);
    auto* layer_layout = new QVBoxLayout(layer_panel);
    layer_layout->setContentsMargins(8, 8, 8, 8);

    auto* layers_title = new QLabel(QStringLiteral("Layers (front to back)"), layer_panel);
    layer_layout->addWidget(layers_title);

    auto* layer_actions = new QWidget(layer_panel);
    auto* layer_actions_layout = new QVBoxLayout(layer_actions);
    layer_actions_layout->setContentsMargins(0, 0, 0, 0);
    add_layer_button_ = new QToolButton(layer_actions);
    add_layer_button_->setObjectName(QStringLiteral("motion-add-layer-button"));
    add_layer_button_->setText(QStringLiteral("Add Layer"));
    add_layer_button_->setPopupMode(QToolButton::InstantPopup);
    auto* add_layer_menu = new QMenu(add_layer_button_);
    const std::array<std::pair<model::LayerKind, const char*>, 4> layer_kinds{{
        {model::LayerKind::Text, "text"},
        {model::LayerKind::Shape, "shape"},
        {model::LayerKind::Image, "image"},
        {model::LayerKind::Video, "video"},
    }};
    for (const auto& [kind, name] : layer_kinds) {
        QAction* action = add_layer_menu->addAction(layerKindName(kind));
        action->setObjectName(QStringLiteral("motion-add-%1-layer-action").arg(QString::fromLatin1(name)));
        connect(action, &QAction::triggered, this, [this, kind] { addLayer(kind); });
    }
    add_layer_button_->setMenu(add_layer_menu);
    layer_actions_layout->addWidget(add_layer_button_);

    remove_layer_button_ = new QPushButton(QStringLiteral("Remove"), layer_actions);
    remove_layer_button_->setObjectName(QStringLiteral("motion-remove-layer-button"));
    layer_actions_layout->addWidget(remove_layer_button_);
    layer_layout->addWidget(layer_actions);

    layer_list_ = new QListWidget(layer_panel);
    layer_list_->setObjectName(QStringLiteral("motion-layer-list"));
    layer_list_->setSelectionMode(QAbstractItemView::SingleSelection);
    layer_list_->setAlternatingRowColors(true);
    layer_layout->addWidget(layer_list_, 1);

    auto* reorder_buttons = new QWidget(layer_panel);
    auto* reorder_layout = new QHBoxLayout(reorder_buttons);
    reorder_layout->setContentsMargins(0, 0, 0, 0);
    move_front_button_ = new QPushButton(QStringLiteral("Move Up"), reorder_buttons);
    move_front_button_->setObjectName(QStringLiteral("motion-layer-move-front-button"));
    move_front_button_->setToolTip(QStringLiteral("Move toward the front of the layer stack"));
    move_back_button_ = new QPushButton(QStringLiteral("Move Down"), reorder_buttons);
    move_back_button_->setObjectName(QStringLiteral("motion-layer-move-back-button"));
    move_back_button_->setToolTip(QStringLiteral("Move toward the back of the layer stack"));
    reorder_layout->addWidget(move_front_button_);
    reorder_layout->addWidget(move_back_button_);
    layer_layout->addWidget(reorder_buttons);

    viewer_ = new CompositionViewer(workspace_);

    transform_panel_ = new QGroupBox(QStringLiteral("Transform"), workspace_);
    transform_panel_->setObjectName(QStringLiteral("motion-transform-inspector"));
    transform_panel_->setMinimumWidth(240);
    auto* transform_layout = new QFormLayout(transform_panel_);
    const std::array<QString, 5> labels{
        QStringLiteral("Position X (normalized)"),
        QStringLiteral("Position Y (normalized)"),
        QStringLiteral("Scale"),
        QStringLiteral("Rotation (degrees)"),
        QStringLiteral("Opacity (0-1)"),
    };
    const std::array<QString, 5> object_names{
        QStringLiteral("motion-transform-position-x"),
        QStringLiteral("motion-transform-position-y"),
        QStringLiteral("motion-transform-scale"),
        QStringLiteral("motion-transform-rotation"),
        QStringLiteral("motion-transform-opacity"),
    };
    const std::array<creative_suite::animation::TransformProperty, 5> properties{
        creative_suite::animation::TransformProperty::PositionX,
        creative_suite::animation::TransformProperty::PositionY,
        creative_suite::animation::TransformProperty::Scale,
        creative_suite::animation::TransformProperty::Rotation,
        creative_suite::animation::TransformProperty::Opacity,
    };
    for (std::size_t index = 0; index < transform_fields_.size(); ++index) {
        auto* field = new QLineEdit(transform_panel_);
        field->setObjectName(object_names[index]);
        field->setAlignment(Qt::AlignRight);
        auto* validator = new QDoubleValidator(
            -std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::max_digits10,
            field);
        validator->setNotation(QDoubleValidator::ScientificNotation);
        validator->setLocale(QLocale::c());
        field->setValidator(validator);
        transform_layout->addRow(labels[index], field);
        transform_fields_[index] = field;
        connect(field, &QLineEdit::editingFinished, this, [this, field, property = properties[index]] {
            updateTransformField(field, property);
        });
    }
    transform_panel_->setEnabled(false);

    workspace_->addWidget(layer_panel);
    workspace_->addWidget(viewer_);
    workspace_->addWidget(transform_panel_);
    workspace_->setStretchFactor(0, 0);
    workspace_->setStretchFactor(1, 1);
    workspace_->setStretchFactor(2, 0);
    workspace_->setSizes({240, 680, 280});
    setCentralWidget(workspace_);
    empty_state_ = nullptr;
    resize(1200, 760);

    connect(layer_list_, &QListWidget::currentRowChanged, this, [this](int row) {
        selectLayerAtRow(row);
    });
    connect(layer_list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* item) {
        updateLayerVisibility(item);
    });
    connect(remove_layer_button_, &QPushButton::clicked, this, [this] {
        removeSelectedLayer();
    });
    connect(move_front_button_, &QPushButton::clicked, this, [this] {
        moveSelectedLayer(-1);
    });
    connect(move_back_button_, &QPushButton::clicked, this, [this] {
        moveSelectedLayer(1);
    });
}

void MainWindow::addLayer(model::LayerKind kind)
{
    if (!document_.has_value()) {
        return;
    }
    const std::size_t layer_number = document_->layers().size() + 1;
    selected_layer_id_ = document_->addLayer(kind, generatedLayerName(kind, layer_number));
    refreshLayerList();
}

void MainWindow::removeSelectedLayer()
{
    if (!document_.has_value() || !selected_layer_id_.has_value()
        || !document_->removeLayer(*selected_layer_id_)) {
        return;
    }
    if (!document_->layers().empty()) {
        selected_layer_id_ = document_->layers().back().id;
    } else {
        selected_layer_id_.reset();
    }
    refreshLayerList();
}

void MainWindow::moveSelectedLayer(int row_delta)
{
    if (!document_.has_value() || !selected_layer_id_.has_value() || layer_list_ == nullptr) {
        return;
    }
    const int current_row = layer_list_->currentRow();
    const int target_row = current_row + row_delta;
    if (current_row < 0 || target_row < 0 || target_row >= layer_list_->count()) {
        return;
    }

    const auto layer_count = document_->layers().size();
    const auto target_model_index = layer_count - 1 - static_cast<std::size_t>(target_row);
    if (document_->moveLayer(*selected_layer_id_, target_model_index)) {
        refreshLayerList();
    }
}

void MainWindow::selectLayerAtRow(int row)
{
    if (!document_.has_value() || layer_list_ == nullptr || row < 0 || row >= layer_list_->count()) {
        selected_layer_id_.reset();
    } else {
        selected_layer_id_ = static_cast<model::LayerId>(
            layer_list_->item(row)->data(Qt::UserRole).toULongLong());
    }
    refreshLayerControls();
    refreshTransformInspector();
    refreshViewer();
}

void MainWindow::updateLayerVisibility(QListWidgetItem* item)
{
    if (!document_.has_value() || item == nullptr) {
        return;
    }
    const auto id = static_cast<model::LayerId>(item->data(Qt::UserRole).toULongLong());
    const bool visible = item->checkState() == Qt::Checked;
    if (document_->setLayerVisible(id, visible)) {
        refreshViewer();
    }
}

void MainWindow::updateTransformField(
    QLineEdit* field,
    creative_suite::animation::TransformProperty property)
{
    if (!document_.has_value() || !selected_layer_id_.has_value() || field == nullptr) {
        refreshTransformInspector();
        return;
    }

    const model::CompositionLayer* layer = selectedLayer();
    bool value_ok = false;
    const double value = field->text().toDouble(&value_ok);
    if (layer == nullptr || !value_ok) {
        refreshTransformInspector();
        return;
    }

    auto transform = layer->transform;
    switch (property) {
    case creative_suite::animation::TransformProperty::PositionX:
        transform.position_x = value;
        break;
    case creative_suite::animation::TransformProperty::PositionY:
        transform.position_y = value;
        break;
    case creative_suite::animation::TransformProperty::Scale:
        transform.scale = value;
        break;
    case creative_suite::animation::TransformProperty::Rotation:
        transform.rotation_degrees = value;
        break;
    case creative_suite::animation::TransformProperty::Opacity:
        transform.opacity = value;
        break;
    }

    if (!document_->setLayerTransform(*selected_layer_id_, transform)) {
        refreshTransformInspector();
        return;
    }
    refreshTransformInspector();
    refreshViewer();
}

void MainWindow::refreshLayerList()
{
    if (layer_list_ == nullptr || !document_.has_value()) {
        return;
    }

    const QSignalBlocker blocker(layer_list_);
    layer_list_->clear();
    const auto& layers = document_->layers();
    int selected_row = -1;
    for (auto layer = layers.rbegin(); layer != layers.rend(); ++layer) {
        auto* item = new QListWidgetItem(QString::fromStdString(layer->name), layer_list_);
        item->setData(Qt::UserRole, QVariant::fromValue<qulonglong>(layer->id));
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
        item->setCheckState(layer->visible ? Qt::Checked : Qt::Unchecked);
        item->setToolTip(layerKindName(layer->kind));
        if (selected_layer_id_ == layer->id) {
            selected_row = layer_list_->row(item);
        }
    }

    if (selected_layer_id_.has_value() && selected_row < 0) {
        selected_layer_id_.reset();
    }
    layer_list_->setCurrentRow(selected_row);
    refreshLayerControls();
    refreshTransformInspector();
    refreshViewer();
}

void MainWindow::refreshLayerControls()
{
    if (layer_list_ == nullptr) {
        return;
    }
    const int row = layer_list_->currentRow();
    add_layer_button_->setEnabled(document_.has_value());
    remove_layer_button_->setEnabled(row >= 0);
    move_front_button_->setEnabled(row > 0);
    move_back_button_->setEnabled(row >= 0 && row + 1 < layer_list_->count());
}

void MainWindow::refreshTransformInspector()
{
    if (transform_panel_ == nullptr) {
        return;
    }
    const model::CompositionLayer* layer = selectedLayer();
    transform_panel_->setEnabled(layer != nullptr);
    if (layer == nullptr) {
        for (auto* field : transform_fields_) {
            const QSignalBlocker blocker(field);
            field->clear();
        }
        return;
    }

    const std::array<double, 5> values{
        layer->transform.position_x,
        layer->transform.position_y,
        layer->transform.scale,
        layer->transform.rotation_degrees,
        layer->transform.opacity,
    };
    for (std::size_t index = 0; index < transform_fields_.size(); ++index) {
        const QSignalBlocker blocker(transform_fields_[index]);
        transform_fields_[index]->setText(formatTransformValue(values[index]));
    }
}

void MainWindow::refreshViewer()
{
    if (viewer_ == nullptr || !document_.has_value()) {
        return;
    }

    std::optional<QPointF> anchor;
    const model::CompositionLayer* layer = selectedLayer();
    if (layer != nullptr && layer->visible) {
        anchor = QPointF(layer->transform.position_x, layer->transform.position_y);
    }
    viewer_->setComposition(document_->canvasSize(), anchor);
}

const model::CompositionLayer* MainWindow::selectedLayer() const noexcept
{
    if (!document_.has_value() || !selected_layer_id_.has_value()) {
        return nullptr;
    }
    const auto& layers = document_->layers();
    const auto layer = std::find_if(layers.begin(), layers.end(), [this](const auto& candidate) {
        return candidate.id == *selected_layer_id_;
    });
    return layer == layers.end() ? nullptr : &*layer;
}

} // namespace motion::ui
